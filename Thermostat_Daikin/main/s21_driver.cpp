#include "s21_driver.h"
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <string.h>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>

static const char *TAG = "S21_DRIVER";
#define BUF_SIZE 256

// How long a status that disagrees with a command we just sent is ignored.
#define S21_CONFIRM_TIMEOUT_US (15 * 1000000LL)
// How many polls in a row the AC can stay silent before the link counts as lost.
#define S21_LINK_LOST_POLLS 3

// 1 = hardware UART, 0 = the older bit-banged software UART. Keep the bit-banged
// version until the UART has proven itself on real units.
#ifndef S21_USE_UART
#define S21_USE_UART 1
#endif

static bool s_connected = false;
static int s_tx_pin = 0;
static int s_rx_pin = 0;

#if S21_USE_UART

#define S21_UART_NUM UART_NUM_1

static esp_err_t s21_line_init() {
    gpio_reset_pin((gpio_num_t)s_tx_pin);
    gpio_reset_pin((gpio_num_t)s_rx_pin);

    uart_config_t cfg = {};
    cfg.baud_rate = 2400;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_EVEN;
    cfg.stop_bits = UART_STOP_BITS_2;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;

    esp_err_t err = uart_param_config(S21_UART_NUM, &cfg);
    // The interface board inverts both lines. Set this before the pins are connected,
    // so TX doesn't idle at the wrong level and send the AC a false start bit.
    if (!err) err = uart_set_line_inverse(S21_UART_NUM, UART_SIGNAL_RXD_INV | UART_SIGNAL_TXD_INV);
    if (!err) err = uart_set_pin(S21_UART_NUM, s_tx_pin, s_rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    // The RX FET only pulls low, so the pin needs a pull-up for the high level.
    if (!err) err = gpio_pullup_en((gpio_num_t)s_rx_pin);
    if (!err && !uart_is_driver_installed(S21_UART_NUM)) err = uart_driver_install(S21_UART_NUM, 1024, 0, 0, NULL, 0);
    if (!err) err = uart_set_rx_full_threshold(S21_UART_NUM, 1);
    return err;
}

static void s21_line_deinit() {
    if (uart_is_driver_installed(S21_UART_NUM)) uart_driver_delete(S21_UART_NUM);
}

static void s21_line_flush_rx() {
    uart_flush_input(S21_UART_NUM);
}

static void s21_line_write(const uint8_t *data, int len) {
    uart_write_bytes(S21_UART_NUM, data, len);
    uart_wait_tx_done(S21_UART_NUM, pdMS_TO_TICKS(200));
}

static int s21_line_read(uint32_t timeout_ms) {
    uint8_t byte;
    return uart_read_bytes(S21_UART_NUM, &byte, 1, pdMS_TO_TICKS(timeout_ms)) == 1 ? byte : -1;
}

#else // !S21_USE_UART

#define BIT_DELAY_US 417
static portMUX_TYPE s_spinlock = portMUX_INITIALIZER_UNLOCKED;

static void sw_write_byte(uint8_t byte) {
    taskENTER_CRITICAL(&s_spinlock);
    gpio_set_level((gpio_num_t)s_tx_pin, 1);
    esp_rom_delay_us(BIT_DELAY_US);
    int ones = 0;
    for (int i = 0; i < 8; i++) {
        int bit = (byte >> i) & 0x01;
        if (bit) ones++;
        gpio_set_level((gpio_num_t)s_tx_pin, bit ? 0 : 1); 
        esp_rom_delay_us(BIT_DELAY_US);
    }
    int parity = (ones % 2 == 0) ? 0 : 1;
    gpio_set_level((gpio_num_t)s_tx_pin, parity ? 0 : 1); 
    esp_rom_delay_us(BIT_DELAY_US);
    gpio_set_level((gpio_num_t)s_tx_pin, 0);
    taskEXIT_CRITICAL(&s_spinlock);
    esp_rom_delay_us(BIT_DELAY_US * 2);
}

static int sw_read_byte(uint32_t timeout_ms) {
    int64_t start = esp_timer_get_time();
    int64_t timeout_us = timeout_ms * 1000;
    while (gpio_get_level((gpio_num_t)s_rx_pin) == 0) {
        if (esp_timer_get_time() - start > timeout_us) return -1;
    }
    esp_rom_delay_us(BIT_DELAY_US / 2);
    if (gpio_get_level((gpio_num_t)s_rx_pin) == 0) return -1;
    esp_rom_delay_us(BIT_DELAY_US);
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        int level = gpio_get_level((gpio_num_t)s_rx_pin);
        if (level == 0) byte |= (1 << i); 
        esp_rom_delay_us(BIT_DELAY_US);
    }
    esp_rom_delay_us(BIT_DELAY_US * 2);
    return byte;
}

static esp_err_t s21_line_init() {
    gpio_config_t tx_conf = {};
    tx_conf.pin_bit_mask = (1ULL << s_tx_pin);
    tx_conf.mode = GPIO_MODE_OUTPUT;
    tx_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    esp_err_t err = gpio_config(&tx_conf);
    if (!err) err = gpio_set_level((gpio_num_t)s_tx_pin, 0);
    gpio_config_t rx_conf = {};
    rx_conf.pin_bit_mask = (1ULL << s_rx_pin);
    rx_conf.mode = GPIO_MODE_INPUT;
    rx_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    if (!err) err = gpio_config(&rx_conf);
    return err;
}

static void s21_line_deinit() {}

static void s21_line_flush_rx() {}

static void s21_line_write(const uint8_t *data, int len) {
    for (int i = 0; i < len; i++) sw_write_byte(data[i]);
}

static int s21_line_read(uint32_t timeout_ms) {
    return sw_read_byte(timeout_ms);
}

#endif // S21_USE_UART

DaikinS21::DaikinS21() {
    m_dirty = false;
    m_powerful_dirty = false;
    m_d1_confirm_until = 0;
    m_d6_confirm_until = 0;
    m_fan_confirm_until = 0;
    m_rg_supported = true;
    m_rg_fan_valid = false;
    m_rg_miss = 0;
    m_rd_supported = true;
    m_rd_miss = 0;
    m_g6_valid = false;
    m_link_miss = 0;
    m_callback = nullptr;
    m_state.power = false;
    m_state.mode = FAIKIN_MODE_AUTO;
    m_state.target_temp = 22.0;
    m_state.fan_speed = FAIKIN_FAN_AUTO;
    m_state.current_temp = 21.0;
    m_state.powerful = false;
    m_state.compressor_on = -1;
    m_state.power_w = -1;
    m_state.energy_total_wh = -1;
    m_state.energy_cool_wh = -1;
    m_state.energy_heat_wh = -1;
    m_energy_supported = true;
    m_energy_miss = 0;
}

bool DaikinS21::IsConnected() const {
    return s_connected;
}

void DaikinS21::Deinit() {
    // Release the pins so another backend can probe them; force a fresh detect.
    s_connected = false;
    s21_line_deinit();
    gpio_config_t in = {};
    in.pin_bit_mask = (1ULL << s_tx_pin) | (1ULL << s_rx_pin);
    in.mode = GPIO_MODE_INPUT;
    in.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&in);
}

bool DaikinS21::Detect() {
    // One handshake round. SendPacket() sets s_connected on a valid framed reply.
    SendPacket('F', '8', NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    SendPacket('F', '1', NULL, 0);
    return s_connected;
}

esp_err_t DaikinS21::Init(int tx_pin, int rx_pin) {
    s_tx_pin = tx_pin;
    s_rx_pin = rx_pin;
    esp_err_t err = s21_line_init();
    if (err) {
        ESP_LOGE(TAG, "S21 line init failed: %s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
    s21_line_flush_rx();
    return ESP_OK;
}

esp_err_t DaikinS21::SendPacket(uint8_t cmd1, uint8_t cmd2, uint8_t *payload, int len) {
    uint8_t buf[64];
    buf[0] = STX; buf[1] = cmd1; buf[2] = cmd2;
    if (len > 0) memcpy(&buf[3], payload, len);
    buf[len + 3] = s21_checksum(buf, len + 3 + 2);
    buf[len + 4] = ETX;
    s21_line_flush_rx();
    s21_line_write(buf, len + 5);

    int ack = s21_line_read(800);
    if (ack == -1) return ESP_ERR_TIMEOUT;
    if (ack == NAK) return ESP_FAIL;

    uint8_t rx_buf[BUF_SIZE];
    int rx_idx = 0;
    if (ack == STX) rx_buf[rx_idx++] = STX;
    else if (ack == ACK) {
        if (cmd1 == 'D') return ESP_OK;   // commands only get the ACK, no reply follows
        int val = s21_line_read(500);
        if (val == STX) rx_buf[rx_idx++] = STX;
        else return ESP_OK; 
    }
    while (rx_idx < BUF_SIZE) {
        int val = s21_line_read(100);
        if (val == -1) break;
        rx_buf[rx_idx++] = (uint8_t)val;
        if (val == ETX) break;
    }

    if (rx_idx > 3 && rx_buf[rx_idx-1] == ETX) {
        // ACK even a bad frame. Otherwise the AC may resend it and collide with the next command.
        const uint8_t ack_byte = ACK;
        s21_line_write(&ack_byte, 1);
        uint8_t calc_crc = s21_checksum(rx_buf, rx_idx);
        if (calc_crc == rx_buf[rx_idx-2]) {
            s_connected = true;
            
            // DEBUG DUMP (Keep this to verify mode byte)
            if (rx_buf[1] == 'G' && rx_buf[2] == '1') {
                printf("[S21] G1 RAW: ");
                for(int k=0; k<rx_idx; k++) printf("%02X ", rx_buf[k]);
                printf("\n");
            }

            if ((rx_buf[1] == 'G' || rx_buf[1] == 'H') && rx_buf[2] == '1') {
                 ParseStatusG1(&rx_buf[3], rx_idx - 5);
            }
            else if (rx_buf[1] == 'S' && rx_buf[2] == 'H') {
                 ParseSensorsSH(&rx_buf[3], rx_idx - 5);
            }
            else if (rx_buf[1] == 'S' && rx_buf[2] == 'G') {   // RG -> fan setting
                 ParseFanSG(&rx_buf[3], rx_idx - 5);
            }
            else if (rx_buf[1] == 'S' && rx_buf[2] == 'd') {   // Rd -> compressor speed
                 ParseCompressorSd(&rx_buf[3], rx_idx - 5);
            }
            else if (rx_buf[1] == 'G' && rx_buf[2] == '6') {
                 ParseSettingsG6(&rx_buf[3], rx_idx - 5);
            }
            else if (rx_buf[1] == 'G' && rx_buf[2] == 'X') {   // FX60 -> power
                 ParsePowerGX(&rx_buf[3], rx_idx - 5);
            }
            else if (rx_buf[1] == 'G' && rx_buf[2] == 'M') {   // FM -> total energy
                 ParseEnergyGM(&rx_buf[3], rx_idx - 5);
            }
            else if (rx_buf[1] == 'G' && rx_buf[2] == 'U') {   // FU04 -> cool/heat energy
                 ParseEnergyGU(&rx_buf[3], rx_idx - 5);
            }
        }
    }
    return ESP_OK;
}

void DaikinS21::ParseStatusG1(uint8_t *payload, int len) {
    if (len < 4) return;
    
    // Decode Mode: Byte 1
    uint8_t raw_mode = payload[1];
    uint8_t mode = FAIKIN_MODE_AUTO;
    
    if (raw_mode == 0x33) mode = FAIKIN_MODE_COOL;      // '3'
    else if (raw_mode == 0x34) mode = FAIKIN_MODE_HEAT; // '4'
    else if (raw_mode == 0x32) mode = FAIKIN_MODE_DRY;  // '2'
    else if (raw_mode == 0x36) mode = FAIKIN_MODE_FAN;  // '6'
    
    bool pwr = (payload[0] == '1');
    // Only heat, cool and auto have a setpoint. Keep the last one in Dry and Fan, whose
    // value means nothing, so coming back to heat or cool doesn't pick up a wrong target.
    bool reports_setpoint = (mode != FAIKIN_MODE_FAN && mode != FAIKIN_MODE_DRY);
    float t = reports_setpoint ? s21_decode_target_temp(payload[2]) : m_state.target_temp;
    bool just_confirmed = false;

    // RG is more reliable, so G1's fan byte is only the fallback. Some units report Quiet
    // as Auto here, so don't let that undo Quiet.
    uint8_t raw_fan = payload[3];
    if (!m_rg_fan_valid && raw_fan && strchr("34567AB", raw_fan) &&
        !(raw_fan == AC_FAN_AUTO && m_state.fan_speed == FAIKIN_FAN_QUIET)) {
        UpdateFanFromAC(s21_decode_fan(raw_fan));
    }

    // A newer command is waiting to be sent, so this status is already out of date.
    if (m_dirty) return;

    // The AC can keep reporting the old values for a while after a command, for example
    // during a heat/cool change. Keep what was commanded until it confirms or time runs out.
    if (m_d1_confirm_until) {
        bool has_setpoint = (m_state.mode != FAIKIN_MODE_FAN && m_state.mode != FAIKIN_MODE_DRY);
        bool confirmed = (pwr == m_state.power && mode == m_state.mode &&
                          (!has_setpoint || payload[2] == (uint8_t)s21_encode_target_temp(m_state.target_temp)));
        if (confirmed) {
            m_d1_confirm_until = 0;
            // Nothing below looks changed because the command already updated m_state, but
            // related attributes (on/off and system mode) still need to be brought in line.
            just_confirmed = true;
        } else if (esp_timer_get_time() < m_d1_confirm_until) {
            return;
        } else {
            m_d1_confirm_until = 0;
            ESP_LOGW(TAG, "AC did not confirm the last command, using the state it reports");
        }
    }

    // Check if something changed
    bool changed = (m_state.power != pwr || m_state.mode != mode || m_state.target_temp != t);
    
    if (changed) {
        ESP_LOGI(TAG, "Status Change Detected! Pwr:%d Mode:%d (Raw:%02X)", pwr, mode, raw_mode);
    }

    m_state.power = pwr;
    m_state.mode = mode;
    m_state.target_temp = t;

    if ((changed || just_confirmed) && m_callback) m_callback(&m_state);
}

void DaikinS21::ParseFanSG(uint8_t *payload, int len) {
    if (len < 1 || !payload[0] || !strchr("34567AB", payload[0])) {
        m_rg_fan_valid = false;
        return;
    }
    m_rg_fan_valid = true;
    UpdateFanFromAC(s21_decode_fan(payload[0]));
}

void DaikinS21::UpdateFanFromAC(uint8_t fan) {
    // Same as ParseStatusG1: don't let an out-of-date status undo a command.
    if (m_dirty) return;
    if (m_fan_confirm_until) {
        if (fan == m_state.fan_speed) {
            m_fan_confirm_until = 0;
        } else if (esp_timer_get_time() < m_fan_confirm_until) {
            return;
        } else {
            m_fan_confirm_until = 0;
        }
    }
    if (m_state.fan_speed != fan) {
        ESP_LOGI(TAG, "Fan speed from AC: %u", fan);
        m_state.fan_speed = fan;
        if (m_callback) m_callback(&m_state);
    }
}

void DaikinS21::ParseCompressorSd(uint8_t *payload, int len) {
    // Three reversed digits, unlike the 4-character temperature sensors.
    if (len < 3 || !s21_valid_int_sensor(payload)) return;
    int speed = (payload[0] - '0') + (payload[1] - '0') * 10 + (payload[2] - '0') * 100;
    int8_t on = speed > 0 ? 1 : 0;
    if (m_state.compressor_on != on) {
        ESP_LOGI(TAG, "Compressor %s", on ? "running" : "stopped");
        m_state.compressor_on = on;
        if (m_callback) m_callback(&m_state);
    }
}

void DaikinS21::ParseSettingsG6(uint8_t *payload, int len) {
    if (len < 4) return;

    memcpy(m_g6, payload, sizeof(m_g6));
    m_g6_valid = true;

    // Check the powerful bit (0x02) on the first payload byte
    bool pow = (payload[0] & S21_FLAG_POWERFUL) != 0;

    // Same as ParseStatusG1: don't let an out-of-date status undo a command.
    if (m_powerful_dirty) return;
    if (m_d6_confirm_until) {
        if (pow == m_state.powerful) {
            m_d6_confirm_until = 0;
        } else if (esp_timer_get_time() < m_d6_confirm_until) {
            return;
        } else {
            m_d6_confirm_until = 0;
        }
    }


    if (m_state.powerful != pow) {
        ESP_LOGI(TAG, "Powerful Mode Change Detected! Powerful:%d", pow);
        m_state.powerful = pow;
        if (m_callback) m_callback(&m_state);
    }
}

void DaikinS21::SendControlD6() {
    uint8_t payload[4];

    // Clear this first: a new command can arrive while SendPacket waits for the reply.
    m_powerful_dirty = false;

    // D6 sets everything at once, so start from what the AC last reported. That keeps
    // comfort, quiet, streamer, sensor and LED as they are. Byte 2 isn't used.
    if (m_g6_valid) {
        memcpy(payload, m_g6, sizeof(payload));
    } else {
        memset(payload, S21_D6_BASE_BYTE, sizeof(payload));
    }
    payload[2] = S21_D6_BASE_BYTE;
    if (m_state.powerful) payload[0] |= S21_FLAG_POWERFUL;
    else payload[0] &= ~S21_FLAG_POWERFUL;

    SendPacket('D', '6', payload, 4);
    m_d6_confirm_until = esp_timer_get_time() + S21_CONFIRM_TIMEOUT_US;
}

void DaikinS21::ParseSensorsSH(uint8_t *payload, int len) {
    float room = s21_decode_float_sensor(payload);
    if (room > 0.0 && room < 50.0) {
        if (fabs(m_state.current_temp - room) > 0.1) {
             ESP_LOGI(TAG, "Room Temp Update (SH): %.1f", room);
             m_state.current_temp = room;
             if (m_callback) m_callback(&m_state);
        }
    }
}

void DaikinS21::ParseSensorsGH(uint8_t *p, int l) {}
void DaikinS21::ParseSensorsG9(uint8_t *p, int l) {}

// --- Energy monitoring (S21, decoded per RevK Faikout; best-guess units) ---

void DaikinS21::ParsePowerGX(uint8_t *payload, int len) {
    // FX60 reply: echoes "60" then a 4-hex value in 10W units.
    if (len < 6 || payload[0] != '6' || payload[1] != '0') return;
    int w = (int)s21_decode_hex_sensor(payload + 2) * 10;
    if (m_state.power_w != w) {
        m_state.power_w = w;
        ESP_LOGI(TAG, "[ENERGY] Power: %d W", w);
        if (m_callback) m_callback(&m_state);
    }
}

void DaikinS21::ParseEnergyGM(uint8_t *payload, int len) {
    // FM reply: 4-hex value in 100Wh units (lifetime total/outside energy).
    if (len < 4) return;
    int wh = (int)s21_decode_hex_sensor(payload) * 100;
    if (m_state.energy_total_wh != wh) {
        m_state.energy_total_wh = wh;
        ESP_LOGI(TAG, "[ENERGY] Total: %d Wh", wh);
        if (m_callback) m_callback(&m_state);
    }
}

void DaikinS21::ParseEnergyGU(uint8_t *payload, int len) {
    // FU04 reply: echoes "04" then two 4-hex values (cooling, heating) in 100Wh
    // units; 0xFFFF = not available. (Decoding is "not fully understood" upstream.)
    if (len < 18 || payload[0] != '0' || payload[1] != '4') return;
    uint16_t c = s21_decode_hex_sensor(payload + 2);
    uint16_t h = s21_decode_hex_sensor(payload + 10);
    bool changed = false;
    if (c != 0xFFFF && m_state.energy_cool_wh != (int)c * 100) { m_state.energy_cool_wh = (int)c * 100; changed = true; }
    if (h != 0xFFFF && m_state.energy_heat_wh != (int)h * 100) { m_state.energy_heat_wh = (int)h * 100; changed = true; }
    if (changed) {
        ESP_LOGI(TAG, "[ENERGY] Cooling: %d Wh, Heating: %d Wh", m_state.energy_cool_wh, m_state.energy_heat_wh);
        if (m_callback) m_callback(&m_state);
    }
}

void DaikinS21::SendControlD1() {
    uint8_t payload[4];

    // Clear this first: a new command can arrive while SendPacket waits for the reply.
    m_dirty = false;

    payload[0] = m_state.power ? '1' : '0';
    switch(m_state.mode) {
        case FAIKIN_MODE_COOL: payload[1] = 0x33; break;
        case FAIKIN_MODE_HEAT: payload[1] = 0x34; break;
        case FAIKIN_MODE_DRY:  payload[1] = 0x32; break;
        case FAIKIN_MODE_FAN:  payload[1] = 0x36; break;
        default: payload[1] = 0x30; break; 
    }
    if (m_state.mode == FAIKIN_MODE_FAN || m_state.mode == FAIKIN_MODE_DRY)
        payload[2] = AC_MIN_TEMP_VALUE;
    else
        payload[2] = (uint8_t)s21_encode_target_temp(m_state.target_temp);

    payload[3] = s21_encode_fan(m_state.fan_speed);
    SendPacket('D', '1', payload, 4);
    m_d1_confirm_until = esp_timer_get_time() + S21_CONFIRM_TIMEOUT_US;
    m_fan_confirm_until = m_d1_confirm_until;
}

void DaikinS21::PollOptional(uint8_t cmd2, bool &supported, int &miss) {
    if (!supported) return;
    vTaskDelay(pdMS_TO_TICKS(100));
    if (SendPacket('R', cmd2, NULL, 0) == ESP_OK) {
        miss = 0;
    } else if (++miss >= 3) {
        supported = false;
        ESP_LOGW(TAG, "AC does not answer R%c", cmd2);
    }
}

void DaikinS21::Poll() {
    if (!s_connected) {
        SendPacket('F', '8', NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
        SendPacket('F', '1', NULL, 0); 
        return; 
    }
    if (m_dirty) {
        SendControlD1();
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (m_powerful_dirty) {
        SendControlD6();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    // Poll Status (F1 -> G1)
    if (SendPacket('F', '1', NULL, 0) == ESP_ERR_TIMEOUT) {
        if (++m_link_miss >= S21_LINK_LOST_POLLS) {
            ESP_LOGW(TAG, "AC stopped answering");
            m_link_miss = 0;
            s_connected = false;   // the manager re-detects the protocol
            return;
        }
    } else {
        m_link_miss = 0;
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    SendPacket('F', '6', NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Poll Sensor (RH -> SH)
    SendPacket('R', 'H', NULL, 0);

    // Poll the fan setting (RG -> SG) and compressor speed (Rd -> Sd). Not every unit answers.
    // G1's fan byte and the temperatures are the fallbacks.
    PollOptional('G', m_rg_supported, m_rg_miss);
    if (!m_rg_supported) m_rg_fan_valid = false;
    PollOptional('d', m_rd_supported, m_rd_miss);

    // Poll energy meters (S21 FX60 power, FM total Wh, FU04 cool/heat Wh).
    // Many units don't support these; if the unit ignores/NAKs them for a few
    // cycles, stop asking so we don't waste time on read timeouts.
    if (m_energy_supported) {
        uint8_t p60[2] = { '6', '0' };
        uint8_t p04[2] = { '0', '4' };
        bool any = false;
        vTaskDelay(pdMS_TO_TICKS(300));
        if (SendPacket('F', 'X', p60, 2) == ESP_OK) any = true;
        vTaskDelay(pdMS_TO_TICKS(300));
        if (SendPacket('F', 'M', NULL, 0) == ESP_OK) any = true;
        vTaskDelay(pdMS_TO_TICKS(300));
        if (SendPacket('F', 'U', p04, 2) == ESP_OK) any = true;
        if (any) {
            m_energy_miss = 0;
        } else if (++m_energy_miss >= 3) {
            m_energy_supported = false;
            ESP_LOGW(TAG, "AC does not answer S21 energy queries (FX/FM/FU); energy polling disabled");
        }
    }
}

// Setters
void DaikinS21::SetPower(bool on) { if(m_state.power != on) { m_state.power = on; m_dirty = true; } }
void DaikinS21::SetMode(uint8_t mode) { if(m_state.mode != mode) { m_state.mode = mode; m_dirty = true; } }
void DaikinS21::SetTemp(float temp) { if(fabs(m_state.target_temp - temp) > 0.1) { m_state.target_temp = temp; m_dirty = true; } }
// Only mark a change: reporting the fan state to Matter calls this again with the same value.
void DaikinS21::SetFan(uint8_t fan) { if(m_state.fan_speed != fan) { m_state.fan_speed = fan; m_dirty = true; } }
// SetStateCallback is defined inline in s21_driver.h
void DaikinS21::SetPowerful(bool on) {
    if(m_state.powerful != on) { 
        m_state.powerful = on; 
        m_powerful_dirty = true;
    } 
}