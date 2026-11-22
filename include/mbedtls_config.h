/*
 * Flightcard - mbedTLS configuration.
 *
 * Pico SDK 2.2's pico_mbedtls_config.h includes "mbedtls_config.h"; this file
 * pulls in Mbed TLS's normal defaults and then selects the RP2350 hardware
 * entropy source and the TLS 1.2/ECDSA/AES-GCM subset we need.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_MBEDTLS_CONFIG_H
#define FC_MBEDTLS_CONFIG_H

#include "mbedtls/mbedtls_config.h"

/* Pico SDK provides mbedtls_hardware_poll() backed by pico_rand. */
#undef MBEDTLS_TIMING_C
#undef MBEDTLS_FS_IO
#undef MBEDTLS_NET_C
#undef MBEDTLS_PSA_ITS_FILE_C
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C
#undef MBEDTLS_SHA3_C
#undef MBEDTLS_SSL_PROTO_TLS1_3
#define MBEDTLS_PLATFORM_MS_TIME_ALT
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_NO_PLATFORM_ENTROPY

/* Keep record buffers inside the documented firmware budget. The CMake
 * definitions set these too, but define them here for direct builds. */
#ifndef MBEDTLS_SSL_IN_CONTENT_LEN
#define MBEDTLS_SSL_IN_CONTENT_LEN 16384
#endif
#ifndef MBEDTLS_SSL_OUT_CONTENT_LEN
#define MBEDTLS_SSL_OUT_CONTENT_LEN 4096
#endif

#endif /* FC_MBEDTLS_CONFIG_H */
