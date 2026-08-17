#include "tail-light.h"
#include "driver/ledc.h"

#define TL_PIN_RUN     GPIO_NUM_26
#define TL_PIN_BRAKE   GPIO_NUM_25
#define TL_PIN_LEFT    GPIO_NUM_33
#define TL_PIN_RIGHT   GPIO_NUM_32

#define TL_MODE        LEDC_LOW_SPEED_MODE
#define TL_TIMER       LEDC_TIMER_0
#define TL_RES         LEDC_TIMER_10_BIT 
#define TL_FREQ_HZ     1000              
#define TL_DUTY_MAX    ((1 << 10) - 1)     
#define CH_RUN         LEDC_CHANNEL_0
#define CH_BRAKE       LEDC_CHANNEL_1
#define CH_LEFT        LEDC_CHANNEL_2
#define CH_RIGHT       LEDC_CHANNEL_3

static inline uint32_t brightness_to_duty(uint8_t level) {
    return ((uint32_t)level * TL_DUTY_MAX) / 255U;
}

static void config_channel(ledc_channel_t ch, gpio_num_t pin) {
    ledc_channel_config_t cfg = {
        .gpio_num   = pin,
        .speed_mode = TL_MODE,
        .channel    = ch,
        .timer_sel  = TL_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&cfg);
}

void tail_lights_init(void) {
    ledc_timer_config_t timer = {
        .speed_mode      = TL_MODE,
        .timer_num       = TL_TIMER,
        .duty_resolution = TL_RES,
        .freq_hz         = TL_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    config_channel(CH_RUN,   TL_PIN_RUN);
    config_channel(CH_BRAKE, TL_PIN_BRAKE);
    config_channel(CH_LEFT,  TL_PIN_LEFT);
    config_channel(CH_RIGHT, TL_PIN_RIGHT);

    ledc_fade_func_install(0);
}

static void set_level(ledc_channel_t ch, uint8_t level) {
    ledc_set_duty(TL_MODE, ch, brightness_to_duty(level));
    ledc_update_duty(TL_MODE, ch);
}

static void fade_to(ledc_channel_t ch, uint8_t level, uint32_t ms) {
    ledc_set_fade_with_time(TL_MODE, ch, brightness_to_duty(level), ms);
    ledc_fade_start(TL_MODE, ch, LEDC_FADE_NO_WAIT);
}

void tail_lights_set_running(bool enable)    { set_level(CH_RUN,   enable ? 255 : 0); }
void tail_lights_set_brake(bool enable)      { set_level(CH_BRAKE, enable ? 255 : 0); }
void tail_lights_set_left_turn(bool enable)  { set_level(CH_LEFT,  enable ? 255 : 0); }
void tail_lights_set_right_turn(bool enable) { set_level(CH_RIGHT, enable ? 255 : 0); }

void tail_lights_running_level(uint8_t level) { set_level(CH_RUN,   level); }
void tail_lights_brake_level(uint8_t level)   { set_level(CH_BRAKE, level); }
void tail_lights_left_level(uint8_t level)    { set_level(CH_LEFT,  level); }
void tail_lights_right_level(uint8_t level)   { set_level(CH_RIGHT, level); }

void tail_lights_running_fade(uint8_t level, uint32_t ms) { fade_to(CH_RUN,   level, ms); }
void tail_lights_brake_fade(uint8_t level, uint32_t ms)   { fade_to(CH_BRAKE, level, ms); }
void tail_lights_left_fade(uint8_t level, uint32_t ms)    { fade_to(CH_LEFT,  level, ms); }
void tail_lights_right_fade(uint8_t level, uint32_t ms)   { fade_to(CH_RIGHT, level, ms); }

void tail_lights_off_all(void) {
    set_level(CH_RUN,   0);
    set_level(CH_BRAKE, 0);
    set_level(CH_LEFT,  0);
    set_level(CH_RIGHT, 0);
}