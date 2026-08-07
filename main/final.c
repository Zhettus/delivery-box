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
    ESP_ERROR_CHECK(eyes_init());
    ESP_ERROR_CHECK(led_init());
    eyes_set_brightness_cap(0.05f); //1.0f is full brightness

    /* Demo: cycle through the states. */
    while (1) {
        start_up(); vTaskDelay(pdMS_TO_TICKS(5000));         
        moving_with_task(); vTaskDelay(pdMS_TO_TICKS(5000));
        success_waiting(); vTaskDelay(pdMS_TO_TICKS(5000));
        success_open_door(); vTaskDelay(pdMS_TO_TICKS(5000));
        moving_without_task(); vTaskDelay(pdMS_TO_TICKS(5000));
        charging(5000);
        failure(); vTaskDelay(pdMS_TO_TICKS(5000));
        failure_not_critical(); vTaskDelay(pdMS_TO_TICKS(5000));
    }
}