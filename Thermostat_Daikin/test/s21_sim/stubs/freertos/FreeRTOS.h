// Stand-in for ESP-IDF. One tick is 10 ms, matching CONFIG_FREERTOS_HZ=100.
#pragma once
#include <cstdint>
#define pdMS_TO_TICKS(ms) ((ms) / 10)
extern int64_t g_sim_time_us;
