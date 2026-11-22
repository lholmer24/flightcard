cmake_minimum_required(VERSION 3.13)

# ---------------------------------------------------------------------------
# Flightcard - C / FreeRTOS firmware for Pico 2 W + PIM715
#
# Required paths (environment or -D):
#   PICO_SDK_PATH          Pico SDK 2.1+
#   FREERTOS_KERNEL_PATH   FreeRTOS-Kernel V11.2.0 with community ports
#   LVGL_PATH              LVGL 9.3.x
# ---------------------------------------------------------------------------
if (NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()
if (NOT DEFINED FREERTOS_KERNEL_PATH)
    set(FREERTOS_KERNEL_PATH $ENV{FREERTOS_KERNEL_PATH})
endif()
if (NOT DEFINED LVGL_PATH)
    set(LVGL_PATH $ENV{LVGL_PATH})
endif()

set(PICO_BOARD pico2_w CACHE STRING "Pico board")
set(PICO_FLASH_SIZE_BYTES 4194304 CACHE STRING "4 MB flash")
option(FC_DISPLAY_DMA "Use DMA for the LVGL display flush" ON)
option(FC_SMP "Use the RP2350 SMP FreeRTOS port" ON)

# ---------------------------------------------------------------------------
# Optional build-time defaults.
#
# These values are written to a generated header in the build directory, never
# to the source tree or the compiler command line. Prefer environment
# variables (they are not stored in CMakeCache.txt):
#
#   FC_WIFI_SSID=... FC_WIFI_PASSWORD=... \
#   FC_HOME_LAT=51.5 FC_HOME_LON=-0.12 FC_TIMEZONE=UTC \
#       cmake -S . -B build ...
#
# Values are defaults only: a provisioned board keeps the config already in
# flash, and the USB CLI can change them at runtime. Delete the build
# directory (or `unset` the variables and reconfigure) before sharing or
# archiving it.
# ---------------------------------------------------------------------------
set(FC_WIFI_SSID "" CACHE STRING "Optional WiFi SSID baked in as a default")
set(FC_WIFI_PASSWORD "" CACHE STRING "Optional WiFi password baked in as a default")
set(FC_HOME_LAT "" CACHE STRING "Optional monitoring latitude in decimal degrees")
set(FC_HOME_LON "" CACHE STRING "Optional monitoring longitude in decimal degrees")
set(FC_TIMEZONE "" CACHE STRING "Optional display timezone (UTC, Europe/London or UTC+HH:MM)")
set(FC_RADIUS_KM "" CACHE STRING "Optional initial search radius in km (default 12)")
set(FC_MAX_ALT_FT "" CACHE STRING "Optional initial altitude ceiling in feet (default 6000)")

if (FC_WIFI_SSID STREQUAL "" AND DEFINED ENV{FC_WIFI_SSID})
    set(FC_WIFI_SSID "$ENV{FC_WIFI_SSID}")
endif()
if (FC_WIFI_PASSWORD STREQUAL "" AND DEFINED ENV{FC_WIFI_PASSWORD})
    set(FC_WIFI_PASSWORD "$ENV{FC_WIFI_PASSWORD}")
endif()
if (FC_HOME_LAT STREQUAL "" AND DEFINED ENV{FC_HOME_LAT})
    set(FC_HOME_LAT "$ENV{FC_HOME_LAT}")
endif()
if (FC_HOME_LON STREQUAL "" AND DEFINED ENV{FC_HOME_LON})
    set(FC_HOME_LON "$ENV{FC_HOME_LON}")
endif()
if (FC_TIMEZONE STREQUAL "" AND DEFINED ENV{FC_TIMEZONE})
    set(FC_TIMEZONE "$ENV{FC_TIMEZONE}")
endif()
if (FC_RADIUS_KM STREQUAL "" AND DEFINED ENV{FC_RADIUS_KM})
    set(FC_RADIUS_KM "$ENV{FC_RADIUS_KM}")
endif()
if (FC_MAX_ALT_FT STREQUAL "" AND DEFINED ENV{FC_MAX_ALT_FT})
    set(FC_MAX_ALT_FT "$ENV{FC_MAX_ALT_FT}")
endif()

string(LENGTH "${FC_WIFI_SSID}" _fc_ssid_len)
string(LENGTH "${FC_WIFI_PASSWORD}" _fc_password_len)
if (_fc_ssid_len GREATER 32)
    message(FATAL_ERROR "FC_WIFI_SSID must be at most 32 bytes")
endif()
if (_fc_password_len GREATER 64)
    message(FATAL_ERROR "FC_WIFI_PASSWORD must be at most 64 bytes")
endif()

if (NOT FC_HOME_LAT STREQUAL "" OR NOT FC_HOME_LON STREQUAL "")
    if (FC_HOME_LAT STREQUAL "" OR FC_HOME_LON STREQUAL "")
        message(FATAL_ERROR "Set both FC_HOME_LAT and FC_HOME_LON, or neither")
    endif()
    if (NOT FC_HOME_LAT MATCHES "^[+-]?[0-9]+([.][0-9]+)?$"
        OR NOT FC_HOME_LON MATCHES "^[+-]?[0-9]+([.][0-9]+)?$")
        message(FATAL_ERROR "FC_HOME_LAT/FC_HOME_LON must be decimal degrees")
    endif()
    if (FC_HOME_LAT LESS -90.0 OR FC_HOME_LAT GREATER 90.0)
        message(FATAL_ERROR "FC_HOME_LAT must be between -90 and 90")
    endif()
    if (FC_HOME_LON LESS -180.0 OR FC_HOME_LON GREATER 180.0)
        message(FATAL_ERROR "FC_HOME_LON must be between -180 and 180")
    endif()
    set(FC_HAS_LOCATION 1)
    set(FC_HOME_LAT_C "${FC_HOME_LAT}f")
    set(FC_HOME_LON_C "${FC_HOME_LON}f")
else()
    set(FC_HAS_LOCATION 0)
    set(FC_HOME_LAT_C "0.0f")
    set(FC_HOME_LON_C "0.0f")
endif()

if (FC_RADIUS_KM STREQUAL "")
    set(FC_RADIUS_KM_C "12.0f")
elseif (NOT FC_RADIUS_KM MATCHES "^[0-9]+([.][0-9]+)?$"
        OR FC_RADIUS_KM LESS 0.1 OR FC_RADIUS_KM GREATER 500.0)
    message(FATAL_ERROR "FC_RADIUS_KM must be a number from 0.1 to 500")
else()
    set(FC_RADIUS_KM_C "${FC_RADIUS_KM}f")
endif()

if (FC_MAX_ALT_FT STREQUAL "")
    set(FC_MAX_ALT_FT_C "6000.0f")
elseif (NOT FC_MAX_ALT_FT MATCHES "^[0-9]+([.][0-9]+)?$"
        OR FC_MAX_ALT_FT LESS 0.0 OR FC_MAX_ALT_FT GREATER 60000.0)
    message(FATAL_ERROR "FC_MAX_ALT_FT must be a number from 0 to 60000")
else()
    set(FC_MAX_ALT_FT_C "${FC_MAX_ALT_FT}f")
endif()

if (FC_TIMEZONE STREQUAL "")
    set(FC_TIMEZONE "UTC")
endif()
string(LENGTH "${FC_TIMEZONE}" _fc_tz_len)
if (_fc_tz_len GREATER 31)
    message(FATAL_ERROR "FC_TIMEZONE must be at most 31 bytes")
endif()

function(fc_escape_c_string out value)
    string(REPLACE "\\" "\\\\" _escaped "${value}")
    string(REPLACE "\"" "\\\"" _escaped "${_escaped}")
    set(${out} "${_escaped}" PARENT_SCOPE)
endfunction()
fc_escape_c_string(FC_WIFI_SSID_ESC "${FC_WIFI_SSID}")
fc_escape_c_string(FC_WIFI_PASSWORD_ESC "${FC_WIFI_PASSWORD}")
fc_escape_c_string(FC_TIMEZONE_ESC "${FC_TIMEZONE}")

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(flightcard C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# FreeRTOS SMP port for RP2350 (ARM non-trustzone).
set(FREERTOS_CONFIG_FILE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/include)
list(APPEND FREERTOS_PORT_PATH
     ${FREERTOS_KERNEL_PATH}/portable/ThirdParty/Community-Supported-Ports/GCC/RP2350_ARM_NTZ)
if (NOT EXISTS ${FREERTOS_PORT_PATH}/FreeRTOS_Kernel_import.cmake)
    message(FATAL_ERROR "RP2350 FreeRTOS port not found at ${FREERTOS_PORT_PATH}")
endif()
include(${FREERTOS_PORT_PATH}/FreeRTOS_Kernel_import.cmake)
target_compile_definitions(FreeRTOS-Kernel INTERFACE FC_SMP=$<BOOL:${FC_SMP}>)

# lwIP options + mbedTLS config are in include/.
set(PICO_LWIP_CONTRIB_PATH ${PICO_SDK_PATH}/lib/lwip/contrib)

# LVGL 9.3 (the desktop CMake path is a plain source glob; it works cross.)
set(LV_BUILD_CONF_PATH ${CMAKE_CURRENT_LIST_DIR}/lv_conf.h)
add_subdirectory(${LVGL_PATH} ${CMAKE_BINARY_DIR}/lvgl)

# ---------------------------------------------------------------------------
# Assets are regenerated from the repository's fonts, sprites, types.json and
# certificate PEMs. Nothing generated is hand-edited.
# ---------------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GEN_DIR})
configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/include/fc_build_config.h.in
    ${GEN_DIR}/fc_build_config.h
    @ONLY)
set(GEN_FILES
    ${GEN_DIR}/fc_fonts.c
    ${GEN_DIR}/fc_types.c
    ${GEN_DIR}/fc_sprites.c
    ${GEN_DIR}/fc_certs.c
    ${GEN_DIR}/fc_assets_generated.h)
add_custom_command(
    OUTPUT ${GEN_FILES}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/convert_assets.py
            --out ${GEN_DIR}
    DEPENDS
        ${CMAKE_CURRENT_LIST_DIR}/tools/convert_assets.py
        ${CMAKE_CURRENT_LIST_DIR}/assets/types.json
        ${CMAKE_CURRENT_LIST_DIR}/certs/gtsr4.pem
        ${CMAKE_CURRENT_LIST_DIR}/certs/ca-bundle.pem
    COMMENT "Generating Flightcard C assets"
    VERBATIM)
add_custom_target(fc_assets DEPENDS ${GEN_FILES})

set(FC_SOURCES
    src/main.c
    src/clock.c
    src/cli.c
    src/config_store.c
    src/demo.c
    src/display.c
    src/dns.c
    src/dns_proto.c
    src/format.c
    src/http.c
    src/json.c
    src/light.c
    src/log.c
    src/net.c
    src/ntp.c
    src/ranking.c
    src/sensors.c
    src/sprite.c
    src/tls_http.c
    src/timezone.c
    src/types.c
    src/ui.c
    src/wifi.c
)

add_executable(flightcard ${FC_SOURCES} ${GEN_FILES})
add_dependencies(flightcard fc_assets)

target_include_directories(flightcard PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/vendor
    ${GEN_DIR}
)

target_compile_definitions(flightcard PRIVATE
    FC_SMP=$<BOOL:${FC_SMP}>
    FC_DISPLAY_DMA=$<BOOL:${FC_DISPLAY_DMA}>
    PICO_STDIO_USB=1
    PICO_STDIO_UART=0
    MBEDTLS_PLATFORM_MEMORY=1
    MBEDTLS_PLATFORM_TIME_ALT=1
    MBEDTLS_SSL_IN_CONTENT_LEN=16384
    MBEDTLS_SSL_OUT_CONTENT_LEN=4096
)

target_compile_options(flightcard PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation
)

target_link_libraries(flightcard PRIVATE
    pico_stdlib
    pico_multicore
    pico_stdio_usb
    pico_cyw43_arch_lwip_sys_freertos
    pico_lwip
    pico_lwip_freertos
    pico_mbedtls
    hardware_spi
    hardware_dma
    hardware_pwm
    hardware_i2c
    hardware_watchdog
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
    lvgl
    m
)

pico_enable_stdio_usb(flightcard 1)
pico_enable_stdio_uart(flightcard 0)
pico_add_extra_outputs(flightcard)
pico_set_program_name(flightcard "flightcard")
pico_set_program_version(flightcard "1.0")

# Host-side unit tests are separate: see tests/Makefile.
