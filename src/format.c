/*
 * Flightcard - formatting.
 *
 * SPDX-License-Identifier: MIT
 */
#include "format.h"

#include <ctype.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void copy_clean(char *dst, size_t cap, const char *src, size_t len)
{
    if (cap == 0) return;
    if (len >= cap) len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

void fc_ellipsize(char *dst, size_t cap, const char *src, int limit)
{
    if (dst == NULL || cap == 0) return;
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    if (limit <= 0) {
        dst[0] = '\0';
        return;
    }
    size_t len = strlen(src);
    if ((int)len <= limit) {
        copy_clean(dst, cap, src, len);
        return;
    }
    size_t keep = (size_t)limit - 1;
    while (keep > 0 && src[keep - 1] == ' ') keep--;
    copy_clean(dst, cap, src, keep);
    size_t used = strlen(dst);
    const char *ell = FC_ELLIPSIS;
    while (*ell && used + 1 < cap) dst[used++] = *ell++;
    dst[used] = '\0';
}

void fc_upper_ellipsize(char *dst, size_t cap, const char *src, int limit)
{
    if (dst == NULL || cap == 0) return;
    char tmp[128];
    size_t n = 0;
    if (src != NULL) {
        while (*src && n + 1 < sizeof(tmp)) {
            tmp[n++] = (char)toupper((unsigned char)*src++);
        }
    }
    tmp[n] = '\0';
    fc_ellipsize(dst, cap, tmp, limit);
}

void fc_wrap_type(const char *text, int limit, char line1[64], char line2[64])
{
    line1[0] = '\0';
    line2[0] = '\0';
    if (text == NULL || limit <= 0) return;

    size_t len = strlen(text);
    if ((int)len <= limit) {
        copy_clean(line1, 64, text, len);
        return;
    }

    char words[16][32];
    int nwords = 0;
    const char *p = text;
    while (*p && nwords < 16) {
        while (*p == ' ') p++;
        if (!*p) break;
        size_t n = 0;
        while (*p && *p != ' ' && n + 1 < sizeof(words[0])) {
            words[nwords][n++] = *p++;
        }
        words[nwords][n] = '\0';
        nwords++;
    }
    if (nwords == 0) return;
    if (nwords == 1) {
        fc_ellipsize(line1, 64, words[0], limit);
        return;
    }

    int best = -1;
    int best_score = 0;
    char best1[64] = {0}, best2[64] = {0};
    for (int i = 1; i < nwords; i++) {
        char a[64] = {0}, b[64] = {0};
        size_t an = 0, bn = 0;
        for (int j = 0; j < i; j++) {
            if (j) a[an++] = ' ';
            size_t wl = strlen(words[j]);
            if (an + wl >= sizeof(a)) break;
            memcpy(a + an, words[j], wl);
            an += wl;
        }
        a[an] = '\0';
        for (int j = i; j < nwords; j++) {
            if (j > i) b[bn++] = ' ';
            size_t wl = strlen(words[j]);
            if (bn + wl >= sizeof(b)) break;
            memcpy(b + bn, words[j], wl);
            bn += wl;
        }
        b[bn] = '\0';
        if ((int)an <= limit && (int)bn <= limit) {
            int score = (int)an - (int)bn;
            if (score < 0) score = -score;
            if (best < 0 || score < best_score) {
                best = i;
                best_score = score;
                snprintf(best1, sizeof(best1), "%s", a);
                snprintf(best2, sizeof(best2), "%s", b);
            }
        }
    }
    if (best >= 0) {
        snprintf(line1, 64, "%s", best1);
        snprintf(line2, 64, "%s", best2);
        return;
    }

    /* No clean break: fill the first line, ellipsise the rest. */
    char first[64] = {0};
    int used_words = 0;
    size_t flen = 0;
    for (int i = 0; i < nwords; i++) {
        size_t wl = strlen(words[i]);
        size_t add = (flen ? 1 : 0) + wl;
        if ((int)(flen + add) > limit) break;
        if (flen) first[flen++] = ' ';
        memcpy(first + flen, words[i], wl);
        flen += wl;
        first[flen] = '\0';
        used_words++;
    }

    char rest[128] = {0};
    size_t rlen = 0;
    for (int i = used_words; i < nwords; i++) {
        if (rlen) rest[rlen++] = ' ';
        size_t wl = strlen(words[i]);
        if (rlen + wl >= sizeof(rest)) break;
        memcpy(rest + rlen, words[i], wl);
        rlen += wl;
    }
    rest[rlen] = '\0';

    snprintf(line1, 64, "%s", first);
    fc_ellipsize(line2, 64, rest, limit);
}

void fc_group_number(char *dst, size_t cap, unsigned long value)
{
    if (dst == NULL || cap == 0) return;
    char raw[24];
    snprintf(raw, sizeof(raw), "%lu", value);
    size_t n = strlen(raw);
    size_t commas = (n - 1) / 3;
    if (n + commas + 1 > cap) {
        snprintf(dst, cap, "%s", raw);
        return;
    }
    size_t out = 0;
    for (size_t i = 0; i < n; i++) {
        if (i && (n - i) % 3 == 0) dst[out++] = ',';
        dst[out++] = raw[i];
    }
    dst[out] = '\0';
}

void fc_format_speed(char *dst, size_t cap, float kt)
{
    snprintf(dst, cap, "%.0f kt", (double)kt);
}

void fc_format_altitude(char *dst, size_t cap, float ft)
{
    double rounded = round((double)ft / 10.0) * 10.0;
    bool neg = rounded < 0;
    unsigned long v = (unsigned long)llround(fabs(rounded));
    char grouped[24];
    fc_group_number(grouped, sizeof(grouped), v);
    snprintf(dst, cap, "%s%s ft", neg ? "-" : "", grouped);
}

void fc_format_distance(char *dst, size_t cap, float km)
{
    snprintf(dst, cap, "%.1f km", (double)km);
}
