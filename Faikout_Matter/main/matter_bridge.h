// Matter over Wi-Fi for Faikout.
//
// Faikout keeps owning Wi-Fi, Ethernet, BLE, MQTT, the web UI and its own OTA. Matter starts once
// the board is on the network, and pairing happens over that network (no BLE).
//
// Faikout.c is C with settings macros that clash with the Matter C++ headers, so the two sides
// only talk through this file.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Thermostat running state bits, as used by Matter.
#define MATTER_RUNNING_HEAT 0x1
#define MATTER_RUNNING_COOL 0x2
#define MATTER_RUNNING_FAN  0x4

// What Matter needs to know about the AC. Clear it with memset() before filling, because
// Faikout.c compares whole snapshots to spot changes.
typedef struct {
    bool power;
    bool power_known;
    bool mode_known;
    bool fan_known;
    bool powerful;
    uint8_t mode;       // FAIKIN_MODE_*
    uint8_t fan;        // FAIKIN_FAN_*
    uint16_t running;   // MATTER_RUNNING_*
    float target;       // C, NAN if unknown
    float current;      // C, NAN if unknown
    int32_t power_w;    // -1 if unknown
    int32_t energy_wh;  // -1 if unknown
} matter_ac_state_t;

// For the web page.
typedef struct {
    bool started;
    bool window_open;
    uint8_t fabrics;
    uint32_t free_internal;     // Heap, bytes
    uint32_t largest_internal;
    uint32_t free_psram;
    char manual_code[24];
    char qr_code[32];
} matter_info_t;

// Implemented in matter_bridge.cpp.
void matter_bridge_start(void);                             // Returns at once; waits for the network in its own task
bool matter_bridge_report(const matter_ac_state_t *state);  // False if Matter isn't running yet
void matter_bridge_open_window(void);                       // Open pairing for 5 minutes
void matter_bridge_info(matter_info_t *info);

// Implemented in Faikout.c. The setters run on the Matter task.
void faikout_matter_snapshot(matter_ac_state_t *state);
void faikout_matter_set_power(bool on);
void faikout_matter_set_mode(uint8_t faikin_mode);
void faikout_matter_set_temp(float celsius);
void faikout_matter_set_fan(uint8_t faikin_fan);
void faikout_matter_set_powerful(bool on);

#ifdef __cplusplus
}
#endif
