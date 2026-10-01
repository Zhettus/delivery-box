#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "config.h"
#include "face.h"
#include "led.h"
#include "motor.h"
#include "states.h"
#include "tail_light.h"

static const char *TAG = "state";

#define PULSE_S          1.0f    /* fade-in (and fade-out) time of the looks */
#define CHARGING_LAP_MS  2000    /* one lap of the charging comet */
#define ANIM_FRAME_MS    10

/* The door moves only if STATES_MOVE_DOOR is 1 (config.h). */
static void door_open(void)  { if (STATES_MOVE_DOOR) motor_open_door();  }
static void door_close(void) { if (STATES_MOVE_DOOR) motor_close_door(); }
static void door_stop(void)  { if (STATES_MOVE_DOOR) motor_stop();       }

void start_up(void)
{
    ESP_LOGI(TAG, "start_up");
    face_set_emotion(FACE_THINKING, PULSE_S);
    led_set(LED_PULSE, RGB_WHITE, PULSE_S);
    tail_lights_off_all();
}

void moving_with_task(void)
{
    ESP_LOGI(TAG, "moving_with_task");
    face_set_emotion(FACE_CONSTANT, PULSE_S);
    led_set(LED_PULSE, RGB_BLUE, PULSE_S);
    tail_lights_off_all();
    door_close();                   /* never drive around with the door open */
}

void moving_without_task(void)
{
    ESP_LOGI(TAG, "moving_without_task");
    face_set_emotion(FACE_CONSTANT, PULSE_S);
    led_set(LED_PULSE, RGB_WHITE, PULSE_S);
    tail_lights_off_all();
    door_close();
}

void success_waiting(void)
{
    ESP_LOGI(TAG, "success_waiting");
    face_set_emotion(FACE_CONSTANT, PULSE_S);
    led_set(LED_PULSE, RGB_GREEN, PULSE_S);
    tail_lights_off_all();
}

void success_open_door(void)
{
    ESP_LOGI(TAG, "success_open_door");
    face_set_emotion(FACE_CONSTANT, PULSE_S);
    led_set(LED_SOLID, RGB_GREEN, PULSE_S);
    tail_lights_off_all();
    door_open();
}

void failure(void)
{
    ESP_LOGI(TAG, "failure");
    face_set_emotion(FACE_CONSTANT, PULSE_S);
    led_set(LED_PULSE, RGB_RED, PULSE_S);
    tail_lights_off_all();
    tail_lights_set_brake(true);
    door_stop();                    /* critical failure: freeze the door */
}

void failure_not_critical(void)
{
    ESP_LOGI(TAG, "failure_not_critical");
    face_set_emotion(FACE_CONSTANT, PULSE_S);
    led_set(LED_PULSE, RGB_YELLOW, PULSE_S);
    tail_lights_off_all();
}

void charging(uint32_t duration_ms)
{
    ESP_LOGI(TAG, "charging");
    face_set_emotion(FACE_CONSTANT, PULSE_S);
    tail_lights_off_all();
    int64_t start = esp_timer_get_time();

    /* Circle the comet continuously for `duration_ms`, then return so the
     * caller can advance to the next state. `progress` wraps at each seam via
     * fmodf, so laps run back-to-back with no blank frame between them. The
     * face fades in during the first half of each lap and out during the
     * second half, driven from the same `progress`, so face and comet start,
     * peak and finish together. The next state's led_set() and
     * face_set_emotion() take control back. */
    for (;;) {
        float elapsed = (float)(esp_timer_get_time() - start) / 1000.0f;
        if (elapsed >= (float)duration_ms) {
            break;
        }

        float progress = fmodf(elapsed / (float)CHARGING_LAP_MS, 1.0f);

        leds_comet_at(RGB_CYAN, progress);

        float level = (progress < 0.5f) ? (progress * 2.0f)
                                        : ((1.0f - progress) * 2.0f);
        face_set_level(level);

        vTaskDelay(pdMS_TO_TICKS(ANIM_FRAME_MS));
    }
}
