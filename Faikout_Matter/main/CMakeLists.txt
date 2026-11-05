# The power and energy measurement code is shared with the Thread firmware.
idf_component_register(
    SRCS "cn_wired_driver.c" "Faikout.c" "bleenv.c" "settings.c" "faikout_i2c.c" "matter_bridge.cpp"
         "../../Thermostat_Daikin/main/power_measurement.cpp"
         "../../Thermostat_Daikin/main/energy_measurement.cpp"
    REQUIRES "ESP32-RevK" "esp_driver_rmt" "esp_driver_uart" "esp_driver_i2c" "esp_driver_usb_serial_jtag" "esp_matter"
    EMBED_FILES "favicon.ico" "apple-touch-icon.png")

target_compile_definitions(${COMPONENT_LIB} PRIVATE FAIKOUT_MATTER CHIP_HAVE_CONFIG_H)
