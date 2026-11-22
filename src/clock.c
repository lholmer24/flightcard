/*
 * Flightcard - shared clock.
 *
 * SPDX-License-Identifier: MIT
 */
#include "clock.h"
#include "platform.h"
#include "timezone.h"

#include <string.h>

static uint32_t base_epoch;
static uint64_t base_ms;
static uint32_t seq;
static char tz_name[40] = "UTC";

void clock_init(void)
{
    /* 2000-01-01 00:00:00 UTC cold-boot placeholder until NTP or a seed. */
    base_epoch = 946684800u;
    base_ms = fc_monotonic_ms();
    seq = 0;
    strncpy(tz_name, "UTC", sizeof(tz_name) - 1);
}

void clock_set_utc(uint32_t epoch)
{
    if (epoch == 0) return;
    __atomic_add_fetch(&seq, 1, __ATOMIC_ACQ_REL);
    __atomic_store_n(&base_epoch, epoch, __ATOMIC_RELAXED);
    __atomic_store_n(&base_ms, fc_monotonic_ms(), __ATOMIC_RELAXED);
    __atomic_add_fetch(&seq, 1, __ATOMIC_ACQ_REL);
}

uint32_t clock_now_utc(void)
{
    uint32_t s1 = 0, s2 = 0, epoch = 0;
    uint64_t base = 0;
    do {
        s1 = __atomic_load_n(&seq, __ATOMIC_ACQUIRE);
        if (s1 & 1u) continue;
        epoch = __atomic_load_n(&base_epoch, __ATOMIC_RELAXED);
        base = __atomic_load_n(&base_ms, __ATOMIC_RELAXED);
        s2 = __atomic_load_n(&seq, __ATOMIC_ACQUIRE);
    } while (s1 != s2);

    uint64_t elapsed = fc_monotonic_ms() - base;
    return epoch + (uint32_t)(elapsed / 1000u);
}

bool clock_is_valid(void)
{
    uint32_t now = clock_now_utc();
    struct tm utc;
    time_t t = (time_t)now;
    if (gmtime_r(&t, &utc) == NULL) return false;
    return (utc.tm_year + 1900) >= 2024;
}

void clock_set_timezone(const char *tz)
{
    if (tz == NULL || *tz == '\0') tz = "UTC";
    strncpy(tz_name, tz, sizeof(tz_name) - 1);
    tz_name[sizeof(tz_name) - 1] = '\0';
}

const char *clock_timezone(void)
{
    return tz_name;
}

void clock_get_local(struct tm *out)
{
    if (out == NULL) return;
    fc_localtime(clock_now_utc(), tz_name, out);
}

struct tm clock_now_local(void)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    clock_get_local(&t);
    return t;
}

uint32_t clock_ms_to_next_minute(void)
{
    uint32_t now = clock_now_utc();
    uint64_t base = __atomic_load_n(&base_ms, __ATOMIC_RELAXED);
    uint64_t elapsed = fc_monotonic_ms() - base;
    uint64_t ms = (uint64_t)now * 1000u + (elapsed % 1000u);
    uint32_t rem = (uint32_t)(ms % 60000u);
    return rem == 0 ? 60000u : 60000u - rem;
}
