/*
 * Flightcard - timezone rules.
 *
 * SPDX-License-Identifier: MIT
 */
#include "timezone.h"
#include "log.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define R_DAY 86400u

static const char *const WEEKDAYS[7] = {
    "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY",
    "THURSDAY", "FRIDAY", "SATURDAY",
};
static const char *const MONTHS[12] = {
    "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE",
    "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER",
};

static bool tz_warned;

const char *fc_weekday_name(int wday)
{
    if (wday < 0 || wday > 6) return "";
    return WEEKDAYS[wday];
}

const char *fc_month_name(int mon)
{
    if (mon < 1 || mon > 12) return "";
    return MONTHS[mon - 1];
}

static int days_in_month(int year, int month)
{
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2) {
        bool leap = (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0);
        return leap ? 29 : 28;
    }
    if (month < 1 || month > 12) return 0;
    return days[month - 1];
}

/* 0 = Sunday ... 6 = Saturday (Sakamoto's algorithm). */
static int weekday_sunday0(int year, int month, int day)
{
    static const int offsets[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) year--;
    return (year + year / 4 - year / 100 + year / 400
            + offsets[month - 1] + day) % 7;
}

int fc_last_sunday(int year, int month)
{
    int day = days_in_month(year, month);
    return day - weekday_sunday0(year, month, day);
}

bool fc_uk_dst_active(const struct tm *utc)
{
    int year = utc->tm_year + 1900;
    int start_day = fc_last_sunday(year, 3);
    int end_day = fc_last_sunday(year, 10);

    /* Compare as civil tuples: [start, end). */
    long now = ((long)(utc->tm_mon + 1) * 100 + utc->tm_mday) * 10000L
               + utc->tm_hour * 100L + utc->tm_min;
    long start = (3L * 100 + start_day) * 10000L + 100L; /* 01:00 */
    long end = (10L * 100 + end_day) * 10000L + 100L;
    return now >= start && now < end;
}

bool fc_parse_fixed_offset(const char *text, int *seconds_east)
{
    if (text == NULL || seconds_east == NULL) return false;

    char buf[40];
    size_t n = 0;
    while (*text && n + 1 < sizeof(buf)) {
        char c = *text++;
        if (!isspace((unsigned char)c)) buf[n++] = (char)toupper((unsigned char)c);
    }
    buf[n] = '\0';
    if (n == 0) {
        *seconds_east = 0;
        return true;
    }
    if (strcmp(buf, "UTC") == 0 || strcmp(buf, "GMT") == 0) {
        *seconds_east = 0;
        return true;
    }
    char *p = buf;
    if (strncmp(p, "UTC", 3) == 0) p += 3;
    else if (strncmp(p, "GMT", 3) == 0) p += 3;
    if (*p == '\0') {
        *seconds_east = 0;
        return true;
    }

    int sign = 1;
    if (*p == '+' || *p == '-') {
        if (*p == '-') sign = -1;
        p++;
    }
    if (*p == '\0') return false;

    if (strchr(p, ':') != NULL) {
        char *end = NULL;
        long hours = strtol(p, &end, 10);
        if (end == p || *end != ':') return false;
        p = end + 1;
        long minutes = strtol(p, &end, 10);
        if (end == p) return false;
        while (*end && isspace((unsigned char)*end)) end++;
        if (*end != '\0') return false;
        if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) return false;
        *seconds_east = (int)(sign * (hours * 3600 + minutes * 60));
        return true;
    }

    char *end = NULL;
    double hours = strtod(p, &end);
    if (end == p) return false;
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end != '\0') return false;
    if (!isfinite(hours) || hours < -24.0 || hours > 24.0) return false;
    *seconds_east = (int)lround(sign * hours * 3600.0);
    return true;
}

static bool is_uk_name(const char *tz)
{
    return strcmp(tz, "auto") == 0
        || strcmp(tz, "europe/london") == 0
        || strcmp(tz, "europe/dublin") == 0
        || strcmp(tz, "europe/lisbon") == 0;
}

int fc_tz_offset_at(uint32_t epoch, const char *tz)
{
    if (tz == NULL || *tz == '\0') tz = "UTC";

    char lower[40];
    size_t n = 0;
    while (tz[n] && n + 1 < sizeof(lower)) {
        lower[n] = (char)tolower((unsigned char)tz[n]);
        n++;
    }
    lower[n] = '\0';

    time_t t = (time_t)epoch;
    struct tm utc;
    if (gmtime_r(&t, &utc) == NULL) {
        memset(&utc, 0, sizeof(utc));
        utc.tm_year = 100;
        utc.tm_mon = 0;
        utc.tm_mday = 1;
    }

    if (is_uk_name(lower)) {
        return fc_uk_dst_active(&utc) ? 3600 : 0;
    }
    if (strcmp(lower, "utc") == 0 || strcmp(lower, "gmt") == 0) {
        return 0;
    }

    int offset = 0;
    if (fc_parse_fixed_offset(tz, &offset)) {
        return offset;
    }

    if (!tz_warned) {
        tz_warned = true;
        fc_log("timezone '%s' not recognised; using UTC", tz);
    }
    return 0;
}

void fc_localtime(uint32_t epoch, const char *tz, struct tm *out)
{
    if (out == NULL) return;
    int offset = fc_tz_offset_at(epoch, tz);
    time_t t = (time_t)((int64_t)epoch + offset);
    if (gmtime_r(&t, out) == NULL) {
        memset(out, 0, sizeof(*out));
        out->tm_year = 100;
        out->tm_mday = 1;
    }
}

static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (m + (m > 2 ? (unsigned)-3 : 9u)) + 2u) / 5u + d - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

uint32_t fc_epoch_from_utc(const struct tm *tm)
{
    if (tm == NULL) return 0;
    int year = tm->tm_year + 1900;
    unsigned month = (unsigned)(tm->tm_mon + 1);
    unsigned day = (unsigned)tm->tm_mday;
    if (month < 1 || month > 12 || day < 1 || day > 31) return 0;
    int64_t days = days_from_civil(year, month, day);
    int64_t secs = days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
    if (secs < 0 || secs > 0xFFFFFFFFll) return 0;
    return (uint32_t)secs;
}
