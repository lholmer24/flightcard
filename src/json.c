/*
 * Flightcard - adsb.fi/adsbdb JSON parsing with jsmn.
 *
 * SPDX-License-Identifier: MIT
 */
#include "json.h"
#include "jsmn.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define TOKEN_CAPACITY 1024

static jsmntok_t tokens[TOKEN_CAPACITY];

static int tok_skip(int count, int i)
{
    if (i < 0 || i >= count) return count;
    if (tokens[i].type == JSMN_OBJECT || tokens[i].type == JSMN_ARRAY) {
        int end = tokens[i].end;
        int j = i + 1;
        while (j < count && tokens[j].start < end) {
            j = tok_skip(count, j);
        }
        return j;
    }
    return i + 1;
}

static bool tok_is_string(const jsmntok_t *t)
{
    return t->type == JSMN_STRING;
}

static bool tok_is_primitive(const jsmntok_t *t)
{
    return t->type == JSMN_PRIMITIVE;
}

static int token_copy(const char *json, const jsmntok_t *tok,
                      char *dst, size_t cap)
{
    if (dst == NULL || cap == 0) return 0;
    size_t out = 0;
    int i = tok->start;
    int end = tok->end;
    while (i < end && out + 1 < cap) {
        char c = json[i++];
        if (c != '\\') {
            dst[out++] = c;
            continue;
        }
        if (i >= end) break;
        char esc = json[i++];
        switch (esc) {
        case '"': dst[out++] = '"'; break;
        case '\\': dst[out++] = '\\'; break;
        case '/': dst[out++] = '/'; break;
        case 'b': dst[out++] = '\b'; break;
        case 'f': dst[out++] = '\f'; break;
        case 'n': dst[out++] = '\n'; break;
        case 'r': dst[out++] = '\r'; break;
        case 't': dst[out++] = '\t'; break;
        case 'u': {
            if (i + 4 > end) { i = end; break; }
            unsigned cp = 0;
            for (int k = 0; k < 4; k++) {
                char h = json[i++];
                cp <<= 4;
                if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
            }
            if (cp <= 0x7F) {
                dst[out++] = (char)cp;
            } else if (cp <= 0x7FF) {
                if (out + 2 >= cap) { i = end; break; }
                dst[out++] = (char)(0xC0 | (cp >> 6));
                dst[out++] = (char)(0x80 | (cp & 0x3F));
            } else {
                if (out + 3 >= cap) { i = end; break; }
                dst[out++] = (char)(0xE0 | (cp >> 12));
                dst[out++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                dst[out++] = (char)(0x80 | (cp & 0x3F));
            }
            break;
        }
        default: dst[out++] = esc; break;
        }
    }
    dst[out] = '\0';
    return (int)out;
}

static bool token_to_double(const char *json, const jsmntok_t *tok,
                            double *out)
{
    char buf[48];
    if (!tok_is_primitive(tok) && !tok_is_string(tok)) return false;
    if (token_copy(json, tok, buf, sizeof(buf)) == 0) return false;
    char *p = buf;
    while (*p && isspace((unsigned char)*p)) p++;
    char *end = NULL;
    double v = strtod(p, &end);
    if (end == p) return false;
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end != '\0') return false;
    *out = v;
    return true;
}

static bool key_equals(const char *json, const jsmntok_t *tok,
                       const char *name)
{
    if (!tok_is_string(tok)) return false;
    int n = tok->end - tok->start;
    if (n != (int)strlen(name)) return false;
    return strncmp(json + tok->start, name, (size_t)n) == 0;
}

static int find_key(const char *json, int count, int object, const char *name)
{
    if (object < 0 || object >= count) return -1;
    if (tokens[object].type != JSMN_OBJECT) return -1;
    int i = object + 1;
    while (i < count && tokens[i].start < tokens[object].end) {
        int key = i;
        int value = i + 1;
        if (value >= count) break;
        if (key_equals(json, &tokens[key], name)) return value;
        i = tok_skip(count, value);
    }
    return -1;
}

static int navigate(const char *json, int count, int root,
                    const char *const *path, int path_parts)
{
    int cur = root;
    for (int i = 0; i < path_parts; i++) {
        cur = find_key(json, count, cur, path[i]);
        if (cur < 0) return -1;
    }
    return cur;
}

static int parse_tokens(const char *json, size_t len)
{
    jsmn_parser parser;
    jsmn_init(&parser);
    int r = jsmn_parse(&parser, json, len, tokens, TOKEN_CAPACITY);
    if (r == JSMN_ERROR_NOMEM) return TOKEN_CAPACITY;
    if (r <= 0) return 0;
    return r;
}

bool fc_json_get_object_string(const char *json, size_t len,
                               const char *const *path, char *dst, size_t cap)
{
    if (dst == NULL || cap == 0) return false;
    dst[0] = '\0';
    int count = parse_tokens(json, len);
    if (count <= 0) return false;

    int path_parts = 0;
    while (path[path_parts]) path_parts++;
    int v = navigate(json, count, 0, path, path_parts);
    if (v < 0 || !tok_is_string(&tokens[v])) return false;
    token_copy(json, &tokens[v], dst, cap);
    return true;
}

bool fc_json_get_object_number(const char *json, size_t len,
                               const char *const *path, double *out)
{
    int count = parse_tokens(json, len);
    if (count <= 0) return false;

    int path_parts = 0;
    while (path[path_parts]) path_parts++;
    int v = navigate(json, count, 0, path, path_parts);
    if (v < 0) return false;
    return token_to_double(json, &tokens[v], out);
}

static void trim(char *s)
{
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

static void upper(char *s)
{
    for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

int fc_parse_feed(const char *json, size_t len, raw_aircraft_t *out, int max)
{
    if (json == NULL || out == NULL || max <= 0) return 0;
    int count = parse_tokens(json, len);
    if (count <= 0) return 0;
    if (tokens[0].type != JSMN_OBJECT) return 0;

    int ac_array = find_key(json, count, 0, "ac");
    if (ac_array < 0 || tokens[ac_array].type != JSMN_ARRAY) return 0;

    int parsed = 0;
    int i = ac_array + 1;
    while (i < count && tokens[i].start < tokens[ac_array].end && parsed < max) {
        if (tokens[i].type != JSMN_OBJECT) {
            i = tok_skip(count, i);
            continue;
        }

        raw_aircraft_t ac;
        memset(&ac, 0, sizeof(ac));
        bool has_baro = false;
        double baro_ft = 0.0;

        int j = i + 1;
        while (j < count && tokens[j].start < tokens[i].end) {
            int key = j;
            int value = j + 1;
            if (value >= count) break;

            char name[24];
            token_copy(json, &tokens[key], name, sizeof(name));

            if (strcmp(name, "hex") == 0) {
                token_copy(json, &tokens[value], ac.hex, sizeof(ac.hex));
            } else if (strcmp(name, "flight") == 0) {
                token_copy(json, &tokens[value], ac.callsign, sizeof(ac.callsign));
                trim(ac.callsign);
            } else if (strcmp(name, "t") == 0) {
                token_copy(json, &tokens[value], ac.type, sizeof(ac.type));
                trim(ac.type);
                upper(ac.type);
                if (strcmp(ac.type, "????") == 0) ac.type[0] = '\0';
            } else if (strcmp(name, "r") == 0) {
                token_copy(json, &tokens[value], ac.reg, sizeof(ac.reg));
                trim(ac.reg);
                upper(ac.reg);
            } else if (strcmp(name, "category") == 0) {
                token_copy(json, &tokens[value], ac.category, sizeof(ac.category));
                trim(ac.category);
                upper(ac.category);
            } else if (strcmp(name, "lat") == 0) {
                double v;
                if (token_to_double(json, &tokens[value], &v)) {
                    ac.lat = (float)v;
                    ac.has_lat = true;
                }
            } else if (strcmp(name, "lon") == 0) {
                double v;
                if (token_to_double(json, &tokens[value], &v)) {
                    ac.lon = (float)v;
                    ac.has_lon = true;
                }
            } else if (strcmp(name, "seen_pos") == 0) {
                double v;
                if (token_to_double(json, &tokens[value], &v)) {
                    ac.seen_pos = (float)v;
                    ac.has_seen = true;
                }
            } else if (strcmp(name, "alt_geom") == 0) {
                double v;
                if (token_to_double(json, &tokens[value], &v)) {
                    ac.alt_ft = (float)v;
                    ac.has_alt = true;
                }
            } else if (strcmp(name, "alt_baro") == 0) {
                char text[16];
                token_copy(json, &tokens[value], text, sizeof(text));
                if (strcmp(text, "ground") == 0) {
                    ac.alt_baro_ground = true;
                } else {
                    double v;
                    if (token_to_double(json, &tokens[value], &v)) {
                        baro_ft = v;
                        has_baro = true;
                    }
                }
            } else if (strcmp(name, "gs") == 0) {
                double v;
                if (token_to_double(json, &tokens[value], &v)) {
                    ac.gs_kt = (float)v;
                    ac.has_gs = true;
                }
            }
            j = tok_skip(count, value);
        }

        if (!ac.has_alt && has_baro) {
            ac.alt_ft = (float)baro_ft;
            ac.has_alt = true;
        }
        if (ac.hex[0] || ac.callsign[0] || ac.has_lat) {
            out[parsed++] = ac;
        }
        i = tok_skip(count, i);
    }
    return parsed;
}
