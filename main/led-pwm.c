#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "eyes.h"

static const char *TAG = "app";

void app_main(void)
{
    ESP_ERROR_CHECK(eyes_init());

    eyes_set_brightness_cap(0.05f);

    for (;;) {
        ESP_LOGI(TAG, "-> thinking");
        eyes_set_emotion(EYE_THINKING);
        vTaskDelay(pdMS_TO_TICKS(15000));

        ESP_LOGI(TAG, "-> constant");
        eyes_set_emotion(EYE_CONSTANT);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}