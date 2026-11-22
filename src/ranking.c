/*
 * Flightcard - ranking and filtering.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ranking.h"
#include "types.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define EARTH_RADIUS_M 6371000.0
#define HOME_ALT_M 25.0

static const char *const HELI_PREFIXES[] = {
    "EC", "AS50", "AS55", "AS65", "AS32", "AS33", "AS35", "AS3B",
    "R22", "R44", "R66", "B06", "B212", "B407", "B412", "B429",
    "S76", "S92", "A109", "A139", "A169", "A189", "AW",
    "H47", "H60", "H64", "H135", "H145", "H160", "H175", "H225",
};

static bool starts_with_any(const char *s, const char *const *prefixes,
                            size_t count)
{
    for (size_t i = 0; i < count; i++) {
        size_t n = strlen(prefixes[i]);
        if (strncmp(s, prefixes[i], n) == 0) return true;
    }
    return false;
}

bool fc_is_helicopter(const char *type_code, const char *category)
{
    if (category != NULL) {
        char c0 = (char)toupper((unsigned char)category[0]);
        char c1 = (char)toupper((unsigned char)category[1]);
        if (c0 == 'A' && c1 == '7' && category[2] == '\0') return true;
    }
    if (type_code == NULL || *type_code == '\0') return false;
    return starts_with_any(type_code, HELI_PREFIXES,
                           sizeof(HELI_PREFIXES) / sizeof(HELI_PREFIXES[0]));
}

float fc_apparent_size_deg(float alt_ft, float ground_m, float span_m)
{
    double up = (double)alt_ft * 0.3048 - HOME_ALT_M;
    double slant = sqrt((double)ground_m * (double)ground_m + up * up);
    double safe = slant > 1.0 ? slant : 1.0;
    return (float)(2.0 * atan(((double)span_m / 2.0) / safe) * (180.0 / M_PI));
}

bool fc_rank_one(const raw_aircraft_t *ac, const config_t *cfg, flight_t *out)
{
    if (ac == NULL || cfg == NULL || out == NULL) return false;

    if (ac->alt_baro_ground) return false;
    if (!ac->has_lat || !ac->has_lon) return false;
    if (ac->has_seen && ac->seen_pos > 10.0f) return false;

    if (ac->category[0] == 'C' || ac->category[0] == 'c') return false;
    if (fc_is_helicopter(ac->type, ac->category)) return false;

    if (!ac->has_alt) return false;
    if (ac->alt_ft > cfg->max_alt_ft) return false;

    if (!ac->has_gs || ac->gs_kt < 40.0f) return false;

    double home_lat = (double)cfg->home_lat;
    double east = EARTH_RADIUS_M
                  * ((double)ac->lon - (double)cfg->home_lon) * (M_PI / 180.0)
                  * cos(home_lat * (M_PI / 180.0));
    double north = EARTH_RADIUS_M
                   * ((double)ac->lat - home_lat) * (M_PI / 180.0);
    double ground = sqrt(east * east + north * north);
    if (ground > (double)cfg->radius_km * 1000.0) return false;

    float span = fc_type_span(ac->type);
    float size = fc_apparent_size_deg(ac->alt_ft, (float)ground, span);

    memset(out, 0, sizeof(*out));
    snprintf(out->callsign, sizeof(out->callsign), "%s", ac->callsign);
    snprintf(out->hex, sizeof(out->hex), "%s", ac->hex);
    snprintf(out->type, sizeof(out->type), "%s", ac->type);
    snprintf(out->category, sizeof(out->category), "%s", ac->category);
    snprintf(out->reg, sizeof(out->reg), "%s", ac->reg);
    out->alt_ft = ac->alt_ft;
    out->gs_kt = ac->gs_kt;
    out->dist_km = (float)(ground / 1000.0);
    out->size_deg = size;
    return true;
}

bool fc_rank_best(const raw_aircraft_t *ac, int count, const config_t *cfg,
                  flight_t *out)
{
    bool have = false;
    flight_t best;
    memset(&best, 0, sizeof(best));

    for (int i = 0; i < count; i++) {
        flight_t candidate;
        if (!fc_rank_one(&ac[i], cfg, &candidate)) continue;
        if (!have || candidate.size_deg > best.size_deg) {
            best = candidate;
            have = true;
        }
    }
    if (have && out != NULL) *out = best;
    return have;
}
