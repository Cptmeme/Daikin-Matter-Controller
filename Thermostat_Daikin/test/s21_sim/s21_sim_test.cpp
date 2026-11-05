// Runs the real S21 driver (main/s21_driver.cpp) against a simulated AC, with no hardware.
// Run it with ./run.sh. Time is simulated, so it finishes instantly.
//
// The simulated AC is a model of how a unit behaves, based on Faikout's protocol notes and
// on what was seen on one real unit. A pass doesn't prove that a given AC behaves the same.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>

#include "driver/uart.h"
#include "s21_driver.h"

int64_t g_sim_time_us = 0;

namespace {

struct SimulatedAC {
    // What the AC reports.
    bool power = false;
    char mode = '4';          // heat
    char target = '@' + 2;    // 19 C
    char fan = 'A';
    unsigned char g6[4] = {'0', '0', '0', '0'};
    int compressor_speed = 0;
    std::string energy = "0100";

    // How it behaves.
    bool answers = true;
    bool supports_rg = true;
    bool supports_rd = true;
    bool corrupt_next_status = false;
    int status_lag = 0;       // polls that a D1 takes to show up in G1

    // A D1 that hasn't taken effect yet.
    bool has_pending = false;
    int pending_polls = 0;
    bool pending_power = false;
    char pending_mode = 0, pending_target = 0, pending_fan = 0;

    // What the driver sent.
    int acks_received = 0;
    int d1_count = 0;
    std::string last_d1;
    unsigned char last_d6[4] = {0, 0, 0, 0};
};

SimulatedAC ac;
std::deque<unsigned char> rx_queue;

void queue_frame(const std::string &body) {
    unsigned char frame[80];
    int len = 0;
    frame[len++] = STX;
    for (char c : body) frame[len++] = (unsigned char)c;
    frame[len++] = 0;
    frame[len++] = ETX;
    frame[len - 2] = s21_checksum(frame, len);
    for (int i = 0; i < len; i++) rx_queue.push_back(frame[i]);
}

void apply_pending() {
    ac.power = ac.pending_power;
    ac.mode = ac.pending_mode;
    ac.target = ac.pending_target;
    ac.fan = ac.pending_fan;
    ac.has_pending = false;
}

void handle_d1(const unsigned char *payload) {
    ac.d1_count++;
    ac.last_d1 = std::string((const char *)payload, 4);
    ac.pending_power = payload[0] == '1';
    ac.pending_mode = payload[1];
    ac.pending_target = payload[2];
    ac.pending_fan = payload[3];
    if (ac.status_lag > 0) {
        ac.has_pending = true;
        ac.pending_polls = ac.status_lag;
    } else {
        apply_pending();
    }
}

void handle_f1() {
    std::string status = "G1";
    status += ac.power ? '1' : '0';
    status += ac.mode;
    status += ac.target;
    status += ac.fan;
    if (ac.has_pending && --ac.pending_polls <= 0) apply_pending();
    size_t start = rx_queue.size();
    queue_frame(status);
    if (ac.corrupt_next_status) {
        ac.corrupt_next_status = false;
        rx_queue[start + 3] ^= 1;
    }
}

}  // namespace

void sim_uart_flush_input() { rx_queue.clear(); }

int sim_uart_write(const unsigned char *data, int len) {
    if (len == 1 && data[0] == ACK) {
        ac.acks_received++;
        return len;
    }
    if (!ac.answers || len < 5 || data[0] != STX) return len;

    char cmd1 = data[1], cmd2 = data[2];
    const unsigned char *payload = data + 3;
    bool known = true;
    std::string reply;

    if (cmd1 == 'D') {
        rx_queue.push_back(ACK);
        if (cmd2 == '1') handle_d1(payload);
        if (cmd2 == '6') {
            memcpy(ac.last_d6, payload, 4);
            memcpy(ac.g6, payload, 4);
        }
        return len;
    }
    if (cmd1 == 'F' && cmd2 == '1') {
        rx_queue.push_back(ACK);
        handle_f1();
        return len;
    }
    if (cmd1 == 'F' && cmd2 == '8') {
        reply = "G80000";
    } else if (cmd1 == 'F' && cmd2 == '6') {
        reply = "G6" + std::string((const char *)ac.g6, 4);
    } else if (cmd1 == 'F' && cmd2 == 'M') {
        reply = "GM" + ac.energy;
    } else if (cmd1 == 'R' && cmd2 == 'H') {
        reply = "SH081+";   // reversed digits: 18.0 C
    } else if (cmd1 == 'R' && cmd2 == 'G' && ac.supports_rg) {
        reply = std::string("SG") + ac.fan;
    } else if (cmd1 == 'R' && cmd2 == 'd' && ac.supports_rd) {
        char digits[4];
        snprintf(digits, sizeof(digits), "%d%d%d", ac.compressor_speed % 10, (ac.compressor_speed / 10) % 10,
                 ac.compressor_speed / 100);
        reply = std::string("Sd") + digits;
    } else {
        known = false;
    }
    rx_queue.push_back(known ? ACK : NAK);
    if (known) queue_frame(reply);
    return len;
}

int sim_uart_read(unsigned char *byte, int ticks) {
    if (rx_queue.empty()) {
        g_sim_time_us += ticks * 10000LL;
        return 0;
    }
    *byte = rx_queue.front();
    rx_queue.pop_front();
    g_sim_time_us += 5000;   // one byte at 2400 baud
    return 1;
}

namespace {

int g_failures = 0;
int g_reports = 0;
ac_state_t g_last_report;

void check(const char *name, bool passed) {
    printf("%-72s %s\n", name, passed ? "PASS" : "FAIL");
    if (!passed) g_failures++;
}

void on_report(const ac_state_t *state) {
    g_reports++;
    g_last_report = *state;
}

DaikinS21 *connect_to_new_ac() {
    ac = SimulatedAC();
    rx_queue.clear();
    g_reports = 0;
    DaikinS21 *driver = new DaikinS21();
    driver->SetStateCallback(on_report);
    driver->Init(21, 20);
    driver->Detect();
    return driver;
}

// The poll task waits 2 s between polls.
void poll(DaikinS21 *driver, int times = 1) {
    for (int i = 0; i < times; i++) {
        driver->Poll();
        g_sim_time_us += 2000000;
    }
}

void test_connecting() {
    DaikinS21 *d = connect_to_new_ac();
    check("detect finds the AC", d->IsConnected());
    poll(d);
    check("room temperature is read", fabs(d->GetState().current_temp - 18.0) < 0.01);
}

void test_mode_change_with_a_slow_ac() {
    DaikinS21 *d = connect_to_new_ac();
    ac.power = true;
    poll(d, 2);
    int before = ac.d1_count;
    ac.status_lag = 2;
    d->SetMode(FAIKIN_MODE_COOL);
    poll(d, 5);
    check("heat to cool on a slow AC sends exactly one command (no flip-flop)", ac.d1_count == before + 1);
    check("  the AC ends up in cool", ac.mode == '3');
    check("  the driver stays on cool", d->GetState().mode == FAIKIN_MODE_COOL);
}

void test_repeating_the_same_fan_speed_sends_nothing() {
    // Reporting the fan state to Matter can come back as a fan write with the same value.
    // Resending on that is what made the AC flip between heat and cool.
    DaikinS21 *d = connect_to_new_ac();
    ac.power = true;
    poll(d, 2);
    int before = ac.d1_count;
    d->SetFan(d->GetState().fan_speed);
    poll(d);
    check("setting the same fan speed again sends no command", ac.d1_count == before);
}

void test_command_the_ac_never_confirms() {
    DaikinS21 *d = connect_to_new_ac();
    ac.power = true;
    poll(d, 2);
    ac.status_lag = 1000;
    d->SetMode(FAIKIN_MODE_COOL);
    poll(d);
    check("unconfirmed command: the commanded mode is kept at first", d->GetState().mode == FAIKIN_MODE_COOL);
    poll(d, 6);
    check("  after the 15 s window, the driver accepts what the AC reports", d->GetState().mode == FAIKIN_MODE_HEAT);
}

void test_dry_and_fan_keep_the_setpoint() {
    DaikinS21 *d = connect_to_new_ac();
    ac.power = true;
    poll(d);
    float target = d->GetState().target_temp;
    ac.mode = '2';    // Dry, set from the remote
    ac.target = 'z';  // meaningless in Dry
    poll(d, 2);
    check("Dry from the remote keeps the setpoint", d->GetState().mode == FAIKIN_MODE_DRY && d->GetState().target_temp == target);
    d->SetMode(FAIKIN_MODE_COOL);
    poll(d);
    check("  going back to cool sends that setpoint", ac.last_d1[2] == (char)s21_encode_target_temp(target));
}

void test_powerful_keeps_other_settings() {
    const unsigned char byte0 = '0' | S21_FLAG_QUIET | S21_FLAG_COMFORT;
    const unsigned char byte1 = '0' | S21_FLAG_STREAMER;
    const unsigned char byte3 = '0' | 0x08;  // sensor
    DaikinS21 *d = connect_to_new_ac();
    ac.power = true;
    ac.g6[0] = byte0;
    ac.g6[1] = byte1;
    ac.g6[3] = byte3;
    poll(d);
    d->SetPowerful(true);
    poll(d);
    check("Powerful on keeps comfort, quiet, streamer and sensor",
          ac.last_d6[0] == (byte0 | S21_FLAG_POWERFUL) && ac.last_d6[1] == byte1 && ac.last_d6[3] == byte3);
    check("  Powerful reads as on", d->GetState().powerful);
    d->SetPowerful(false);
    poll(d);
    check("Powerful off clears only that flag", ac.last_d6[0] == byte0 && ac.last_d6[1] == byte1 && ac.last_d6[3] == byte3);
}

void test_fan_speeds() {
    DaikinS21 *d = connect_to_new_ac();
    ac.power = true;
    poll(d);
    d->SetFan(FAIKIN_FAN_QUIET);
    poll(d);
    check("Quiet is sent as B", ac.last_d1[3] == 'B');
    poll(d, 2);
    check("  Quiet stays set when read back", d->GetState().fan_speed == FAIKIN_FAN_QUIET);

    d = connect_to_new_ac();
    ac.power = true;
    poll(d);
    ac.fan = '5';
    poll(d);
    check("a fan change on the remote is read back (level 3)", d->GetState().fan_speed == FAIKIN_FAN_3);
    d->SetTemp(25);
    poll(d);
    check("  a setpoint command then keeps that fan speed", ac.last_d1[3] == '5');
}

void test_ac_without_rg() {
    DaikinS21 *d = connect_to_new_ac();
    ac.supports_rg = false;
    ac.power = true;
    poll(d, 4);
    ac.fan = '7';
    poll(d);
    check("AC without RG: the fan is read from G1 (level 5)", d->GetState().fan_speed == FAIKIN_FAN_5);
    d->SetFan(FAIKIN_FAN_QUIET);
    poll(d);
    ac.fan = 'A';
    poll(d, 5);
    check("  G1 reporting Quiet as Auto doesn't undo Quiet", d->GetState().fan_speed == FAIKIN_FAN_QUIET);
}

void test_report_on_confirmation() {
    DaikinS21 *d = connect_to_new_ac();
    poll(d);
    int before = g_reports;
    d->SetPower(true);
    poll(d);
    check("a report goes out when the AC confirms a command", g_reports > before && g_last_report.power);
}

void test_compressor() {
    DaikinS21 *d = connect_to_new_ac();
    ac.power = true;
    poll(d);
    check("compressor reads as stopped", d->GetState().compressor_on == 0);
    ac.compressor_speed = 24;
    poll(d);
    check("compressor reads as running at 24 Hz", d->GetState().compressor_on == 1);

    d = connect_to_new_ac();
    ac.supports_rd = false;
    poll(d, 4);
    check("AC without Rd: compressor stays unknown", d->GetState().compressor_on == -1);
}

void test_lost_link() {
    DaikinS21 *d = connect_to_new_ac();
    poll(d);
    ac.answers = false;
    d->Poll();
    d->Poll();
    check("two silent polls keep the link", d->IsConnected());
    d->Poll();
    check("three silent polls lose the link", !d->IsConnected());

    d = connect_to_new_ac();
    poll(d);
    ac.answers = false;
    poll(d, 2);
    ac.answers = true;
    poll(d, 3);
    check("a short silence doesn't lose the link", d->IsConnected());
}

void test_framing() {
    DaikinS21 *d = connect_to_new_ac();
    poll(d);
    int before = ac.acks_received;
    poll(d);
    int clean_poll = ac.acks_received - before;
    before = ac.acks_received;
    ac.corrupt_next_status = true;
    poll(d);
    check("a bad frame still gets an ACK", clean_poll > 0 && ac.acks_received - before == clean_poll);

    d = connect_to_new_ac();
    poll(d);
    int64_t start = g_sim_time_us;
    d->Poll();
    int64_t plain_poll = g_sim_time_us - start;
    d->SetTemp(25);
    start = g_sim_time_us;
    d->Poll();
    check("a command doesn't wait for a reply that never comes", g_sim_time_us - start - plain_poll < 600000);
}

void test_helpers() {
    // 'G' + 'M' + '`' + '`' + '`' + 'a' adds up to 0x215, which truncates to NAK (0x15).
    unsigned char frame[] = {STX, 'G', 'M', '`', '`', '`', 'a', 0, ETX};
    check("a checksum equal to NAK is promoted by 2", s21_checksum(frame, sizeof(frame)) == NAK + 2);
    check("a sensor value that isn't digits reads as NaN", std::isnan(s21_decode_float_sensor((const unsigned char *)"---+")));
}

}  // namespace

int main() {
    test_connecting();
    test_mode_change_with_a_slow_ac();
    test_repeating_the_same_fan_speed_sends_nothing();
    test_command_the_ac_never_confirms();
    test_dry_and_fan_keep_the_setpoint();
    test_powerful_keeps_other_settings();
    test_fan_speeds();
    test_ac_without_rg();
    test_report_on_confirmation();
    test_compressor();
    test_lost_link();
    test_framing();
    test_helpers();
    printf("\n%d failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
