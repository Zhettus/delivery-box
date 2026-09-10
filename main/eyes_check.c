#include "components/led/led.h"
#include "components/eyes/eyes.h"
#include "components/tail-light/tail-light.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#define PULSE_S  2.0f     

void app_main(void)
{
    ESP_ERROR_CHECK(eyes_init());
    eyes_set_brightness_cap(0.5f);


    while (1) {
        eyes_set_emotion(EYE_THINKING, PULSE_S);
    }
}