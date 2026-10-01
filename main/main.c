/*
 * main.c — Start everything, then cycle through the robot states.
 *
 * START WITH THE DOOR CLOSED: the shaft position at power-up is "closed".
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

#include "config.h"
#include "face.h"
#include "led.h"
#include "motor.h"
#include "states.h"
#include "tail_light.h"

#if TEST_RUN

#if !STATES_MOVE_DOOR
#error "TEST_RUN needs STATES_MOVE_DOOR 1 (config.h): the states move the door"
#endif

static const char *TAG = "main";

/*
 * Test run (times in config.h):
 *
 *   1. wait TEST_START_DELAY_MS     everything off (time to switch on the 12 V)
 *   2. start_up()                   white pulse, TEST_STARTUP_MS
 *   3. open the door                green pulse (success_waiting); 15 deg
 *                                   clockwise with the solenoid pulling
 *                                   meanwhile, then opens counterclockwise
 *   4. wait TEST_OPEN_PAUSE_MS      green pulse, door open
 *   5. close the door               green pulse; closes until the door
 *                                   switch closes; 1 s after the switch
 *                                   the solenoid goes OFF (latch locks),
 *                                   0.3 s later the motor stops
 *   then again from 2.
 * Tail lights: only the DRL (running light) on, from step 2 on. Face: solid on at FACE_BRIGHTNESS_CAP (5 %) from
 * step 2 on.
 *
 * If the door does not reach open or closed, the motor stops, the box shows
 * failure_not_critical() (yellow) and the test ends until the next reset.
 */
static bool door_arrived(const char *where)
{
    if (motor_wait_done(60000)) {
        ESP_LOGI(TAG, "door %s at %.1f deg", where, motor_get_angle());
        return true;
    }
    ESP_LOGE(TAG, "the door did not reach %s (at %.1f deg)", where, motor_get_angle());
    return false;
}

static void wait_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/* Tail lights: only the DRL (running light) on. Called after each state
 * function, because the states set the tail lights themselves. */
static void drl_only(void)
{
    tail_lights_off_all();
    tail_lights_set_running(true);
}

void app_main(void)
{
    ESP_ERROR_CHECK(motor_init());      /* first: motor stopped, door locked */
    tail_lights_init();
    ESP_ERROR_CHECK(face_init());
    ESP_ERROR_CHECK(led_init());

    ESP_LOGW(TAG, "TEST RUN: door %.0f deg, open at %.1f rpm, close at %.1f rpm",
             DOOR_OPEN_ANGLE_DEG, DOOR_OPEN_RPM, DOOR_CLOSE_RPM);

    ESP_LOGI(TAG, "1. waiting %d ms", TEST_START_DELAY_MS);
    wait_ms(TEST_START_DELAY_MS);

    while (1) {
        ESP_LOGI(TAG, "2. start up");
        start_up();                                     /* white pulse */
        face_set_emotion(FACE_CONSTANT, 1.0f);          /* face solid on, not breathing */
        drl_only();
        wait_ms(TEST_STARTUP_MS);

        ESP_LOGI(TAG, "3. opening");
        success_waiting();                              /* green pulse */
        drl_only();
        motor_open_door();
        if (!door_arrived("OPEN")) {
            break;
        }

        ESP_LOGI(TAG, "4. open, waiting %d ms", TEST_OPEN_PAUSE_MS);
        wait_ms(TEST_OPEN_PAUSE_MS);

        ESP_LOGI(TAG, "5. closing");
        motor_close_door();                             /* still green pulse */
        if (!door_arrived("CLOSED")) {
            break;
        }
    }

    motor_stop();
    failure_not_critical();                             /* yellow pulse */
    drl_only();
    ESP_LOGE(TAG, "test run stopped. Fix the cause, then reset the board.");
}

#else

static void hold(void)
{
    vTaskDelay(pdMS_TO_TICKS(STATE_HOLD_MS));
}

void app_main(void)
{
    ESP_ERROR_CHECK(motor_init());      /* first: motor stopped, door locked */
    tail_lights_init();
    ESP_ERROR_CHECK(face_init());
    ESP_ERROR_CHECK(led_init());

    /* If the door is blocked on every retry, show it. */
    motor_on_fault(failure_not_critical);

    /* Door travel and speed come from config.h (DOOR_OPEN_ANGLE_DEG,
     * DOOR_OPEN_RPM, DOOR_CLOSE_RPM). To change them while running:
     *
     *     motor_set_open_angle(120.0f);    // shaft turns 120 degrees to open
     *     motor_set_speed(10.0f, 5.0f);    // open at 10 rpm, close at 5 rpm
     */

    /* Demo: cycle through the states. */
    while (1) {
        start_up();                 hold();
        moving_with_task();         hold();
        success_waiting();          hold();
        success_open_door();        hold();     /* the door opens  */
        moving_without_task();      hold();     /* the door closes */
        charging(STATE_HOLD_MS);
        failure();                  hold();
        failure_not_critical();     hold();
    }
}

#endif
