/*
 * Flightcard - tiny platform abstraction used by host unit tests.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_PLATFORM_H
#define FC_PLATFORM_H

#include <stdint.h>

uint64_t fc_monotonic_ms(void);
void fc_delay_ms(uint32_t ms);

#endif /* FC_PLATFORM_H */
