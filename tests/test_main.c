/*
 * Flightcard - host unit tests for the portable core.
 *
 * SPDX-License-Identifier: MIT
 */
#include "cli.h"
#include "clock.h"
#include "config_store.h"
#include "demo.h"
#include "dns_proto.h"
#include "format.h"
#include "http.h"
#include "json.h"
#include "light.h"
#include "ntp.h"
#include "ranking.h"
#include "sprite.h"
#include "timezone.h"
#include "types.h"
#include "fc_config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
static int checks;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_STR(a, b) do { \
    checks++; \
    const char *a_ = (a); const char *b_ = (b); \
    if (strcmp(a_, b_) != 0) { \
        failures++; \
        printf("FAIL %s:%d: '%s' != '%s'\n", __FILE__, __LINE__, a_, b_); \
    } \
} while (0)

static void test_timezone(void)
{
    CHECK(fc_last_sunday(2026, 3) == 29);
    CHECK(fc_last_sunday(2026, 10) == 25);
    CHECK(fc_last_sunday(2024, 3) == 31);

    struct tm t;
    fc_localtime(1795211220u, "Europe/London", &t);
    CHECK(t.tm_year + 1900 == 2026);
    CHECK(t.tm_mon + 1 == 11);
    CHECK(t.tm_mday == 20);
    CHECK(t.tm_hour == 21);
    CHECK(t.tm_min == 47);
    CHECK(t.tm_wday == 5); /* Friday */
    CHECK_STR(fc_weekday_name(t.tm_wday), "FRIDAY");
    CHECK_STR(fc_month_name(t.tm_mon + 1), "NOVEMBER");

    CHECK(fc_tz_offset_at(1795211220u, "Europe/London") == 0);
    CHECK(fc_tz_offset_at(1782864000u, "Europe/London") == 3600); /* 2026-07-01 */
    CHECK(fc_tz_offset_at(1795211220u, "UTC") == 0);

    int off = 0;
    CHECK(fc_parse_fixed_offset("UTC+01:00", &off) && off == 3600);
    CHECK(fc_parse_fixed_offset("UTC-5", &off) && off == -18000);
    CHECK(fc_parse_fixed_offset("+5.5", &off) && off == 19800);
    CHECK(fc_parse_fixed_offset("5", &off) && off == 18000);
    CHECK(fc_parse_fixed_offset("GMT", &off) && off == 0);

    CHECK(fc_epoch_from_utc(&t) == 1795211220u);
}

static void make_ac(raw_aircraft_t *ac, const char *type, float lat, float lon,
                    float alt, float gs)
{
    memset(ac, 0, sizeof(*ac));
    snprintf(ac->hex, sizeof(ac->hex), "abc123");
    snprintf(ac->callsign, sizeof(ac->callsign), "BAW123");
    snprintf(ac->type, sizeof(ac->type), "%s", type);
    snprintf(ac->category, sizeof(ac->category), "A3");
    snprintf(ac->reg, sizeof(ac->reg), "G-TEST");
    ac->has_lat = ac->has_lon = ac->has_alt = ac->has_gs = ac->has_seen = true;
    ac->lat = lat;
    ac->lon = lon;
    ac->alt_ft = alt;
    ac->gs_kt = gs;
    ac->seen_pos = 2.0f;
}

static void test_ranking(void)
{
    config_t cfg;
    config_defaults(&cfg);
    /* Ranking fixtures use a fixed, non-personal reference point. */
    cfg.home_lat = 51.55f;
    cfg.home_lon = -0.10f;
    raw_aircraft_t ac;
    flight_t out;

    make_ac(&ac, "A388", 51.50f, -0.10f, 3550.0f, 148.0f);
    CHECK(fc_rank_one(&ac, &cfg, &out));
    CHECK(out.dist_km > 3.0f && out.dist_km < 6.0f);
    CHECK(out.size_deg > 0.0f);

    ac.alt_baro_ground = true;
    CHECK(!fc_rank_one(&ac, &cfg, &out));
    ac.alt_baro_ground = false;

    ac.seen_pos = 11.0f;
    CHECK(!fc_rank_one(&ac, &cfg, &out));
    ac.seen_pos = 2.0f;

    snprintf(ac.category, sizeof(ac.category), "C1");
    CHECK(!fc_rank_one(&ac, &cfg, &out));
    snprintf(ac.category, sizeof(ac.category), "A7");
    CHECK(!fc_rank_one(&ac, &cfg, &out));
    snprintf(ac.category, sizeof(ac.category), "A3");

    snprintf(ac.type, sizeof(ac.type), "EC35");
    CHECK(!fc_rank_one(&ac, &cfg, &out));
    snprintf(ac.type, sizeof(ac.type), "A388");

    ac.alt_ft = 7000.0f;
    CHECK(!fc_rank_one(&ac, &cfg, &out));
    ac.alt_ft = 3550.0f;

    ac.gs_kt = 39.0f;
    CHECK(!fc_rank_one(&ac, &cfg, &out));
    ac.gs_kt = 148.0f;

    ac.lat = 52.2f;
    CHECK(!fc_rank_one(&ac, &cfg, &out));
    ac.lat = 51.50f;

    raw_aircraft_t two[2];
    make_ac(&two[0], "C172", 51.50f, -0.10f, 2300.0f, 96.0f);
    make_ac(&two[1], "A388", 51.50f, -0.10f, 3550.0f, 148.0f);
    CHECK(fc_rank_best(two, 2, &cfg, &out));
    CHECK_STR(out.type, "A388");

    CHECK(fc_is_helicopter("EC35", "A3"));
    CHECK(fc_is_helicopter("A320", "A7"));
    CHECK(!fc_is_helicopter("A320", "A3"));
}

static void test_format(void)
{
    char buf[64];
    fc_ellipsize(buf, sizeof(buf), "ABCDEFGH", 5);
    CHECK_STR(buf, "ABCD" FC_ELLIPSIS);
    fc_ellipsize(buf, sizeof(buf), "ABC", 5);
    CHECK_STR(buf, "ABC");

    char l1[64], l2[64];
    fc_wrap_type("Airbus A380-800", 18, l1, l2);
    CHECK_STR(l1, "Airbus A380-800");
    CHECK_STR(l2, "");
    fc_wrap_type("De Havilland Dash 8 Q400", 18, l1, l2);
    CHECK_STR(l1, "De Havilland");
    CHECK_STR(l2, "Dash 8 Q400");
    fc_wrap_type("Bombardier CRJ-1000", 18, l1, l2);
    CHECK_STR(l1, "Bombardier");
    CHECK_STR(l2, "CRJ-1000");

    fc_format_speed(buf, sizeof(buf), 148.4f);
    CHECK_STR(buf, "148 kt");
    fc_format_altitude(buf, sizeof(buf), 3550.0f);
    CHECK_STR(buf, "3,550 ft");
    fc_format_altitude(buf, sizeof(buf), 3544.0f);
    CHECK_STR(buf, "3,540 ft");
    fc_format_altitude(buf, sizeof(buf), 3555.0f);
    CHECK_STR(buf, "3,560 ft");
    fc_format_distance(buf, sizeof(buf), 8.42f);
    CHECK_STR(buf, "8.4 km");
}

static void test_json(void)
{
    const char *feed =
        "{\"ac\":["
        "{\"hex\":\"abc123\",\"flight\":\" BAW123 \",\"t\":\"a388\","
        "\"r\":\"g-test\",\"category\":\"A3\",\"alt_geom\":3550,"
        "\"gs\":148,\"lat\":51.5,\"lon\":-0.1,\"seen_pos\":2},"
        "{\"hex\":\"def456\",\"flight\":\"EZY1\",\"t\":\"A320\","
        "\"alt_baro\":\"ground\",\"gs\":120,\"lat\":51.4,\"lon\":-0.2},"
        "{\"hex\":\"bad\",\"flight\":\"BAD\",\"lat\":\"nope\"},"
        "{\"hex\":\"ghi789\",\"flight\":\"LOG1\",\"t\":\"dh8d\","
        "\"alt_baro\":5100,\"gs\":186,\"lat\":51.6,\"lon\":-0.2}"
        "]}";
    raw_aircraft_t ac[8];
    int n = fc_parse_feed(feed, strlen(feed), ac, 8);
    CHECK(n == 4);
    CHECK_STR(ac[0].callsign, "BAW123");
    CHECK_STR(ac[0].type, "A388");
    CHECK_STR(ac[0].reg, "G-TEST");
    CHECK(ac[0].has_alt && ac[0].alt_ft == 3550.0f);
    CHECK(ac[1].alt_baro_ground);
    CHECK(ac[3].has_alt && ac[3].alt_ft == 5100.0f);

    const char *adsbdb =
        "{\"response\":{\"aircraft\":{\"icao_type\":\"A320\","
        "\"registration\":\"G-TEST\",\"manufacturer\":\"Airbus\","
        "\"type\":\"A320-214\"}}}";
    const char *path[] = {"response", "aircraft", "registration", NULL};
    char reg[16];
    CHECK(fc_json_get_object_string(adsbdb, strlen(adsbdb), path, reg, sizeof(reg)));
    CHECK_STR(reg, "G-TEST");
    const char *path2[] = {"response", "aircraft", "type", NULL};
    char model[16];
    CHECK(fc_json_get_object_string(adsbdb, strlen(adsbdb), path2, model, sizeof(model)));
    CHECK_STR(model, "A320-214");
}

static void test_http(void)
{
    const char *fixed =
        "HTTP/1.1 201 Created\r\nContent-Length: 5\r\n\r\nhello";
    fc_http_response_t r;
    char decoded[64];
    CHECK(fc_http_parse(fixed, strlen(fixed), &r, decoded, sizeof(decoded))
          == FC_HTTP_OK);
    CHECK(r.status == 201);
    CHECK(r.body_len == 5);
    CHECK(strncmp(r.body, "hello", 5) == 0);

    const char *incomplete =
        "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nhello";
    CHECK(fc_http_parse(incomplete, strlen(incomplete), &r, decoded, sizeof(decoded))
          == FC_HTTP_INCOMPLETE);

    const char *chunked =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n";
    CHECK(fc_http_parse(chunked, strlen(chunked), &r, decoded, sizeof(decoded))
          == FC_HTTP_OK);
    CHECK(r.body_len == 11);
    CHECK(strncmp(decoded, "hello world", 11) == 0);

    char req[256];
    size_t n = fc_http_build_request(req, sizeof(req), "opendata.adsb.fi",
                                     "/api/v3/lat/51.5/lon/-0.1/dist/8");
    CHECK(n > 0);
    CHECK(strstr(req, "Host: opendata.adsb.fi\r\n") != NULL);
    CHECK(strstr(req, "User-Agent: flightcard/1.0\r\n") != NULL);
}

static void test_dns_ntp(void)
{
    uint8_t query[128];
    uint16_t id = 0x4C55;
    size_t qlen = fc_dns_build_query(query, sizeof(query),
                                     "opendata.adsb.fi", id);
    CHECK(qlen > 12);

    uint8_t reply[256];
    memset(reply, 0, sizeof(reply));
    memcpy(reply, query, qlen);
    reply[2] = 0x81;
    reply[3] = 0x80;
    reply[7] = 1; /* ANCOUNT = 1 */
    size_t p = qlen;
    reply[p++] = 0xC0;
    reply[p++] = 0x0C;
    reply[p++] = 0x00;
    reply[p++] = 0x01; /* A */
    reply[p++] = 0x00;
    reply[p++] = 0x01; /* IN */
    reply[p++] = 0;
    reply[p++] = 0;
    reply[p++] = 0;
    reply[p++] = 60;
    reply[p++] = 0;
    reply[p++] = 4;
    reply[p++] = 104;
    reply[p++] = 26;
    reply[p++] = 14;
    reply[p++] = 99;
    char ip[16];
    CHECK(fc_dns_parse_response(reply, p, id, ip));
    CHECK_STR(ip, "104.26.14.99");

    uint8_t ntp[FC_NTP_PACKET_SIZE];
    fc_ntp_build(ntp);
    CHECK(ntp[0] == 0x23);
    uint32_t epoch = 1795211220u + FC_NTP_UNIX_OFFSET;
    ntp[0] = 0x24; /* server response */
    ntp[1] = 2;
    ntp[40] = (uint8_t)(epoch >> 24);
    ntp[41] = (uint8_t)(epoch >> 16);
    ntp[42] = (uint8_t)(epoch >> 8);
    ntp[43] = (uint8_t)epoch;
    uint32_t out = 0;
    CHECK(fc_ntp_parse(ntp, &out));
    CHECK(out == 1795211220u);
}

static void test_config_cli(void)
{
    config_t cfg;
    config_defaults(&cfg);
    CHECK(cfg.magic == FC_CONFIG_MAGIC);
    CHECK(cfg.poll_seconds == 10);
    CHECK(cfg.home_lat == 0.0f && cfg.home_lon == 0.0f);

    uint8_t wire[256];
    size_t n = config_serialize(&cfg, wire, sizeof(wire));
    CHECK(n == 176);
    config_t copy;
    CHECK(config_deserialize(&copy, wire, n));
    CHECK(copy.poll_seconds == cfg.poll_seconds);
    CHECK(fabsf(copy.home_lon - cfg.home_lon) < 0.0001f);

    wire[20] ^= 0x01;
    CHECK(!config_deserialize(&copy, wire, n));

    char out[1200];
    cli_action_t action = cli_execute(&cfg, "set wifi MyNet secretpass", out, sizeof(out));
    CHECK(action == CLI_ACTION_NONE);
    CHECK_STR(cfg.wifi_ssid, "MyNet");
    CHECK_STR(cfg.wifi_password, "secretpass");

    cli_execute(&cfg, "show", out, sizeof(out));
    CHECK(strstr(out, "secretpass") == NULL);
    CHECK(strstr(out, "********") != NULL);
    CHECK(strstr(out, "MyNet") != NULL);

    cli_execute(&cfg, "set home 51.5 -0.12", out, sizeof(out));
    CHECK(fabsf(cfg.home_lat - 51.5f) < 0.001f);
    cli_execute(&cfg, "set radius 15", out, sizeof(out));
    CHECK(fabsf(cfg.radius_km - 15.0f) < 0.001f);
    cli_execute(&cfg, "set palette amber", out, sizeof(out));
    CHECK(cfg.palette == FC_PALETTE_AMBER);
    cli_execute(&cfg, "set tz UTC+01:00", out, sizeof(out));
    CHECK_STR(cfg.timezone, "UTC+01:00");
    CHECK(cli_execute(&cfg, "save", out, sizeof(out)) == CLI_ACTION_SAVE);
    CHECK(cli_execute(&cfg, "reboot", out, sizeof(out)) == CLI_ACTION_REBOOT);
}

static void test_light_demo_sprite(void)
{
    CHECK(fabsf(fc_lux_to_brightness(0.0f) - 0.05f) < 0.001f);
    CHECK(fabsf(fc_lux_to_brightness(100.0f) - 0.30f) < 0.01f);
    CHECK(fabsf(fc_lux_to_brightness(1000.0f) - 0.45f) < 0.01f);
    CHECK(fabsf(fc_lux_to_brightness(100000.0f) - 0.45f) < 0.001f);

    int mode = FC_PALETTE_CABIN;
    CHECK(fc_palette_step(&mode, 10.0f, 20.0f));
    CHECK(mode == FC_PALETTE_AMBER);
    CHECK(!fc_palette_step(&mode, 25.0f, 20.0f));
    CHECK(fc_palette_step(&mode, 33.0f, 20.0f));
    CHECK(mode == FC_PALETTE_CABIN);

    const card_msg_t *card = fc_demo_card(0);
    CHECK(card->valid);
    CHECK_STR(card->flight.type, "A388");
    CHECK_STR(card->airline, "EMIRATES");
    CHECK(card->has_route);

    CHECK_STR(fc_sprite_family("DH8D", "A3"), "_prop");
    CHECK_STR(fc_sprite_family("EC35", "A3"), "_heli");
    CHECK_STR(fc_sprite_family("A388", "A3"), "_jumbo4");
    CHECK_STR(fc_sprite_family("C172", "A1"), "_light");
    CHECK(fc_sprite_find("A388", "A3") != NULL);
    CHECK(fc_sprite_find("ZZZZ", "A3") != NULL);
    CHECK_STR(fc_sprite_find("A388", "A3")->name, "A388");
    CHECK_STR(fc_sprite_find("EC35", "A7")->name, "EC35");

    CHECK_STR(fc_type_name("A388"), "Airbus A380-800");
    CHECK(fc_type_span("A388") > 79.0f && fc_type_span("A388") < 81.0f);
    CHECK(fc_type_lookup("NOPE") == NULL);
}

static void test_clock(void)
{
    clock_init();
    clock_set_utc(1795211220u);
    CHECK(clock_is_valid());
    CHECK(clock_now_utc() >= 1795211220u);
    struct tm t = clock_now_local();
    CHECK(t.tm_hour == 21);
    clock_set_timezone("UTC");
    t = clock_now_local();
    CHECK(t.tm_hour == 21);
    clock_set_timezone("UTC+01:00");
    t = clock_now_local();
    CHECK(t.tm_hour == 22);
    clock_set_timezone("Europe/London");
}

int main(void)
{
    test_timezone();
    test_ranking();
    test_format();
    test_json();
    test_http();
    test_dns_ntp();
    test_config_cli();
    test_light_demo_sprite();
    test_clock();

    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
