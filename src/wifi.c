/*
 * Flightcard - CYW43 WiFi bring-up and recovery.
 *
 * SPDX-License-Identifier: MIT
 */
#include "net.h"
#include "config_store.h"
#include "log.h"

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/dhcp.h"

#include <string.h>

static bool arch_ready;

void dns_cache_clear(void);

static bool arch_init(void)
{
    if (arch_ready) return true;
    LOG_I("wifi: arch init (UK regulatory domain)");
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_UK) != 0) {
        LOG_E("wifi: cyw43_arch_init failed");
        return false;
    }
    cyw43_arch_enable_sta_mode();
    LOG_I("wifi: STA mode enabled");
    arch_ready = true;
    const config_t *cfg = config_store_get();
    if (!cfg->wifi_power_save) {
        cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
        LOG_I("wifi: power management disabled");
    } else {
        LOG_I("wifi: power management enabled");
    }
    return true;
}

bool net_wifi_connect(void)
{
    if (!arch_init()) return false;

    const config_t *cfg = config_store_get();
    if (cfg->wifi_ssid[0] == '\0') {
        LOG_W("wifi: no SSID configured");
        return false;
    }
    if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP) {
        g_wifi_state = 2;
        return true;
    }

    g_wifi_state = 1;
    g_net_phase = 1;
    LOG_I("wifi: joining '%s'", cfg->wifi_ssid);
    int rc = cyw43_arch_wifi_connect_timeout_ms(
        cfg->wifi_ssid, cfg->wifi_password, CYW43_AUTH_WPA2_AES_PSK, 30000);
    int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    if (rc == 0 && status == CYW43_LINK_UP) {
        g_wifi_state = 2;
        g_net_phase = 0;
        if (netif_default != NULL) {
            char ip[24];
            ip4addr_ntoa_r(netif_ip4_addr(netif_default), ip, sizeof(ip));
            LOG_I("wifi: connected, ip %s", ip);
        } else {
            LOG_I("wifi: connected");
        }
        return true;
    }
    LOG_W("wifi: connect failed rc %d status %d", rc, status);
    if (status == CYW43_LINK_BADAUTH) {
        LOG_W("wifi: bad password");
    }
    g_wifi_state = 0;
    g_net_phase = 0;
    return false;

}

void net_wifi_reset(void)
{
    LOG_W("wifi: resetting association and DNS cache");
    dns_cache_clear();
    if (arch_ready) {
        cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
        sleep_ms(500);
    }
    g_wifi_state = 0;
}
