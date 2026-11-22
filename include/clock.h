/*
 * Flightcard - shared UTC clock service.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_CLOCK_H
#define FC_CLOCK_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

void clock_init(void);
void clock_set_utc(uint32_t epoch);
uint32_t clock_now_utc(void);
bool clock_is_valid(void);          /* year >= 2024 */
struct tm clock_now_local(void);    /* uses the configured timezone */
void clock_get_local(struct tm *out);
void clock_set_timezone(const char *tz);
const char *clock_timezone(void);

/* Milliseconds until the next local minute boundary (1..60000). */
uint32_t clock_ms_to_next_minute(void);

#endif /* FC_CLOCK_H */
