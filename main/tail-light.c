#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "led_strip.h"
#include "string.h"
#include "eyes.h"

#define RUN    GPIO_NUM_26   // IN1
#define BRAKE  GPIO_NUM_25    // IN2
#define LEFT   GPIO_NUM_33   // IN3
#define RIGHT  GPIO_NUM_32    // IN4
#define DATA_PIN_1          19
#define DATA_PIN_2          18          /* <-- GPIO the second strip is on */

#define NUM_LEDS            29          /* pixels PER strip */
#define NUM_STRIPS          2
#define MAX_BRIGHTNESS      220
#define FADE_IN_MS          2000
#define FADE_OUT_MS         600

/* Frame pacing. The sketch spun as fast as it could; under FreeRTOS we must
 * yield. ~10 ms/frame is plenty for a 2 s fade and keeps the dither smooth. */
#define FRAME_INTERVAL_MS   10

/* setMaxPowerInVoltsAndMilliamps(5, 2000) -> 10000 mW, shared by both strips */
#define MAX_POWER_MW        (5U * 2000U)

/* TypicalLEDStrip == 0xFFB0F0 */
#define CORRECTION_R        0xFF
#define CORRECTION_G        0xB0
#define CORRECTION_B        0xF0

/* The sketch declared COLOR_ORDER as RGB while a WS2815 is natively GRB, so
 * red/green came out swapped on real hardware. Set this to 1 to reproduce the
 * sketch bit-for-bit; leave at 0 for colours that actually match their names. */
#define SKETCH_RGB_ORDER    0
typedef struct {
    uint8_t r, g, b;
} rgb_t;
typedef struct {
    led_strip_handle_t handle;
    int                gpio;
    rgb_t              leds[NUM_LEDS];      /* the "leds[]" from the sketch */
    rgb_t              dither[NUM_LEDS];    /* temporal dithering residuals */
} strip_t;


#define RGB_RED     ((rgb_t){0xFF, 0x00, 0x00})
#define RGB_BLUE    ((rgb_t){0x00, 0x00, 0xFF})
#define RGB_YELLOW  ((rgb_t){0xFF, 0xFF, 0x00})
#define RGB_WHITE   ((rgb_t){0xFF, 0xFF, 0xFF})
#define RGB_CYAN    ((rgb_t){0x00, 0xFF, 0xFF})
#define RGB_BLACK   ((rgb_t){0x00, 0x00, 0x00})


static const gpio_num_t CH[4] = { RUN, BRAKE, LEFT, RIGHT };
static const rgb_t LED_STRIP[4] = {RGB_RED, RGB_BLUE, RGB_YELLOW, RGB_WHITE};

static inline void relay_on (gpio_num_t pin) { gpio_set_level(pin, 0); }  // close
static inline void relay_off(gpio_num_t pin) { gpio_set_level(pin, 1); }  // open

static void all_off(void) {
    for (int i = 0; i < 4; i++) relay_off(CH[i]);
}


static strip_t s_strips[NUM_STRIPS] = {
    { .gpio = DATA_PIN_1 },
    { .gpio = DATA_PIN_2 },
};

static uint8_t s_brightness;          

static inline uint32_t millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* FastLED scale8 (fixed-point variant: scale==255 is true unity gain) */
static inline uint8_t scale8(uint8_t i, uint8_t scale)
{
    return (uint8_t)(((uint16_t)i * (uint16_t)scale + i) >> 8);
}

/* FastLED scale8_video: never lets a non-zero value fade fully to zero */
static inline uint8_t scale8_video(uint8_t i, uint8_t scale)
{
    uint8_t v = (uint8_t)(((uint16_t)i * (uint16_t)scale) >> 8);
    return v + ((i && scale) ? 1 : 0);
}

/* FastLED dim8_video: the gamma curve the sketch applies to the fade ramp */
static inline uint8_t dim8_video(uint8_t x)
{
    return scale8_video(x, x);
}

/* scale8 with a carried-over remainder -> temporal (binary) dithering.
 * The fractional part left over from one frame is added to the next, so a
 * value of e.g. 0.5 alternates 0/1 instead of truncating to 0. */
static inline uint8_t scale8_dither(uint8_t i, uint8_t scale, uint8_t *residual)
{
    uint16_t v = (uint16_t)i * (uint16_t)scale + i + *residual;  /* max 65535 */
    *residual = (uint8_t)(v & 0xFF);
    return (uint8_t)(v >> 8);
}

/* --------------------------------------------------------- power management */

/* FastLED's power model, at 5 V: 16 mA red, 11 mA green, 15 mA blue, 1 mA idle.
 * Summed over every LED on every strip, since they share one supply. */
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

/* ------------------------------------------------------------------ output */

/* Equivalent of FastLED.show(): correction + brightness + dither, applied to
 * every strip, then all of them are pushed out. */
static void led_show(void)
{
    uint8_t scale = power_limited_brightness(s_brightness);

    /* Fold the colour correction into the per-channel scale factors once. */
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
            led_strip_set_pixel(st->handle, i, g, r, b);  /* reproduce R/G swap */
#else
            led_strip_set_pixel(st->handle, i, r, g, b);
#endif
        }
    }

    for (int s = 0; s < NUM_STRIPS; ++s) {
        led_strip_refresh(s_strips[s].handle);
    }
}

/* Fills every strip with the same colour. */
static void fill_solid(rgb_t color)
{
    for (int s = 0; s < NUM_STRIPS; ++s) {
        for (int i = 0; i < NUM_LEDS; ++i) {
            s_strips[s].leds[i] = color;
        }
    }
}

/* ------------------------------------------------------------------- fades */

static void fade_in(rgb_t color, uint16_t duration_ms)
{
    s_brightness = 0;              /* dark before loading colour (no pop) */
    fill_solid(color);
    led_show();

    uint32_t start = millis(), t;
    while ((t = millis() - start) < duration_ms) {
        uint8_t level = (uint8_t)(((uint32_t)MAX_BRIGHTNESS * t) / duration_ms);
        s_brightness = dim8_video(level);
        led_show();
        vTaskDelay(pdMS_TO_TICKS(FRAME_INTERVAL_MS));
    }

    s_brightness = MAX_BRIGHTNESS;
    led_show();
}

static void fade_out(uint16_t duration_ms)
{
    uint8_t start_level = s_brightness;   /* start from the real level */

    uint32_t start = millis(), t;
    while ((t = millis() - start) < duration_ms) {
        uint8_t level = (uint8_t)(start_level -
                                  ((uint32_t)start_level * t) / duration_ms);
        s_brightness = dim8_video(level);
        led_show();
        vTaskDelay(pdMS_TO_TICKS(FRAME_INTERVAL_MS));
    }

    s_brightness = 0;
    led_show();

    fill_solid(RGB_BLACK);        /* wipe buffers, nothing can reappear */
    for (int s = 0; s < NUM_STRIPS; ++s) {
        memset(s_strips[s].dither, 0, sizeof(s_strips[s].dither));
    }
    led_show();
}

/* -------------------------------------------------------------------- init */

static void strip_init(void)
{
    for (int s = 0; s < NUM_STRIPS; ++s) {
        led_strip_config_t strip_config = {
            .strip_gpio_num   = s_strips[s].gpio,
            .max_leds         = NUM_LEDS,
            .led_pixel_format = LED_PIXEL_FORMAT_GRB,   /* WS2815 native order */
            .led_model        = LED_MODEL_WS2812,       /* WS2815 == WS2812 timing */
            .flags.invert_out = false,
        };

        /* One RMT channel per strip. DMA is off: the ESP32-S3 has only a
         * single DMA-capable RMT channel, so a second one cannot have it. */
        led_strip_rmt_config_t rmt_config = {
            .clk_src           = RMT_CLK_SRC_DEFAULT,
            .resolution_hz     = 10 * 1000 * 1000,      /* 10 MHz, 0.1 us tick */
            .mem_block_symbols = 64,
            .flags.with_dma    = false,
        };

        ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config,
                                                 &s_strips[s].handle));
        ESP_ERROR_CHECK(led_strip_clear(s_strips[s].handle));

    }
}



void app_main(void) {
    ESP_ERROR_CHECK(eyes_init());
    strip_init();
    s_brightness = 0;
    fill_solid(RGB_BLACK);
    eyes_set_brightness_cap(0.05f);

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << RUN) | (1ULL << BRAKE) |
                        (1ULL << LEFT) | (1ULL << RIGHT),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    all_off();  

    while (1) {
        for (int i = 0; i < 4; i++) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            all_off();                       
            relay_on(CH[i]);
            fade_in(LED_STRIP[i], FADE_IN_MS);
            eyes_set_emotion(EYE_THINKING);
            vTaskDelay(pdMS_TO_TICKS(7000));
            fade_out(FADE_OUT_MS);  
            
        }
        /*
        fade_in(RGB_RED,    FADE_IN_MS);  fade_out(FADE_OUT_MS);
        fade_in(RGB_BLUE,   FADE_IN_MS);  fade_out(FADE_OUT_MS);
        fade_in(RGB_YELLOW, FADE_IN_MS);  fade_out(FADE_OUT_MS);
        fade_in(RGB_WHITE,  FADE_IN_MS);  fade_out(FADE_OUT_MS);
        fade_in(RGB_CYAN,   FADE_IN_MS);  fade_out(FADE_OUT_MS);*/
    }
}