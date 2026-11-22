/*
 * Flightcard - DNS wire format.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dns_proto.h"

#include <stdio.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
           | ((uint32_t)p[2] << 8) | p[3];
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

size_t fc_dns_build_query(uint8_t *buf, size_t cap, const char *host,
                          uint16_t id)
{
    if (buf == NULL || host == NULL || cap < 12 + 5) return 0;
    size_t n = 0;
    wr16(buf + 0, id);
    wr16(buf + 2, 0x0100); /* standard query, recursion desired */
    wr16(buf + 4, 1);      /* QDCOUNT */
    wr16(buf + 6, 0);
    wr16(buf + 8, 0);
    wr16(buf + 10, 0);
    n = 12;

    const char *p = host;
    while (*p) {
        const char *dot = strchr(p, '.');
        size_t label = dot ? (size_t)(dot - p) : strlen(p);
        if (label == 0 || label > 63) return 0;
        if (n + 1 + label + 5 > cap) return 0;
        buf[n++] = (uint8_t)label;
        memcpy(buf + n, p, label);
        n += label;
        if (!dot) break;
        p = dot + 1;
    }
    buf[n++] = 0;
    wr16(buf + n, 1); /* A */
    n += 2;
    wr16(buf + n, 1); /* IN */
    n += 2;
    return n;
}

static bool skip_name(const uint8_t *data, size_t len, size_t *pos)
{
    size_t p = *pos;
    int guard = 0;
    while (p < len) {
        uint8_t c = data[p++];
        if (c == 0) {
            *pos = p;
            return true;
        }
        if ((c & 0xC0) == 0xC0) {
            if (p >= len) return false;
            p++;
            *pos = p;
            return true;
        }
        if (p + c > len) return false;
        p += c;
        if (++guard > 128) return false;
    }
    return false;
}

bool fc_dns_parse_response(const uint8_t *data, size_t len, uint16_t id,
                           char ip_out[16])
{
    if (data == NULL || ip_out == NULL || len < 12) return false;
    if (rd16(data) != id) return false;
    uint16_t flags = rd16(data + 2);
    if ((flags & 0x8000u) == 0) return false;      /* not a response */
    if ((flags & 0x000Fu) != 0) return false;      /* error rcode */
    uint16_t qd = rd16(data + 4);
    uint16_t an = rd16(data + 6);
    if (an == 0) return false;

    size_t pos = 12;
    for (uint16_t i = 0; i < qd; i++) {
        if (!skip_name(data, len, &pos) || pos + 4 > len) return false;
        pos += 4;
    }

    for (uint16_t i = 0; i < an; i++) {
        if (!skip_name(data, len, &pos) || pos + 10 > len) return false;
        uint16_t type = rd16(data + pos);
        uint16_t cls = rd16(data + pos + 2);
        uint16_t rdlen = rd16(data + pos + 8);
        pos += 10;
        if (pos + rdlen > len) return false;
        if (type == 1 && cls == 1 && rdlen == 4) {
            const uint8_t *a = data + pos;
            if (snprintf(ip_out, 16, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]) < 0) {
                return false;
            }
            return true;
        }
        pos += rdlen;
    }
    (void)rd32;
    return false;
}
