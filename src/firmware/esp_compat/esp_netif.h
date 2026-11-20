#ifndef PIM_COMPAT_ESP_NETIF_H
#define PIM_COMPAT_ESP_NETIF_H

#include "esp_err.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"

/* An esp_netif_t is an lwIP netif here; ESP-IDF's wrapper object does not exist.
   Upstream only asks four things of it: is it up, what is its IPv4 address, what
   is it called, and is it PPP. */

typedef struct netif esp_netif_t;

typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { uint32_t addr[4]; uint8_t zone; } esp_ip6_addr_t;
typedef struct {
    esp_ip4_addr_t ip;
    esp_ip4_addr_t netmask;
    esp_ip4_addr_t gw;
} esp_netif_ip_info_t;

#define ESP_NETIF_FLAG_IS_PPP (1 << 3)

esp_netif_t *esp_netif_get_default_netif(void);
esp_netif_t *esp_netif_find_if(bool (*predicate)(esp_netif_t *netif, void *arg), void *arg);
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key);
/* "WIFI_STA_DEF" for the Wi-Fi station, "WG_DEF" for a WireGuard tunnel; the
   distinction matters because upstream only advertises LAN interfaces. */
const char *esp_netif_get_ifkey(esp_netif_t *netif);
uint32_t esp_netif_get_flags(esp_netif_t *netif);
bool esp_netif_is_netif_up(esp_netif_t *netif);
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info);
/* IPv6 is off in lwipopts, so these report "not supported" and the IPv6 direct
   path is skipped rather than half-attempted. */
esp_err_t esp_netif_get_ip6_linklocal(esp_netif_t *netif, esp_ip6_addr_t *address);
esp_err_t esp_netif_create_ip6_linklocal(esp_netif_t *netif);

#endif
