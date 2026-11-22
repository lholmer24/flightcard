/*
 * Flightcard - BH1750 and optional I2C RTC.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_SENSORS_H
#define FC_SENSORS_H

#include <stdbool.h>
#include <stdint.h>

void sensors_init(void);

bool sensors_light_present(void);
bool sensors_read_lux(float *lux);

bool sensors_rtc_present(void);
bool sensors_rtc_read(uint32_t *epoch);
bool sensors_rtc_write(uint32_t epoch);

#endif /* FC_SENSORS_H */
