/*
 * main.c — Start everything, then cycle through the robot states.
 *
 * START WITH THE DOOR CLOSED: the shaft position at power-up is "closed".
 */
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

#include "audio.h"
#include "board.h"
#include "config.h"
#include "face.h"
#include "led.h"
#include "motor.h"
#include "sound.h"
#include "states.h"
#include "tail_light.h"

static const char *TAG = "main";

/* Someone held the door and it went back: beep (the door task calls this). */
static void on_door_held(bool now_open)
{
    if (DOOR_HELD_SOUND) {
        sound_play(SOUND_DOOR_HELD);
    }
}

/* One I2C bus for the INA228 (motor current) and the WM8960 (audio HAT). */
static i2c_master_bus_handle_t start_i2c_bus(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = I2C_PORT_BOARD,
        .sda_io_num        = PIN_I2C_SDA,
        .scl_io_num        = PIN_I2C_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,   /* backup; the 4.7k externals do the work */
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));
    ESP_LOGI(TAG, "I2C: INA228 0x%02X %s, WM8960 0x%02X %s", INA228_ADDRESS,
             i2c_master_probe(bus, INA228_ADDRESS, 100) == ESP_OK ? "found" : "MISSING",
             AUDIO_WM8960_ADDRESS, i2c_master_probe(bus, AUDIO_WM8960_ADDRESS, 100) == ESP_OK ? "found" : "MISSING");
    return bus;
}

static void start_everything(void)
{
    /* A failed first I2C attempt now and then is normal on the shared bus and
     * is retried (motor-audio-check): keep the driver's own messages quiet. */
    esp_log_level_set("i2c.master", ESP_LOG_NONE);

    i2c_master_bus_handle_t bus = start_i2c_bus();
    ESP_ERROR_CHECK(motor_init(bus));   /* first: motor stopped, door locked */
    /* Audio after the INA228: the codec is set up before the I2S clocks start. */
    if (sound_init(bus) != ESP_OK) {
        ESP_LOGW(TAG, "no audio HAT: the box stays silent");
    }
    tail_lights_init();
    ESP_ERROR_CHECK(face_init());
    ESP_ERROR_CHECK(led_init());
    motor_on_door_held(on_door_held);
}

#if TEST_RUN

#if !STATES_MOVE_DOOR
#error "TEST_RUN needs STATES_MOVE_DOOR 1 (config.h): the states move the door"
#endif

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
 * Hold the door while it moves to test the current guard: it goes back
 * (closing -> open, opening -> closed) and beeps. The test then waits
 * TEST_OPEN_PAUSE_MS and tries that step again.
 *
 * If the door does not reach open or closed otherwise, the motor stops, the
 * box shows failure_not_critical() (yellow) and the test ends until the next
 * reset.
 */
static motor_result_t door_arrived(const char *where)
{
    motor_result_t r = motor_wait(60000);
    if (r == MOTOR_DONE) {
        ESP_LOGI(TAG, "door %s at %.1f deg", where, motor_get_angle());
    } else if (r == MOTOR_WENT_BACK) {
        ESP_LOGW(TAG, "door held on the way to %s: went back (at %.1f deg)",
                 where, motor_get_angle());
    } else {
        ESP_LOGE(TAG, "the door did not reach %s (at %.1f deg)", where, motor_get_angle());
    }
    return r;
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
    start_everything();

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
        motor_result_t r = door_arrived("OPEN");
        if (r == MOTOR_WENT_BACK) {                     /* closed again: start over */
            wait_ms(TEST_OPEN_PAUSE_MS);
            continue;
        }
        if (r != MOTOR_DONE) {
            break;
        }

        ESP_LOGI(TAG, "4. open, waiting %d ms", TEST_OPEN_PAUSE_MS);
        wait_ms(TEST_OPEN_PAUSE_MS);

        ESP_LOGI(TAG, "5. closing");
        motor_close_door();                             /* still green pulse */
        while ((r = door_arrived("CLOSED")) == MOTOR_WENT_BACK) {
            wait_ms(TEST_OPEN_PAUSE_MS);                /* open again: close again */
            ESP_LOGI(TAG, "5. closing again");
            motor_close_door();
        }
        if (r != MOTOR_DONE) {
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
    start_everything();

    /* If the door gives up (stuck both ways, timed out), show it. */
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
