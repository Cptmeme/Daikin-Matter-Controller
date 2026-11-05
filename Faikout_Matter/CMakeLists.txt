# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5...3.25)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "Please set ESP_MATTER_PATH to the path of the esp-matter repo")
endif()
if(NOT DEFINED ENV{FAIKOUT_BOARD})
    message(FATAL_ERROR "Build with ./build.sh <board>, see README.md")
endif()
set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

set(PROJECT_VER "1.0")
set(PROJECT_VER_NUMBER 1)

# An existing sdkconfig.<board> takes priority over these files: delete it after changing them.
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.matter;sdkconfig.defaults.matter-$ENV{FAIKOUT_BOARD}")
if("$ENV{FAIKOUT_BOARD}" MATCHES "^(c6|c5|eth|poe)$")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.defaults.matter-small")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components")

project(Faikout)

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
