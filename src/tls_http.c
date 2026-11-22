/*
 * Flightcard - bounded HTTPS client (mbedTLS + lwIP sockets).
 *
 * SPDX-License-Identifier: MIT
 */
#include "net.h"
#include "clock.h"
#include "http.h"
#include "log.h"
#include "platform.h"
#include "fc_assets_generated.h"

#include "lwip/sockets.h"
#include "lwip/ip4_addr.h"

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/platform.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#define IO_BUF_SIZE (FC_HTTP_BODY_MAX + 2048)

static char io_buf[IO_BUF_SIZE];
static bool tls_ready;
static mbedtls_ssl_config ssl_conf;
static mbedtls_x509_crt ca_chain;
static mbedtls_ctr_drbg_context ctr_drbg;
static mbedtls_entropy_context entropy;

#if defined(MBEDTLS_PLATFORM_MEMORY)
static void *fc_calloc(size_t n, size_t size)
{
    size_t total = n * size;
    void *p = pvPortMalloc(total);
    if (p) memset(p, 0, total);
    return p;
}
#endif

mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return (mbedtls_ms_time_t)fc_monotonic_ms();
}

static mbedtls_time_t fc_mbedtls_time(mbedtls_time_t *out)
{
    mbedtls_time_t now = (mbedtls_time_t)clock_now_utc();
    if (out) *out = now;
    return now;
}

static bool tls_init(void)
{
    if (tls_ready) return true;

#if defined(MBEDTLS_PLATFORM_MEMORY)
    mbedtls_platform_set_calloc_free(fc_calloc, vPortFree);
#endif
    mbedtls_platform_set_time(fc_mbedtls_time);

    mbedtls_ssl_config_init(&ssl_conf);
    mbedtls_x509_crt_init(&ca_chain);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_entropy_init(&entropy);

    static const char personal[] = "flightcard";
    int rc = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                   (const unsigned char *)personal,
                                   sizeof(personal) - 1);
    if (rc != 0) {
        LOG_E("tls: ctr_drbg_seed %d", rc);
        return false;
    }

    if (fc_ca_bundle_pem[0]) {
        rc = mbedtls_x509_crt_parse(&ca_chain,
                                    (const unsigned char *)fc_ca_bundle_pem,
                                    strlen(fc_ca_bundle_pem) + 1);
        if (rc != 0) LOG_W("tls: CA bundle parse result %d", rc);
    }
    rc = mbedtls_x509_crt_parse(&ca_chain,
                                (const unsigned char *)fc_gtsr4_pem,
                                strlen(fc_gtsr4_pem) + 1);
    if (rc != 0) LOG_W("tls: GTS Root R4 parse result %d", rc);

    rc = mbedtls_ssl_config_defaults(&ssl_conf, MBEDTLS_SSL_IS_CLIENT,
                                     MBEDTLS_SSL_TRANSPORT_STREAM,
                                     MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0) {
        LOG_E("tls: config_defaults %d", rc);
        return false;
    }
    mbedtls_ssl_conf_authmode(&ssl_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&ssl_conf, &ca_chain, NULL);
    mbedtls_ssl_conf_rng(&ssl_conf, mbedtls_ctr_drbg_random, &ctr_drbg);
    mbedtls_ssl_conf_read_timeout(&ssl_conf, 12000);
    mbedtls_ssl_conf_min_tls_version(&ssl_conf, MBEDTLS_SSL_VERSION_TLS1_2);

    tls_ready = true;
    LOG_I("tls: mbedTLS ready, CA chain loaded");
    return true;
}

static int tcp_connect(const char *ip, uint16_t port, uint32_t timeout_ms)
{
    int fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_W("tcp: socket failed");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (!ip4addr_aton(ip, (ip4_addr_t *)&addr.sin_addr)) {
        LOG_W("tcp: bad address '%s'", ip);
        lwip_close(fd);
        return -1;
    }

    lwip_fcntl(fd, F_SETFL, O_NONBLOCK);
    int rc = lwip_connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc < 0) {
        /*
         * A non-blocking lwIP connect returns -1/EINPROGRESS. The Pico
         * cyw43/lwIP wrapper builds lwIP with LWIP_PROVIDE_ERRNO=1, so lwIP
         * writes its own errno while newlib's <errno.h> macro reads a
         * different variable (and uses different E* values). Do not test
         * errno here; wait for the socket and read SO_ERROR instead.
         */
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        rc = lwip_poll(&pfd, 1, (int)timeout_ms);
        if (rc <= 0) {
            LOG_W("tcp: connect timeout to %s", ip);
            lwip_close(fd);
            return -1;
        }
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        if (lwip_getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) != 0
            || so_error != 0) {
            LOG_W("tcp: connect error %d to %s", so_error, ip);
            lwip_close(fd);
            return -1;
        }
    }
    lwip_fcntl(fd, F_SETFL, 0);

    struct timeval tv;
    tv.tv_sec = (time_t)(timeout_ms / 1000);
    tv.tv_usec = (suseconds_t)((timeout_ms % 1000) * 1000);
    lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    lwip_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return fd;
}

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    int fd = *(int *)ctx;
    int n = lwip_send(fd, buf, len, 0);
    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        }
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return n;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    int fd = *(int *)ctx;
    int n = lwip_recv(fd, buf, len, 0);
    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            return MBEDTLS_ERR_SSL_WANT_READ;
        }
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    if (n == 0) return MBEDTLS_ERR_NET_CONN_RESET;
    return n;
}

static void log_mbedtls(const char *what, int rc)
{
    if (rc == 0) return;
    char err[96];
    mbedtls_strerror(rc, err, sizeof(err));
    LOG_W("tls: %s failed: -0x%04x %s", what, (unsigned)-rc, err);
}

int net_https_get(const char *host, const char *path,
                  const char **body, size_t *body_len, uint32_t timeout_ms)
{
    if (body) *body = NULL;
    if (body_len) *body_len = 0;
    g_net_phase = 4;
    if (!tls_init()) return -1;
    if (!clock_is_valid()) {
        LOG_W("tls: refusing request before a valid clock");
        g_net_phase = 0;
        return -1;
    }

    char ip[16];
    if (!net_dns_resolve(host, ip, 2000)) { g_net_phase = 0; return -1; }

    int fd = tcp_connect(ip, 443, timeout_ms);
    if (fd < 0) { g_net_phase = 0; return -1; }

    mbedtls_ssl_context ssl;
    mbedtls_ssl_init(&ssl);
    int rc = mbedtls_ssl_setup(&ssl, &ssl_conf);
    if (rc != 0) {
        log_mbedtls("setup", rc);
        mbedtls_ssl_free(&ssl);
        lwip_close(fd);
        return -1;
    }
    rc = mbedtls_ssl_set_hostname(&ssl, host);
    if (rc != 0) {
        log_mbedtls("set_hostname", rc);
        mbedtls_ssl_free(&ssl);
        lwip_close(fd);
        return -1;
    }
    mbedtls_ssl_set_bio(&ssl, &fd, bio_send, bio_recv, NULL);

    uint64_t deadline = fc_monotonic_ms() + timeout_ms;
    do {
        rc = mbedtls_ssl_handshake(&ssl);
        if (rc == 0) break;
        if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
            log_mbedtls("handshake", rc);
            mbedtls_ssl_free(&ssl);
            lwip_close(fd);
            return -1;
        }
    } while (fc_monotonic_ms() < deadline);
    if (rc != 0) {
        LOG_W("tls: handshake timed out");
        mbedtls_ssl_free(&ssl);
        lwip_close(fd);
        return -1;
    }

    char request[512];
    size_t req_len = fc_http_build_request(request, sizeof(request), host, path);
    if (req_len == 0) {
        mbedtls_ssl_free(&ssl);
        lwip_close(fd);
        return -1;
    }
    size_t sent = 0;
    while (sent < req_len) {
        rc = mbedtls_ssl_write(&ssl, (const unsigned char *)request + sent,
                               req_len - sent);
        if (rc > 0) {
            sent += (size_t)rc;
        } else if (rc == MBEDTLS_ERR_SSL_WANT_READ
                   || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (fc_monotonic_ms() > deadline) break;
        } else {
            log_mbedtls("write", rc);
            break;
        }
    }

    size_t io_len = 0;
    int status = -1;
    fc_http_response_t response;
    while (sent == req_len && fc_monotonic_ms() < deadline) {
        if (io_len + 1 >= sizeof(io_buf)) {
            LOG_W("http: response buffer full");
            break;
        }
        rc = mbedtls_ssl_read(&ssl, (unsigned char *)io_buf + io_len,
                              sizeof(io_buf) - io_len - 1);
        if (rc > 0) {
            io_len += (size_t)rc;
            io_buf[io_len] = '\0';

            const char *sep = strstr(io_buf, "\r\n\r\n");
            size_t body_off = sep ? (size_t)(sep + 4 - io_buf) : 0;
            char *decoded = body_off ? io_buf + body_off : NULL;
            fc_http_result_t pr = fc_http_parse(io_buf, io_len, &response,
                                                decoded,
                                                decoded ? sizeof(io_buf) - body_off : 0);
            if (pr == FC_HTTP_OK) {
                status = response.status;
                if (body) *body = response.body;
                if (body_len) *body_len = response.body_len;
                break;
            }
            if (pr == FC_HTTP_ERROR) {
                LOG_W("http: parse error");
                break;
            }
        } else if (rc == MBEDTLS_ERR_SSL_WANT_READ
                   || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        } else if (rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            break;
        } else {
            log_mbedtls("read", rc);
            break;
        }
    }

    mbedtls_ssl_close_notify(&ssl);
    mbedtls_ssl_free(&ssl);
    lwip_close(fd);
    g_net_phase = 0;
    return status;
}
