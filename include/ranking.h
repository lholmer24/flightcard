/*
 * Flightcard - feed filtering and apparent-size ranking.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_RANKING_H
#define FC_RANKING_H

#include <stdbool.h>
#include <stdint.h>

#include "flight.h"
#include "fc_config.h"

#define FC_MAX_AIRCRAFT 64

typedef struct {
    char  hex[8];
    char  callsign[12];
    char  type[6];
    char  category[4];
    char  reg[12];
    bool  has_lat;
    bool  has_lon;
    bool  has_alt;
    bool  has_gs;
    bool  has_seen;
    bool  alt_baro_ground;
    float lat;
    float lon;
    float alt_ft;
    float gs_kt;
    float seen_pos;
} raw_aircraft_t;

bool fc_is_helicopter(const char *type_code, const char *category);
float fc_apparent_size_deg(float alt_ft, float ground_m, float span_m);

/* Applies the filters in the documented order; returns true if accepted. */
bool fc_rank_one(const raw_aircraft_t *ac, const config_t *cfg, flight_t *out);

/* Returns the largest accepted aircraft, or false if none. */
bool fc_rank_best(const raw_aircraft_t *ac, int count, const config_t *cfg,
                  flight_t *out);

#endif /* FC_RANKING_H */
