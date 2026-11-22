/*
 * Flightcard - allocation-free adsb.fi feed parser.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_JSON_H
#define FC_JSON_H

#include <stdbool.h>
#include <stddef.h>

#include "ranking.h"

/* Parse up to max entries from the "ac" array. Malformed entries are skipped.
 * Returns the number of valid entries (0 on malformed top-level JSON). */
int fc_parse_feed(const char *json, size_t len, raw_aircraft_t *out, int max);

/* Helpers also used by the adsbdb enrichment parser. `path` is a NULL-terminated
 * array of object keys, e.g. {"response", "aircraft", "icao_type", NULL}. */
bool fc_json_get_object_string(const char *json, size_t len,
                               const char *const *path, char *dst, size_t cap);
bool fc_json_get_object_number(const char *json, size_t len,
                               const char *const *path, double *out);

#endif /* FC_JSON_H */
