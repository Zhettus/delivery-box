/*
 * mic_ros.c — Live microphone over micro-ROS. See mic_ros.h.
 *
 * Tasks:  capture (core 1): I2S -> mic_dsp (HPF, AGC, limiter) -> decimation to
 *                           16 kHz -> 160-sample chunks -> message buffer
 *         ros     (core 0): the only task that calls rcl/rclc; publishes the chunks.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"
#include "freertos/task.h"

#include <rcl/error_handling.h>
#include <rcl/rcl.h>
#include <rclc/executor.h>
#include <rclc/rclc.h>
#include <rmw_microros/rmw_microros.h>

#include <std_msgs/msg/bool.h>
#include <std_msgs/msg/int16_multi_array.h>

#include "audio.h"
#include "config.h"
#include "mic_dsp.h"
#include "mic_ros.h"
#include "usb_jtag_transport.h"

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG || CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG
#error "micro-ROS uses the native USB port: logs must go to UART0. Delete sdkconfig and \
build again (sdkconfig.microros sets it), or see README.md."
#endif

static const char *TAG = "mic_ros";

/* ---- audio ---------------------------------------------------------------- */
#define MIC_RATE_HZ       AUDIO_SAMPLE_RATE_HZ
#define DECIM_FACTOR      (MIC_RATE_HZ / MIC_STREAM_RATE_HZ)
#define CHUNK_SAMPLES     160             /* 10 ms at 16 kHz = 320 B (fits the 512 B MTU) */
#define READ_FRAMES       (CHUNK_SAMPLES * DECIM_FACTOR)
#define STALE_FRAMES      2048            /* thrown away when the mic is switched on */
#define DECIM_CUTOFF_HZ   7000.0f

_Static_assert(MIC_STREAM_RATE_HZ == 16000, "listen_live.py plays 16 kHz");
_Static_assert(MIC_RATE_HZ % MIC_STREAM_RATE_HZ == 0,
               "AUDIO_SAMPLE_RATE_HZ must be a multiple of MIC_STREAM_RATE_HZ");

/* ---- ros ------------------------------------------------------------------ */
#define NODE_NAME         "delivery_box"
#define TOPIC_ENABLE      MIC_ROS_TOPIC_PREFIX "/enable"
#define TOPIC_STATUS      MIC_ROS_TOPIC_PREFIX "/status"
#define TOPIC_AUDIO       MIC_ROS_TOPIC_PREFIX "/audio"
#define STATUS_PERIOD_MS  1000
#define PING_PERIOD_MS    1000
#define STATS_PERIOD_MS   5000
#define BUFFER_BYTES      (16 * 1024)     /* ~0.5 s of chunks between capture and ros */

#define RC_TRY(fn) do { rcl_ret_t _rc = (fn); if (_rc != RCL_RET_OK) {          \
    ESP_LOGE(TAG, "%s failed (%d) line %d", #fn, (int)_rc, __LINE__);         \
    rcl_reset_error(); return false; } } while (0)
#define RC_SOFT(fn) do { rcl_ret_t _rc = (fn); (void)_rc; } while (0)

typedef struct {
    uint32_t offset;                      /* first sample's index in the stream */
    int16_t  samples[CHUNK_SAMPLES];
} chunk_t;

static MessageBufferHandle_t s_chunks;
static volatile bool         s_enabled;        /* set by the ros task, read by capture */
static volatile uint32_t     s_dropped_chunks; /* capture could not queue a chunk      */
static volatile uint32_t     s_captured_chunks;

/* ---- capture task --------------------------------------------------------- */
static void capture_task(void *arg)
{
    static int32_t in24[READ_FRAMES];
    static int16_t proc[READ_FRAMES];
    static int16_t dec[READ_FRAMES / DECIM_FACTOR + 1];
    mic_dsp_t     dsp;
    mic_decim_t   decim;
    chunk_t       chunk;
    size_t        fill = 0;
    uint32_t      stream_pos = 0;
    bool          was_enabled = false;

    mic_dsp_config_t cfg;
    mic_dsp_default_config(&cfg, MIC_RATE_HZ);
    cfg.mode    = MIC_PROC_MODE;
    cfg.gain_db = MIC_GAIN_DB;
    cfg.hpf_hz  = MIC_HPF_HZ;

    for (;;) {
        if (!s_enabled) {
            was_enabled = false;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (!was_enabled) {                    /* just switched on: fresh state */
            was_enabled = true;
            mic_dsp_init(&dsp, &cfg);
            mic_decim_init(&decim, DECIM_CUTOFF_HZ, MIC_RATE_HZ, DECIM_FACTOR);
            fill = 0;
            stream_pos = 0;
            for (size_t left = STALE_FRAMES; left > 0; ) {   /* old DMA content */
                size_t want = left > READ_FRAMES ? READ_FRAMES : left, got = 0;
                if (audio_mic_read(in24, want, &got, 100) != ESP_OK || got == 0) {
                    break;
                }
                left -= got;
            }
        }

        size_t got = 0;
        esp_err_t err = audio_mic_read(in24, READ_FRAMES, &got, 100);
        if (err != ESP_OK || got == 0) {
            ESP_LOGW(TAG, "mic read: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        mic_dsp_process(&dsp, in24, proc, got);
        size_t n = mic_decim_process(&decim, proc, got, dec);

        for (size_t i = 0; i < n; i++) {
            if (fill == 0) {
                chunk.offset = stream_pos;
            }
            chunk.samples[fill++] = dec[i];
            if (fill == CHUNK_SAMPLES) {
                s_captured_chunks++;
                /* whole message or nothing: the position still advances, so the
                 * receiver sees the gap in data_offset */
                if (xMessageBufferSend(s_chunks, &chunk, sizeof(chunk), 0) != sizeof(chunk)) {
                    s_dropped_chunks++;
                }
                stream_pos += CHUNK_SAMPLES;
                fill = 0;
            }
        }
    }
}

/* ---- ros entities (only touched by the ros task) -------------------------- */
static rcl_allocator_t   s_alloc;
static rclc_support_t    s_support;
static rcl_node_t        s_node;
static rcl_subscription_t s_sub_enable;
static rcl_publisher_t   s_pub_audio, s_pub_status;
static rclc_executor_t   s_executor;

static std_msgs__msg__Bool             s_in_enable, s_out_status;
static std_msgs__msg__Int16MultiArray  s_out_audio;
static int16_t                         s_audio_buf[CHUNK_SAMPLES];

static volatile bool s_enable_request;
static volatile bool s_enable_changed;

static void on_enable(const void *msgin)
{
    s_enable_request = ((const std_msgs__msg__Bool *)msgin)->data;
    s_enable_changed = true;
}

static void publish_status(void)
{
    s_out_status.data = s_enabled;
    RC_SOFT(rcl_publish(&s_pub_status, &s_out_status, NULL));
}

static void init_audio_message(void)
{
    memset(&s_out_audio, 0, sizeof(s_out_audio));    /* no dims, no labels */
    s_out_audio.data.data     = s_audio_buf;
    s_out_audio.data.size     = CHUNK_SAMPLES;
    s_out_audio.data.capacity = CHUNK_SAMPLES;
}

static bool create_entities(void)
{
    s_alloc = rcl_get_default_allocator();
    RC_TRY(rclc_support_init(&s_support, 0, NULL, &s_alloc));
    RC_TRY(rclc_node_init_default(&s_node, NODE_NAME, "", &s_support));
    RC_TRY(rclc_subscription_init_default(&s_sub_enable, &s_node,
           ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Bool), TOPIC_ENABLE));
    RC_TRY(rclc_publisher_init_default(&s_pub_status, &s_node,
           ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Bool), TOPIC_STATUS));
    RC_TRY(rclc_publisher_init_best_effort(&s_pub_audio, &s_node,
           ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int16MultiArray), TOPIC_AUDIO));

    s_executor = rclc_executor_get_zero_initialized_executor();
    RC_TRY(rclc_executor_init(&s_executor, &s_support.context, 1, &s_alloc));
    RC_TRY(rclc_executor_add_subscription(&s_executor, &s_sub_enable, &s_in_enable,
                                          on_enable, ON_NEW_DATA));
    init_audio_message();
    return true;
}

static void destroy_entities(void)
{
    rmw_context_t *rmw_ctx = rcl_context_get_rmw_context(&s_support.context);
    if (rmw_ctx) {
        (void)rmw_uros_set_context_entity_destroy_session_timeout(rmw_ctx, 0);
    }
    RC_SOFT(rcl_subscription_fini(&s_sub_enable, &s_node));
    RC_SOFT(rcl_publisher_fini(&s_pub_audio, &s_node));
    RC_SOFT(rcl_publisher_fini(&s_pub_status, &s_node));
    RC_SOFT(rclc_executor_fini(&s_executor));
    RC_SOFT(rcl_node_fini(&s_node));
    RC_SOFT(rclc_support_fini(&s_support));
}

static void flush_chunks(void)
{
    chunk_t discard;
    while (xMessageBufferReceive(s_chunks, &discard, sizeof(discard), 0) > 0) {
    }
}

static void ros_task(void *arg)
{
    chunk_t chunk;

    for (;;) {
        s_enabled = false;                     /* OFF until asked */
        flush_chunks();

        ESP_LOGI(TAG, "waiting for the micro-ROS agent on the native USB port...");
        while (rmw_uros_ping_agent(200, 1) != RMW_RET_OK) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        if (!create_entities()) {
            ESP_LOGE(TAG, "could not create the ROS entities, retrying");
            destroy_entities();
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        ESP_LOGI(TAG, "connected: node '%s' is up (mic OFF, send true on %s)",
                 NODE_NAME, TOPIC_ENABLE);

        s_enable_changed = false;
        publish_status();
        TickType_t last_ping   = xTaskGetTickCount();
        TickType_t last_status = last_ping;
        TickType_t last_stats  = last_ping;
        uint32_t   published = 0;

        for (;;) {
            rclc_executor_spin_some(&s_executor, 0);

            if (s_enable_changed) {
                s_enable_changed = false;
                if (s_enable_request != s_enabled) {
                    if (!s_enable_request) {
                        s_enabled = false;
                        flush_chunks();
                    } else {
                        flush_chunks();
                        s_enabled = true;
                    }
                    ESP_LOGI(TAG, "microphone %s", s_enabled ? "ON" : "OFF");
                }
                publish_status();
                last_status = xTaskGetTickCount();
            }

            /* wake up as soon as a chunk is ready, or after 5 ms to keep spinning */
            size_t len = xMessageBufferReceive(s_chunks, &chunk, sizeof(chunk),
                                               pdMS_TO_TICKS(5));
            if (len == sizeof(chunk) && s_enabled) {
                memcpy(s_audio_buf, chunk.samples, sizeof(s_audio_buf));
                s_out_audio.layout.data_offset = chunk.offset;
                RC_SOFT(rcl_publish(&s_pub_audio, &s_out_audio, NULL));
                published++;
            }

            TickType_t now = xTaskGetTickCount();
            if (now - last_status >= pdMS_TO_TICKS(STATUS_PERIOD_MS)) {
                last_status = now;
                publish_status();
            }
            if (s_enabled && now - last_stats >= pdMS_TO_TICKS(STATS_PERIOD_MS)) {
                last_stats = now;
                ESP_LOGI(TAG, "stream: published=%" PRIu32 " captured=%" PRIu32
                              " dropped=%" PRIu32, published,
                         (uint32_t)s_captured_chunks, (uint32_t)s_dropped_chunks);
            }
            if (now - last_ping >= pdMS_TO_TICKS(PING_PERIOD_MS)) {
                last_ping = now;
                if (rmw_uros_ping_agent(100, 3) != RMW_RET_OK) {
                    break;
                }
            }
        }

        ESP_LOGW(TAG, "agent lost: microphone OFF");
        s_enabled = false;
        destroy_entities();
    }
}

esp_err_t mic_ros_start(void)
{
    if (audio_sample_rate() != MIC_RATE_HZ) {
        ESP_LOGE(TAG, "the I2S is not running at %d Hz: no microphone stream", MIC_RATE_HZ);
        return ESP_ERR_INVALID_STATE;
    }
    s_chunks = xMessageBufferCreate(BUFFER_BYTES);
    if (!s_chunks) {
        ESP_LOGE(TAG, "no memory for the chunk buffer");
        return ESP_ERR_NO_MEM;
    }

    rmw_ret_t rc = rmw_uros_set_custom_transport(true, NULL,
                                                 usb_jtag_open, usb_jtag_close,
                                                 usb_jtag_write, usb_jtag_read);
    if (rc != RMW_RET_OK) {
        ESP_LOGE(TAG, "rmw_uros_set_custom_transport failed (%d)", (int)rc);
        return ESP_FAIL;
    }
    /* Below the door control (10) and its current sensing (9). */
    if (xTaskCreatePinnedToCore(capture_task, "mic_capture", 8192, NULL, 6, NULL, 1) != pdPASS ||
        xTaskCreatePinnedToCore(ros_task, "mic_ros", 16384, NULL, 4, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "cannot create the tasks");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "microphone: %d Hz -> %d Hz stream on %s/audio", MIC_RATE_HZ,
             MIC_STREAM_RATE_HZ, MIC_ROS_TOPIC_PREFIX);
    return ESP_OK;
}
