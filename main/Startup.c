#include "components/led/led.h"
#include "components/eyes/eyes.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PULSE_S  1.2f     

void app_main(void)
{
    ESP_ERROR_CHECK(eyes_init());
    ESP_ERROR_CHECK(led_init());
    eyes_set_brightness_cap(0.05f);

    eyes_set_emotion(EYE_THINKING, PULSE_S);
    led_set(LED_PULSE, RGB_WHITE, PULSE_S);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));   
    }
}