#include "tail-light.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PIN_RUN    GPIO_NUM_26   /* IN1 */
#define PIN_BRAKE  GPIO_NUM_25   /* IN2 */
#define PIN_LEFT   GPIO_NUM_33   /* IN3 */
#define PIN_RIGHT  GPIO_NUM_32   /* IN4 */

#if CONFIG_IDF_TARGET_ESP32
  #define PIN_IS_FLASH(p)  ((p) >= 6 && (p) <= 11)
  _Static_assert(!PIN_IS_FLASH(PIN_RUN),   "PIN_RUN is an SPI flash pin");
  _Static_assert(!PIN_IS_FLASH(PIN_BRAKE), "PIN_BRAKE is an SPI flash pin");
  _Static_assert(!PIN_IS_FLASH(PIN_LEFT),  "PIN_LEFT is an SPI flash pin");
  _Static_assert(!PIN_IS_FLASH(PIN_RIGHT), "PIN_RIGHT is an SPI flash pin");
#endif

#define RELAY_CLOSED  0
#define RELAY_OPEN    1

static const char *TAG = "tail-light";

static light_t g_state   = {0};
static bool    g_inited  = false;


static inline void write_lamp(gpio_num_t pin, bool on)
{
    gpio_set_level(pin, on ? RELAY_CLOSED : RELAY_OPEN);
}

static void push(void)
{
    write_lamp(PIN_RUN,   g_state.run);
    write_lamp(PIN_BRAKE, g_state.brake);
    write_lamp(PIN_LEFT,  g_state.left);
    write_lamp(PIN_RIGHT, g_state.right);
}


void light_init(void)
{
    if (g_inited) {
        return;
    }

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_RUN)  | (1ULL << PIN_BRAKE) |
                        (1ULL << PIN_LEFT) | (1ULL << PIN_RIGHT),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        return;
    }

    g_state  = (light_t){0};
    push();                       
    g_inited = true;

    ESP_LOGI(TAG, "init ok: run=%d brake=%d left=%d right=%d (active low)",
             PIN_RUN, PIN_BRAKE, PIN_LEFT, PIN_RIGHT);
}

void light_set(light_t s)
{
    g_state = s;
    push();
}

light_t light_get(void)
{
    return g_state;
}

void light_run  (bool on) { g_state.run   = on; push(); }
void light_brake(bool on) { g_state.brake = on; push(); }
void light_left (bool on) { g_state.left  = on; push(); }
void light_right(bool on) { g_state.right = on; push(); }

void light_all_off(void) { light_set((light_t){0}); }
void light_moving (void) { light_set((light_t){ .run = true }); }
void light_stop   (void) { light_set((light_t){ .run = true, .brake = true }); }

void light_hazards(bool on)
{
    g_state.left  = on;
    g_state.right = on;
    push();
}

void light_signal_left(bool on)
{
    g_state.left  = on;
    g_state.right = false;
    push();
}

void light_signal_right(bool on)
{
    g_state.right = on;
    g_state.left  = false;
    push();
}




/*#include "tail-light.h"
#include "driver/gpio.h"
#include "esp_log.h"              // ESP_LOGI and friends
#include "freertos/FreeRTOS.h"    // pdMS_TO_TICKS
#include "freertos/task.h"        // vTaskDelay

#define PIN_RUN    GPIO_NUM_10   // IN1
#define PIN_BRAKE  GPIO_NUM_9    // IN2
#define PIN_LEFT   GPIO_NUM_7    // IN3
#define PIN_RIGHT  GPIO_NUM_8    // IN4

static const char *TAG = "tail-light";
static light_t g_state = {0};

static inline void write_lamp(gpio_num_t pin, bool on) {
    gpio_set_level(pin, on ? 0 : 1);          
}

static void push(void) {                     
    write_lamp(PIN_RUN,   g_state.run);
    write_lamp(PIN_BRAKE, g_state.brake);
    write_lamp(PIN_LEFT,  g_state.left);
    write_lamp(PIN_RIGHT, g_state.right);
}

void light_init(void) {
    gpio_config_t io = {
        .pin_bit_mask = (1ULL<<PIN_RUN)|(1ULL<<PIN_BRAKE)|(1ULL<<PIN_LEFT)|(1ULL<<PIN_RIGHT),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    g_state = (light_t){0};
    push();                             
}

void    light_set(light_t s) { g_state = s; push(); }
light_t light_get(void)      { return g_state; }

void light_run  (bool on) { g_state.run   = on; push(); }
void light_brake(bool on) { g_state.brake = on; push(); }
void light_left (bool on) { g_state.left  = on; push(); }
void light_right(bool on) { g_state.right = on; push(); }


void light_all_off(void)         { light_set((light_t){0}); }
void light_moving (void)         { light_set((light_t){ .run=true }); }
void light_stop   (void)         { light_set((light_t){ .run=true, .brake=true }); }
void light_hazards(bool on)      { g_state.left = on; g_state.right = on; push(); }
void light_signal_left (bool on) { g_state.left  = on; g_state.right = false; push(); }
void light_signal_right(bool on) { g_state.right = on; g_state.left  = false; push(); }
*/
/*
void app_main(void){
    light_init();

    ESP_LOGI(TAG, "Lights are starting....");

    light_run(true);                     // running light on
    vTaskDelay(pdMS_TO_TICKS(5000));
    light_run(false);

    light_stop();                        // run + brake (no argument)
    vTaskDelay(pdMS_TO_TICKS(3000));
    light_all_off();                     // clear the stop state

    light_left(true);                    // left signal — hardware sweeps
    vTaskDelay(pdMS_TO_TICKS(5000));
    light_left(false);

    ESP_LOGI(TAG, "DONE");
}*/