#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "daikin_s21.h"
#include "ac_state.h"
#include "ac_backend.h"

// Daikin S21 protocol backend: UART at 2400 baud 8E2, inverted through the
// BSS138 level shifters. See S21_USE_UART in s21_driver.cpp.
class DaikinS21 : public ACBackend {
public:
    DaikinS21();

    // --- ACBackend ---
    const char *Name() const override { return "S21"; }
    esp_err_t Init(int tx_pin, int rx_pin) override;
    void Deinit() override;
    bool Detect() override;
    bool IsConnected() const override;
    void Poll() override;

    void SetPower(bool on) override;
    void SetMode(uint8_t mode) override;
    void SetTemp(float temp) override;
    void SetFan(uint8_t fan) override;
    void SetPowerful(bool on) override;

    ac_state_t GetState() const override { return m_state; }
    void SetStateCallback(s21_state_change_cb_t cb) override { m_callback = cb; }

private:
    ac_state_t m_state;
    bool m_dirty;
    bool m_powerful_dirty;
    int64_t m_d1_confirm_until;   // esp_timer time to stop waiting for D1 to show up in G1; 0 = not waiting
    int64_t m_d6_confirm_until;   // same for D6 and G6
    int64_t m_fan_confirm_until;  // same for the fan setting in D1
    bool m_rg_supported;          // set false if the unit doesn't answer RG
    bool m_rg_fan_valid;          // RG gives a usable fan setting, so ignore G1's
    int m_rg_miss;                // consecutive RG polls with no answer
    bool m_rd_supported;          // set false if the unit doesn't answer Rd
    int m_rd_miss;                // consecutive Rd polls with no answer
    uint8_t m_g6[4];              // last G6 payload, so D6 can keep the other settings
    bool m_g6_valid;
    int m_link_miss;              // consecutive F1 polls with no answer
    s21_state_change_cb_t m_callback;
    bool m_energy_supported;   // set false if the unit NAKs the energy queries
    int m_energy_miss;         // consecutive cycles with no energy response

    // Internal helpers
    esp_err_t SendPacket(uint8_t cmd1, uint8_t cmd2, uint8_t *payload, int len);
    void ParseStatusG1(uint8_t *payload, int len);
    void ParseSettingsG6(uint8_t *payload, int len);
    void ParseFanSG(uint8_t *payload, int len);     // RG -> fan setting
    void UpdateFanFromAC(uint8_t fan);
    void ParseCompressorSd(uint8_t *payload, int len); // Rd -> compressor speed
    void PollOptional(uint8_t cmd2, bool &supported, int &miss);
    void ParseSensorsGH(uint8_t *payload, int len);
    void ParseSensorsG9(uint8_t *payload, int len);
    void ParseSensorsSH(uint8_t *payload, int len);
    void ParsePowerGX(uint8_t *payload, int len);   // FX60 -> instantaneous W
    void ParseEnergyGM(uint8_t *payload, int len);   // FM   -> total Wh
    void ParseEnergyGU(uint8_t *payload, int len);   // FU04 -> cool/heat Wh
    void SendControlD1();
    void SendControlD6();
};
