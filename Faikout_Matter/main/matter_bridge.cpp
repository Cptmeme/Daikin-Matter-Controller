// Matter over Wi-Fi for Faikout. See matter_bridge.h.
//
// Device model, fan scale and setpoint rules come from the Thread firmware (Thermostat_Daikin, 1.6),
// where they were tested with Apple Home.

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_matter.h>

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <lib/support/Span.h>
#include <platform/CHIPDeviceLayer.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <setup_payload/SetupPayload.h>

#include "faikin_enums.h"
#include "matter_bridge.h"
#include "../../Thermostat_Daikin/main/power_measurement.h" // Shared with the Thread firmware

using namespace esp_matter;
using namespace chip::app::Clusters;
using chip::DeviceLayer::ConnectivityManager;
using chip::DeviceLayer::ConnectivityMgr;
using chip::DeviceLayer::PlatformMgr;

// Faikout builds with the log level at "error" and logs its milestones with ESP_LOGE, so do the same.
static const char *TAG = "Matter";

static constexpr uint16_t k_window_seconds = 300;

static uint16_t s_ac_endpoint;
static uint16_t s_powerful_endpoint;
static volatile bool s_started;

// True while a report runs. Matter's fan logic reacts to reports by writing the fan attributes
// back, and those writes aren't user commands.
static bool s_reporting_fan;

static int16_t celsius_to_matter(float c) { return static_cast<int16_t>(lroundf(c * 100.0f)); }
static float matter_to_celsius(int16_t v) { return static_cast<float>(v) / 100.0f; }

// ---------------------------------------------------------------------------------------------
// Fan slider scale: 0% is Quiet, 1-99% steps through levels 1 to 5, and 100% is Auto.
// Apple Home only offers the slider, so Quiet and Auto sit at its two ends. Matter ties 0% to
// FanMode Off, so Off means Quiet here. The fan never actually stops.

static uint8_t fan_to_matter_mode(uint8_t fan)
{
    switch (fan) {
    case FAIKIN_FAN_QUIET: return 0; // Off
    case FAIKIN_FAN_1:
    case FAIKIN_FAN_2:     return 1; // Low
    case FAIKIN_FAN_3:     return 2; // Medium
    case FAIKIN_FAN_4:
    case FAIKIN_FAN_5:     return 3; // High
    default:               return 5; // Auto
    }
}

static uint8_t matter_mode_to_fan(uint8_t mode)
{
    switch (mode) {
    case 0:  return FAIKIN_FAN_QUIET;
    case 1:  return FAIKIN_FAN_1;
    case 2:  return FAIKIN_FAN_3;
    case 3:  return FAIKIN_FAN_5;
    default: return FAIKIN_FAN_AUTO; // On, Auto, Smart
    }
}

// Each value must map back to the same speed through percent_to_fan().
static uint8_t fan_to_percent(uint8_t fan)
{
    switch (fan) {
    case FAIKIN_FAN_QUIET: return 0;
    case FAIKIN_FAN_1:     return 20;
    case FAIKIN_FAN_2:     return 40;
    case FAIKIN_FAN_3:     return 60;
    case FAIKIN_FAN_4:     return 80;
    case FAIKIN_FAN_5:     return 99;
    default:               return 100;
    }
}

static uint8_t percent_to_fan(uint8_t percent)
{
    if (percent == 0)   return FAIKIN_FAN_QUIET;
    if (percent >= 100) return FAIKIN_FAN_AUTO;
    if (percent <= 20)  return FAIKIN_FAN_1;
    if (percent <= 40)  return FAIKIN_FAN_2;
    if (percent <= 60)  return FAIKIN_FAN_3;
    if (percent <= 80)  return FAIKIN_FAN_4;
    return FAIKIN_FAN_5;
}

// ---------------------------------------------------------------------------------------------
// Controller -> AC

// Matter has separate heat and cool setpoints, but the AC only has one target.
static void apply_stored_setpoint(uint32_t attribute_id)
{
    esp_matter_attr_val_t stored = esp_matter_invalid(nullptr);
    if (attribute::get_val(s_ac_endpoint, Thermostat::Id, attribute_id, &stored) == ESP_OK) {
        faikout_matter_set_temp(matter_to_celsius(stored.val.i16));
    }
}

static void thermostat_write(uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    matter_ac_state_t now;
    faikout_matter_snapshot(&now);

    if (attribute_id == Thermostat::Attributes::SystemMode::Id) {
        uint8_t mode = val->val.u8;
        if (mode == 0) {
            faikout_matter_set_power(false);
            return;
        }
        if (!now.power) {
            faikout_matter_set_power(true);
        }
        switch (mode) {
        case 1:
            faikout_matter_set_mode(FAIKIN_MODE_AUTO);
            break;
        case 3:
            faikout_matter_set_mode(FAIKIN_MODE_COOL);
            apply_stored_setpoint(Thermostat::Attributes::OccupiedCoolingSetpoint::Id);
            break;
        case 4:
            faikout_matter_set_mode(FAIKIN_MODE_HEAT);
            apply_stored_setpoint(Thermostat::Attributes::OccupiedHeatingSetpoint::Id);
            break;
        case 7:
            faikout_matter_set_mode(FAIKIN_MODE_FAN);
            break;
        case 8:
            faikout_matter_set_mode(FAIKIN_MODE_DRY);
            break;
        default:
            break;
        }
    } else if (attribute_id == Thermostat::Attributes::OccupiedCoolingSetpoint::Id ||
               attribute_id == Thermostat::Attributes::OccupiedHeatingSetpoint::Id) {
        // While on, only the setpoint for the current mode applies. Dry and Fan have none.
        // While off, or in Auto where the AC has a single target, the latest write wins.
        bool is_heat = attribute_id == Thermostat::Attributes::OccupiedHeatingSetpoint::Id;
        if (!now.power || now.mode == FAIKIN_MODE_AUTO || (now.mode == FAIKIN_MODE_HEAT && is_heat) ||
            (now.mode == FAIKIN_MODE_COOL && !is_heat)) {
            faikout_matter_set_temp(matter_to_celsius(val->val.i16));
        }
    }
}

static void fan_write(uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    if (s_reporting_fan) {
        return;
    }
    if (attribute_id == FanControl::Attributes::FanMode::Id) {
        faikout_matter_set_fan(matter_mode_to_fan(val->val.u8));
    } else if (attribute_id == FanControl::Attributes::PercentSetting::Id) {
        // Nullable: null means the controller wants Auto.
        bool is_null = val->type == ESP_MATTER_VAL_TYPE_NULLABLE_UINT8 && val->val.u8 == 0xFF;
        faikout_matter_set_fan(is_null ? FAIKIN_FAN_AUTO : percent_to_fan(val->val.u8));
    }
}

static esp_err_t attribute_cb(attribute::callback_type_t type, uint16_t endpoint, uint32_t cluster, uint32_t attribute_id,
                              esp_matter_attr_val_t *val, void *)
{
    if (type != attribute::PRE_UPDATE) {
        return ESP_OK;
    }
    if (endpoint == s_powerful_endpoint) {
        if (cluster == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id) {
            faikout_matter_set_powerful(val->val.b);
        }
        return ESP_OK;
    }
    if (endpoint != s_ac_endpoint) {
        return ESP_OK;
    }
    if (cluster == Thermostat::Id) {
        thermostat_write(attribute_id, val);
    } else if (cluster == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id) {
        faikout_matter_set_power(val->val.b);
    } else if (cluster == FanControl::Id) {
        fan_write(attribute_id, val);
    }
    return ESP_OK;
}

static esp_err_t identify_cb(identification::callback_type_t, uint16_t, uint8_t, uint8_t, void *)
{
    return ESP_OK;
}

// ---------------------------------------------------------------------------------------------
// AC -> controllers

static uint8_t faikin_mode_to_system_mode(const matter_ac_state_t &s)
{
    if (!s.power) {
        return 0; // Off
    }
    switch (s.mode) {
    case FAIKIN_MODE_COOL: return 3;
    case FAIKIN_MODE_HEAT: return 4;
    case FAIKIN_MODE_FAN:  return 7;
    case FAIKIN_MODE_DRY:  return 8;
    default:               return 1; // Auto
    }
}

static void report_on_chip_task(intptr_t context)
{
    matter_ac_state_t *s = reinterpret_cast<matter_ac_state_t *>(context);
    esp_matter_attr_val_t val;

    // Current temperature, shown on both clusters. Null while the AC hasn't given one.
    nullable<int16_t> current;
    if (!isnan(s->current)) {
        current = celsius_to_matter(s->current);
    }
    val = esp_matter_nullable_int16(current);
    attribute::report(s_ac_endpoint, Thermostat::Id, Thermostat::Attributes::LocalTemperature::Id, &val);
    attribute::report(s_ac_endpoint, TemperatureMeasurement::Id, TemperatureMeasurement::Attributes::MeasuredValue::Id, &val);

    if (!isnan(s->target)) {
        val = esp_matter_int16(celsius_to_matter(s->target));
        if (s->mode == FAIKIN_MODE_HEAT || s->mode == FAIKIN_MODE_AUTO) {
            attribute::report(s_ac_endpoint, Thermostat::Id, Thermostat::Attributes::OccupiedHeatingSetpoint::Id, &val);
        }
        if (s->mode == FAIKIN_MODE_COOL || s->mode == FAIKIN_MODE_AUTO) {
            attribute::report(s_ac_endpoint, Thermostat::Id, Thermostat::Attributes::OccupiedCoolingSetpoint::Id, &val);
        }
    }

    if (s->power_known) {
        val = esp_matter_bool(s->power);
        attribute::report(s_ac_endpoint, OnOff::Id, OnOff::Attributes::OnOff::Id, &val);
    }
    if (s->power_known && s->mode_known) {
        val = esp_matter_enum8(faikin_mode_to_system_mode(*s));
        attribute::report(s_ac_endpoint, Thermostat::Id, Thermostat::Attributes::SystemMode::Id, &val);
    }

    val = esp_matter_bitmap16(s->running);
    attribute::report(s_ac_endpoint, Thermostat::Id, Thermostat::Attributes::ThermostatRunningState::Id, &val);

    if (s->fan_known) {
        s_reporting_fan = true;
        val = esp_matter_enum8(fan_to_matter_mode(s->fan));
        attribute::report(s_ac_endpoint, FanControl::Id, FanControl::Attributes::FanMode::Id, &val);
        uint8_t percent = fan_to_percent(s->fan);
        val = esp_matter_nullable_uint8(percent);
        attribute::report(s_ac_endpoint, FanControl::Id, FanControl::Attributes::PercentSetting::Id, &val);
        val = esp_matter_uint8(percent);
        attribute::report(s_ac_endpoint, FanControl::Id, FanControl::Attributes::PercentCurrent::Id, &val);
        s_reporting_fan = false;
    }

    val = esp_matter_bool(s->powerful);
    attribute::report(s_powerful_endpoint, OnOff::Id, OnOff::Attributes::OnOff::Id, &val);

    if (s->power_w >= 0) {
        power_measurement_set_watts(s->power_w);
    }

    // Only on change, because each update sends an energy event.
    static int32_t s_last_energy_wh = -1;
    if (s->energy_wh >= 0 && s->energy_wh != s_last_energy_wh) {
        s_last_energy_wh = s->energy_wh;
        energy_measurement_set_wh(s_ac_endpoint, s->energy_wh);
    }

    free(s);
}

bool matter_bridge_report(const matter_ac_state_t *state)
{
    if (!s_started) {
        return false;
    }
    matter_ac_state_t *copy = static_cast<matter_ac_state_t *>(malloc(sizeof(*copy)));
    if (!copy) {
        return false;
    }
    *copy = *state;
    if (PlatformMgr().ScheduleWork(report_on_chip_task, reinterpret_cast<intptr_t>(copy)) != CHIP_NO_ERROR) {
        free(copy);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Pairing

// Faikout is already on the network, so pairing only ever needs mDNS.
static void open_window_on_chip_task(intptr_t)
{
    chip::CommissioningWindowManager &manager = chip::Server::GetInstance().GetCommissioningWindowManager();
    if (manager.IsCommissioningWindowOpen()) {
        return;
    }
    CHIP_ERROR err = manager.OpenBasicCommissioningWindow(chip::System::Clock::Seconds16(k_window_seconds),
                                                          chip::CommissioningWindowAdvertisement::kDnssdOnly);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to open pairing window: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

void matter_bridge_open_window(void)
{
    if (s_started) {
        PlatformMgr().ScheduleWork(open_window_on_chip_task, 0);
    }
}

void matter_bridge_info(matter_info_t *info)
{
    memset(info, 0, sizeof(*info));
    info->free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    info->largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    info->free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (!s_started) {
        return;
    }
    info->started = true;

    chip::RendezvousInformationFlags flags(chip::RendezvousInformationFlag::kOnNetwork);
    chip::MutableCharSpan manual(info->manual_code, sizeof(info->manual_code));
    chip::MutableCharSpan qr(info->qr_code, sizeof(info->qr_code));

    chip::DeviceLayer::StackLock lock;
    chip::Server &server = chip::Server::GetInstance();
    info->fabrics = server.GetFabricTable().FabricCount();
    info->window_open = server.GetCommissioningWindowManager().IsCommissioningWindowOpen();
    if (GetManualPairingCode(manual, flags) != CHIP_NO_ERROR) {
        info->manual_code[0] = 0;
    }
    if (GetQRCode(qr, flags) != CHIP_NO_ERROR) {
        info->qr_code[0] = 0;
    }
}

static void event_cb(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGE(TAG, "Paired");
        break;
    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGE(TAG, "Pairing failed, fail safe timer expired");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGE(TAG, "Pairing window opened");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGE(TAG, "Pairing window closed");
        break;
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        // With the last controller gone, be ready to pair again.
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            open_window_on_chip_task(0);
        }
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------------------------
// Startup

static bool create_data_model()
{
    node::config_t node_config;
    node_t *node = node::create(&node_config, attribute_cb, identify_cb);
    if (!node) {
        return false;
    }

    // Room Air Conditioner (device type 0x0072) bundles OnOff and Thermostat.
    endpoint::room_air_conditioner::config_t ac_config;
    ac_config.on_off.on_off = false;
    ac_config.thermostat.feature_flags = 35; // Heat, Cool, Auto
    ac_config.thermostat.control_sequence_of_operation = 4;
    endpoint_t *ac = endpoint::room_air_conditioner::create(node, &ac_config, ENDPOINT_FLAG_NONE, nullptr);
    if (!ac) {
        return false;
    }
    s_ac_endpoint = endpoint::get_id(ac);

    cluster::fan_control::config_t fan_config;
    fan_config.fan_mode = 5;          // Auto
    fan_config.fan_mode_sequence = 2; // Off, Low, Medium, High, Auto
    cluster_t *fan = cluster::fan_control::create(ac, &fan_config, CLUSTER_FLAG_SERVER);
    if (!fan) {
        return false;
    }
    cluster::fan_control::feature::fan_auto::add(fan);

    cluster::temperature_measurement::config_t temperature_config;
    if (!cluster::temperature_measurement::create(ac, &temperature_config, CLUSTER_FLAG_SERVER)) {
        return false;
    }

    cluster::electrical_power_measurement::config_t power_config;
    power_config.feature_flags = cluster::electrical_power_measurement::feature::alternating_current::get_id();
    power_config.delegate = power_measurement_delegate();
    if (!cluster::electrical_power_measurement::create(ac, &power_config, CLUSTER_FLAG_SERVER)) {
        return false;
    }

    cluster::electrical_energy_measurement::config_t energy_config;
    energy_config.feature_flags = cluster::electrical_energy_measurement::feature::imported_energy::get_id() |
                                  cluster::electrical_energy_measurement::feature::cumulative_energy::get_id();
    if (!cluster::electrical_energy_measurement::create(ac, &energy_config, CLUSTER_FLAG_SERVER)) {
        return false;
    }

    // Powerful mode as a separate switch.
    endpoint::on_off_plug_in_unit::config_t powerful_config;
    powerful_config.on_off.on_off = false;
    endpoint_t *powerful = endpoint::on_off_plug_in_unit::create(node, &powerful_config, ENDPOINT_FLAG_NONE, nullptr);
    if (!powerful) {
        return false;
    }
    s_powerful_endpoint = endpoint::get_id(powerful);
    return true;
}

// Faikout's framework brings up and reconnects Wi-Fi. Waiting until the station is on the network
// means the SDK's first look at Wi-Fi finds it connected, so it never tries to connect by itself.
static bool station_ready()
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        return false;
    }
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        return false;
    }
    esp_ip6_addr_t link_local;
    return esp_netif_get_ip6_linklocal(netif, &link_local) == ESP_OK; // Matter needs IPv6
}

static void hand_wifi_to_app_on_chip_task(intptr_t context)
{
    // From here on the SDK watches Wi-Fi but doesn't start, stop or connect it.
    CHIP_ERROR err = ConnectivityMgr().SetWiFiStationMode(ConnectivityManager::kWiFiStationMode_ApplicationControlled);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to leave Wi-Fi to the application: %" CHIP_ERROR_FORMAT, err.Format());
    }
    energy_measurement_init(static_cast<uint16_t>(context));
    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kOnNetwork));
}

static void matter_task(void *)
{
    while (!station_ready()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGE(TAG, "Wi-Fi is up, starting. Free heap %u", static_cast<unsigned>(esp_get_free_heap_size()));

    if (!create_data_model()) {
        ESP_LOGE(TAG, "Failed to create the data model");
        vTaskDelete(nullptr);
        return;
    }
    esp_err_t err = esp_matter::start(event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start: %s", esp_err_to_name(err));
        vTaskDelete(nullptr);
        return;
    }
    PlatformMgr().ScheduleWork(hand_wifi_to_app_on_chip_task, static_cast<intptr_t>(s_ac_endpoint));
    s_started = true;
    ESP_LOGE(TAG, "Started. Free heap %u", static_cast<unsigned>(esp_get_free_heap_size()));
    vTaskDelete(nullptr);
}

void matter_bridge_start(void)
{
    xTaskCreate(matter_task, "matter_start", 8192, nullptr, 5, nullptr);
}
