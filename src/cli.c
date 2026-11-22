/*
 * Flightcard - CLI parser and USB CDC polling.
 *
 * SPDX-License-Identifier: MIT
 */
#include "cli.h"
#include "config_store.h"
#include "log.h"
#include "timezone.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef FC_HOST_TEST
#include "clock.h"
#include "hardware/watchdog.h"
#include "net.h"
#include "pico/async_context_freertos.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "task.h"
#endif

static void append(char *out, size_t cap, const char *fmt, ...)
{
    if (out == NULL || cap == 0) return;
    size_t used = strlen(out);
    if (used + 1 >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(out + used, cap - used, fmt, ap);
    va_end(ap);
}

static bool parse_float(const char *s, float *out)
{
    if (s == NULL || *s == '\0') return false;
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s || *end != '\0' || !isfinite(v)) return false;
    *out = (float)v;
    return true;
}

static bool parse_uint(const char *s, unsigned long *out)
{
    if (s == NULL || *s == '\0') return false;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (end == s || *end != '\0') return false;
    *out = v;
    return true;
}

static const char *palette_name(int p)
{
    switch (p) {
    case FC_PALETTE_CABIN: return "cabin";
    case FC_PALETTE_AMBER: return "amber";
    default: return "auto";
    }
}

static void show_config(const config_t *c, char *out, size_t cap)
{
    append(out, cap, "ssid: %s\n", c->wifi_ssid[0] ? c->wifi_ssid : "(unset)");
    append(out, cap, "password: %s\n", c->wifi_password[0] ? "********" : "(unset)");
    append(out, cap, "home: %.6f, %.6f\n", (double)c->home_lat, (double)c->home_lon);
    append(out, cap, "radius: %.2f km\n", (double)c->radius_km);
    append(out, cap, "ceiling: %.0f ft\n", (double)c->max_alt_ft);
    append(out, cap, "poll: %u s\n", (unsigned)c->poll_seconds);
    append(out, cap, "timezone: %s\n", c->timezone);
    append(out, cap, "palette: %s\n", palette_name(c->palette));
    append(out, cap, "brightness: %.2f\n", (double)c->fixed_brightness);
    append(out, cap, "auto_brightness: %u\n", (unsigned)c->auto_brightness);
    append(out, cap, "night_lux: %.1f\n", (double)c->night_lux);
    append(out, cap, "demo: %u\n", (unsigned)c->demo_mode);
    append(out, cap, "wifi_power_save: %u\n", (unsigned)c->wifi_power_save);
    append(out, cap, "seed_epoch: %lu\n", (unsigned long)c->seed_epoch);
}

static void show_help(char *out, size_t cap)
{
    append(out, cap,
           "Flightcard commands:\n"
           "  help\n"
           "  show                   (also net/clock state)\n"
           "  tasks                  (task states / async lock holder)\n"
           "  set wifi <ssid> <password>\n"
           "  set ssid <ssid with spaces>\n"
           "  set password <password with spaces>\n"
           "  set home <lat> <lon>\n"
           "  set radius <km>\n"
           "  set ceiling <ft>\n"
           "  set poll <seconds>\n"
           "  set tz <Europe/London|UTC|UTC+01:00|+5.5>\n"
           "  set palette <auto|cabin|amber>\n"
           "  set night <lux>\n"
           "  set brightness <0..1>\n"
           "  set auto_brightness <0|1>\n"
           "  set demo <0|1>\n"
           "  set wifi_power_save <0|1>\n"
           "  save\n"
           "  reboot\n");
}

cli_action_t cli_execute(config_t *cfg, const char *line,
                         char *out, size_t cap)
{
    if (out != NULL && cap > 0) out[0] = '\0';
    if (cfg == NULL || line == NULL) return CLI_ACTION_NONE;

    char copy[192] = {0};
    size_t n = 0;
    while (*line && n + 1 < sizeof(copy)) {
        char ch = *line++;
        if (ch == '\r' || ch == '\n') break;
        copy[n++] = ch;
    }
    copy[n] = '\0';

    char *cmd = strtok(copy, " \t");
    if (cmd == NULL) return CLI_ACTION_NONE;

    if (strcasecmp(cmd, "help") == 0) {
        show_help(out, cap);
        return CLI_ACTION_NONE;
    }
#ifndef FC_HOST_TEST
    if (strcasecmp(cmd, "tasks") == 0) {
        TaskStatus_t status[16];
        UBaseType_t count = uxTaskGetSystemState(status, 16, NULL);
        async_context_freertos_t *ac =
            (async_context_freertos_t *)cyw43_arch_async_context();
        TaskHandle_t holder = NULL;
        if (ac != NULL && ac->lock_mutex != NULL) {
            holder = (TaskHandle_t)xSemaphoreGetMutexHolder(ac->lock_mutex);
        }
        append(out, cap, "tasks: %u, async lock holder: %s\n",
               (unsigned)count, pcTaskGetName(holder));
        for (UBaseType_t i = 0; i < count; i++) {
#if FC_SMP
            append(out, cap, "  %-20s state=%u prio=%u aff=0x%x hwm=%u\n",
                   status[i].pcTaskName, (unsigned)status[i].eCurrentState,
                   (unsigned)status[i].uxCurrentPriority,
                   (unsigned)status[i].uxCoreAffinityMask,
                   (unsigned)status[i].usStackHighWaterMark);
#else
            append(out, cap, "  %-20s state=%u prio=%u hwm=%u\n",
                   status[i].pcTaskName, (unsigned)status[i].eCurrentState,
                   (unsigned)status[i].uxCurrentPriority,
                   (unsigned)status[i].usStackHighWaterMark);
#endif
        }
        return CLI_ACTION_NONE;
    }
#endif
    if (strcasecmp(cmd, "show") == 0) {
        show_config(cfg, out, cap);
#ifndef FC_HOST_TEST
        append(out, cap, "wifi_state: %d\nnet_phase: %d\nnet_heartbeat: %lu\n"
               "clock_valid: %d\nclock_utc: %lu\n",
               g_wifi_state, g_net_phase, (unsigned long)g_net_heartbeat,
               clock_is_valid() ? 1 : 0, (unsigned long)clock_now_utc());
#endif
        return CLI_ACTION_NONE;
    }
    if (strcasecmp(cmd, "save") == 0) {
        append(out, cap, "saving configuration...\n");
        return CLI_ACTION_SAVE;
    }
    if (strcasecmp(cmd, "reboot") == 0) {
        append(out, cap, "rebooting...\n");
        return CLI_ACTION_REBOOT;
    }
    if (strcasecmp(cmd, "set") != 0) {
        append(out, cap, "error: unknown command '%s' (try help)\n", cmd);
        return CLI_ACTION_NONE;
    }

    char *key = strtok(NULL, " \t");
    if (key == NULL) {
        append(out, cap, "error: set needs a key\n");
        return CLI_ACTION_NONE;
    }

    if (strcasecmp(key, "wifi") == 0) {
        char *ssid = strtok(NULL, " \t");
        char *password = strtok(NULL, " \t");
        if (ssid == NULL) {
            append(out, cap, "error: set wifi <ssid> <password>\n");
            return CLI_ACTION_NONE;
        }
        snprintf(cfg->wifi_ssid, sizeof(cfg->wifi_ssid), "%s", ssid);
        snprintf(cfg->wifi_password, sizeof(cfg->wifi_password), "%s",
                 password ? password : "");
        append(out, cap, "wifi set (password hidden)\n");
        return CLI_ACTION_NONE;
    }

    char *rest = key + strlen(key) + 1;
    while (*rest == ' ' || *rest == '\t') rest++;

    if (strcasecmp(key, "ssid") == 0) {
        size_t n = strlen(rest);
        while (n > 0 && (rest[n - 1] == ' ' || rest[n - 1] == '\t')) n--;
        if (n >= sizeof(cfg->wifi_ssid)) {
            append(out, cap, "error: SSID too long\n");
            return CLI_ACTION_NONE;
        }
        memcpy(cfg->wifi_ssid, rest, n);
        cfg->wifi_ssid[n] = '\0';
        append(out, cap, "ssid set\n");
        return CLI_ACTION_NONE;
    }
    if (strcasecmp(key, "password") == 0) {
        size_t n = strlen(rest);
        while (n > 0 && (rest[n - 1] == ' ' || rest[n - 1] == '\t')) n--;
        if (n >= sizeof(cfg->wifi_password)) {
            append(out, cap, "error: password too long\n");
            return CLI_ACTION_NONE;
        }
        memcpy(cfg->wifi_password, rest, n);
        cfg->wifi_password[n] = '\0';
        append(out, cap, "password set (hidden)\n");
        return CLI_ACTION_NONE;
    }

    char *a = strtok(NULL, " \t");
    if (a == NULL) {
        append(out, cap, "error: set %s needs a value\n", key);
        return CLI_ACTION_NONE;
    }

    if (strcasecmp(key, "home") == 0) {
        char *b = strtok(NULL, " \t");
        float lat, lon;
        if (b == NULL || !parse_float(a, &lat) || !parse_float(b, &lon)
            || lat < -90.0f || lat > 90.0f || lon < -180.0f || lon > 180.0f) {
            append(out, cap, "error: set home <lat> <lon>\n");
            return CLI_ACTION_NONE;
        }
        cfg->home_lat = lat;
        cfg->home_lon = lon;
        append(out, cap, "home set\n");
        return CLI_ACTION_NONE;
    }

    if (strcasecmp(key, "radius") == 0) {
        float v;
        if (!parse_float(a, &v) || v <= 0.0f || v > 500.0f) {
            append(out, cap, "error: radius must be > 0 and <= 500 km\n");
            return CLI_ACTION_NONE;
        }
        cfg->radius_km = v;
        append(out, cap, "radius set\n");
        return CLI_ACTION_NONE;
    }
    if (strcasecmp(key, "ceiling") == 0) {
        float v;
        if (!parse_float(a, &v) || v < 0.0f || v > 60000.0f) {
            append(out, cap, "error: ceiling must be 0..60000 ft\n");
            return CLI_ACTION_NONE;
        }
        cfg->max_alt_ft = v;
        append(out, cap, "ceiling set\n");
        return CLI_ACTION_NONE;
    }
    if (strcasecmp(key, "poll") == 0) {
        unsigned long v;
        if (!parse_uint(a, &v) || v < 1 || v > 3600) {
            append(out, cap, "error: poll must be 1..3600 seconds\n");
            return CLI_ACTION_NONE;
        }
        cfg->poll_seconds = (uint16_t)v;
        append(out, cap, "poll set\n");
        return CLI_ACTION_NONE;
    }
    if (strcasecmp(key, "tz") == 0) {
        int offset;
        if (strlen(a) >= sizeof(cfg->timezone)) {
            append(out, cap, "error: timezone too long\n");
            return CLI_ACTION_NONE;
        }
        if (strcasecmp(a, "Europe/London") != 0
            && strcasecmp(a, "Europe/Dublin") != 0
            && strcasecmp(a, "Europe/Lisbon") != 0
            && strcasecmp(a, "UTC") != 0
            && strcasecmp(a, "GMT") != 0
            && !fc_parse_fixed_offset(a, &offset)) {
            append(out, cap, "error: unrecognised timezone\n");
            return CLI_ACTION_NONE;
        }
        snprintf(cfg->timezone, sizeof(cfg->timezone), "%s", a);
        append(out, cap, "timezone set\n");
        return CLI_ACTION_NONE;
    }
    if (strcasecmp(key, "palette") == 0) {
        if (strcasecmp(a, "auto") == 0) cfg->palette = FC_PALETTE_AUTO;
        else if (strcasecmp(a, "cabin") == 0) cfg->palette = FC_PALETTE_CABIN;
        else if (strcasecmp(a, "amber") == 0) cfg->palette = FC_PALETTE_AMBER;
        else {
            append(out, cap, "error: palette must be auto, cabin or amber\n");
            return CLI_ACTION_NONE;
        }
        append(out, cap, "palette set\n");
        return CLI_ACTION_NONE;
    }
    if (strcasecmp(key, "night") == 0) {
        float v;
        if (!parse_float(a, &v) || v < 0.0f || v > 100000.0f) {
            append(out, cap, "error: night must be 0..100000 lux\n");
            return CLI_ACTION_NONE;
        }
        cfg->night_lux = v;
        append(out, cap, "night threshold set\n");
        return CLI_ACTION_NONE;
    }
    if (strcasecmp(key, "brightness") == 0) {
        float v;
        if (!parse_float(a, &v) || v < 0.0f || v > 1.0f) {
            append(out, cap, "error: brightness must be 0..1\n");
            return CLI_ACTION_NONE;
        }
        cfg->fixed_brightness = v;
        append(out, cap, "brightness set\n");
        return CLI_ACTION_NONE;
    }
    if (strcasecmp(key, "auto_brightness") == 0
        || strcasecmp(key, "demo") == 0
        || strcasecmp(key, "wifi_power_save") == 0) {
        unsigned long v;
        if (!parse_uint(a, &v) || v > 1) {
            append(out, cap, "error: set %s <0|1>\n", key);
            return CLI_ACTION_NONE;
        }
        if (strcasecmp(key, "auto_brightness") == 0) cfg->auto_brightness = (uint8_t)v;
        else if (strcasecmp(key, "demo") == 0) cfg->demo_mode = (uint8_t)v;
        else cfg->wifi_power_save = (uint8_t)v;
        append(out, cap, "%s set\n", key);
        return CLI_ACTION_NONE;
    }

    append(out, cap, "error: unknown key '%s' (try help)\n", key);
    return CLI_ACTION_NONE;
}

#ifndef FC_HOST_TEST

static char line_buf[192];
static size_t line_len;

void cli_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

void cli_poll(void)
{
    for (int i = 0; i < 32; i++) {
        int ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT) break;
        if (ch == '\r' || ch == '\n') {
            if (line_len == 0) continue;
            line_buf[line_len] = '\0';
            line_len = 0;
            char out[1200];
            cli_action_t action = cli_execute(config_store_mutable(), line_buf,
                                              out, sizeof(out));
            if (out[0]) cli_printf("%s", out);
            if (action == CLI_ACTION_SAVE) {
                bool ok = config_store_save();
                cli_printf(ok ? "saved\n" : "save failed\n");
            } else if (action == CLI_ACTION_REBOOT) {
                cli_printf("rebooting\n");
                sleep_ms(100);
                watchdog_reboot(0, 0, 10);
                while (true) tight_loop_contents();
            }
        } else if (ch == 0x7F || ch == 0x08) {
            if (line_len) line_len--;
        } else if (ch >= 32 && ch < 127 && line_len + 1 < sizeof(line_buf)) {
            line_buf[line_len++] = (char)ch;
        }
    }
}

#else /* FC_HOST_TEST */

void cli_printf(const char *fmt, ...) { (void)fmt; }
void cli_poll(void) {}
#endif
