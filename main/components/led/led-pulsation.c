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

// TypicalLEDStrip == 0xFFB0F0 
#define CORRECTION_R        0xFF
#define CORRECTION_G        0xB0
#define CORRECTION_B        0xF0
#define SKETCH_RGB_ORDER    1

#define FADE_SKIP_LEVEL     0.002f

/*Comet loop (leds_comet_at / leds_fill_to): the two strips form one closed
  racetrack.
  s_strips[] order is { GPIO19, GPIO18 }, so left (GPIO18) is index 1. */
#define LEFT_STRIP_IDX      1           // GPIO 18 
#define RIGHT_STRIP_IDX     0           // GPIO 19 
#define LOOP_LEN            (NUM_LEDS * NUM_STRIPS)   // pixels around the loop
#define COMET_LEN           10          // lit length of the moving comet

#define MAX_BRIGHTNESS      220
#define FRAME_INTERVAL_MS   10          // fade frame pacing; yields to FreeRTOS

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

/* Manual override: while set, led_task yields and stops touching the strips so
 * a direct animation (leds_comet_at / leds_fill_to) has sole control of the
 * hardware. led_set() clears it to hand control back to the fade task. */
static volatile bool       s_manual       = false;

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
        /* A direct animation owns the strips; don't fight it over the wire. */
        if (s_manual) {
            xTaskDelayUntil(&last_wake, period);
            continue;
        }

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

    s_manual       = false;   /* hand control back to the fade task */
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

/* circle/comet */

/* Map a loop position [0, LOOP_LEN) to a physical (strip, led).
 * 0..28   -> left strip,  LED 0..28 (ascending)
 * 29..57  -> right strip, LED 28..0 (descending)
 * so the two seams (left#28<->right#28 and right#0<->left#0) are the physically
 * adjacent strip ends, and advancing the position walks one continuous loop. */
static void loop_to_pixel(int p, int *strip, int *led)
{
    if (p < NUM_LEDS) {
        *strip = LEFT_STRIP_IDX;
        *led   = p;
    } else {
        *strip = RIGHT_STRIP_IDX;
        *led   = (2 * NUM_LEDS - 1) - p;   /* 29->28 ... 57->0 */
    }
}

/* Draw a comet whose head is at fractional loop position `head`: a bright head
 * with a linearly fading tail behind it. The head straddles two pixels
 * (anti-aliased) so sub-pixel head positions render smoothly. */
static void render_comet(rgb_t color, float head)
{
    fill_solid(RGB_BLACK);

    for (int p = 0; p < LOOP_LEN; p++) {
        /* g = how far pixel p sits behind the head, wrapped to [0, LOOP_LEN). */
        float g = head - (float)p;
        if (g < 0.0f) {
            g += (float)LOOP_LEN;
        }

        float intensity;
        if (g < COMET_LEN) {
            intensity = 1.0f - g / (float)COMET_LEN;          /* head + tail */
        } else if (g > (float)LOOP_LEN - 1.0f) {
            intensity = 1.0f - ((float)LOOP_LEN - g);         /* leading edge */
        } else {
            continue;                                         /* dark */
        }

        int strip, led;
        loop_to_pixel(p, &strip, &led);
        s_strips[strip].leds[led].r = (uint8_t)(color.r * intensity);
        s_strips[strip].leds[led].g = (uint8_t)(color.g * intensity);
        s_strips[strip].leds[led].b = (uint8_t)(color.b * intensity);
    }
}

/* Fill sweep: every pixel the head has already passed is solid `color`, the
 * pixel the head is currently on is partially lit, the rest are dark. Every
 * loop position is written, so no separate clear is needed. */
static void render_fill(rgb_t color, float head)
{
    for (int p = 0; p < LOOP_LEN; p++) {
        float lit = head - (float)p;      /* >=1 full, in (0,1) partial, else off */
        float intensity = (lit >= 1.0f) ? 1.0f : (lit > 0.0f ? lit : 0.0f);

        int strip, led;
        loop_to_pixel(p, &strip, &led);
        s_strips[strip].leds[led].r = (uint8_t)(color.r * intensity);
        s_strips[strip].leds[led].g = (uint8_t)(color.g * intensity);
        s_strips[strip].leds[led].b = (uint8_t)(color.b * intensity);
    }
}

void leds_comet_at(rgb_t color, float progress)
{
    s_manual     = true;   /* freeze led_task for the duration of the animation */
    s_last_level = 0.0f;   /* so the next led_set() fades in fresh, not from stale */
    s_brightness = MAX_BRIGHTNESS;
    render_comet(color, progress * (float)LOOP_LEN);
    led_show();
}

void leds_fill_to(rgb_t color, float progress)
{
    s_manual     = true;   /* freeze led_task for the duration of the animation */
    s_last_level = 0.0f;   /* so the next led_set() fades in fresh, not from stale */
    s_brightness = MAX_BRIGHTNESS;
    render_fill(color, progress * (float)LOOP_LEN);
    led_show();
}



 //compet pair from  s_div_middle every s_div_spawn_ms;
 //travel outward using |i - middle|, so left and right are exact mirrors
#define DIV_MAX_COMETS   24  

typedef struct {
    int64_t born_us;
    bool    alive;
} div_comet_t;

static div_comet_t       s_div[DIV_MAX_COMETS];
static TaskHandle_t      s_div_task     = NULL;

static volatile bool     s_div_run      = false;
static volatile float    s_div_middle   = (float)NUM_LEDS / 2.0f;
static volatile float    s_div_speed    = 45.0f;   /* pixels / second   */
static volatile float    s_div_tail     = 7.0f;    /* pixels            */
static volatile uint32_t s_div_spawn_ms = 180;     /* between pairs     */
static volatile uint32_t s_div_frame_ms = 5;       /* render tick       */
static volatile rgb_t    s_div_color    = { 255, 255, 255 };

static void div_render(int64_t now_us)
{
    float m    = s_div_middle;
    float spd  = s_div_speed;
    float tail = s_div_tail < 1.0f ? 1.0f : s_div_tail;

    for (int i = 0; i < NUM_LEDS; i++) {
        float a    = fabsf((float)i - m);   /* distance from middle */
        float best = 0.0f;

        for (int c = 0; c < DIV_MAX_COMETS; c++) {
            if (!s_div[c].alive) {
                continue;
            }
            float d = spd * (float)(now_us - s_div[c].born_us) / 1e6f;
            float g = d - a;                 /* how far pixel sits behind the head */

            float inten;
            if (g >= 0.0f && g < tail) {
                inten = 1.0f - g / tail;     /* head (outer) -> tail (toward middle) */
            } else if (g < 0.0f && g > -1.0f) {
                inten = 1.0f + g;            /* sub-pixel leading edge, smooth motion */
            } else {
                continue;
            }
            if (inten > best) {
                best = inten;
            }
        }

        uint8_t v = dim8_video((uint8_t)(best * 255.0f + 0.5f));
        uint8_t r = scale8(s_div_color.r, v);
        uint8_t g = scale8(s_div_color.g, v);
        uint8_t b = scale8(s_div_color.b, v);

        for (int s = 0; s < NUM_STRIPS; s++) {
            s_strips[s].leds[i].r = r;
            s_strips[s].leds[i].g = g;
            s_strips[s].leds[i].b = b;
        }
    }
}

static void div_task(void *arg)
{
    for (int c = 0; c < DIV_MAX_COMETS; c++) {
        s_div[c].alive = false;
    }

    int64_t now        = esp_timer_get_time();
    int64_t next_spawn = now;                 /* first pair immediately */

    for (;;) {
        if (!s_div_run) {
            break;
        }

        now = esp_timer_get_time();

        float m     = s_div_middle;
        float tail  = s_div_tail;
        float max_a = fmaxf(m, (float)(NUM_LEDS - 1) - m);
        float max_d = max_a + tail + 1.0f;    /* head + tail fully past the end */

        /* retire comets that have left the strip */
        for (int c = 0; c < DIV_MAX_COMETS; c++) {
            if (!s_div[c].alive) {
                continue;
            }
            float d = s_div_speed * (float)(now - s_div[c].born_us) / 1e6f;
            if (d > max_d) {
                s_div[c].alive = false;
            }
        }

        /* emit on schedule; born_us = scheduled time keeps spacing even */
        uint32_t interval_us = s_div_spawn_ms ? s_div_spawn_ms * 1000U : 1000U;
        while ((int64_t)(now - next_spawn) >= 0) {
            for (int c = 0; c < DIV_MAX_COMETS; c++) {
                if (!s_div[c].alive) {
                    s_div[c].alive   = true;
                    s_div[c].born_us = next_spawn;
                    break;
                }
            }
            next_spawn += interval_us;
        }

        s_manual     = true;                  /* keep led_task off the wire */
        s_brightness = s_max_bright;
        div_render(now);
        led_show();

        vTaskDelay(pdMS_TO_TICKS(s_div_frame_ms ? s_div_frame_ms : 1));
    }

    /* strips off, hand the hardware back to the fade task */
    fill_solid(RGB_BLACK);
    clear_dither();
    s_brightness = 0;
    led_show();
    s_div_task = NULL;
    s_manual   = false;
    vTaskDelete(NULL);
}

void leds_diverge_start(void)
{
    if (s_div_task) {
        return;                               /* already running */
    }
    s_div_run    = true;
    s_last_level = 0.0f;                       /* next led_set() fades in fresh */

    if (xTaskCreate(div_task, "led_div", 3072, NULL, 3, &s_div_task) != pdPASS) {
        s_div_run  = false;
        s_div_task = NULL;
        ESP_LOGE(TAG, "diverge task create failed");
    }
}

void leds_diverge_stop(void)
{
    s_div_run = false;                         /* task cleans up + self-deletes */
}

void leds_diverge_set_middle(float middle_px)          { s_div_middle   = middle_px; }
void leds_diverge_set_spawn_interval_ms(uint32_t ms)   { s_div_spawn_ms = ms; }
void leds_diverge_set_frame_ms(uint32_t ms)            { s_div_frame_ms = ms; }
void leds_diverge_set_speed(float px_per_s)            { s_div_speed    = px_per_s; }
void leds_diverge_set_tail(float tail_px)              { s_div_tail     = tail_px; }
void leds_diverge_set_color(rgb_t color)               { s_div_color    = color; }





/*led_init();

leds_diverge_set_middle(NUM_LEDS / 2.0f);   // where the pairs are born
leds_diverge_set_spawn_interval_ms(180);    // how often a new pair appears
leds_diverge_set_frame_ms(5);               // update rate (smoothness)
leds_diverge_set_speed(45.0f);              // outward travel speed
leds_diverge_start();

// ...tune any of the setters live while it runs...

leds_diverge_stop();                        // black out + return to led_task*/