/*
 * mic_check.c — Microphone (ICS43434) check: a level meter in the log, a
 * wiring diagnosis, and (with the audio HAT) record 3 s -> play it back.
 *
 * Build this program instead of the firmware:
 *     idf.py -DMIC_CHECK=1 build flash monitor
 * Back to the firmware:
 *     idf.py -DMIC_CHECK=0 build
 *
 * Uses the same audio code, pins and settings as the firmware (config.h,
 * board.h). The audio HAT is NOT needed for the meter: without it the ESP32
 * still makes the I2S clocks. Nothing else is started: the motor is held
 * stopped and braked, the latch stays locked.
 *
 * Every RECORD_EVERY_S seconds of metering, if the HAT answers, it records
 * RECORD_S seconds ("speak now") and plays them back on the speaker.
 *
 * Wiring (ICS43434):  DOUT -> GPIO17   BCLK -> GPIO14   LRCL -> GPIO15
 *                     SEL  -> GND      3V   -> 3.3 V    GND  -> GND
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio.h"
#include "board.h"
#include "config.h"
#include "mic_dsp.h"
#include "sound.h"

static const char *TAG = "mic_check";

#define WINDOW_MS       500       /* one meter line per window             */
#define READ_FRAMES     480       /* frames per read (10 ms at 48 kHz)      */
#define RECORD_EVERY_S  10        /* seconds of metering between recordings */
#define RECORD_S        3         /* length of the recording               */
#define FULL_SCALE      8388608.0 /* 2^23: 24-bit samples                   */
#define BAR_WIDTH       30        /* -90 dBFS .. 0 dBFS                     */

/* The door motor and latch are not started: hold them safe. */
static void hold_motor_stopped(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_MOTOR_PWM) | (1ULL << PIN_MOTOR_BRAKE) |
                        (1ULL << PIN_DOOR_LATCH),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(PIN_MOTOR_PWM, 0);                       /* no speed      */
    gpio_set_level(PIN_MOTOR_BRAKE, MOTOR_BRAKE_LEVEL_ON);  /* braked        */
    gpio_set_level(PIN_DOOR_LATCH, 0);                      /* latch locked  */
}

/* ---- level meter -------------------------------------------------------------- */

typedef struct {
    double   sumsq;
    int32_t  peak;
    int32_t  first;          /* first sample of the window              */
    bool     all_same;       /* every sample equal to `first`           */
    uint32_t n;
} slot_stats_t;

static void stats_add(slot_stats_t *s, int32_t v)
{
    if (s->n == 0) {
        s->first    = v;
        s->all_same = true;
    } else if (v != s->first) {
        s->all_same = false;
    }
    int32_t a = v < 0 ? -v : v;
    if (a > s->peak) {
        s->peak = a;
    }
    s->sumsq += (double)v * (double)v;
    s->n++;
}

static double to_dbfs(double x)
{
    return 20.0 * log10(fmax(x, 1.0) / FULL_SCALE);
}

static double rms_dbfs(const slot_stats_t *s)
{
    return to_dbfs(s->n ? sqrt(s->sumsq / s->n) : 0.0);
}

static bool has_signal(const slot_stats_t *s)
{
    return s->n > 0 && !s->all_same;
}

static void bar(char *out, double dbfs)
{
    int filled = (int)lround((dbfs + 90.0) / 90.0 * BAR_WIDTH);
    if (filled < 0) filled = 0;
    if (filled > BAR_WIDTH) filled = BAR_WIDTH;
    for (int i = 0; i < BAR_WIDTH; i++) {
        out[i] = i < filled ? '#' : ' ';
    }
    out[BAR_WIDTH] = '\0';
}

/* What a window says about the wiring (NULL = looks fine). */
static const char *diagnose(const slot_stats_t *l, const slot_stats_t *r)
{
    if (!has_signal(l) && has_signal(r)) {
        return "the mic talks in the RIGHT slot: SEL is on 3.3 V -> connect SEL to GND";
    }
    if (!has_signal(l) && l->first == 0) {
        return "NO DATA (all 0): DOUT -> GPIO17 not connected, or the mic has no 3V/GND";
    }
    if (!has_signal(l)) {
        return "STUCK (always the same value): DOUT shorted to 3.3 V/GND, or no BCLK/LRCL";
    }
    if (rms_dbfs(l) > -10.0) {
        return "LOUD NOISE all the time: BCLK (GPIO14) / LRCL (GPIO15) swapped or loose?";
    }
    return NULL;
}

/* One meter line: WINDOW_MS of audio, both slots. */
static void meter_window(void)
{
    static int32_t lr[READ_FRAMES * 2];
    slot_stats_t   l = { 0 }, r = { 0 };
    uint32_t       want = AUDIO_SAMPLE_RATE_HZ * WINDOW_MS / 1000;

    while (l.n < want) {
        size_t got = 0;
        esp_err_t err = audio_mic_read_stereo(lr, READ_FRAMES, &got, 200);
        if (err != ESP_OK || got == 0) {
            ESP_LOGE(TAG, "I2S read failed (%s)", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(100));
            return;
        }
        for (size_t i = 0; i < got; i++) {
            stats_add(&l, lr[2 * i]);
            stats_add(&r, lr[2 * i + 1]);
        }
    }

    char b[BAR_WIDTH + 1];
    bar(b, rms_dbfs(&l));
    const char *problem = diagnose(&l, &r);
    if (problem) {
        ESP_LOGW(TAG, "mic |%s| %6.1f dBFS  -> %s", b, rms_dbfs(&l), problem);
    } else {
        ESP_LOGI(TAG, "mic |%s| %6.1f dBFS (peak %6.1f)", b, rms_dbfs(&l),
                 to_dbfs(l.peak));
    }
}

/* ---- record + play back (needs the HAT) ---------------------------------------- */

static void put16(uint8_t *p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }

/* 44-byte header of a 16-bit mono PCM WAV with `samples` samples. */
static void wav_header(uint8_t *h, uint32_t samples, uint32_t rate)
{
    uint32_t data = samples * 2;
    memcpy(h, "RIFF", 4);       put32(h + 4, 36 + data);
    memcpy(h + 8, "WAVEfmt ", 8); put32(h + 16, 16);
    put16(h + 20, 1);           put16(h + 22, 1);             /* PCM, mono      */
    put32(h + 24, rate);        put32(h + 28, rate * 2);      /* byte rate      */
    put16(h + 32, 2);           put16(h + 34, 16);            /* align, bits    */
    memcpy(h + 36, "data", 4);  put32(h + 40, data);
}

static void record_and_play(void)
{
    const uint32_t samples = AUDIO_SAMPLE_RATE_HZ * RECORD_S;
    uint8_t *wav = heap_caps_malloc(44 + samples * 2, MALLOC_CAP_SPIRAM);
    if (!wav) {
        ESP_LOGW(TAG, "no PSRAM for the recording: playback skipped");
        return;
    }
    int16_t *pcm = (int16_t *)(wav + 44);

    /* Same processing as the stream to the computer (config.h MIC_*). */
    mic_dsp_config_t cfg;
    mic_dsp_default_config(&cfg, AUDIO_SAMPLE_RATE_HZ);
    cfg.mode    = MIC_PROC_MODE;
    cfg.gain_db = MIC_GAIN_DB;
    cfg.hpf_hz  = MIC_HPF_HZ;
    mic_dsp_t dsp;
    mic_dsp_init(&dsp, &cfg);

    ESP_LOGW(TAG, ">>> RECORDING %d s: speak now <<<", RECORD_S);
    static int32_t in24[READ_FRAMES];
    for (uint32_t done = 0; done < samples;) {
        size_t want = samples - done < READ_FRAMES ? samples - done : READ_FRAMES, got = 0;
        if (audio_mic_read(in24, want, &got, 200) != ESP_OK || got == 0) {
            break;
        }
        mic_dsp_process(&dsp, in24, pcm + done, got);
        done += got;
    }
    wav_header(wav, samples, AUDIO_SAMPLE_RATE_HZ);

    ESP_LOGW(TAG, ">>> PLAYING IT BACK (gain now %.1f dB) <<<", mic_dsp_gain_db(&dsp));
    if (audio_play_wav(wav, wav + 44 + samples * 2) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(50));
        while (audio_busy()) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    free(wav);
}

/* ---- main ------------------------------------------------------------------------ */

void app_main(void)
{
    hold_motor_stopped();
    /* A failed first I2C attempt now and then is normal and is retried. */
    esp_log_level_set("i2c.master", ESP_LOG_NONE);

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = I2C_PORT_BOARD,
        .sda_io_num        = PIN_I2C_SDA,
        .scl_io_num        = PIN_I2C_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,   /* backup; the 4.7k externals do the work */
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    /* Starts the I2S both ways; the codec only if the HAT answers. */
    esp_err_t err = sound_init(bus);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        ESP_LOGE(TAG, "I2S did not start (%s): nothing to check", esp_err_to_name(err));
        return;
    }
    bool speaker = audio_speaker_available();
    ESP_LOGI(TAG, "ICS43434: DOUT -> GPIO%d, BCLK -> GPIO%d, LRCL -> GPIO%d, SEL -> GND, "
                  "%d Hz", PIN_I2S_DIN, PIN_I2S_BCLK, PIN_I2S_WS, AUDIO_SAMPLE_RATE_HZ);
    ESP_LOGI(TAG, "audio HAT: %s", speaker
             ? "found: record + play back every 10 s"
             : "not found: level meter only (no playback)");
    ESP_LOGI(TAG, "quiet room ~ -70..-60 dBFS, talking close ~ -40..-25, clap ~ -15..-5");

    vTaskDelay(pdMS_TO_TICKS(200));             /* let the mic start up */
    for (;;) {
        for (int w = 0; w < RECORD_EVERY_S * 1000 / WINDOW_MS; w++) {
            meter_window();
        }
        if (speaker) {
            record_and_play();
        }
    }
}
