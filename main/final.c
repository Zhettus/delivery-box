#include "components/led/led-blink.h"
#include "components/eyes/eyes.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "string.h"
void app_main(void)
{
    ESP_ERROR_CHECK(eyes_init());
    ESP_ERROR_CHECK(led_init());
    eyes_set_brightness_cap(0.05f);

    eyes_set_emotion(EYE_THINKING, 2.5f);

    while (1) {
        led_set(LED_SOLID, RGB_RED,    1.0f);  vTaskDelay(pdMS_TO_TICKS(3000));
        led_set(LED_PULSE, RGB_BLUE,   0.6f);  vTaskDelay(pdMS_TO_TICKS(5000));
        led_set(LED_BLINK, RGB_YELLOW, 0.25f); vTaskDelay(pdMS_TO_TICKS(3000));
        led_set_off(2.0f);                     vTaskDelay(pdMS_TO_TICKS(3000));
    }
}