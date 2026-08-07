#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LED_FADE_DEFAULT   2.5f

#define LED_FADE_MIN       0.01f

typedef struct {
    uint8_t r, g, b;
} rgb_t;

#define LED_RGB(rr, gg, bb)  ((rgb_t){ (rr), (gg), (bb) })

#define RGB_RED     LED_RGB(0xFF, 0x00, 0x00)
#define RGB_GREEN   LED_RGB(0x00, 0xFF, 0x00)
#define RGB_BLUE    LED_RGB(0x00, 0x00, 0xFF)
#define RGB_YELLOW  LED_RGB(0xFF, 0xFF, 0x00)
#define RGB_CYAN    LED_RGB(0x00, 0xFF, 0xFF)
#define RGB_WHITE   LED_RGB(0xFF, 0xFF, 0xFF)
#define RGB_BLACK   LED_RGB(0x00, 0x00, 0x00)

typedef enum {
    LED_OFF = 0,  
    LED_SOLID, 
    LED_PULSE,   
    LED_BLINK,  
} led_mode_t;

esp_err_t led_init(void);

void led_set(led_mode_t mode, rgb_t color, float fade_s);

void led_set_off(float fade_s);

void led_set_zero(void);

void led_run_sequence(rgb_t color, float fade_in_s, float hold_s,
                      float fade_out_s);

void startup_led(void);

led_mode_t led_get_mode(void);

led_mode_t led_get_target(void);

rgb_t led_get_color(void);

float led_get_fade(void);

bool led_is_transitioning(void);

void led_set_max_brightness(uint8_t max);

void leds_comet_at(rgb_t color, float progress);

void leds_fill_to(rgb_t color, float progress);

#ifdef __cplusplus
}
#endif