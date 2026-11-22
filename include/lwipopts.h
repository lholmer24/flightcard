/*
 * Flightcard - lwIP options for FreeRTOS SMP on RP2350.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FC_LWIPOPTS_H
#define FC_LWIPOPTS_H

#define NO_SYS                      0
#define LWIP_SOCKET                 1
#define LWIP_NETCONN                1
#define LWIP_NETIF_API              1
#define LWIP_DNS                    1
#define LWIP_DHCP                   1
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_STATS                  0

/*
 * MEM_ALIGNMENT must match the CPU word size. lwIP defaults this to 1, which
 * lets pbuf payloads start on odd addresses; the CYW43 receive path then hands
 * an unaligned buffer to newlib's word-wise memcpy, faulting the async-context
 * task on ARMv8-M while it holds the CYW43 lock (WiFi join appears to hang).
 * The Pico lwIP examples set this to 4 for the same reason.
 */
#define MEM_ALIGNMENT               4

#define MEM_SIZE                    (32 * 1024)
#define MEMP_NUM_PBUF               32
#define MEMP_NUM_UDP_PCB            6
#define MEMP_NUM_TCP_PCB            8
#define MEMP_NUM_TCP_PCB_LISTEN     2
#define MEMP_NUM_TCP_SEG            32
#define PBUF_POOL_SIZE              16
#define PBUF_POOL_BUFSIZE           1536

#define TCP_MSS                     1460
#define TCP_SND_BUF                 (4 * TCP_MSS)
#define TCP_WND                     (4 * TCP_MSS)
#define TCP_QUEUE_OOSEQ             0
#define LWIP_TCP_KEEPALIVE          1
#define LWIP_TCP_SACK_OUT           0

#define TCPIP_THREAD_STACKSIZE      4096
#define TCPIP_THREAD_PRIO           3
#define TCPIP_MBOX_SIZE             16
#define DEFAULT_UDP_RECVMBOX_SIZE   8
#define DEFAULT_TCP_RECVMBOX_SIZE   16
#define DEFAULT_ACCEPTMBOX_SIZE     4
#define DEFAULT_THREAD_STACKSIZE    2048
#define DEFAULT_THREAD_PRIO         2

#define LWIP_SO_RCVTIMEO            1
#define LWIP_SO_SNDTIMEO            1
#define LWIP_SO_RCVBUF              0
#define LWIP_SO_LINGER              0

#define LWIP_TCPIP_CORE_LOCKING     1
#define LWIP_TCPIP_CORE_LOCKING_INPUT 1
#define LWIP_NETCONN_FULLDUPLEX     0
#define LWIP_NETCONN_SEM_PER_THREAD 0

#define LWIP_COMPAT_SOCKETS         0
#define LWIP_POSIX_SOCKETS_IO_NAMES 0
#define LWIP_SOCKET_SELECT          1
#define LWIP_SOCKET_POLL            1

#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define LWIP_SINGLE_NETIF           1
#define LWIP_NETIF_LOOPBACK         0
#define LWIP_HAVE_LOOPIF            0

#define DNS_MAX_SERVERS             2
#define LWIP_DNS_SECURE             0
#define LWIP_DNS_SUPPORT_MDNS_QUERIES 0

#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0

#define LWIP_TIMEVAL_PRIVATE        0
/*
 * pico_cyw43_arch_lwip_sys_freertos builds lwIP with LWIP_PROVIDE_ERRNO=1, so
 * this include is ignored and lwIP keeps a private errno. Its values (and
 * EINPROGRESS) also differ from newlib's. Do not compare errno after lwip_*
 * calls; use getsockopt(SO_ERROR)/poll, as tls_http.c does.
 */
#define LWIP_ERRNO_INCLUDE          <errno.h>

#define LWIP_ALTCP                  0
#define LWIP_ALTCP_TLS              0

#endif /* FC_LWIPOPTS_H */
