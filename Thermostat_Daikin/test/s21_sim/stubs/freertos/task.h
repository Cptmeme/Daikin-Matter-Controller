// Stand-in for ESP-IDF. Waiting just moves the simulated clock forward.
#pragma once
static inline void vTaskDelay(int ticks) { g_sim_time_us += ticks * 10000LL; }
