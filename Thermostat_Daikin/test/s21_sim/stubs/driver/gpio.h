// Stand-in for ESP-IDF, just enough to compile s21_driver.cpp on a computer.
#pragma once
#include <cstdint>
#include "esp_err.h"
typedef int gpio_num_t;
enum { GPIO_MODE_INPUT, GPIO_MODE_OUTPUT };
enum { GPIO_PULLUP_DISABLE, GPIO_PULLUP_ENABLE };
struct gpio_config_t {
    uint64_t pin_bit_mask;
    int mode;
    int pull_up_en;
};
static inline esp_err_t gpio_config(const gpio_config_t *) { return ESP_OK; }
static inline esp_err_t gpio_reset_pin(gpio_num_t) { return ESP_OK; }
static inline esp_err_t gpio_pullup_en(gpio_num_t) { return ESP_OK; }
