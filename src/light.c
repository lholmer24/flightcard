/*
 * Flightcard - backlight and palette policy.
 *
 * SPDX-License-Identifier: MIT
 */
#include "light.h"

#include <math.h>
#include <stddef.h>

#define MIN_BRIGHTNESS 0.05f
#define MAX_BRIGHTNESS 0.45f

static const float LUX_X[] = {0.0f, 10.0f, 100.0f, 1000.0f, 10000.0f};
static const float LUX_Y[] = {0.05f, 0.10f, 0.30f, 0.45f, 0.45f};

float fc_lux_to_brightness(float lux)
{
    if (lux <= 0.0f) return MIN_BRIGHTNESS;
    for (unsigned i = 0; i + 1 < sizeof(LUX_X) / sizeof(LUX_X[0]); i++) {
        if (lux <= LUX_X[i + 1]) {
            double x0 = log((double)LUX_X[i] + 1.0);
            double x1 = log((double)LUX_X[i + 1] + 1.0);
            double x = log((double)lux + 1.0);
            double t = (x - x0) / (x1 - x0);
            float v = LUX_Y[i] + (float)t * (LUX_Y[i + 1] - LUX_Y[i]);
            if (v < MIN_BRIGHTNESS) v = MIN_BRIGHTNESS;
            if (v > MAX_BRIGHTNESS) v = MAX_BRIGHTNESS;
            return v;
        }
    }
    return MAX_BRIGHTNESS;
}

bool fc_palette_step(int *mode, float lux, float night_lux)
{
    if (mode == NULL || night_lux <= 0.0f) return false;
    if (*mode == FC_PALETTE_CABIN && lux < night_lux) {
        *mode = FC_PALETTE_AMBER;
        return true;
    }
    if (*mode == FC_PALETTE_AMBER && lux > night_lux * 1.6f) {
        *mode = FC_PALETTE_CABIN;
        return true;
    }
    return false;
}

float fc_brightness_slew(float current, float target)
{
    return current + (target - current) * 0.4f;
}
