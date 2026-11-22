/*
 * Flightcard - SNTP packet helpers.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_NTP_H
#define FC_NTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FC_NTP_PACKET_SIZE 48
#define FC_NTP_UNIX_OFFSET 2208988800u

void fc_ntp_build(uint8_t packet[FC_NTP_PACKET_SIZE]);
bool fc_ntp_parse(const uint8_t packet[FC_NTP_PACKET_SIZE], uint32_t *epoch);

#endif /* FC_NTP_H */
