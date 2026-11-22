/*
 * Flightcard - text and number formatting shared by the UI and tests.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_FORMAT_H
#define FC_FORMAT_H

#include <stddef.h>
#include <stdint.h>

#define FC_ELLIPSIS "\xE2\x80\xA6" /* U+2026 */

/* Copy at most limit characters; append an ellipsis if truncated. */
void fc_ellipsize(char *dst, size_t cap, const char *src, int limit);
void fc_upper_ellipsize(char *dst, size_t cap, const char *src, int limit);

/* Balanced two-line wrap, measured in characters (matches the reference layout).
 * Returns 2 lines, either may be empty. */
void fc_wrap_type(const char *text, int limit, char line1[64], char line2[64]);

/* "148 kt", "3,550 ft" (nearest 10 ft), "8.4 km". */
void fc_format_speed(char *dst, size_t cap, float kt);
void fc_format_altitude(char *dst, size_t cap, float ft);
void fc_format_distance(char *dst, size_t cap, float km);

/* Insert thousands separators into a non-negative integer. */
void fc_group_number(char *dst, size_t cap, unsigned long value);

#endif /* FC_FORMAT_H */
