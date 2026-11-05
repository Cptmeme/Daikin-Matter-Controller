// Stand-in for ESP-IDF, just enough to compile s21_driver.cpp on a computer.
#pragma once
#include <cstdio>
#define ESP_LOGI(tag, ...) do { (void)(tag); } while (0)
#define ESP_LOGW(tag, ...) do { (void)(tag); } while (0)
#define ESP_LOGE(tag, ...) do { (void)(tag); } while (0)
