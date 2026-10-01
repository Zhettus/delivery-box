/*
 * switch_test.c — Door-closed switch only.
 *
 * Wiring (same as the main firmware, board.h PIN_DOOR_SWITCH):
 *   GPIO5 ── switch ── GND      (internal pull-up)
 *
 * Prints every 100 ms:
 *   1   switch pressed (closed) = door closed
 *   0   switch released (open)  = door open
 */
#include <stdio.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SWITCH_GPIO      GPIO_NUM_5
#define PRESSED_LEVEL    0          /* switch to GND: pressed reads LOW */
#define PRINT_EVERY_MS   100

void app_main(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << SWITCH_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);

    while (1) {
        int pressed = gpio_get_level(SWITCH_GPIO) == PRESSED_LEVEL;
        printf("%d\n", pressed);
        vTaskDelay(pdMS_TO_TICKS(PRINT_EVERY_MS));
    }
}
