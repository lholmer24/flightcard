/*
 * Flightcard - fixed-offset and UK civil time without tzdata.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_TIMEZONE_H
#define FC_TIMEZONE_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* Seconds east of UTC at the supplied UTC instant. Unrecognised names fall
 * back to UTC and log once. */
int fc_tz_offset_at(uint32_t epoch, const char *tz);

/* Convert a UTC epoch to local civil time. */
void fc_localtime(uint32_t epoch, const char *tz, struct tm *out);

const char *fc_weekday_name(int wday);   /* 0 = Sunday */
const char *fc_month_name(int mon);      /* 1 = January */

/* UTC civil time -> Unix epoch (no libc timegm dependency). */
uint32_t fc_epoch_from_utc(const struct tm *tm);

/* Exposed for tests. */
int fc_last_sunday(int year, int month);
bool fc_uk_dst_active(const struct tm *utc);
bool fc_parse_fixed_offset(const char *text, int *seconds_east);

#endif /* FC_TIMEZONE_H */
