/* Device states. Each function runs one cycle of a state and returns; the
 * caller loops to sustain it. The animated states drive the LED strip and the
 * eyes from a single `progress` value each frame, so the two stay in lockstep. */

#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include "states.h"
#include "led.h"
#include "eyes.h"

#define BLINK_FADE_IN_MS   2000
#define BLINK_FADE_OUT_MS  600
#define ANIM_FRAME_MS      10     /* animation frame pacing (yields to FreeRTOS) */
#define SUCCESS_LAP_MS     2000   /* one comet lap in success_waiting            */
#define PULSE_S  1.0f 

void start_up(void) 
{
    eyes_set_emotion(EYE_THINKING, PULSE_S);
    led_set(LED_PULSE, RGB_WHITE, PULSE_S);
}

void moving_with_task(void)
{
    eyes_set_emotion(EYE_CONSTANT, PULSE_S);
    led_set(LED_PULSE, RGB_BLUE, PULSE_S);
}

void moving_without_task(void)
{
    eyes_set_emotion(EYE_CONSTANT, PULSE_S);
    led_set(LED_PULSE, RGB_WHITE, PULSE_S);
}

void success_waiting(void) 
{
    eyes_set_emotion(EYE_CONSTANT, PULSE_S);
    led_set(LED_PULSE, RGB_GREEN, PULSE_S);
}

void success_open_door(void) 
{
    eyes_set_emotion(EYE_CONSTANT, PULSE_S);
    led_set(LED_SOLID, RGB_GREEN, PULSE_S);
}

void failure(void) 
{
    eyes_set_emotion(EYE_CONSTANT, PULSE_S);
    led_set(LED_PULSE, RGB_RED, PULSE_S);
}

void failure_not_critical(void) 
{
    eyes_set_emotion(EYE_CONSTANT, PULSE_S);
    led_set(LED_PULSE, RGB_YELLOW, PULSE_S);
}

void charging(uint32_t duration_ms)
{
    eyes_set_emotion(EYE_CONSTANT, PULSE_S);
    int64_t start = esp_timer_get_time();

    /* Circle the comet continuously for `duration_ms`, then return so the
     * caller can advance to the next state. `progress` wraps at each seam via
     * fmodf, so laps run back-to-back with no blank frame between them. The
     * manual override is released when the next state calls led_set() /
     * eyes_set_emotion(). Pass 0 to run one lap; the loop breaks once elapsed
     * reaches the requested duration. */
    for (;;) {
        float elapsed = (float)(esp_timer_get_time() - start) / 1000.0f;
        if (elapsed >= (float)duration_ms) {
            break;
        }

        float progress = fmodf(elapsed / (float)SUCCESS_LAP_MS, 1.0f);

        leds_comet_at(RGB_CYAN, progress);

        float level = (progress < 0.5f) ? (progress * 2.0f)
                                        : ((1.0f - progress) * 2.0f);
        eyes_set_level(level);

        vTaskDelay(pdMS_TO_TICKS(ANIM_FRAME_MS));
    }
}

/* Green comet circling both strips, one lap per call. The eyes fade in while
 * the comet crosses the first strip (progress 0..0.5) and fade out while it
 * crosses the second (0.5..1) -- driven from the same `progress`, so the eyes
 * and the comet start, peak (at the crossover) and finish together. */
 /*
void success_waiting(void)
{
    int64_t start = esp_timer_get_time();

    for (;;) {
        float progress = (float)(esp_timer_get_time() - start) / 1000.0f
                         / (float)SUCCESS_LAP_MS;
        if (progress >= 1.0f) {
            break;
        }

        leds_comet_at(RGB_GREEN, progress);

        float level = (progress < 0.5f) ? (progress * 2.0f)
                                        : ((1.0f - progress) * 2.0f);
        eyes_set_level(level);

        vTaskDelay(pdMS_TO_TICKS(ANIM_FRAME_MS));
    }
}
*/ 
/* Door-open confirmation. The green sweep continues around the loop as in
 * success_waiting, but now it FILLS -- every pixel it passes stays lit -- until
 * both strips are solid green. The eyes fade in over the exact same interval
 * (both start and finish together). Once full, strip and eyes are held on. */

 /*
void success_open_door(void)
{
    int64_t start = esp_timer_get_time();

    for (;;) {
        float progress = (float)(esp_timer_get_time() - start) / 1000.0f
                         / (float)OPEN_DOOR_MS;
        if (progress >= 1.0f) {
            break;
        }

        leds_fill_to(RGB_GREEN, progress);   
        eyes_set_level(progress);           

        vTaskDelay(pdMS_TO_TICKS(ANIM_FRAME_MS));
    }
    leds_fill_to(RGB_GREEN, 1.0f);
    eyes_set_emotion(EYE_CONSTANT);
}
*/
