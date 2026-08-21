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

leds_diverge_set_middle(NUM_LEDS / 2.0f);   // where the pairs are born
leds_diverge_set_spawn_interval_ms(180);    // how often a new pair appears
leds_diverge_set_frame_ms(5);               // update rate (smoothness)
leds_diverge_set_speed(45.0f);              // outward travel speed
leds_diverge_start();

leds_diverge_stop(); 
    }
}