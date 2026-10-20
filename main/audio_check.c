/*
 * audio_check.c — Audio HAT (WM8960) check: plays main/gm.wav in a loop.
 *
 * Build this program instead of the firmware:
 *     idf.py -DAUDIO_CHECK=1 build flash monitor
 * Back to the firmware:
 *     idf.py -DAUDIO_CHECK=0 build
 *
 * Uses the same audio code, pins and settings as the firmware (config.h,
 * board.h), so if it plays here, it plays there. Nothing else is started:
 * the motor is held stopped and braked, the latch stays locked.
 *
 * What to look for in the log:
 *   I2C: WM8960 0x1A found        the HAT answers on the shared I2C bus
 *   audio: I2S ... speaker OK     the codec was configured
 *   loop N: playing gm.wav        you should hear it every few seconds
 */
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio.h"
#include "board.h"
#include "config.h"
#include "sound.h"

static const char *TAG = "audio_check";

#define PAUSE_MS  1000          /* silence between two plays */

/* The door motor and latch are not started: hold them safe. */
static void hold_motor_stopped(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_MOTOR_PWM) | (1ULL << PIN_MOTOR_BRAKE) |
                        (1ULL << PIN_DOOR_LATCH),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(PIN_MOTOR_PWM, 0);                       /* no speed      */
    gpio_set_level(PIN_MOTOR_BRAKE, MOTOR_BRAKE_LEVEL_ON);  /* braked        */
    gpio_set_level(PIN_DOOR_LATCH, 0);                      /* latch locked  */
}

void app_main(void)
{
    hold_motor_stopped();
    /* A failed first I2C attempt now and then is normal and is retried. */
    esp_log_level_set("i2c.master", ESP_LOG_NONE);

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

    bool found = i2c_master_probe(bus, AUDIO_WM8960_ADDRESS, 100) == ESP_OK;
    ESP_LOGI(TAG, "I2C (SDA GPIO%d, SCL GPIO%d): WM8960 0x%02X %s", PIN_I2C_SDA, PIN_I2C_SCL,
             AUDIO_WM8960_ADDRESS, found ? "found" : "MISSING");

    esp_err_t err = sound_init(bus);            /* codec first, then I2S */
    if (err != ESP_OK || !audio_speaker_available()) {
        ESP_LOGE(TAG, "the WM8960 does not answer (%s). Check: HAT P3 (SDA) -> GPIO%d, "
                      "P5 (SCL) -> GPIO%d, 4.7k pull-ups to 3.3 V, HAT 3.3 V and 5 V, common GND.",
                 esp_err_to_name(err), PIN_I2C_SDA, PIN_I2C_SCL);
        ESP_LOGE(TAG, "nothing to play: fix the wiring, then reset the board.");
        return;
    }
    ESP_LOGI(TAG, "I2S: BCLK GPIO%d -> P12, WS GPIO%d -> P35, DOUT GPIO%d -> P40, "
                  "%d Hz, volume %d%%", PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT,
             AUDIO_SAMPLE_RATE_HZ, AUDIO_VOLUME_PERCENT);

    for (uint32_t loop = 1;; loop++) {
        ESP_LOGI(TAG, "loop %lu: playing gm.wav", (unsigned long)loop);
        sound_play(SOUND_GREETING);             /* = main/gm.wav */
        vTaskDelay(pdMS_TO_TICKS(50));          /* let it start */
        while (audio_busy()) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        vTaskDelay(pdMS_TO_TICKS(PAUSE_MS));
    }
}
