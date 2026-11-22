/*
 * Flightcard - type lookup (table itself is generated).
 *
 * SPDX-License-Identifier: MIT
 */
#include "types.h"

#include <string.h>

const fc_aircraft_type_t *fc_type_lookup(const char *code)
{
    if (code == NULL || *code == '\0') return NULL;
    for (unsigned i = 0; i < fc_type_count; i++) {
        if (strcmp(fc_types[i].code, code) == 0) return &fc_types[i];
    }
    return NULL;
}

const char *fc_type_name(const char *code)
{
    const fc_aircraft_type_t *t = fc_type_lookup(code);
    if (t != NULL && t->name != NULL && *t->name) return t->name;
    return (code != NULL && *code) ? code : "Unknown";
}

float fc_type_span(const char *code)
{
    const fc_aircraft_type_t *t = fc_type_lookup(code);
    if (t != NULL && t->span_m > 0.0f) return t->span_m;
    return 40.0f;
}
