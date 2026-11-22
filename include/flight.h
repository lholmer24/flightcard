/*
 * Flightcard - inter-task flight/card messages.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_FLIGHT_H
#define FC_FLIGHT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char  callsign[12];
    char  hex[8];
    char  type[6];          /* ICAO type code */
    char  category[4];
    char  reg[12];
    float alt_ft;
    float gs_kt;
    float dist_km;
    float size_deg;
} flight_t;

typedef struct {
    flight_t flight;
    char     type_name[40];  /* local table or adsbdb */
    char     airline[32];
    char     origin[4];
    char     dest[4];
    bool     has_route;
    bool     enriched;
    bool     valid;          /* false = clear the card and show the clock */
    uint32_t sequence;       /* latest-wins */
} card_msg_t;

#endif /* FC_FLIGHT_H */
