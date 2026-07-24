#include <math.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "led_strip.h"

#include "led.h"

static const char *TAG = "led_fade";


#define DATA_PIN_1          19
#define DATA_PIN_2          18

#define NUM_LEDS            29         
#define NUM_STRIPS          2

#define DEFAULT_MAX_BRIGHT  220
#define FRAME_INTERVAL_MS   10       

#define MAX_POWER_MW        (5U * 2000U)

/* TypicalLEDStrip == 0xFFB0F0 */
#define CORRECTION_R        0xFF
#define CORRECTION_G        0xB0
#define CORRECTION_B        0xF0
#define SKETCH_RGB_ORDER    0

#define FADE_SKIP_LEVEL     0.002f

typedef struct {
    led_strip_handle_t handle;
    int                gpio;
    rgb_t              leds[NUM_LEDS];
    rgb_t              dither[NUM_LEDS];
} strip_t;


static strip_t s_strips[NUM_STRIPS] = {
    { .gpio = DATA_PIN_1 },
    { .gpio = DATA_PIN_2 },
};

static bool    s_inited;
static uint8_t s_brightness;                        
static uint8_t s_max_bright = DEFAULT_MAX_BRIGHT;

static volatile led_mode_t s_target       = LED_OFF;
static volatile rgb_t      s_target_color = { 0, 0, 0 };
static volatile float      s_target_fade  = LED_FADE_DEFAULT;

/* Currently being rendered. */
static volatile led_mode_t s_active       = LED_OFF;
static volatile rgb_t      s_active_color = { 0, 0, 0 };
static volatile float      s_active_fade  = LED_FADE_DEFAULT;
static int64_t             s_active_t0    = 0;

/* Crossfade bookkeeping. */
static volatile bool       s_fading_out   = false;
static int64_t             s_fade_t0      = 0;
static float               s_fade_from    = 0.0f;
static float               s_fade_dur     = LED_FADE_DEFAULT;

static float               s_last_level   = 0.0f;

static inline uint8_t scale8(uint8_t i, uint8_t scale)
{
    return (uint8_t)(((uint16_t)i * (uint16_t)scale + i) >> 8);
}

static inline uint8_t scale8_video(uint8_t i, uint8_t scale)
{
    uint8_t v = (uint8_t)(((uint16_t)i * (uint16_t)scale) >> 8);
    return v + ((i && scale) ? 1 : 0);
}

static inline uint8_t dim8_video(uint8_t x)
{
    return scale8_video(x, x);
}

static inline uint8_t scale8_dither(uint8_t i, uint8_t scale, uint8_t *residual)
{
    uint16_t v = (uint16_t)i * (uint16_t)scale + i + *residual;
    *residual = (uint8_t)(v & 0xFF);
    return (uint8_t)(v >> 8);
}

static uint32_t unscaled_power_mw(void)
{
    uint32_t r = 0, g = 0, b = 0;

    for (int s = 0; s < NUM_STRIPS; ++s) {
        for (int i = 0; i < NUM_LEDS; ++i) {
            r += s_strips[s].leds[i].r;
            g += s_strips[s].leds[i].g;
            b += s_strips[s].leds[i].b;
        }
    }

    r = (r * 80U) >> 8;   /* 16 mA * 5 V */
    g = (g * 55U) >> 8;   /* 11 mA * 5 V */
    b = (b * 75U) >> 8;   /* 15 mA * 5 V */

    return r + g + b + (5U * NUM_LEDS * NUM_STRIPS);
}

static uint8_t power_limited_brightness(uint8_t target)
{
    uint32_t requested = (unscaled_power_mw() * target) / 256U;

    if (requested <= MAX_POWER_MW || requested == 0) {
        return target;
    }
    return (uint8_t)(((uint32_t)target * MAX_POWER_MW) / requested);
}

static void led_show(void)
{
    uint8_t scale = power_limited_brightness(s_brightness);

    uint8_t sr = scale8(CORRECTION_R, scale);
    uint8_t sg = scale8(CORRECTION_G, scale);
    uint8_t sb = scale8(CORRECTION_B, scale);

    for (int s = 0; s < NUM_STRIPS; ++s) {
        strip_t *st = &s_strips[s];

        for (int i = 0; i < NUM_LEDS; ++i) {
            uint8_t r = scale8_dither(st->leds[i].r, sr, &st->dither[i].r);
            uint8_t g = scale8_dither(st->leds[i].g, sg, &st->dither[i].g);
            uint8_t b = scale8_dither(st->leds[i].b, sb, &st->dither[i].b);

#if SKETCH_RGB_ORDER
            led_strip_set_pixel(st->handle, i, g, r, b);
#else
            led_strip_set_pixel(st->handle, i, r, g, b);
#endif
        }
    }

    for (int s = 0; s < NUM_STRIPS; ++s) {
        led_strip_refresh(s_strips[s].handle);
    }
}

static void fill_solid(rgb_t color)
{
    for (int s = 0; s < NUM_STRIPS; ++s) {
        for (int i = 0; i < NUM_LEDS; ++i) {
            s_strips[s].leds[i] = color;
        }
    }
}

static void clear_dither(void)
{
    for (int s = 0; s < NUM_STRIPS; ++s) {
        memset(s_strips[s].dither, 0, sizeof(s_strips[s].dither));
    }
}

static inline bool rgb_equal(rgb_t a, rgb_t b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

static float render_mode(led_mode_t mode, float t, float fade)
{
    if (fade < LED_FADE_MIN) {
        fade = LED_FADE_MIN;
    }

    switch (mode) {

    case LED_SOLID:
        return (t < fade) ? (t / fade) : 1.0f;

    case LED_PULSE: {
        float p = fmodf(t, 2.0f * fade);
        return (p < fade) ? (p / fade)
                          : (2.0f - p / fade);
    }

    case LED_BLINK: {
        float p = fmodf(t, 2.0f * fade);
        return (p < fade) ? 1.0f : 0.0f;
    }

    case LED_OFF:
    default:
        return 0.0f;
    }
}

static void enter_state(led_mode_t mode, int64_t now_us)
{
    s_active       = mode;
    s_active_color = s_target_color;
    s_active_fade  = s_target_fade;
    s_active_t0    = now_us;
    s_fading_out   = false;
}


static esp_err_t strip_init(void)
{
    for (int s = 0; s < NUM_STRIPS; ++s) {
        led_strip_config_t strip_config = {
            .strip_gpio_num   = s_strips[s].gpio,
            .max_leds         = NUM_LEDS,
            .led_pixel_format = LED_PIXEL_FORMAT_GRB,  
            .led_model        = LED_MODEL_WS2812,   
            .flags.invert_out = false,
        };

        led_strip_rmt_config_t rmt_config = {
            .clk_src           = RMT_CLK_SRC_DEFAULT,
            .resolution_hz     = 10 * 1000 * 1000,     
            .mem_block_symbols = 64,
            .flags.with_dma    = false,
        };

        esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config,
                                                 &s_strips[s].handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "strip %d (GPIO%d) init failed: %s",
                     s, s_strips[s].gpio, esp_err_to_name(err));
            return err;
        }

        led_strip_clear(s_strips[s].handle);
        ESP_LOGI(TAG, "strip %d: GPIO%d, %d pixels",
                 s, s_strips[s].gpio, NUM_LEDS);
    }
    return ESP_OK;
}

static void led_task(void *arg)
{
    const TickType_t period = pdMS_TO_TICKS(FRAME_INTERVAL_MS);
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        int64_t now_us = esp_timer_get_time();
        float   level;

        if (!s_fading_out &&
            (s_target != s_active ||
             s_target_fade != s_active_fade ||
             !rgb_equal(s_target_color, s_active_color))) {

            if (s_last_level <= FADE_SKIP_LEVEL) {
                enter_state(s_target, now_us); 
            } else {
                s_fading_out = true;
                s_fade_t0    = now_us;
                s_fade_from  = s_last_level;
                s_fade_dur   = s_target_fade; 
                if (s_fade_dur < LED_FADE_MIN) {
                    s_fade_dur = LED_FADE_MIN;
                }
            }
        }

        if (s_fading_out) {
            float f = (float)(now_us - s_fade_t0) / 1e6f;
            if (f >= s_fade_dur) {
                enter_state(s_target, now_us);
                level = render_mode(s_active, 0.0f, s_active_fade);
            } else {
                level = s_fade_from * (1.0f - f / s_fade_dur);
            }
        } else {
            float t = (float)(now_us - s_active_t0) / 1e6f;
            level = render_mode(s_active, t, s_active_fade);
        }

        if (level <= 0.0f && s_last_level > 0.0f) {
            fill_solid(RGB_BLACK);
            clear_dither();
        }

        s_last_level = level;

        fill_solid(s_active_color);
        s_brightness = dim8_video((uint8_t)(level * (float)s_max_bright));
        led_show();

        xTaskDelayUntil(&last_wake, period);
    }
}


esp_err_t led_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    esp_err_t err = strip_init();
    if (err != ESP_OK) {
        return err;
    }

    s_brightness = 0;
    fill_solid(RGB_BLACK);
    clear_dither();
    s_active_t0 = esp_timer_get_time();

    BaseType_t ok = xTaskCreate(led_task, "led_fade", 3072, NULL, 3, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate failed");
        return ESP_ERR_NO_MEM;
    }

    s_inited = true;
    ESP_LOGI(TAG, "init ok: %d strips x %d px, default fade %.1f s, max %u",
             NUM_STRIPS, NUM_LEDS, LED_FADE_DEFAULT, s_max_bright);
    return ESP_OK;
}

void led_set(led_mode_t mode, rgb_t color, float fade_s)
{
    if (!(fade_s >= LED_FADE_MIN)) {   
        fade_s = LED_FADE_MIN;
    }

    s_target_color = color;
    s_target_fade  = fade_s;
    s_target       = mode;
}

void led_set_off(float fade_s)
{
    led_set(LED_OFF, RGB_BLACK, fade_s);
}

void led_set_zero(void)
{
    led_set(LED_OFF, RGB_BLACK, 0.0f);
}

void led_run_sequence(rgb_t color, float fade_in_s, float hold_s,
                      float fade_out_s)
{
    led_set(LED_SOLID, color, fade_in_s);
    vTaskDelay(pdMS_TO_TICKS((uint32_t)((fade_in_s + hold_s) * 1000.0f)));

    led_set_off(fade_out_s);
    vTaskDelay(pdMS_TO_TICKS((uint32_t)(fade_out_s * 1000.0f)));
}

void startup_led(void)
{
    led_init();
    led_run_sequence(RGB_WHITE, LED_FADE_DEFAULT, 0.0f, LED_FADE_DEFAULT);
}

led_mode_t led_get_mode(void)      { return s_active; }
led_mode_t led_get_target(void)    { return s_target; }
float      led_get_fade(void)      { return s_active_fade; }
bool       led_is_transitioning(void) { return s_fading_out; }

rgb_t led_get_color(void)
{
    rgb_t c = { s_active_color.r, s_active_color.g, s_active_color.b };
    return c;
}

void led_set_max_brightness(uint8_t max)
{
    s_max_bright = max;
    ESP_LOGI(TAG, "max brightness -> %u", max);
}