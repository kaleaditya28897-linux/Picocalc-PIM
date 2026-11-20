#include "pim/boottrace.h"
#include "pim/wifi.h"
#if PIM_TAILSCALE
#include "microlink.h"
/* src/firmware/tailscale.c: the running client, or NULL when it is not up. */
microlink_t *pim_ts_client(void);
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "hardware/watchdog.h"
#include "lwip/dns.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "pico/cyw43_arch.h"
#include "pico/rand.h"
#include "pico/time.h"
#include "pim/net.h"
#include "pim/sshcrypto.h"
#include "pim/web.h"

/* In the polled build these are no-ops. In the Tailscale build (FreeRTOS, lwIP
   with NO_SYS=0) they take the lwIP core lock, which every lwIP call made
   outside an lwIP callback needs. Bracketing a short call is always safe, even
   from inside a callback; holding the lock across a wait is not, because the
   stack could then never deliver the result being waited for, so the loops
   below bracket only the call that starts the work. */
#define NET_BEGIN() cyw43_arch_lwip_begin()
#define NET_END() cyw43_arch_lwip_end()

/* The lwIP lock above is not the radio driver's lock. Calls straight into the
   driver - scanning, leaving a network - touch cyw43_state, which in this build
   the driver's own task touches too, so they need its lock. In the polled build
   there is no second thread and nothing to take. The SDK only checks this in
   debug builds, so a release build does not complain: it just races. */
#if PIM_TAILSCALE
#define RADIO_BEGIN() cyw43_thread_enter()
#define RADIO_END() cyw43_thread_exit()
#else
#define RADIO_BEGIN() ((void)0)
#define RADIO_END() ((void)0)
#endif

#define HTTP_PORT 8080
#define REQUEST_MAX (PIM_WEB_BODY_MAX + 1024)
#define RESPONSE_MAX 16384
/* At least TCP_WND, so a full window always fits in one go. That is what lets
   the receive callback below be all-or-nothing, which is what stops it losing
   bytes: a smaller ring could be handed a pbuf chain it could never accept, and
   refusing it forever would wedge the connection. */
#define TELNET_RX_MAX 6144

typedef struct {
    size_t length;
    size_t sent;
    char header[320];
    size_t header_length;
    size_t header_sent;
    bool active;
} reply_t;

static bool wifi_initialized;
static bool wifi_up;
static uint32_t wifi_activity_ms;
#define WIFI_IDLE_OFF_MS (15U * 60U * 1000U)

static void touch(void) { wifi_activity_ms = to_ms_since_boot(get_absolute_time()); }
static char wifi_address[16];
static struct tcp_pcb *listener;
static struct tcp_pcb *client;
static bool client_aborted;
/* 29 KB of buffers that only matter while the web server is listening, which is
   a small part of the time. Held on the heap rather than in .bss so the memory
   is there for the rest of the firmware when the server is off. */
static char *request;
static size_t request_length;
static char *response;
static reply_t reply;
static pim_web_t web;
static pim_wifi_hooks_t hooks;

static struct tcp_pcb *telnet;
/* The receive ring, held only while a connection is open: 6 KB that the
   Tailscale client can use the rest of the time. */
static uint8_t *telnet_rx;
static size_t telnet_head, telnet_count;
static bool telnet_open_flag;

static void refresh_clock(void) {
    if (hooks.today != NULL) web.today = hooks.today(&web.minutes);
}

static void reset_client(void) {
    client = NULL;
    request_length = 0;
    reply.active = false;
}

static void finish_client(struct tcp_pcb *pcb) {
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_err(pcb, NULL);
    if (tcp_close(pcb) != ERR_OK) {
        tcp_abort(pcb);
        client_aborted = true;
    }
    reset_client();
}

static void pump(struct tcp_pcb *pcb) {
    while (reply.header_sent < reply.header_length) {
        size_t room = tcp_sndbuf(pcb), count = reply.header_length - reply.header_sent;
        if (room == 0) return;
        if (count > room) count = room;
        if (tcp_write(pcb, reply.header + reply.header_sent, (u16_t)count, TCP_WRITE_FLAG_COPY) != ERR_OK) return;
        reply.header_sent += count;
    }
    while (reply.sent < reply.length) {
        size_t room = tcp_sndbuf(pcb), count = reply.length - reply.sent;
        if (room == 0) break;
        if (count > room) count = room;
        if (count > 0xFFFF) count = 0xFFFF;
        if (tcp_write(pcb, response + reply.sent, (u16_t)count, TCP_WRITE_FLAG_COPY) != ERR_OK) break;
        reply.sent += count;
    }
    tcp_output(pcb);
    if (reply.sent >= reply.length) finish_client(pcb);
}

static err_t client_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    (void)arg;
    (void)len;
    client_aborted = false;
    if (reply.active) pump(pcb);
    return client_aborted ? ERR_ABRT : ERR_OK;
}

static void client_error(void *arg, err_t error) {
    (void)arg;
    (void)error;
    reset_client();
}

static const char *header_value(const char *headers, const char *name) {
    size_t length = strlen(name);
    const char *line = strchr(headers, '\n');
    while (line != NULL) {
        line++;
        if (strncasecmp(line, name, length) == 0) {
            line += length;
            while (*line == ' ') line++;
            return line;
        }
        line = strchr(line, '\n');
    }
    return NULL;
}

static void respond(struct tcp_pcb *pcb) {
    char method[8], target[200], cookie[32] = "";
    const char *end = strstr(request, "\r\n\r\n"), *value;
    size_t header_size, content_length = 0;
    pim_web_response_t result;
    if (end == NULL) return;
    header_size = (size_t)(end - request) + 4;
    if (sscanf(request, "%7s %199s", method, target) != 2) {
        finish_client(pcb);
        return;
    }
    value = header_value(request, "Content-Length:");
    if (value != NULL) content_length = (size_t)strtoul(value, NULL, 10);
    if (content_length > PIM_WEB_BODY_MAX) {
        reply.length = (size_t)snprintf(response, RESPONSE_MAX, "Request too large\n");
        result = (pim_web_response_t){.status = 413, .type = "text/plain", .length = reply.length};
    } else {
        if (request_length < header_size + content_length) return;
        value = header_value(request, "Cookie:");
        if (value != NULL) {
            const char *pin = strstr(value, "pim=");
            if (pin != NULL) {
                size_t i = 4;
                memcpy(cookie, "pim=", 4);
                pin += 4;
                while (i < sizeof(cookie) - 1 && pin[i - 4] != '\0' && pin[i - 4] != ';' && pin[i - 4] != '\r' && pin[i - 4] != '\n') {
                    cookie[i] = pin[i - 4];
                    i++;
                }
                cookie[i] = '\0';
            }
        }
        request[header_size + content_length] = '\0';
        refresh_clock();
        web.changed = false;
        pim_web_handle(&web, method, target, cookie, request + header_size, content_length,
                       response, RESPONSE_MAX, &result);
    }
    reply.length = result.length;
    reply.sent = 0;
    reply.header_sent = 0;
    reply.header_length = (size_t)snprintf(
        reply.header, sizeof(reply.header),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\n%s%s%s%s",
        result.status, result.status < 300 ? "OK" : (result.status < 400 ? "See Other" : "Error"),
        result.type, (unsigned)result.length,
        result.location[0] != '\0' ? "Location: " : "", result.location,
        result.location[0] != '\0' ? "\r\n" : "",
        result.set_cookie ? "Set-Cookie: pim=" : "");
    if (result.set_cookie) {
        size_t used = strlen(reply.header);
        snprintf(reply.header + used, sizeof(reply.header) - used, "%s; Path=/; HttpOnly; SameSite=Strict\r\n", web.pin);
    }
    {
        size_t used = strlen(reply.header);
        snprintf(reply.header + used, sizeof(reply.header) - used, "\r\n");
        reply.header_length = strlen(reply.header);
    }
    reply.active = true;
    pump(pcb);
    if (web.changed && hooks.changed != NULL) hooks.changed();
}

static err_t client_receive(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t error) {
    if (request == NULL || response == NULL) {       /* server not really up */
        if (p != NULL) { tcp_recved(pcb, p->tot_len); pbuf_free(p); }
        finish_client(pcb);
        return client_aborted ? ERR_ABRT : ERR_OK;
    }
    (void)arg;
    client_aborted = false;
    if (p == NULL || error != ERR_OK) {
        if (p != NULL) pbuf_free(p);
        finish_client(pcb);
        return client_aborted ? ERR_ABRT : ERR_OK;
    }
    if (telnet_rx == NULL) { pbuf_free(p); return ERR_OK; }
    tcp_recved(pcb, p->tot_len);
    if (!reply.active) {
        if (request_length + p->tot_len > REQUEST_MAX) {
            pbuf_free(p);
            finish_client(pcb);
            return client_aborted ? ERR_ABRT : ERR_OK;
        }
        pbuf_copy_partial(p, request + request_length, p->tot_len, 0);
        request_length += p->tot_len;
        request[request_length] = '\0';
        pbuf_free(p);
        respond(pcb);
        return client_aborted ? ERR_ABRT : ERR_OK;
    }
    pbuf_free(p);
    return ERR_OK;
}

static err_t server_accept(void *arg, struct tcp_pcb *pcb, err_t error) {
    (void)arg;
    if (error != ERR_OK || client != NULL) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    touch();
    client = pcb;
    request_length = 0;
    reply.active = false;
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, client_receive);
    tcp_sent(pcb, client_sent);
    tcp_err(pcb, client_error);
    return ERR_OK;
}

static void free_server_buffers(void) {
    free(request);
    free(response);
    request = NULL;
    response = NULL;
    request_length = 0;
}

static bool start_server(void) {
    bool ok = false;
    if (listener != NULL) return true;
    if (request == NULL) request = malloc(REQUEST_MAX + 1);
    if (response == NULL) response = malloc(RESPONSE_MAX);
    if (request == NULL || response == NULL) {
        free_server_buffers();
        return false;                   /* not enough memory to serve anything */
    }
    NET_BEGIN();
    listener = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (listener != NULL) {
        if (tcp_bind(listener, IP_ANY_TYPE, HTTP_PORT) != ERR_OK) {
            tcp_close(listener);
            listener = NULL;
        } else {
            listener = tcp_listen_with_backlog(listener, 1);
            if (listener != NULL) {
                tcp_accept(listener, server_accept);
                ok = true;
            }
        }
    }
    NET_END();
    return ok;
}

void pim_wifi_attach(pim_database_t *database, const char *storage_path, const char *root,
                     const pim_wifi_hooks_t *callbacks) {
    web.database = database;
    web.storage_path = storage_path;
    web.root = root;
    if (callbacks != NULL) hooks = *callbacks;
}

static void (*progress_hook)(const char *);
void pim_wifi_set_progress(void (*hook)(const char *)) { progress_hook = hook; }
static void progress(const char *stage) {
    if (progress_hook != NULL) progress_hook(stage);
}

static char wifi_error[128] = "";
const char *pim_wifi_error(void) { return wifi_error; }

static bool wifi_init(void) {
    if (!wifi_initialized) {
        pim_boot_stage(PIM_BOOT_WIFI_INIT);
        if (cyw43_arch_init_with_country(CYW43_COUNTRY('I', 'N', 0)) != 0) {
            snprintf(wifi_error, sizeof(wifi_error), "Radio failed to start");
            return false;
        }
        cyw43_arch_enable_sta_mode();
        wifi_initialized = true;
        pim_boot_stage(PIM_BOOT_RUNNING);
        touch();
    }
    return true;
}

static void trim_controls(char *dest, size_t size, const char *source) {
    size_t length = 0;
    for (; *source != '\0' && length + 1 < size; source++)
        if ((unsigned char)*source >= 0x20 && (unsigned char)*source != 0x7f) dest[length++] = *source;
    dest[length] = '\0';
}

bool pim_wifi_connect(const char *raw_ssid, const char *raw_password, char *address, size_t address_size) {
    uint32_t number;
    char ssid[40], password[72];
    if (raw_ssid == NULL || raw_password == NULL) return false;
    trim_controls(ssid, sizeof(ssid), raw_ssid);
    trim_controls(password, sizeof(password), raw_password);
    if (ssid[0] == '\0' || password[0] == '\0') return false;
    wifi_error[0] = '\0';
    if (!wifi_init()) return false;
    static const uint32_t auth_modes[] = {CYW43_AUTH_WPA2_MIXED_PSK, CYW43_AUTH_WPA2_AES_PSK,
                                          CYW43_AUTH_WPA3_WPA2_AES_PSK};
    int joined = -1, codes[3] = {0, 0, 0};
    for (size_t i = 0; i < sizeof(auth_modes) / sizeof(auth_modes[0]) && joined != 0; i++) {
        static const char *const stages[] = {"Joining (mixed)", "Joining (AES)", "Joining (WPA3)"};
        progress(stages[i]);
        RADIO_BEGIN();
        cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
        RADIO_END();
        pim_boot_stage(PIM_BOOT_WIFI_JOIN);
#if PIM_TAILSCALE
        /* The watchdog stays on: this call blocks for up to fifteen seconds, and
           the idle task feeds the watchdog while it does. Switching it off here
           is what turned a failure inside the radio driver into a device frozen
           with no way to say so. */
        joined = codes[i] = cyw43_arch_wifi_connect_timeout_ms(ssid, password, auth_modes[i], 15000);
#else
        /* The polled build has no idle task to feed it, and this call does not
           return for fifteen seconds. */
        watchdog_disable();
        joined = codes[i] = cyw43_arch_wifi_connect_timeout_ms(ssid, password, auth_modes[i], 15000);
        watchdog_enable(8000, true);
#endif
        pim_boot_stage(PIM_BOOT_RUNNING);
    }
    if (joined != 0) {
        snprintf(wifi_error, sizeof(wifi_error), "Join failed: mixed %d, aes %d, wpa3 %d. ssid len %u, pw len %u, first %02x, last %02x/%02x",
                 codes[0], codes[1], codes[2], (unsigned)strlen(ssid), (unsigned)strlen(password), (unsigned char)password[0],
                 (unsigned char)raw_ssid[strlen(raw_ssid) ? strlen(raw_ssid) - 1 : 0],
                 (unsigned char)raw_password[strlen(raw_password) ? strlen(raw_password) - 1 : 0]);
        wifi_up = false;
        return false;
    }
    progress("Starting server");
    if (!start_server() || netif_default == NULL) {
        snprintf(wifi_error, sizeof(wifi_error), "Web server failed to start");
        wifi_up = false;
        return false;
    }
    progress("Making PIN");
    number = get_rand_32() % 10000;
    snprintf(web.pin, sizeof(web.pin), "%04u", (unsigned)number);
    web.failures = 0;
    wifi_up = true;
    touch();
    snprintf(wifi_address, sizeof(wifi_address), "%s", ip4addr_ntoa(netif_ip4_addr(netif_default)));
    snprintf(address, address_size, "%s", wifi_address);
    return true;
}

bool pim_wifi_connected(void) { return wifi_up; }
const char *pim_wifi_pin(void) { return web.pin; }
const char *pim_wifi_address(void) { return wifi_address; }

typedef struct {
    pim_wifi_ap_t *list;
    int max, count;
} scan_t;

static int scan_result(void *env, const cyw43_ev_scan_result_t *result) {
    scan_t *scan = env;
    char ssid[33];
    int found = -1;
    if (result == NULL || result->ssid_len == 0) return 0;
    memcpy(ssid, result->ssid, result->ssid_len);
    ssid[result->ssid_len] = '\0';
    for (int i = 0; i < scan->count; i++)
        if (strcmp(scan->list[i].ssid, ssid) == 0) found = i;
    if (found >= 0) {
        if (result->rssi > scan->list[found].rssi) scan->list[found].rssi = result->rssi;
    } else if (scan->count < scan->max) {
        strcpy(scan->list[scan->count].ssid, ssid);
        scan->list[scan->count++].rssi = result->rssi;
    }
    return 0;
}

int pim_wifi_scan(pim_wifi_ap_t *list, int max) {
    scan_t scan = {list, max, 0};
    cyw43_wifi_scan_options_t options;
    absolute_time_t end = make_timeout_time_ms(10000);
    int result;
    if (!wifi_init()) return -1;
    pim_boot_stage(PIM_BOOT_WIFI_SCAN);
    memset(&options, 0, sizeof(options));
    RADIO_BEGIN();
    result = cyw43_wifi_scan(&cyw43_state, &options, &scan, scan_result);
    RADIO_END();
    if (result != 0) {
        pim_boot_stage(PIM_BOOT_RUNNING);
        return -1;
    }
    for (;;) {
        bool active;
        RADIO_BEGIN();
        active = cyw43_wifi_scan_active(&cyw43_state);
        RADIO_END();
        if (!active || time_reached(end)) break;
        cyw43_arch_poll();              /* nothing to do in the threaded build */
        watchdog_update(); sleep_ms(1);
    }
    for (int i = 1; i < scan.count; i++)
        for (int j = i; j > 0 && list[j].rssi > list[j - 1].rssi; j--) {
            pim_wifi_ap_t swap = list[j];
            list[j] = list[j - 1];
            list[j - 1] = swap;
        }
    result = scan.count;
    pim_boot_stage(PIM_BOOT_RUNNING);
    return result;
}

typedef struct {
    ip_addr_t address;
    bool resolved, failed;
} lookup_t;

static void dns_done(const char *name, const ip_addr_t *address, void *arg) {
    lookup_t *lookup = arg;
    (void)name;
    if (address != NULL) {
        lookup->address = *address;
        lookup->resolved = true;
    } else {
        lookup->failed = true;
    }
}

static bool resolve(const char *host, ip_addr_t *address) {
    lookup_t lookup = {0};
    absolute_time_t end = make_timeout_time_ms(10000);
    err_t error;
    NET_BEGIN();
    error = dns_gethostbyname(host, &lookup.address, dns_done, &lookup);
    NET_END();
    if (error == ERR_OK) {
        *address = lookup.address;
        return true;
    }
    if (error != ERR_INPROGRESS) return false;
    while (!lookup.resolved && !lookup.failed && !time_reached(end)) {
        cyw43_arch_poll();
        watchdog_update(); sleep_ms(1);
    }
    if (!lookup.resolved) return false;
    *address = lookup.address;
    return true;
}

typedef struct {
    uint8_t packet[PIM_NTP_PACKET_SIZE];
    size_t length;
    bool received;
} ntp_t;

static void ntp_receive(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *address, u16_t port) {
    ntp_t *state = arg;
    (void)pcb;
    (void)address;
    (void)port;
    if (p->tot_len >= PIM_NTP_PACKET_SIZE) {
        pbuf_copy_partial(p, state->packet, PIM_NTP_PACKET_SIZE, 0);
        state->length = PIM_NTP_PACKET_SIZE;
        state->received = true;
    }
    pbuf_free(p);
}

bool pim_wifi_ntp(uint32_t *unix_seconds) {
    static const char *const hosts[] = {"pool.ntp.org", "time.google.com", "time.cloudflare.com"};
    ntp_t state = {0};
    struct udp_pcb *pcb;
    bool ok = false, resolved_any = false;
    wifi_error[0] = '\0';
    if (!wifi_up) {
        snprintf(wifi_error, sizeof(wifi_error), "Not connected to Wi-Fi");
        return false;
    }
    NET_BEGIN();
    if (ip_addr_isany(dns_getserver(0))) dns_setserver(0, &(ip_addr_t)IPADDR4_INIT_BYTES(8, 8, 8, 8));
    pcb = udp_new();
    if (pcb != NULL) udp_recv(pcb, ntp_receive, &state);
    NET_END();
    if (pcb == NULL) {
        snprintf(wifi_error, sizeof(wifi_error), "Out of memory");
        return false;
    }
    for (size_t h = 0; h < sizeof(hosts) / sizeof(hosts[0]) && !ok; h++) {
        ip_addr_t server;
        progress(hosts[h]);
        if (!resolve(hosts[h], &server)) continue;
        resolved_any = true;
        for (int attempt = 0; attempt < 3 && !ok; attempt++) {
            uint8_t packet[PIM_NTP_PACKET_SIZE];
            struct pbuf *p;
            absolute_time_t end = make_timeout_time_ms(3000);
            NET_BEGIN();
            p = pbuf_alloc(PBUF_TRANSPORT, PIM_NTP_PACKET_SIZE, PBUF_RAM);
            if (p != NULL) {
                pim_ntp_request(packet);
                memcpy(p->payload, packet, sizeof(packet));
                udp_sendto(pcb, p, &server, 123);
                pbuf_free(p);
            }
            NET_END();
            if (p == NULL) break;
            while (!state.received && !time_reached(end)) {
                cyw43_arch_poll();
                watchdog_update(); sleep_ms(1);
            }
            ok = state.received && pim_ntp_decode(state.packet, state.length, unix_seconds);
            state.received = false;
        }
    }
    NET_BEGIN();
    udp_remove(pcb);
    NET_END();
    if (!ok) {
        snprintf(wifi_error, sizeof(wifi_error), resolved_any ? "No reply from time servers (UDP 123 blocked?)"
                                                                : "DNS lookup failed");
    }
    return ok;
}

static void telnet_reset(void) {
    telnet = NULL;
    telnet_open_flag = false;
}

static err_t telnet_receive(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t error) {
    (void)arg;
    if (p == NULL || error != ERR_OK) {
        if (p != NULL) pbuf_free(p);
        tcp_arg(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_err(pcb, NULL);
        if (tcp_close(pcb) != ERR_OK) tcp_abort(pcb);
        telnet_reset();
        return ERR_OK;
    }
    /* Take all of it or none of it. Returning ERR_MEM without freeing tells lwIP
       to keep the data and offer it again, which leaves the window closed until
       the terminal has drained what it already has. Acknowledging bytes that
       were then dropped - which is what this used to do when the ring filled -
       loses them silently and corrupts the screen mid-transfer. */
    if (p->tot_len > TELNET_RX_MAX - telnet_count) return ERR_MEM;
    for (struct pbuf *q = p; q != NULL; q = q->next) {
        const uint8_t *data = q->payload;
        for (u16_t i = 0; i < q->len; i++) {
            telnet_rx[(telnet_head + telnet_count++) % TELNET_RX_MAX] = data[i];
        }
    }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void telnet_error(void *arg, err_t error) {
    (void)arg;
    (void)error;
    telnet_reset();
}

typedef struct { bool done; err_t result; } connect_t;

static err_t telnet_connected(void *arg, struct tcp_pcb *pcb, err_t error) {
    connect_t *state = arg;
    (void)pcb;
    state->result = error;
    state->done = true;
    return ERR_OK;
}

#if PIM_TAILSCALE
/* ---- reaching a host over the tailnet ----

   A Tailscale address is not routable over the local network: packets for it
   have to go through the tunnel, which the client carries its own TCP over. So
   the terminal's transport dials either lwIP or the tunnel depending on where
   the host is, and nothing above here - telnet, SSH, the terminal screen - has
   to know which. That is what lets a saved connection to a tailnet machine work
   with no relay on another computer.

   Tailscale hands out addresses from 100.64.0.0/10. A name is put to the
   control plane's DNS, so an alias like "pve1" reaches the right machine even
   though the local network has never heard of it. */
static microlink_tcp_socket_t *tunnel;

static bool tailnet_target(const char *host, uint32_t *out) {
    microlink_t *ml = pim_ts_client();
    uint32_t ip;
    if (ml == NULL) return false;              /* no client, or not up yet */
    ip = microlink_parse_ip(host);
    if (ip == 0) ip = microlink_resolve(ml, host);
    if ((ip & 0xffc00000u) != 0x64400000u) return false;
    *out = ip;
    return true;
}

static void tunnel_drop(void) {
    if (tunnel == NULL) return;
    microlink_tcp_close(tunnel);
    tunnel = NULL;
    telnet_open_flag = false;
}
#endif

bool pim_telnet_open(const char *host, uint16_t port) {
    ip_addr_t address;
    connect_t state = {false, ERR_OK};
    absolute_time_t end;
    if (!wifi_up || host == NULL || host[0] == '\0' || port == 0) return false;
    pim_telnet_close();
    if (telnet_rx == NULL) telnet_rx = malloc(TELNET_RX_MAX);
    if (telnet_rx == NULL) return false;
#if PIM_TAILSCALE
    {
        uint32_t vpn_ip;
        if (tailnet_target(host, &vpn_ip)) {
            tunnel = microlink_tcp_connect(pim_ts_client(), vpn_ip, port, 15000);
            if (tunnel == NULL) return false;
            telnet_open_flag = true;
            return true;
        }
    }
#endif
    if (!resolve(host, &address)) return false;
    NET_BEGIN();
    telnet = tcp_new_ip_type(IP_GET_TYPE(&address));
    if (telnet != NULL) {
        telnet_head = telnet_count = 0;
        tcp_arg(telnet, &state);
        tcp_err(telnet, telnet_error);
        if (tcp_connect(telnet, &address, port, telnet_connected) != ERR_OK) {
            tcp_abort(telnet);
            telnet_reset();
        }
    }
    NET_END();
    if (telnet == NULL) return false;
    end = make_timeout_time_ms(15000);
    while (!state.done && telnet != NULL && !time_reached(end)) {
        cyw43_arch_poll();
        watchdog_update(); sleep_ms(1);
    }
    if (telnet == NULL) return false;
    if (!state.done || state.result != ERR_OK) {
        NET_BEGIN();
        if (telnet != NULL) {
            tcp_arg(telnet, NULL);
            tcp_err(telnet, NULL);
            tcp_abort(telnet);
            telnet_reset();
        }
        NET_END();
        return false;
    }
    NET_BEGIN();
    tcp_arg(telnet, NULL);
    tcp_recv(telnet, telnet_receive);
    NET_END();
    telnet_open_flag = true;
    return true;
}

void pim_telnet_close(void) {
#if PIM_TAILSCALE
    tunnel_drop();
#endif
    NET_BEGIN();
    if (telnet != NULL) {
        struct tcp_pcb *pcb = telnet;
        tcp_arg(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_err(pcb, NULL);
        if (tcp_close(pcb) != ERR_OK) tcp_abort(pcb);
    }
    telnet_reset();
    NET_END();
    telnet_head = telnet_count = 0;
    free(telnet_rx);
    telnet_rx = NULL;
}

bool pim_telnet_is_open(void) { return telnet_open_flag; }

size_t pim_telnet_read(uint8_t *data, size_t max) {
    size_t count = 0;
#if PIM_TAILSCALE
    if (tunnel != NULL) {
        /* A 1 ms wait makes this a poll: the terminal calls it from the UI
           loop and must not stall there. */
        int got = microlink_tcp_recv(tunnel, data, max, 1);
        if (got < 0) { tunnel_drop(); return 0; }   /* the far end went away */
        return got > 0 ? (size_t)got : 0;
    }
#endif
    /* The ring is filled from an lwIP callback, which in the threaded build runs
       on the tcpip thread rather than this one; the core lock keeps the two off
       each other's indices. */
    NET_BEGIN();
    while (count < max && telnet_count > 0) {
        data[count++] = telnet_rx[telnet_head];
        telnet_head = (telnet_head + 1) % TELNET_RX_MAX;
        telnet_count--;
    }
    NET_END();
    return count;
}

bool pim_telnet_write(const uint8_t *data, size_t length) {
    bool ok = false;
#if PIM_TAILSCALE
    if (tunnel != NULL) {
        if (microlink_tcp_send(tunnel, data, length) == ESP_OK) return true;
        tunnel_drop();
        return false;
    }
#endif
    NET_BEGIN();
    if (telnet != NULL && length <= tcp_sndbuf(telnet) &&
        tcp_write(telnet, data, (u16_t)length, TCP_WRITE_FLAG_COPY) == ERR_OK) {
        ok = tcp_output(telnet) == ERR_OK;
    }
    NET_END();
    return ok;
}

void pim_wifi_web_stop(void) {
    if (client != NULL) finish_client(client);
    NET_BEGIN();
    if (listener != NULL) { tcp_close(listener); listener = NULL; }
    NET_END();
    free_server_buffers();
}

bool pim_wifi_web_start(void) {
    if (!wifi_up) return false;
    return start_server();
}

/* Powers the radio down; it is the largest battery draw once a network is joined. */
void pim_wifi_off(void) {
    if (!wifi_initialized) return;
    pim_telnet_close();
    if (client != NULL) finish_client(client);
    NET_BEGIN();
    if (listener != NULL) { tcp_close(listener); listener = NULL; }
    NET_END();
    free_server_buffers();
    cyw43_arch_deinit();
    wifi_initialized = false;
    wifi_up = false;
    wifi_address[0] = '\0';
}

void pim_wifi_poll(void) {
    if (!wifi_initialized) return;
    cyw43_arch_poll();
    if (telnet_open_flag) touch();
    else if (client == NULL && to_ms_since_boot(get_absolute_time()) - wifi_activity_ms > WIFI_IDLE_OFF_MS) pim_wifi_off();
}

/* ---- entropy for SSH ----

   The SSH client's ephemeral exchange keys and packet padding come from here.
   pim_ssh_random is weak in the core library, where it reads /dev/urandom for
   the host tests; on the device it has to be the hardware source. */
bool pim_ssh_random(void *buffer, size_t length) {
    uint8_t *out = buffer;
    while (length > 0) {
        uint64_t word = get_rand_64();
        size_t chunk = length < sizeof(word) ? length : sizeof(word);
        memcpy(out, &word, chunk);
        out += chunk;
        length -= chunk;
    }
    return true;
}
