// Stand-in for ESP-IDF. Reads and writes go to the simulated AC in s21_sim_test.cpp.
#pragma once
#include "esp_err.h"
#define UART_NUM_1 1
#define UART_DATA_8_BITS 3
#define UART_PARITY_EVEN 2
#define UART_STOP_BITS_2 3
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
#define UART_SIGNAL_RXD_INV 1
#define UART_SIGNAL_TXD_INV 2
#define UART_PIN_NO_CHANGE -1
struct uart_config_t {
    int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk;
};
void sim_uart_flush_input();
int sim_uart_write(const unsigned char *data, int len);
int sim_uart_read(unsigned char *byte, int ticks);
static inline esp_err_t uart_param_config(int, const uart_config_t *) { return ESP_OK; }
static inline esp_err_t uart_set_line_inverse(int, unsigned) { return ESP_OK; }
static inline esp_err_t uart_set_pin(int, int, int, int, int) { return ESP_OK; }
static inline bool uart_is_driver_installed(int) { return true; }
static inline esp_err_t uart_driver_install(int, int, int, int, void *, int) { return ESP_OK; }
static inline esp_err_t uart_driver_delete(int) { return ESP_OK; }
static inline esp_err_t uart_set_rx_full_threshold(int, int) { return ESP_OK; }
static inline esp_err_t uart_flush_input(int) { sim_uart_flush_input(); return ESP_OK; }
static inline int uart_write_bytes(int, const void *data, int len) { return sim_uart_write((const unsigned char *)data, len); }
static inline esp_err_t uart_wait_tx_done(int, int) { return ESP_OK; }
static inline int uart_read_bytes(int, void *byte, int, int ticks) { return sim_uart_read((unsigned char *)byte, ticks); }
