#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

#define I2S_BCLK    GPIO_NUM_26
#define I2S_WS      GPIO_NUM_25
#define I2S_DIN     GPIO_NUM_33
#define SAMPLE_RATE 16000          // ICS-43434 valid range ~6.25k–51.6k Hz
#define FRAMES      256

static const char *TAG = "mic";
static i2s_chan_handle_t rx_chan;
static int32_t raw[FRAMES];

static void mic_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_chan));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCLK,
            .ws   = I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = I2S_DIN,
        },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;   // match L/R -> GND

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
    ESP_LOGI(TAG, "I2S mic ready @ %d Hz", SAMPLE_RATE);
}

void app_main(void)
{
    mic_init();

    size_t bytes_read;
    while (1) {
        if (i2s_channel_read(rx_chan, raw, sizeof(raw), &bytes_read, portMAX_DELAY) == ESP_OK) {
            int n = bytes_read / sizeof(int32_t);
            int32_t peak = 0;
            for (int i = 0; i < n; i++) {
                int32_t s = raw[i] >> 8;          // 24-bit signed sample
                int32_t a = s < 0 ? -s : s;
                if (a > peak) peak = a;
            }
            int bars = peak >> 18;                // scale to ~0..30
            if (bars > 30) bars = 30;
            printf("[%.*s%*s] %ld\n", bars, "##############################",
                   30 - bars, "", (long)peak);
        }
    }
}