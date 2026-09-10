#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "eyes.h"

static const char *TAG = "eyes";


#define EYE_L_GPIO          19
#define EYE_R_GPIO          18

#define EYE_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define EYE_LEDC_TIMER      LEDC_TIMER_1
#define EYE_CH_L            LEDC_CHANNEL_0
#define EYE_CH_R            LEDC_CHANNEL_1

#define EYE_FREQ_HZ         1500        
#define EYE_INVERT          0


#define EYE_MIN_PULSE_US    20


#define EYE_GAMMA           2.2f
#define EYE_TICK_HZ         100        

#define LEDC_SRC_CLK_HZ     80000000ULL 
#define TWO_PI_F            6.2831853f

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static eye_emotion_t s_target       = EYE_OFF;
static float         s_target_fade  = EYE_FADE_DEFAULT;
static bool          s_manual       = false;
static float         s_manual_level = 0.0f;
static bool          s_resync       = false;

static volatile float s_cap = 0.05f;

static volatile eye_emotion_t s_active        = EYE_OFF;
static volatile float         s_active_fade   = EYE_FADE_DEFAULT;
static volatile bool          s_transitioning = false;

static int64_t  s_t0         = 0;   
static float    s_from       = 0.0f; 
static float    s_ramp_dur   = 0.0f; 
static float    s_last_level = 0.0f;
static bool     s_rising     = false;

static uint32_t s_duty_max   = 0;          
static volatile float s_min_frac = 0.0f;     
static uint32_t s_last_duty  = UINT32_MAX;  

static float pulse_to_frac(uint32_t us)
{
    float f = (float)us * (float)EYE_FREQ_HZ / 1e6f;
    return (f > 1.0f) ? 1.0f : f;
}

static ledc_timer_bit_t pick_resolution(uint32_t freq_hz)
{
    uint32_t bits = 1;
    while (bits < 16 && ((uint64_t)freq_hz << (bits + 1)) <= LEDC_SRC_CLK_HZ) {
        bits++;
    }
    return (ledc_timer_bit_t)bits;
}

static uint32_t level_to_duty(float level, bool lit)
{
    if (!lit) {
        return 0;
    }
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;

    float top = (float)s_duty_max * s_cap;
    float min = (float)s_duty_max * s_min_frac;
    if (min > top) {
        min = top;
    }

    float d = min + powf(level, EYE_GAMMA) * (top - min);
    uint32_t duty = (uint32_t)(d + 0.5f);
    if (duty > s_duty_max) {
        duty = s_duty_max;
    }
    return duty;
}

static float ease(float x)
{
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    return x * x * (3.0f - 2.0f * x);
}

static int64_t breath_period_us(float fade)
{
    int64_t p = (int64_t)(2.0f * fade * 1e6f);   
    return (p > 0) ? p : 1;
}

static float breathe(int64_t now_us)
{
    int64_t period = breath_period_us(s_active_fade);
    int64_t ph = (now_us - s_t0) % period;
    if (ph < 0) {
        ph += period;
    }
    float x = (float)ph / (float)period;
    return 0.5f - 0.5f * cosf(TWO_PI_F * x);
}

static float ramp(int64_t now_us, float to)
{
    float x = 1.0f;
    if (s_ramp_dur > 0.0f) {
        x = ((float)(now_us - s_t0) / 1e6f) / s_ramp_dur;
    }
    if (x >= 1.0f) {
        s_transitioning = false;
        return to;
    }
    return s_from + (to - s_from) * ease(x);
}

static void enter_state(eye_emotion_t e, float fade, int64_t now_us)
{
    float from = s_last_level;

    s_active      = e;
    s_active_fade = fade;
    s_from        = from;
    s_t0          = now_us;

    switch (e) {

    case EYE_THINKING: {
        float c = 1.0f - 2.0f * from;
        if (c >  1.0f) c =  1.0f;
        if (c < -1.0f) c = -1.0f;
        float theta = acosf(c);                 
        if (!s_rising) {
            theta = TWO_PI_F - theta;            
        }
        int64_t period = breath_period_us(fade);
        s_t0 = now_us - (int64_t)(theta / TWO_PI_F * (float)period);
        s_ramp_dur      = 0.0f;
        s_transitioning = false;
        break;
    }

    case EYE_CONSTANT:
        s_ramp_dur      = fade * (1.0f - from);
        s_transitioning = (s_ramp_dur > 0.0f);
        break;

    case EYE_OFF:
    default:
        s_ramp_dur      = fade * from;
        s_transitioning = (s_ramp_dur > 0.0f);
        break;
    }
}

static inline void write_duty(ledc_channel_t ch, uint32_t duty)
{
    ledc_set_duty(EYE_LEDC_MODE, ch, duty);
    ledc_update_duty(EYE_LEDC_MODE, ch);
}

static void warn_if_no_room(float cap)
{
    if (s_min_frac > 0.0f && cap <= s_min_frac * 1.5f) {
        ESP_LOGW(TAG, "cap %.1f%% is too close to the %.1f%% floor: "
                      "pulsing will be weak or flat, raise the cap",
                 cap * 100.0f, s_min_frac * 100.0f);
    }
}


static void eyes_task(void *arg)
{
    const TickType_t period = pdMS_TO_TICKS(1000 / EYE_TICK_HZ);
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        int64_t now_us = esp_timer_get_time();

        taskENTER_CRITICAL(&s_lock);
        eye_emotion_t target       = s_target;
        float         target_fade  = s_target_fade;
        bool          manual       = s_manual;
        float         manual_level = s_manual_level;
        bool          resync       = s_resync;
        s_resync = false;
        taskEXIT_CRITICAL(&s_lock);

        if (resync || target != s_active || target_fade != s_active_fade) {
            enter_state(target, target_fade, now_us);
        }

        float level;
        switch (s_active) {
        case EYE_THINKING: level = breathe(now_us);    break;
        case EYE_CONSTANT: level = ramp(now_us, 1.0f); break;
        case EYE_OFF:
        default:           level = ramp(now_us, 0.0f); break;
        }

        bool lit;
        if (manual) {
            level = manual_level;
            lit   = (level > 0.0f);
        } else {
            lit = (s_active != EYE_OFF) || (level > 0.0f);
        }

        if (level > s_last_level) {
            s_rising = true;
        } else if (level < s_last_level) {
            s_rising = false;
        }
        s_last_level = level;

        uint32_t duty = level_to_duty(level, lit);
        if (duty != s_last_duty) {
            write_duty(EYE_CH_L, duty);
            write_duty(EYE_CH_R, duty);
            s_last_duty = duty;
        }

        xTaskDelayUntil(&last_wake, period);
    }
}


esp_err_t eyes_init(void)
{
    ledc_timer_bit_t bits = pick_resolution(EYE_FREQ_HZ);
    s_duty_max = 1u << bits;

    s_min_frac = pulse_to_frac(EYE_MIN_PULSE_US);

    ledc_timer_config_t timer = {
        .speed_mode      = EYE_LEDC_MODE,
        .timer_num       = EYE_LEDC_TIMER,
        .duty_resolution = bits,
        .freq_hz         = EYE_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(err));
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
            .flags.output_invert = EYE_INVERT,
        };
        err = ledc_channel_config(&ch);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ledc_channel_config[%d] failed: %s",
                     i, esp_err_to_name(err));
            return err;
        }
    }
    s_last_duty = 0;
    s_t0 = esp_timer_get_time();

    BaseType_t ok = xTaskCreate(eyes_task, "eyes", 3072, NULL, 4, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "init ok: %d Hz, %d-bit, floor %d us = %.2f%%, default fade %.1f s, cap %.1f%%",
             EYE_FREQ_HZ, (int)bits, EYE_MIN_PULSE_US, s_min_frac * 100.0f,
             EYE_FADE_DEFAULT, s_cap * 100.0f);
    warn_if_no_room(s_cap);
    return ESP_OK;
}

void eyes_set_emotion(eye_emotion_t e, float fade_s)
{
    if (!(fade_s >= EYE_FADE_MIN)) {
        fade_s = EYE_FADE_MIN;
    }

    taskENTER_CRITICAL(&s_lock);
    if (s_manual) {
        s_resync = true; 
    }
    s_manual      = false;
    s_target_fade = fade_s;
    s_target      = e;
    taskEXIT_CRITICAL(&s_lock);
}

void eyes_set_level(float level)
{
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;

    taskENTER_CRITICAL(&s_lock);
    s_manual_level = level;
    s_manual       = true;
    taskEXIT_CRITICAL(&s_lock);
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
    return s_transitioning;
}

void eyes_set_brightness_cap(float cap)
{
    if (cap < 0.0f) cap = 0.0f;
    if (cap > 1.0f) cap = 1.0f;
    s_cap = cap;
    ESP_LOGI(TAG, "brightness cap -> %.1f%%", cap * 100.0f);
    warn_if_no_room(cap);
}

void eyes_set_min_pulse_us(uint32_t us)
{
    s_min_frac = pulse_to_frac(us);
    ESP_LOGI(TAG, "floor -> %u us = %.2f%%", (unsigned)us, s_min_frac * 100.0f);
}