/*
 * Flightcard - ICAO type table.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_TYPES_H
#define FC_TYPES_H

#include <stdbool.h>

typedef struct {
    const char *code;
    const char *name;
    float span_m;
} fc_aircraft_type_t;

/* Generated from tools/types.json by tools/convert_assets.py. */
extern const fc_aircraft_type_t fc_types[];
extern const unsigned fc_type_count;

const fc_aircraft_type_t *fc_type_lookup(const char *code);
const char *fc_type_name(const char *code); /* returns code if unknown */
float fc_type_span(const char *code);       /* default 40.0 m */

#endif /* FC_TYPES_H */
