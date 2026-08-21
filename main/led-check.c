#include "components/led/led.h"
#include "components/eyes/eyes.h"
#include "states.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "string.h"
void app_main(void)
{
    while (1) {
        led_init();
        leds_diverge_set_color((rgb_t){ 255, 255, 255 });  
        leds_diverge_set_tail(7.0f); //length of comet
        leds_diverge_set_middle(29 / 2.0f);   // where the pairs are born
        leds_diverge_set_spawn_interval_ms(1000);    // comet rate
        leds_diverge_set_frame_ms(20);               // update rate (smoothness)
        leds_diverge_set_speed(12.0f);              // outward travel speed
        leds_diverge_start();
    }
}