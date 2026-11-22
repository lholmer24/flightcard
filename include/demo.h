/*
 * Flightcard - demo cards.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_DEMO_H
#define FC_DEMO_H

#include "flight.h"

#define FC_DEMO_CARDS 5

const card_msg_t *fc_demo_card(unsigned index);
void fc_demo_clock_epoch(uint32_t *epoch);

#endif /* FC_DEMO_H */
