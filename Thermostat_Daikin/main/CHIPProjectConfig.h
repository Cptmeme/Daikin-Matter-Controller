#pragma once

// This changes the "Manufacturer" field in the device settings
#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME            "Daikin"

// This changes the "Model" field and the default name in the app
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME           "Daikin A/C"

// "Hardware version" field (Basic Information cluster). Default is "TEST_VERSION".
#define CHIP_DEVICE_CONFIG_DEFAULT_DEVICE_HARDWARE_VERSION_STRING "2.0"

// The firmware version isn't set here: controllers see PROJECT_VER and
// PROJECT_VER_NUMBER from CMakeLists.txt.