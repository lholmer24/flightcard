/*
 * Flightcard - bounded DNS A-record resolver with cache and static fallback.
 *
 * SPDX-License-Identifier: MIT
 */
#include "net.h"
#include "dns_proto.h"
#include "log.h"
#include "platform.h"

#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/sockets.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#define DNS_CACHE_SIZE 8
#define DNS_TIMEOUT_MS 2000

typedef struct {
    char host[64];
    char ip[16];
} dns_entry_t;

static dns_entry_t cache[DNS_CACHE_SIZE];
static int cache_count;

typedef struct {
    const char *host;
    const char *ip;
} static_entry_t;

static const static_entry_t STATIC_DNS[] = {
    {"opendata.adsb.fi", "104.26.14.99"},
    {"api.adsbdb.com", "91.99.163.199"},
};

void dns_cache_clear(void)
{
    memset(cache, 0, sizeof(cache));
    cache_count = 0;
}

static bool is_ipv4(const char *s)
{
    int parts = 0;
    int value = 0;
    int digits = 0;
    for (;;) {
        char c = *s++;
        if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
            digits++;
            if (value > 255 || digits > 3) return false;
        } else if (c == '.' || c == '\0') {
            if (digits == 0) return false;
            parts++;
            value = 0;
            digits = 0;
            if (c == '\0') break;
        } else {
            return false;
        }
    }
    return parts == 4;
}

static bool query_server(const char *server, const char *host, char ip[16],
                         uint32_t timeout_ms)
{
    int fd = lwip_socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;

    struct timeval tv;
    tv.tv_sec = (time_t)(timeout_ms / 1000);
    tv.tv_usec = (suseconds_t)((timeout_ms % 1000) * 1000);
    lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t query[300];
    uint16_t id = (uint16_t)(fc_monotonic_ms() ^ (uintptr_t)host);
    size_t qlen = fc_dns_build_query(query, sizeof(query), host, id);
    if (qlen == 0) {
        lwip_close(fd);
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    if (!ip4addr_aton(server, (ip4_addr_t *)&addr.sin_addr)) {
        lwip_close(fd);
        return false;
    }

    bool ok = false;
    if (lwip_sendto(fd, query, qlen, 0, (struct sockaddr *)&addr,
                    sizeof(addr)) == (int)qlen) {
        uint8_t reply[512];
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int n = lwip_recvfrom(fd, reply, sizeof(reply), 0,
                              (struct sockaddr *)&from, &from_len);
        if (n > 0) ok = fc_dns_parse_response(reply, (size_t)n, id, ip);
    }
    lwip_close(fd);
    return ok;
}

static void cache_put(const char *host, const char *ip)
{
    if (cache_count < DNS_CACHE_SIZE) {
        snprintf(cache[cache_count].host, sizeof(cache[cache_count].host), "%s", host);
        snprintf(cache[cache_count].ip, sizeof(cache[cache_count].ip), "%s", ip);
        cache_count++;
    }
}

bool net_dns_resolve(const char *host, char ip[16], uint32_t timeout_ms)
{
    if (host == NULL || ip == NULL) return false;
    if (is_ipv4(host)) {
        snprintf(ip, 16, "%s", host);
        return true;
    }
    for (int i = 0; i < cache_count; i++) {
        if (strcmp(cache[i].host, host) == 0) {
            snprintf(ip, 16, "%s", cache[i].ip);
            return true;
        }
    }

    LOG_I("dns: %s", host);
    char servers[2][24];
    int server_count = 0;
    for (int i = 0; i < DNS_MAX_SERVERS; i++) {
        const ip_addr_t *s = dns_getserver((u8_t)i);
        if (s != NULL && !ip_addr_isany(s)) {
            const char *text = ipaddr_ntoa(s);
            if (text != NULL && is_ipv4(text)) {
                snprintf(servers[server_count], sizeof(servers[0]), "%s", text);
                server_count++;
            }
        }
    }

    for (int i = 0; i < server_count; i++) {
        if (query_server(servers[i], host, ip, timeout_ms)) {
            LOG_I("dns: %s -> %s", host, ip);
            cache_put(host, ip);
            return true;
        }
    }

    for (unsigned i = 0; i < sizeof(STATIC_DNS) / sizeof(STATIC_DNS[0]); i++) {
        if (strcmp(host, STATIC_DNS[i].host) == 0) {
            LOG_W("dns: static fallback %s -> %s", host, STATIC_DNS[i].ip);
            snprintf(ip, 16, "%s", STATIC_DNS[i].ip);
            cache_put(host, ip);
            return true;
        }
    }

    static const char *public_servers[] = {"1.1.1.1", "8.8.8.8"};
    for (unsigned i = 0; i < sizeof(public_servers) / sizeof(public_servers[0]); i++) {
        if (query_server(public_servers[i], host, ip, timeout_ms)) {
            LOG_I("dns: %s -> %s (public)", host, ip);
            cache_put(host, ip);
            return true;
        }
    }

    LOG_W("dns: failed for %s", host);
    return false;
}
