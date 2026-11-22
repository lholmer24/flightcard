/*
 * Flightcard - LVGL interface.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_UI_H
#define FC_UI_H

#include <stdbool.h>

#include "flight.h"

void ui_init(void);
void ui_update_clock(void);          /* cheap; redraws only on a minute change */
void ui_show_clock(void);
void ui_show_card(const card_msg_t *msg);
void ui_set_palette(int mode);       /* FC_PALETTE_CABIN / AMBER */
int ui_palette(void);
void ui_refresh_sprite(void);

#endif /* FC_UI_H */
