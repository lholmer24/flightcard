/*
 * Flightcard - network task: WiFi, NTP, adsb.fi feed, adsbdb enrichment.
 *
 * SPDX-License-Identifier: MIT
 */
#include "net.h"
#include "clock.h"
#include "config_store.h"
#include "http.h"
#include "json.h"
#include "log.h"
#include "ntp.h"
#include "platform.h"
#include "ranking.h"
#include "types.h"

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/sockets.h"
#include "lwip/ip4_addr.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>

QueueHandle_t g_card_queue;
volatile uint32_t g_net_heartbeat;
volatile int g_wifi_state;
volatile int g_net_phase;

static uint32_t sequence;
static char feed_path[128];
static int feed_nm;
static uint32_t next_ntp_ms;
static uint32_t next_poll_ms;

#define NTP_RESYNC_MS (6u * 60u * 60u * 1000u)
#define NTP_RETRY_MS (60u * 1000u)
#define CACHE_MAX 30

static const char *const NTP_SERVERS[] = {
    "162.159.200.1", "162.159.200.123", "216.239.35.0",
};

typedef struct {
    char hex[8];
    char type[6];
    char reg[12];
    char manufacturer[32];
    char model[32];
} meta_entry_t;

typedef struct {
    char callsign[12];
    char airline[32];
    char origin[4];
    char dest[4];
    bool has_route;
} route_entry_t;

static meta_entry_t meta_cache[CACHE_MAX];
static int meta_count;
static route_entry_t route_cache[CACHE_MAX];
static int route_count;
static int route_next;

static bool fetch_json(const char *host, const char *path,
                       const char **body, size_t *body_len, uint32_t timeout_ms)
{
    for (int attempt = 0; attempt < 2; attempt++) {
        int status = net_https_get(host, path, body, body_len, timeout_ms);
        if (status == 200 && *body != NULL && *body_len > 0) return true;
        /* Do not log the request path: the feed path embeds the location. */
        LOG_W("http: %s status %d%s", host, status,
              attempt == 0 ? " (retry)" : "");
    }
    return false;
}

static void publish(const card_msg_t *msg)
{
    if (g_card_queue != NULL) {
        xQueueOverwrite(g_card_queue, msg);
    }
}

static void publish_clear(void)
{
    card_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.valid = false;
    msg.sequence = ++sequence;
    publish(&msg);
}

static bool ntp_query(const char *server, uint32_t timeout_ms, uint32_t *epoch)
{
    int fd = lwip_socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;

    struct timeval tv;
    tv.tv_sec = (time_t)(timeout_ms / 1000);
    tv.tv_usec = (suseconds_t)((timeout_ms % 1000) * 1000);
    lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(123);
    if (!ip4addr_aton(server, (ip4_addr_t *)&addr.sin_addr)) {
        lwip_close(fd);
        return false;
    }

    uint8_t packet[FC_NTP_PACKET_SIZE];
    fc_ntp_build(packet);
    bool ok = false;
    if (lwip_sendto(fd, packet, sizeof(packet), 0,
                    (struct sockaddr *)&addr, sizeof(addr)) == (int)sizeof(packet)) {
        uint8_t reply[FC_NTP_PACKET_SIZE];
        int n = lwip_recvfrom(fd, reply, sizeof(reply), 0, NULL, NULL);
        if (n == (int)sizeof(reply)) ok = fc_ntp_parse(reply, epoch);
    }
    lwip_close(fd);
    return ok;
}

bool net_ntp_sync(uint32_t timeout_ms)
{
    g_net_phase = 2;
    for (unsigned i = 0; i < sizeof(NTP_SERVERS) / sizeof(NTP_SERVERS[0]); i++) {
        uint32_t epoch = 0;
        LOG_I("ntp: query %s", NTP_SERVERS[i]);
        if (ntp_query(NTP_SERVERS[i], timeout_ms, &epoch)) {
            clock_set_utc(epoch);
            LOG_I("ntp: %s -> %lu", NTP_SERVERS[i], (unsigned long)epoch);
            g_net_phase = 0;
            return true;
        }
        LOG_W("ntp: %s timed out", NTP_SERVERS[i]);
    }
    g_net_phase = 0;
    return false;
}

static const meta_entry_t *meta_cache_lookup(const char *hex)
{
    for (int i = 0; i < meta_count; i++) {
        if (strcasecmp(meta_cache[i].hex, hex) == 0) return &meta_cache[i];
    }
    return NULL;
}

static void meta_cache_put(const meta_entry_t *entry)
{
    if (meta_count >= CACHE_MAX) meta_count = 0;
    meta_cache[meta_count++] = *entry;
}

static bool route_field_known(const char *value)
{
    return value[0] != '\0' && strcmp(value, "?") != 0;
}

static const route_entry_t *route_cache_lookup(const char *callsign)
{
    for (int i = 0; i < route_count; i++) {
        if (strcasecmp(route_cache[i].callsign, callsign) == 0) {
            return &route_cache[i];
        }
    }
    return NULL;
}

static void route_cache_put(const route_entry_t *entry)
{
    for (int i = 0; i < route_count; i++) {
        if (strcasecmp(route_cache[i].callsign, entry->callsign) == 0) {
            route_cache[i] = *entry;
            return;
        }
    }
    if (route_count < CACHE_MAX) {
        route_cache[route_count++] = *entry;
    } else {
        /* Replace the oldest entry instead of discarding the whole cache. */
        route_cache[route_next] = *entry;
        route_next = (route_next + 1) % CACHE_MAX;
    }
}

static void apply_route(const route_entry_t *entry, card_msg_t *card)
{
    snprintf(card->airline, sizeof(card->airline), "%s", entry->airline);
    snprintf(card->origin, sizeof(card->origin), "%s", entry->origin);
    snprintf(card->dest, sizeof(card->dest), "%s", entry->dest);
    card->has_route = entry->has_route;
}

static void enrich_meta(const flight_t *flight, card_msg_t *card)
{
    snprintf(card->type_name, sizeof(card->type_name), "%s",
             fc_type_name(flight->type));

    const fc_aircraft_type_t *local = fc_type_lookup(flight->type);
    if (local != NULL && flight->reg[0]) return;

    const meta_entry_t *cached = meta_cache_lookup(flight->hex);
    if (cached != NULL) {
        if (cached->type[0]) snprintf(card->flight.type, sizeof(card->flight.type), "%s", cached->type);
        if (cached->reg[0]) snprintf(card->flight.reg, sizeof(card->flight.reg), "%s", cached->reg);
        if (local == NULL && cached->manufacturer[0]) {
            snprintf(card->type_name, sizeof(card->type_name), "%s %s",
                     cached->manufacturer, cached->model);
        }
        return;
    }

    if (flight->hex[0] == '\0') return;
    char path[64];
    snprintf(path, sizeof(path), "/v0/aircraft/%s", flight->hex);
    const char *body = NULL;
    size_t len = 0;
    if (!fetch_json("api.adsbdb.com", path, &body, &len, 5000)) return;

    meta_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.hex, sizeof(entry.hex), "%s", flight->hex);
    static const char *const p_type[] = {"response", "aircraft", "icao_type", NULL};
    static const char *const p_reg[] = {"response", "aircraft", "registration", NULL};
    static const char *const p_mfr[] = {"response", "aircraft", "manufacturer", NULL};
    static const char *const p_model[] = {"response", "aircraft", "type", NULL};
    fc_json_get_object_string(body, len, p_type, entry.type, sizeof(entry.type));
    fc_json_get_object_string(body, len, p_reg, entry.reg, sizeof(entry.reg));
    fc_json_get_object_string(body, len, p_mfr, entry.manufacturer, sizeof(entry.manufacturer));
    fc_json_get_object_string(body, len, p_model, entry.model, sizeof(entry.model));
    meta_cache_put(&entry);

    if (entry.type[0]) {
        snprintf(card->flight.type, sizeof(card->flight.type), "%s", entry.type);
    }
    if (entry.reg[0]) {
        snprintf(card->flight.reg, sizeof(card->flight.reg), "%s", entry.reg);
    }
    if (local == NULL && entry.manufacturer[0]) {
        snprintf(card->type_name, sizeof(card->type_name), "%s %s",
                 entry.manufacturer, entry.model);
    } else {
        snprintf(card->type_name, sizeof(card->type_name), "%s",
                 fc_type_name(card->flight.type));
    }
}

static void enrich_route(const flight_t *flight, card_msg_t *card)
{
    snprintf(card->airline, sizeof(card->airline), "%s", "");
    snprintf(card->origin, sizeof(card->origin), "?");
    snprintf(card->dest, sizeof(card->dest), "?");
    card->has_route = false;

    if (flight->callsign[0] == '\0') return;

    char callsign[12];
    snprintf(callsign, sizeof(callsign), "%s", flight->callsign);
    for (char *p = callsign; *p; p++) *p = (char)toupper((unsigned char)*p);

    const route_entry_t *cached = route_cache_lookup(callsign);
    if (cached != NULL &&
        route_field_known(cached->origin) && route_field_known(cached->dest)) {
        apply_route(cached, card);
        return;
    }

    char path[64];
    snprintf(path, sizeof(path), "/v0/callsign/%s", callsign);
    const char *body = NULL;
    size_t len = 0;
    if (!fetch_json("api.adsbdb.com", path, &body, &len, 5000)) {
        if (cached != NULL) apply_route(cached, card);
        return;
    }

    route_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    /* Keep the feed's ICAO callsign as the cache key; the response's
     * callsign_iata is a different identifier and would never match. */
    snprintf(entry.callsign, sizeof(entry.callsign), "%s", callsign);
    static const char *const p_airline[] = {"response", "flightroute", "airline", "name", NULL};
    static const char *const p_origin[] = {"response", "flightroute", "origin", "iata_code", NULL};
    static const char *const p_dest[] = {"response", "flightroute", "destination", "iata_code", NULL};
    fc_json_get_object_string(body, len, p_airline, entry.airline, sizeof(entry.airline));
    fc_json_get_object_string(body, len, p_origin, entry.origin, sizeof(entry.origin));
    fc_json_get_object_string(body, len, p_dest, entry.dest, sizeof(entry.dest));

    /* A partial response must not erase route details we already know. */
    if (cached != NULL) {
        if (entry.airline[0] == '\0') {
            snprintf(entry.airline, sizeof(entry.airline), "%s", cached->airline);
        }
        if (!route_field_known(entry.origin)) {
            snprintf(entry.origin, sizeof(entry.origin), "%s", cached->origin);
        }
        if (!route_field_known(entry.dest)) {
            snprintf(entry.dest, sizeof(entry.dest), "%s", cached->dest);
        }
    }

    if (!route_field_known(entry.origin)) snprintf(entry.origin, sizeof(entry.origin), "?");
    if (!route_field_known(entry.dest)) snprintf(entry.dest, sizeof(entry.dest), "?");
    entry.has_route = route_field_known(entry.origin) || route_field_known(entry.dest);
    if (entry.has_route) route_cache_put(&entry);

    apply_route(&entry, card);
}

static bool poll_feed(void)
{
    g_net_phase = 3;
    const config_t *cfg = config_store_get();
    const char *body = NULL;
    size_t len = 0;
    if (!fetch_json("opendata.adsb.fi", feed_path, &body, &len, 12000)) {
        g_net_phase = 0;
        return false;
    }

    raw_aircraft_t aircraft[FC_MAX_AIRCRAFT];
    int count = fc_parse_feed(body, len, aircraft, FC_MAX_AIRCRAFT);
    if (count == 0) {
        LOG_I("feed: no aircraft in %d nm", feed_nm);
        publish_clear();
        g_net_phase = 0;
        return true;
    }

    flight_t leader;
    if (!fc_rank_best(aircraft, count, cfg, &leader)) {
        LOG_I("feed: %d aircraft, none in channel", count);
        publish_clear();
        g_net_phase = 0;
        return true;
    }

    card_msg_t card;
    memset(&card, 0, sizeof(card));
    card.valid = true;
    card.flight = leader;
    snprintf(card.type_name, sizeof(card.type_name), "%s",
             fc_type_name(leader.type));
    snprintf(card.origin, sizeof(card.origin), "?");
    snprintf(card.dest, sizeof(card.dest), "?");
    card.sequence = ++sequence;
    publish(&card);
    LOG_I("leader: %s %s %.1f km %.0f kt %.0f ft",
          leader.callsign[0] ? leader.callsign : leader.hex,
          leader.type, (double)leader.dist_km, (double)leader.gs_kt,
          (double)leader.alt_ft);

    enrich_meta(&leader, &card);
    enrich_route(&leader, &card);
    card.enriched = true;
    card.sequence = ++sequence;
    publish(&card);
    g_net_phase = 0;
    return true;
}

static void build_feed_path(void)
{
    const config_t *cfg = config_store_get();
    feed_nm = (int)ceilf(cfg->radius_km / 1.852f) + 1;
    if (feed_nm < 1) feed_nm = 1;
    snprintf(feed_path, sizeof(feed_path),
             "/api/v3/lat/%.5f/lon/%.5f/dist/%d",
             (double)cfg->home_lat, (double)cfg->home_lon, feed_nm);
    LOG_I("feed: adsb.fi %d nm, poll %u s", feed_nm, cfg->poll_seconds);
}

void net_task(void *arg)
{
    (void)arg;
    LOG_I("net task started on core %u", (unsigned)portGET_CORE_ID());

    next_ntp_ms = 0;
    next_poll_ms = 0;
    build_feed_path();
    int failures = 0;

    for (;;) {
        g_net_heartbeat++;
        uint32_t now = (uint32_t)fc_monotonic_ms();

        if (!net_wifi_connect()) {
            failures++;
            if (failures % 4 == 0) net_wifi_reset();
            uint32_t delay = 5u << (failures > 4 ? 4 : (failures - 1));
            if (delay > 60) delay = 60;
            LOG_W("net: wifi unavailable, retry in %lu s", (unsigned long)delay);
            vTaskDelay(pdMS_TO_TICKS(delay * 1000u));
            continue;
        }

        if (!clock_is_valid() || (int32_t)(now - next_ntp_ms) >= 0) {
            if (net_ntp_sync(3000)) {
                next_ntp_ms = now + NTP_RESYNC_MS;
            } else {
                next_ntp_ms = now + NTP_RETRY_MS;
                failures++;
                if (failures % 4 == 0) net_wifi_reset();
            }
        }

        if (clock_is_valid() && (int32_t)(now - next_poll_ms) >= 0) {
            const config_t *cfg = config_store_get();
            if (poll_feed()) {
                failures = 0;
                next_poll_ms = (uint32_t)fc_monotonic_ms()
                               + (uint32_t)cfg->poll_seconds * 1000u;
            } else {
                failures++;
                uint32_t delay = 5u << (failures > 4 ? 4 : (failures - 1));
                if (delay > 60) delay = 60;
                LOG_W("net: poll failed (%d in a row), backoff %lu s",
                      failures, (unsigned long)delay);
                next_poll_ms = (uint32_t)fc_monotonic_ms() + delay * 1000u;
                if (failures % 4 == 0) {
                    net_wifi_reset();
                    build_feed_path();
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
