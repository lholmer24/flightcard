/*
 * Flightcard - demo cards.
 *
 * SPDX-License-Identifier: MIT
 */
#include "demo.h"

#include <string.h>

static const card_msg_t DEMO[FC_DEMO_CARDS] = {
    {
        .flight = {
            .callsign = "EK1", .hex = "8963f6", .type = "A388",
            .category = "A3", .reg = "A6-EVG",
            .alt_ft = 3550.0f, .gs_kt = 148.0f, .dist_km = 8.4f,
        },
        .type_name = "Airbus A380-800",
        .airline = "EMIRATES",
        .origin = "DXB", .dest = "LHR",
        .has_route = true, .enriched = true, .valid = true,
        .sequence = 1,
    },
    {
        .flight = {
            .callsign = "LOG53T", .hex = "407b4d", .type = "DH8D",
            .category = "A2", .reg = "G-LMTC",
            .alt_ft = 5100.0f, .gs_kt = 186.0f, .dist_km = 10.8f,
        },
        .type_name = "De Havilland Dash 8 Q400",
        .airline = "LOGANAIR",
        .origin = "LDY", .dest = "LCY",
        .has_route = true, .enriched = true, .valid = true,
        .sequence = 2,
    },
    {
        .flight = {
            .callsign = "EDV5123", .hex = "a1b2c3", .type = "CRJX",
            .category = "A3", .reg = "N601XJ",
            .alt_ft = 4800.0f, .gs_kt = 284.0f, .dist_km = 6.8f,
        },
        .type_name = "Bombardier CRJ-1000",
        .airline = "DELTA CONNECTION",
        .origin = "JFK", .dest = "BOS",
        .has_route = true, .enriched = true, .valid = true,
        .sequence = 3,
    },
    {
        .flight = {
            .callsign = "HELIMED", .hex = "407abc", .type = "EC35",
            .category = "A7", .reg = "G-HMED",
            .alt_ft = 1240.0f, .gs_kt = 118.0f, .dist_km = 3.6f,
        },
        .type_name = "Airbus H135",
        .airline = "HELI MED SERVICE",
        .origin = "BQH", .dest = "?",
        .has_route = true, .enriched = true, .valid = true,
        .sequence = 4,
    },
    {
        .flight = {
            .callsign = "N172SP", .hex = "a4b1c2", .type = "C172",
            .category = "A1", .reg = "N172SP",
            .alt_ft = 2300.0f, .gs_kt = 96.0f, .dist_km = 6.2f,
        },
        .type_name = "Cessna 172",
        .airline = "",
        .origin = "?", .dest = "?",
        .has_route = false, .enriched = true, .valid = true,
        .sequence = 5,
    },
};

const card_msg_t *fc_demo_card(unsigned index)
{
    return &DEMO[index % FC_DEMO_CARDS];
}

void fc_demo_clock_epoch(uint32_t *epoch)
{
    /* 2026-11-20 21:47:00 UTC, matching the firmware's demo clock. */
    if (epoch) *epoch = 1795211220u;
}
