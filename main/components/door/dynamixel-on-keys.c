#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"

#define UART_DXL   UART_NUM_1      // servo bus
#define DXL_TX     17
#define DXL_RX     16
#define UART_KEY   UART_NUM_0      // USB console (keyboard in + messages out)

#define DXL_ID     1
#define DXL_BAUD   57600

#define ADDR_TORQUE_ENABLE   64
#define ADDR_OPERATING_MODE  11
#define ADDR_GOAL_POSITION   116
#define ADDR_PRESENT_POS     132
#define OP_POSITION          3

#define STEP_DEG   30
#define MIN_DEG    0
#define MAX_DEG    360

static int target = 180;          // commanded angle in degrees

static void cprintf(const char *fmt, ...) {
    char buf[96];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) uart_write_bytes(UART_KEY, buf, n);
}

//CRC verified against the standard check vector
static uint16_t crc16(const uint8_t *p, int n) {
    uint16_t c = 0;
    for (int i = 0; i < n; i++) { c ^= (uint16_t)p[i] << 8;
        for (int b = 0; b < 8; b++) c = (c & 0x8000) ? (c << 1) ^ 0x8005 : c << 1; }
    return c;
}

static int dxl_build(uint8_t *buf, uint8_t instr, const uint8_t *params, int plen) {
    int i = 0;
    buf[i++]=0xFF; buf[i++]=0xFF; buf[i++]=0xFD; buf[i++]=0x00; buf[i++]=DXL_ID;
    int L = plen + 3;
    buf[i++]=L & 0xFF; buf[i++]=L >> 8; buf[i++]=instr;
    for (int k = 0; k < plen; k++) buf[i++]=params[k];
    uint16_t c = crc16(buf, i); buf[i++]=c & 0xFF; buf[i++]=c >> 8;
    return i;
}

static int dxl_txrx(const uint8_t *pkt, int len, uint8_t *rx, int rxcap) {
    uart_flush_input(UART_DXL);
    uart_write_bytes(UART_DXL, (const char *)pkt, len);
    uart_wait_tx_done(UART_DXL, pdMS_TO_TICKS(20));
    if (!rx) return 0;
    return uart_read_bytes(UART_DXL, rx, rxcap, pdMS_TO_TICKS(100));
}

static void dxl_write(uint16_t addr, const uint8_t *data, int len) {
    uint8_t params[16];
    params[0]=addr & 0xFF; params[1]=addr >> 8;
    for (int k = 0; k < len; k++) params[2+k]=data[k];
    uint8_t pkt[32];
    int n = dxl_build(pkt, 0x03, params, len + 2);
    dxl_txrx(pkt, n, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
}

static void dxl_write8(uint16_t addr, uint8_t v)  { dxl_write(addr, &v, 1); }
static void dxl_write32(uint16_t addr, uint32_t v){ uint8_t d[4]={v,v>>8,v>>16,v>>24}; dxl_write(addr,d,4); }

static bool dxl_ping(void) {
    uint8_t pkt[16]; int n = dxl_build(pkt, 0x01, NULL, 0);
    uint8_t rx[64]; int got = dxl_txrx(pkt, n, rx, sizeof(rx));
    for (int k = 0; k + 8 <= got; k++)
        if (rx[k]==0xFF&&rx[k+1]==0xFF&&rx[k+2]==0xFD&&rx[k+3]==0x00&&rx[k+4]==DXL_ID&&rx[k+7]==0x55)
            return true;
    return false;
}

// Returns current angle in degrees, or -1 if no valid reply.
static int dxl_read_present_deg(void) {
    uint8_t params[4] = { ADDR_PRESENT_POS & 0xFF, ADDR_PRESENT_POS >> 8, 4, 0 };
    uint8_t pkt[32]; int n = dxl_build(pkt, 0x02 /*READ*/, params, 4);
    uint8_t rx[64]; int got = dxl_txrx(pkt, n, rx, sizeof(rx));
    for (int k = 0; k + 13 <= got; k++)
        if (rx[k]==0xFF&&rx[k+1]==0xFF&&rx[k+2]==0xFD&&rx[k+3]==0x00&&rx[k+4]==DXL_ID&&rx[k+7]==0x55) {
            uint32_t pos = rx[k+9] | (rx[k+10]<<8) | (rx[k+11]<<16) | ((uint32_t)rx[k+12]<<24);
            return (pos * 360 + 2048) / 4096;      // steps -> degrees, rounded
        }
    return -1;
}

static void goal_deg(int deg) {
    uint32_t steps = ((uint32_t)deg * 4096 + 180) / 360;   // degrees -> steps, rounded
    dxl_write32(ADDR_GOAL_POSITION, steps);
}

static void move_by(int d) {
    target += d;
    if (target < MIN_DEG) target = MIN_DEG;
    if (target > MAX_DEG) target = MAX_DEG;
    goal_deg(target);
    cprintf("target = %d deg\r\n", target);
}

static void uart_init_port(uart_port_t port, int baud, int tx, int rx) {
    uart_config_t cfg = { .baud_rate=baud, .data_bits=UART_DATA_8_BITS,
        .parity=UART_PARITY_DISABLE, .stop_bits=UART_STOP_BITS_1,
        .flow_ctrl=UART_HW_FLOWCTRL_DISABLE, .source_clk=UART_SCLK_DEFAULT };
    uart_driver_install(port, 256, 0, 0, NULL, 0);
    uart_param_config(port, &cfg);
    uart_set_pin(port, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

void app_main(void) {
    uart_init_port(UART_DXL, DXL_BAUD, DXL_TX, DXL_RX);
    uart_init_port(UART_KEY, 115200, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE); // console pins
    vTaskDelay(pdMS_TO_TICKS(300));

    if (!dxl_ping()) { cprintf("No servo. Check ground / TX-1k-RX / ID / baud / 12 V.\r\n"); return; }
    cprintf("Servo found.\r\n");

    dxl_write8(ADDR_TORQUE_ENABLE, 0);
    dxl_write8(ADDR_OPERATING_MODE, OP_POSITION);
    dxl_write8(ADDR_TORQUE_ENABLE, 1);

    int here = dxl_read_present_deg();         // start from actual position, no boot jump
    if (here >= MIN_DEG && here <= MAX_DEG) target = here;
    goal_deg(target);
    cprintf("Ready. Left / Right arrows (or a / d) move %d deg per press.\r\n", STEP_DEG);

    uint8_t c;
    while (1) {
        if (uart_read_bytes(UART_KEY, &c, 1, pdMS_TO_TICKS(100)) <= 0) continue;

        if (c == 0x1B) {                       // ESC: arrow-key sequence
            uint8_t seq[2];
            if (uart_read_bytes(UART_KEY, seq, 2, pdMS_TO_TICKS(20)) == 2 &&
                (seq[0] == '[' || seq[0] == 'O')) {
                if      (seq[1] == 'D') move_by(-STEP_DEG);   // left
                else if (seq[1] == 'C') move_by(+STEP_DEG);   // right
            }
        } else if (c == 'a') {
            move_by(-STEP_DEG);
        } else if (c == 'd') {
            move_by(+STEP_DEG);
        }
    }
}