/*
 * Flightcard - ST7789/LVGL display driver.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_DISPLAY_DRIVER_H
#define FC_DISPLAY_DRIVER_H

#include <stdint.h>

#define FC_DISPLAY_W 320
#define FC_DISPLAY_H 240

/* Initialises SPI, the panel, PWM backlight and LVGL. */
void display_init(void);

/* 0.0 .. 1.0. */
void display_set_backlight(float value);

/* Run LVGL's timer handler (only from display_task). */
void display_poll(void);

/* Hardware diagnostics for the bench (solid bars / colour order). */
void display_test_pattern(void);

#endif /* FC_DISPLAY_DRIVER_H */
