/*
 * Flightcard - minimal HTTP/1.1 response parser.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_HTTP_H
#define FC_HTTP_H

#include <stdbool.h>
#include <stddef.h>

#define FC_HTTP_BODY_MAX (32u * 1024u)

typedef enum {
    FC_HTTP_OK = 0,
    FC_HTTP_INCOMPLETE,      /* need more bytes */
    FC_HTTP_ERROR,
} fc_http_result_t;

typedef struct {
    int status;
    bool chunked;
    bool has_content_length;
    size_t content_length;
    const char *body;        /* points into input for content-length body */
    size_t body_len;
} fc_http_response_t;

/* Parse a complete response. For chunked bodies, `decoded` receives the
 * de-chunked body; for fixed bodies the caller may use r->body directly. */
fc_http_result_t fc_http_parse(const char *data, size_t len,
                               fc_http_response_t *r,
                               char *decoded, size_t decoded_cap);

/* Build "GET path HTTP/1.1" with Host/User-Agent/Connection: close. */
size_t fc_http_build_request(char *buf, size_t cap,
                             const char *host, const char *path);

#endif /* FC_HTTP_H */
