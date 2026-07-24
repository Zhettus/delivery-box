#include <stdio.h>#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

#define UART_PORT       UART_NUM_1
#define TXD_PIN         17          
#define RXD_PIN         16      
#define DXL_BAUD        57600   
#define DXL_ID          1          

#define ADDR_TORQUE_ENABLE   64
#define ADDR_OPERATING_MODE  11
#define ADDR_GOAL_POSITION   116
#define OP_POSITION          3     //control mode (single turn)

#define INST_PING    0x01
#define INST_READ    0x02
#define INST_WRITE   0x03
#define INST_STATUS  0x55

#define DEG_TO_STEP(d)  (uint32_t)(((uint32_t)(d) * 4096UL + 180UL) / 360UL)

static const char *TAG = "xl430";

static uint16_t crc16(const uint8_t *data, uint16_t len) //checking if the bits are corrupted 
{
    uint16_t crc = 0x0000;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000) crc = (crc << 1) ^ 0x8005;
            else              crc <<= 1;
        }
    }
    return crc;
}

static int dxl_build(uint8_t *buf, uint8_t id, uint8_t instr,
                     const uint8_t *params, uint16_t param_len) //instruction packet into a byte buffer
{
    int i = 0;
    buf[i++] = 0xFF;
    buf[i++] = 0xFF;
    buf[i++] = 0xFD;
    buf[i++] = 0x00;
    buf[i++] = id;

    uint16_t length = param_len + 3;         // instruction + params + 2 CRC
    buf[i++] = length & 0xFF;                // length low
    buf[i++] = (length >> 8) & 0xFF;         // length high
    buf[i++] = instr;
    for (uint16_t k = 0; k < param_len; k++) buf[i++] = params[k];

    uint16_t crc = crc16(buf, i);            // CRC over everything so far
    buf[i++] = crc & 0xFF;
    buf[i++] = (crc >> 8) & 0xFF;
    return i;
}

static void dxl_send(const uint8_t *pkt, int len)
{
    uart_flush_input(UART_PORT);
    uart_write_bytes(UART_PORT, (const char *)pkt, len);
    uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(20));
}

static void dxl_write(uint8_t id, uint16_t addr, const uint8_t *data, uint16_t data_len)
{
    uint8_t params[16];
    params[0] = addr & 0xFF;
    params[1] = (addr >> 8) & 0xFF;
    for (uint16_t k = 0; k < data_len; k++) params[2 + k] = data[k];

    uint8_t pkt[32];
    int n = dxl_build(pkt, id, INST_WRITE, params, data_len + 2);
    dxl_send(pkt, n);
    vTaskDelay(pdMS_TO_TICKS(10));           // let the servo process it
}

static void dxl_write8(uint8_t id, uint16_t addr, uint8_t v)
{
    dxl_write(id, addr, &v, 1);
}

static void dxl_write32(uint8_t id, uint16_t addr, uint32_t v)
{
    uint8_t d[4] = { v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF };
    dxl_write(id, addr, d, 4);
}

static bool dxl_ping(uint8_t id)
{
    uint8_t pkt[16];
    int n = dxl_build(pkt, id, INST_PING, NULL, 0);

    uart_flush_input(UART_PORT);
    uart_write_bytes(UART_PORT, (const char *)pkt, n);
    uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(20));

    uint8_t rx[64];
    int got = uart_read_bytes(UART_PORT, rx, sizeof(rx), pdMS_TO_TICKS(100));

    for (int k = 0; k + 8 <= got; k++) {
        if (rx[k] == 0xFF && rx[k + 1] == 0xFF && rx[k + 2] == 0xFD &&
            rx[k + 3] == 0x00 && rx[k + 4] == id && rx[k + 7] == 0x55) {
            return true;
        }
    }
    return false;
}

static void uart_init(void)
{
    const uart_config_t cfg = {
        .baud_rate  = DXL_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, 256, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, TXD_PIN, RXD_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

void app_main(void)
{
    uart_init();
    vTaskDelay(pdMS_TO_TICKS(300));

    if (!dxl_ping(DXL_ID)) {
        ESP_LOGE(TAG, "No response. Check common ground, TX/1k/RX, ID, baud, 12 V.");
        return;                             
    }
    ESP_LOGI(TAG, "Servo found.");

    dxl_write8(DXL_ID, ADDR_TORQUE_ENABLE, 0);
    dxl_write8(DXL_ID, ADDR_OPERATING_MODE, OP_POSITION);
    dxl_write8(DXL_ID, ADDR_TORQUE_ENABLE, 1);
    const uint32_t targets[2] = { DEG_TO_STEP(30), DEG_TO_STEP(90) };
    int count = 0;
    while (count < 4) {                       // 10 iters = 5 full back-and-forth cycles
        uint32_t goal = targets[count % 2];
        ESP_LOGI(TAG, "-> %lu steps", (unsigned long)goal);
        dxl_write32(DXL_ID, ADDR_GOAL_POSITION, goal);
        vTaskDelay(pdMS_TO_TICKS(1200));       // travel + settle
        count++;
    }


    ESP_LOGI(TAG, "Done.");
}