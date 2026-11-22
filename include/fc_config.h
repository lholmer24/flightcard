/*
 * Flightcard - configuration.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_CONFIG_H
#define FC_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FC_SSID_MAX 32
#define FC_PASSWORD_MAX 64
#define FC_TZ_MAX 31

typedef struct {
    uint32_t magic;             /* 'FLGT' (0x54474C46 little-endian) */
    uint16_t version;
    uint16_t length;
    uint32_t crc32;
    char wifi_ssid[33];
    char wifi_password[65];
    float home_lat, home_lon;
    float radius_km, max_alt_ft;
    uint16_t poll_seconds;
    char timezone[32];
    uint8_t auto_brightness;
    uint8_t palette;            /* 0 auto, 1 cabin, 2 amber */
    uint8_t wifi_power_save;    /* 0 disabled (default), 1 enabled */
    uint8_t demo_mode;
    float fixed_brightness;
    float night_lux;
    uint32_t seed_epoch;        /* optional fallback time; 0 = none */
} config_t;

enum {
    FC_PALETTE_AUTO = 0,
    FC_PALETTE_CABIN = 1,
    FC_PALETTE_AMBER = 2,
};

#define FC_CONFIG_MAGIC 0x54474C46u
#define FC_CONFIG_VERSION 1u

/* Defaults are populated by config_defaults(). */
void config_defaults(config_t *cfg);

/* CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320). */
uint32_t fc_crc32(const void *data, size_t len);

/* Serialise to a stable little-endian format. Returns bytes written or 0. */
size_t config_serialize(const config_t *cfg, uint8_t *buf, size_t cap);

/* Validate magic/version/length/CRC and copy into cfg. */
bool config_deserialize(config_t *cfg, const uint8_t *buf, size_t len);

#endif /* FC_CONFIG_H */
