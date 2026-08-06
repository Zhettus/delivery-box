#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "components/tail-light/mosfet.h"


void app_main(void) {
    tail_lights_init();
    while (1){
        tail_lights_set_brake(true);
        vTaskDelay(pdMS_TO_TICKS(3000));
        tail_lights_set_left_turn(true);
        vTaskDelay(pdMS_TO_TICKS(3000));
        tail_lights_set_right_turn(true);
        vTaskDelay(pdMS_TO_TICKS(3000));
        tail_lights_set_running(true);
        vTaskDelay(pdMS_TO_TICKS(3000));
        tail_lights_off_all();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}