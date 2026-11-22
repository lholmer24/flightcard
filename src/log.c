/*
 * Flightcard - logging and platform shims.
 *
 * SPDX-License-Identifier: MIT
 */
#include "log.h"
#include "platform.h"

#include <stdarg.h>
#include <stdio.h>

#ifndef FC_HOST_TEST
#include "pico/stdlib.h"
#include "pico/time.h"

uint64_t fc_monotonic_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

void fc_delay_ms(uint32_t ms)
{
    sleep_ms(ms);
}
#else
#include <time.h>

uint64_t fc_monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

void fc_delay_ms(uint32_t ms)
{
    (void)ms;
}
#endif

void fc_log(const char *fmt, ...)
{
    uint64_t ms = fc_monotonic_ms();
    printf("[%lu.%03lu] ", (unsigned long)(ms / 1000u),
           (unsigned long)(ms % 1000u));
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

void fc_log_hexdump(const void *data, unsigned len)
{
    const uint8_t *p = (const uint8_t *)data;
    for (unsigned i = 0; i < len; i++) {
        if ((i % 16) == 0) printf("%04x: ", i);
        printf("%02x ", p[i]);
        if ((i % 16) == 15) printf("\n");
    }
    if ((len % 16) != 0) printf("\n");
}
