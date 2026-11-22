/*
 * Flightcard - HTTP parsing.
 *
 * SPDX-License-Identifier: MIT
 */
#include "http.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool header_name_is(const char *line, size_t len, const char *name)
{
    size_t n = strlen(name);
    if (len < n + 1 || line[n] != ':') return false;
    return strncasecmp(line, name, n) == 0;
}

static bool contains_token(const char *value, size_t len, const char *token)
{
    const char *p = value;
    const char *end = value + len;
    size_t tl = strlen(token);
    while (p + tl <= end) {
        if (strncasecmp(p, token, tl) == 0) return true;
        p++;
    }
    return false;
}

fc_http_result_t fc_http_parse(const char *data, size_t len,
                               fc_http_response_t *r,
                               char *decoded, size_t decoded_cap)
{
    if (data == NULL || r == NULL) return FC_HTTP_ERROR;
    memset(r, 0, sizeof(*r));

    const char *end = NULL;
    for (size_t i = 0; i + 3 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n'
            && data[i + 2] == '\r' && data[i + 3] == '\n') {
            end = data + i + 4;
            break;
        }
        if (data[i] == '\n' && data[i + 1] == '\n') {
            end = data + i + 2;
            break;
        }
    }
    if (end == NULL) return FC_HTTP_INCOMPLETE;

    const char *line_end = strstr(data, "\r\n");
    if (line_end == NULL) line_end = strchr(data, '\n');
    if (line_end == NULL) return FC_HTTP_ERROR;

    int status = 0;
    if (sscanf(data, "HTTP/%*d.%*d %d", &status) != 1) return FC_HTTP_ERROR;
    r->status = status;

    const char *p = line_end;
    while (p < end) {
        while (p < end && (*p == '\r' || *p == '\n')) p++;
        const char *eol = p;
        while (eol < end && *eol != '\r' && *eol != '\n') eol++;
        size_t line_len = (size_t)(eol - p);
        if (line_len == 0) break;

        if (header_name_is(p, line_len, "Content-Length")) {
            const char *v = p + strlen("Content-Length") + 1;
            while (v < eol && (*v == ' ' || *v == '\t')) v++;
            char nbuf[24];
            size_t n = (size_t)(eol - v);
            if (n >= sizeof(nbuf)) return FC_HTTP_ERROR;
            memcpy(nbuf, v, n);
            nbuf[n] = '\0';
            char *ep = NULL;
            unsigned long value = strtoul(nbuf, &ep, 10);
            if (ep == nbuf) return FC_HTTP_ERROR;
            r->content_length = (size_t)value;
            r->has_content_length = true;
        } else if (header_name_is(p, line_len, "Transfer-Encoding")) {
            const char *v = p + strlen("Transfer-Encoding") + 1;
            if (contains_token(v, (size_t)(eol - v), "chunked")) {
                r->chunked = true;
            }
        }
        p = eol;
    }

    const char *body = end;
    size_t available = len - (size_t)(body - data);

    if (r->chunked) {
        if (decoded == NULL || decoded_cap == 0) return FC_HTTP_ERROR;
        size_t out = 0;
        const char *q = body;
        const char *limit = data + len;
        while (q < limit) {
            const char *eol = q;
            while (eol + 1 < limit && !(*eol == '\r' && eol[1] == '\n')) eol++;
            if (eol + 1 >= limit) return FC_HTTP_INCOMPLETE;
            char sizebuf[16];
            size_t n = (size_t)(eol - q);
            if (n >= sizeof(sizebuf)) return FC_HTTP_ERROR;
            memcpy(sizebuf, q, n);
            sizebuf[n] = '\0';
            char *ep = NULL;
            unsigned long chunk = strtoul(sizebuf, &ep, 16);
            if (ep == sizebuf) return FC_HTTP_ERROR;
            q = eol + 2;
            if (chunk == 0) break; /* trailers ignored */
            if ((size_t)(limit - q) < (size_t)chunk + 2) return FC_HTTP_INCOMPLETE;
            if (out + chunk > decoded_cap) return FC_HTTP_ERROR;
            memcpy(decoded + out, q, (size_t)chunk);
            out += (size_t)chunk;
            q += chunk;
            if (q + 1 < limit && q[0] == '\r' && q[1] == '\n') q += 2;
            else if (q < limit && *q == '\n') q += 1;
            else return FC_HTTP_ERROR;
        }
        r->body = decoded;
        r->body_len = out;
        return FC_HTTP_OK;
    }

    if (r->has_content_length) {
        if (r->content_length > FC_HTTP_BODY_MAX) return FC_HTTP_ERROR;
        if (available < r->content_length) return FC_HTTP_INCOMPLETE;
        r->body = body;
        r->body_len = r->content_length;
        return FC_HTTP_OK;
    }

    /* No framing: the caller must have read to EOF. */
    r->body = body;
    r->body_len = available;
    return FC_HTTP_OK;
}

size_t fc_http_build_request(char *buf, size_t cap,
                             const char *host, const char *path)
{
    if (buf == NULL || cap == 0 || host == NULL || path == NULL) return 0;
    int n = snprintf(buf, cap,
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "User-Agent: flightcard/1.0\r\n"
                     "Accept: application/json\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     path, host);
    if (n < 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}
