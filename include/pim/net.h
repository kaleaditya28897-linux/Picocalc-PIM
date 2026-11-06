#ifndef PIM_NET_H
#define PIM_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pim/pim.h"

/* ---- NTP time sync ---- */
#define PIM_NTP_PACKET_SIZE 48
void pim_ntp_request(uint8_t packet[PIM_NTP_PACKET_SIZE]);
/* Validates an NTP server reply and returns its Unix time; false for short, wrong-mode or kiss-of-death replies. */
bool pim_ntp_decode(const uint8_t *packet, size_t length, uint32_t *unix_seconds);
/* Converts Unix time plus a UTC offset (minutes) to a local date and minute of day (years 2000-2099 only). */
bool pim_time_from_unix(uint32_t unix_seconds, int tz_minutes, pim_date_t *date, uint16_t *minutes);

/* ---- Saved Wi-Fi networks, kept in /PIM/wifi.cfg (most recently used first) ---- */
#define PIM_WIFI_MAX_NETWORKS 4
#define PIM_WIFI_SSID_MAX 32
#define PIM_WIFI_PASSWORD_MAX 63
typedef struct { char ssid[PIM_WIFI_SSID_MAX + 1]; char password[PIM_WIFI_PASSWORD_MAX + 1]; } pim_wifi_network_t;
typedef struct {
    size_t count;
    pim_wifi_network_t networks[PIM_WIFI_MAX_NETWORKS];
    int16_t tz_minutes;
} pim_wifi_config_t;
void pim_wifi_config_init(pim_wifi_config_t *config);
bool pim_wifi_config_load(const char *path, pim_wifi_config_t *config);
bool pim_wifi_config_save(const char *path, const pim_wifi_config_t *config);
/* Stores or updates a network and moves it to the front; the oldest is dropped when full. */
bool pim_wifi_config_remember(pim_wifi_config_t *config, const char *ssid, const char *password);
bool pim_wifi_config_forget(pim_wifi_config_t *config, size_t index);
const pim_wifi_network_t *pim_wifi_config_find(const pim_wifi_config_t *config, const char *ssid);
bool pim_tz_valid(int tz_minutes);

/* ---- iCalendar (.ics) ---- */
size_t pim_ics_export(const pim_database_t *database, char *out, size_t out_size);
typedef struct { size_t events, todos, skipped, rejected; } pim_ics_result_t;
/* Imports VEVENT and VTODO items. Floating, UTC and TZID times are all taken as local time. Duplicates are skipped. */
pim_ics_result_t pim_ics_import(pim_database_t *database, const char *text);

#endif
