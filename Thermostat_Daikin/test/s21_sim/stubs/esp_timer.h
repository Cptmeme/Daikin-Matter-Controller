// Stand-in for ESP-IDF. Time is simulated, so the tests run instantly.
#pragma once
#include <cstdint>
extern int64_t g_sim_time_us;
static inline int64_t esp_timer_get_time() { return g_sim_time_us; }
