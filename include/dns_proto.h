/*
 * Flightcard - bounded DNS wire-format helpers.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_DNS_PROTO_H
#define FC_DNS_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Build an A-record query. Returns packet length or 0. */
size_t fc_dns_build_query(uint8_t *buf, size_t cap, const char *host,
                          uint16_t id);

/* Parse an A record. Writes a dotted quad to ip_out. */
bool fc_dns_parse_response(const uint8_t *data, size_t len, uint16_t id,
                           char ip_out[16]);

#endif /* FC_DNS_PROTO_H */
