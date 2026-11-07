#ifndef PIM_WIFI_H
#define PIM_WIFI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pim/pim.h"

typedef struct { char ssid[33]; int rssi; } pim_wifi_ap_t;
typedef struct {
    pim_date_t (*today)(uint16_t *minutes); /* current device date and minute of day */
    void (*changed)(void);                  /* the web client modified the data */
} pim_wifi_hooks_t;

/* Gives the web server access to the database and user-file directory. Call once at startup. */
void pim_wifi_attach(pim_database_t *database, const char *storage_path, const char *root, const pim_wifi_hooks_t *hooks);
/* Joins a WPA2 network and starts the web server on port 8080; `address` receives the IPv4 address. */
bool pim_wifi_connect(const char *ssid, const char *password, char *address, size_t address_size);
bool pim_wifi_connected(void);
const char *pim_wifi_error(void);
void pim_wifi_set_progress(void (*hook)(const char *stage));
/* Blocking scan (about 5 s); returns the number of distinct networks sorted by signal, or -1 on failure. */
int pim_wifi_scan(pim_wifi_ap_t *list, int max);
/* Blocking SNTP query over the current connection. */
bool pim_wifi_ntp(uint32_t *unix_seconds);
/* Login PIN for the web interface, regenerated on every connection. */
const char *pim_wifi_pin(void);
const char *pim_wifi_address(void);
/* Disconnects and powers the radio off; also done automatically after 15 idle minutes. */
void pim_wifi_off(void);
void pim_wifi_poll(void);

/* Telnet transport: raw bytes in and out; protocol and terminal handling live in the core. */
bool pim_telnet_open(const char *host, uint16_t port);
void pim_telnet_close(void);
bool pim_telnet_is_open(void);
size_t pim_telnet_read(uint8_t *data, size_t max);
bool pim_telnet_write(const uint8_t *data, size_t length);

#endif
