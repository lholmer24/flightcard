/*
 * Flightcard - SNTP packet helpers.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ntp.h"

#include <string.h>

static uint32_t rd32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
           | ((uint32_t)p[2] << 8) | p[3];
}

void fc_ntp_build(uint8_t packet[FC_NTP_PACKET_SIZE])
{
    memset(packet, 0, FC_NTP_PACKET_SIZE);
    packet[0] = 0x23; /* LI = 0, VN = 4, Mode = 3 (client) */
}

bool fc_ntp_parse(const uint8_t packet[FC_NTP_PACKET_SIZE], uint32_t *epoch)
{
    if (packet == NULL || epoch == NULL) return false;
    uint8_t mode = packet[0] & 0x07;
    uint8_t stratum = packet[1];
    if (mode != 4 && mode != 5) return false;
    if (stratum == 0 || stratum > 15) return false;

    uint32_t seconds = rd32be(packet + 40);
    if (seconds < FC_NTP_UNIX_OFFSET) return false;
    uint32_t unix_time = seconds - FC_NTP_UNIX_OFFSET;
    if (unix_time < 1700000000u || unix_time > 4102444800u) return false;
    *epoch = unix_time;
    return true;
}
