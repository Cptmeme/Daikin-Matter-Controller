#pragma once

// "Manufacturer" field in the device settings
#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME            "Daikin"

// "Model" field and the default name in the app
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME           "Daikin A/C"

// "Hardware version" field (Basic Information cluster). Default is "TEST_VERSION".
#define CHIP_DEVICE_CONFIG_DEFAULT_DEVICE_HARDWARE_VERSION_STRING "Faikout"

// If reconnecting to a controller fails after a restart, try again after 30 seconds rather than the
// SDK's 5 minutes, during which home apps can show the device as not responding.
#define CHIP_CONFIG_SUBSCRIPTION_TIMEOUT_RESUMPTION_MIN_RETRY_INTERVAL_SECS 30
#define CHIP_CONFIG_SUBSCRIPTION_TIMEOUT_RESUMPTION_WAIT_TIME_MULTIPLIER_SECS 30
