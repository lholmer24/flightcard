/*
 * Flightcard - network task.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_NET_H
#define FC_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"

extern QueueHandle_t g_card_queue;
extern volatile uint32_t g_net_heartbeat;
extern volatile int g_wifi_state; /* 0 down, 1 connecting, 2 up */
extern volatile int g_net_phase;   /* 0 idle, 1 wifi, 2 ntp, 3 feed, 4 https */

void net_task(void *arg);

/* Shared helpers (also used by tests / diagnostics). */
bool net_wifi_connect(void);
void net_wifi_reset(void);
bool net_ntp_sync(uint32_t timeout_ms);
int net_https_get(const char *host, const char *path,
                  const char **body, size_t *body_len,
                  uint32_t timeout_ms);
bool net_dns_resolve(const char *host, char ip[16], uint32_t timeout_ms);

#endif /* FC_NET_H */
