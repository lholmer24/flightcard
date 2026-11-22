/*
 * Flightcard - BH1750 / backlight / palette policy.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_LIGHT_H
#define FC_LIGHT_H

#include <stdbool.h>

#include "fc_config.h"

float fc_lux_to_brightness(float lux);

/* Returns true when the palette changed. mode is FC_PALETTE_CABIN/AMBER. */
bool fc_palette_step(int *mode, float lux, float night_lux);

/* Slew towards a target with the 0.4 factor used by the display policy. */
float fc_brightness_slew(float current, float target);

#endif /* FC_LIGHT_H */
