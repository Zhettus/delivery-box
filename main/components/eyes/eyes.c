
#include <math.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "eyes.h"

static const char *TAG = "eyes";

#define EYE_L_GPIO          16
#define EYE_R_GPIO          17

#define EYE_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define EYE_LEDC_TIMER      LEDC_TIMER_1
#define EYE_CH_L            LEDC_CHANNEL_0
#define EYE_CH_R            LEDC_CHANNEL_1

#define EYE_RES_BITS        LEDC_TIMER_14_BIT
#define EYE_DUTY_FULL       16383
#define EYE_FREQ_HZ         4882

#define EYE_GAMMA           2.2f
#define EYE_TICK_HZ         100

#define FADE_SKIP_LEVEL     0.002f
static volatile eye_emotion_t s_target      = EYE_OFF;
static volatile float         s_target_fade = EYE_FADE_DEFAULT;

static volatile eye_emotion_t s_active      = EYE_OFF;
static volatile float         s_active_fade = EYE_FADE_DEFAULT;
static int64_t                s_active_t0   = 0;

static volatile bool          s_fading_out  = false;
static int64_t                s_fade_t0     = 0;
static float                  s_fade_from   = 0.0f;
static float                  s_fade_dur    = EYE_FADE_DEFAULT;

static float                  s_last_level  = 0.0f;

static volatile float         s_cap         = 0.05f;

/* Manual override: when set, the task drives the eyes to s_manual_level and
 * ignores the emotion state machine. eyes_set_emotion() clears it. */
static volatile bool          s_manual       = false;
static volatile float         s_manual_level = 0.0f;


static uint32_t level_to_duty(float level)
{
    if (level <= 0.0f) {
        return 0;
    }
    if (level > 1.0f) {
        level = 1.0f;
    }
    float d = powf(level, EYE_GAMMA) * (float)EYE_DUTY_FULL * s_cap;
    return (uint32_t)(d + 0.5f);
}

static float render_state(eye_emotion_t e, float t, float fade)
{
    if (fade < EYE_FADE_MIN) {
        fade = EYE_FADE_MIN;
    }

    switch (e) {

    case EYE_THINKING: {
        float p = fmodf(t, 2.0f * fade);
        return (p < fade) ? (p / fade)
                          : (2.0f - p / fade);
    }

    case EYE_CONSTANT:
        return (t < fade) ? (t / fade) : 1.0f;

    case EYE_OFF:
    default:
        return 0.0f;
    }
}

static inline void write_duty(ledc_channel_t ch, uint32_t duty)
{
    ledc_set_duty(EYE_LEDC_MODE, ch, duty);
    ledc_update_duty(EYE_LEDC_MODE, ch);
}

static void enter_state(eye_emotion_t e, int64_t now_us)
{
    s_active      = e;
    s_active_fade = s_target_fade;   /* adopt the requested speed */
    s_active_t0   = now_us;
    s_fading_out  = false;
}


static void eyes_task(void *arg)
{
    const TickType_t period = pdMS_TO_TICKS(1000 / EYE_TICK_HZ);
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        int64_t now_us = esp_timer_get_time();
        float   level;

        if (!s_fading_out &&
            (s_target != s_active || s_target_fade != s_active_fade)) {

            if (s_last_level <= FADE_SKIP_LEVEL) {
                enter_state(s_target, now_us);
            } else {
                s_fading_out = true;
                s_fade_t0    = now_us;
                s_fade_from  = s_last_level;
                s_fade_dur   = s_target_fade;
                if (s_fade_dur < EYE_FADE_MIN) {
                    s_fade_dur = EYE_FADE_MIN;
                }
            }
        }

        if (s_fading_out) {
            float f = (float)(now_us - s_fade_t0) / 1e6f;
            if (f >= s_fade_dur) {
                enter_state(s_target, now_us);
                level = render_state(s_active, 0.0f, s_active_fade);
            } else {
                level = s_fade_from * (1.0f - f / s_fade_dur);
            }
        } else {
            float t = (float)(now_us - s_active_t0) / 1e6f;
            level = render_state(s_active, t, s_active_fade);
        }

        s_last_level = level;

        /* Manual override wins: the caller is driving brightness directly
         * (e.g. syncing the eyes to a strip animation). Keep s_last_level in
         * step so handing control back to an emotion resumes smoothly. */
        float out = level;
        if (s_manual) {
            out = s_manual_level;
            s_last_level = out;
        }

        uint32_t duty = level_to_duty(out);
        write_duty(EYE_CH_L, duty);
        write_duty(EYE_CH_R, duty);

        xTaskDelayUntil(&last_wake, period);
    }
}

esp_err_t eyes_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = EYE_LEDC_MODE,
        .timer_num       = EYE_LEDC_TIMER,
        .duty_resolution = EYE_RES_BITS,
        .freq_hz         = EYE_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s -- check that "
                      "freq x 2^resolution <= 80 MHz", esp_err_to_name(err));
        return err;
    }

    const int gpios[2] = { EYE_L_GPIO, EYE_R_GPIO };
    const ledc_channel_t chans[2] = { EYE_CH_L, EYE_CH_R };

    for (int i = 0; i < 2; i++) {
        ledc_channel_config_t ch = {
            .speed_mode = EYE_LEDC_MODE,
            .channel    = chans[i],
            .timer_sel  = EYE_LEDC_TIMER,
            .intr_type  = LEDC_INTR_DISABLE,
            .gpio_num   = gpios[i],
            .duty       = 0,
            .hpoint     = 0,
        };
        err = ledc_channel_config(&ch);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ledc_channel_config[%d] failed: %s",
                     i, esp_err_to_name(err));
            return err;
        }
    }

    s_active_t0 = esp_timer_get_time();

    BaseType_t ok = xTaskCreate(eyes_task, "eyes", 3072, NULL, 4, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "init ok: %d Hz, %d-bit, default fade %.1f s, cap %.1f%%",
             EYE_FREQ_HZ, (int)EYE_RES_BITS, EYE_FADE_DEFAULT, s_cap * 100.0f);
    return ESP_OK;
}

void eyes_set_emotion(eye_emotion_t e, float fade_s)
{
    if (!(fade_s >= EYE_FADE_MIN)) {   /* also catches NaN */
        fade_s = EYE_FADE_MIN;
    }

    s_manual      = false;   /* hand control back to the fade state machine */
    s_target_fade = fade_s;
    s_target      = e;
}

void eyes_set_level(float level)
{
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    s_manual_level = level;
    s_manual       = true;
}

void eyes_set_emotion_default(eye_emotion_t e)
{
    eyes_set_emotion(e, EYE_FADE_DEFAULT);
}

void eyes_set_zero(void)
{
    eyes_set_emotion(EYE_OFF, 0.0f); 
}

eye_emotion_t eyes_get_emotion(void)
{
    return s_active;
}

eye_emotion_t eyes_get_target(void)
{
    return s_target;
}

float eyes_get_fade(void)
{
    return s_active_fade;
}

bool eyes_is_transitioning(void)
{
    return s_fading_out;
}

void eyes_set_brightness_cap(float cap)
{
    if (cap < 0.0f) cap = 0.0f;
    if (cap > 1.0f) cap = 1.0f;
    s_cap = cap;
    ESP_LOGI(TAG, "brightness cap -> %.1f%%", cap * 100.0f);
}


/*#include <math.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "eyes.h"

static const char *TAG = "eyes";

#define EYE_L_GPIO          16
#define EYE_R_GPIO          17

#define EYE_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define EYE_LEDC_TIMER      LEDC_TIMER_1 
#define EYE_CH_L            LEDC_CHANNEL_0
#define EYE_CH_R            LEDC_CHANNEL_1

#define EYE_RES_BITS        LEDC_TIMER_14_BIT
#define EYE_DUTY_FULL       16383
#define EYE_FREQ_HZ         4882

#define EYE_GAMMA           2.2f
#define EYE_TICK_HZ         100

#define FADE_S              2.5f

#define FADE_SKIP_LEVEL     0.002f

static volatile eye_emotion_t s_target = EYE_OFF;

static volatile eye_emotion_t s_active     = EYE_OFF;
static int64_t                s_active_t0  = 0;
static bool                   s_fading_out = false;
static int64_t                s_fade_t0    = 0;
static float                  s_fade_from  = 0.0f;
static float                  s_last_level = 0.0f;

static volatile float s_cap = 0.05f;

static uint32_t level_to_duty(float level)
{
    if (level <= 0.0f) {
        return 0;
    }
    if (level > 1.0f) {
        level = 1.0f;
    }
    float d = powf(level, EYE_GAMMA) * (float)EYE_DUTY_FULL * s_cap;
    return (uint32_t)(d + 0.5f);
}

static float render_state(eye_emotion_t e, float t)
{
    switch (e) {

    case EYE_THINKING: {
        float p = fmodf(t, 2.0f * FADE_S);
        return (p < FADE_S) ? (p / FADE_S)
                            : (2.0f - p / FADE_S);
    }

    case EYE_CONSTANT:
        return (t < FADE_S) ? (t / FADE_S) : 1.0f;

    case EYE_OFF:
    default:
        return 0.0f;
    }
}


static inline void write_duty(ledc_channel_t ch, uint32_t duty)
{
    ledc_set_duty(EYE_LEDC_MODE, ch, duty);
    ledc_update_duty(EYE_LEDC_MODE, ch);
}

static void enter_state(eye_emotion_t e, int64_t now_us)
{
    s_active     = e;
    s_active_t0  = now_us;
    s_fading_out = false;
}

static void eyes_task(void *arg)
{
    const TickType_t period = pdMS_TO_TICKS(1000 / EYE_TICK_HZ);
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        int64_t now_us = esp_timer_get_time();
        float   level;

        if (!s_fading_out && s_target != s_active) {
            if (s_last_level <= FADE_SKIP_LEVEL) {
                enter_state(s_target, now_us);
            } else {
                s_fading_out = true;
                s_fade_t0    = now_us;
                s_fade_from  = s_last_level;
            }
        }

        if (s_fading_out) {
            float f = (float)(now_us - s_fade_t0) / 1e6f;
            if (f >= FADE_S) {
                enter_state(s_target, now_us);
                level = render_state(s_active, 0.0f);
            } else {
                level = s_fade_from * (1.0f - f / FADE_S);
            }
        } else {
            float t = (float)(now_us - s_active_t0) / 1e6f;
            level = render_state(s_active, t);
        }

        s_last_level = level;
        uint32_t duty = level_to_duty(level);
        write_duty(EYE_CH_L, duty);
        write_duty(EYE_CH_R, duty);

        xTaskDelayUntil(&last_wake, period);
    }
}

esp_err_t eyes_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = EYE_LEDC_MODE,
        .timer_num       = EYE_LEDC_TIMER,
        .duty_resolution = EYE_RES_BITS,
        .freq_hz         = EYE_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s -- check that "
                      "freq x 2^resolution <= 80 MHz", esp_err_to_name(err));
        return err;
    }

    const int gpios[2] = { EYE_L_GPIO, EYE_R_GPIO };
    const ledc_channel_t chans[2] = { EYE_CH_L, EYE_CH_R };

    for (int i = 0; i < 2; i++) {
        ledc_channel_config_t ch = {
            .speed_mode = EYE_LEDC_MODE,
            .channel    = chans[i],
            .timer_sel  = EYE_LEDC_TIMER,
            .intr_type  = LEDC_INTR_DISABLE,
            .gpio_num   = gpios[i],
            .duty       = 0,       
            .hpoint     = 0,
        };
        err = ledc_channel_config(&ch);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ledc_channel_config[%d] failed: %s",
                     i, esp_err_to_name(err));
            return err;
        }
    }

    s_active_t0 = esp_timer_get_time();

    BaseType_t ok = xTaskCreate(eyes_task, "eyes", 3072, NULL, 4, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "init ok: %d Hz, %d-bit, fade %.1f s, cap %.1f%%",
             EYE_FREQ_HZ, (int)EYE_RES_BITS, FADE_S, s_cap * 100.0f);
    return ESP_OK;
}

void eyes_set_emotion(eye_emotion_t e)
{
    s_target = e;
}

eye_emotion_t eyes_get_emotion(void)
{
    return s_active;
}

eye_emotion_t eyes_get_target(void)
{
    return s_target;
}

bool eyes_is_transitioning(void)
{
    return s_fading_out;
}

void eyes_set_brightness_cap(float cap)
{
    if (cap < 0.0f) cap = 0.0f;
    if (cap > 1.0f) cap = 1.0f;
    s_cap = cap;
    ESP_LOGI(TAG, "brightness cap -> %.1f%%", cap * 100.0f);
}*/