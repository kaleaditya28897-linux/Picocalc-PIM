/**
 * @file ml_tcp.c
 * @brief MicroLink v2 TCP Socket API
 *
 * Provides TCP connections over the Tailscale VPN tunnel.
 * Uses standard BSD sockets routed through the WireGuard netif.
 *
 * lwIP routes 100.64.0.0/10 traffic through the WG netif automatically
 * (set up in ml_wg_mgr.c). This module triggers the WG handshake,
 * waits for tunnel establishment, then lets standard TCP through.
 *
 * Runs on the application's task. The peer table and WireGuard handshake
 * state belong to the wg_mgr task, so the handshake (and CallMeMaybe) is
 * requested through its queue (ML_PEER_REQ_HANDSHAKE), and the tunnel state
 * is read from the session table wg_mgr publishes (ml_wg_mgr_peer_is_up).
 */

#include "microlink_internal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>

static const char *TAG = "ml_tcp";

/* Ask wg_mgr for a handshake + CallMeMaybe toward dest_ip */
static void tcp_request_handshake(microlink_t *ml, uint32_t dest_ip) {
    esp_err_t err = ml_wg_mgr_post_request(ml, ML_PEER_REQ_HANDSHAKE, dest_ip);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not queue WG handshake request (%s)", esp_err_to_name(err));
    }
}

/* ============================================================================
 * Internal Types
 * ========================================================================== */

struct microlink_tcp_socket {
    microlink_t *ml;
    int fd;
    uint32_t peer_ip;
    uint16_t peer_port;
    bool connected;
};

/* --- PicoDeck change; see PROVENANCE.md ---

   A bounded connect. SO_SNDTIMEO was set above in the expectation that it
   limits connect(), and on lwIP it does not: netconn_connect waits on its
   completion semaphore with a timeout of zero - wait for ever - and
   send_timeout is consulted only when closing. So a peer whose path silently
   drops packets held this call for TCP's whole retransmission schedule, well
   over a minute. That call is made from the task that draws the screen, so the
   whole device appeared to freeze instead of the connection simply failing.

   Non-blocking connect, then select() with the deadline the caller asked for,
   then back to blocking for the session itself (whose reads and writes do
   honour SO_RCVTIMEO and SO_SNDTIMEO). Returns 0 on success, -1 otherwise.

   F_GETFL, F_SETFL, O_NONBLOCK and select come from lwip/sockets.h below, not
   from the newlib headers: lwIP guards its own definitions with #ifndef, so
   including <fcntl.h> first gave this file O_NONBLOCK 0x4000 while lwIP was
   compiled with 1 - and its F_SETFL, masking against its own value, refused the
   unknown bit with ENOSYS. Every connection failed with "errno=38". */
static int pim_connect_bounded(int fd, const struct sockaddr *addr, socklen_t len,
                               uint32_t timeout_ms)
{
    int on = 1, off = 0;
    int result;

    /* FIONBIO through lwip_ioctl, not O_NONBLOCK through fcntl. The fcntl route
       depends on O_NONBLOCK meaning the same thing here as inside lwIP, and it
       did not: newlib's value reached this file through some header, lwIP's
       F_SETFL rejected the unknown bit with ENOSYS, and every connection on the
       device failed with errno 38. FIONBIO is lwIP's own request and newlib
       does not define it. */
    if (lwip_ioctl(fd, FIONBIO, &on) != 0) {
        /* Never let the bounding itself cost the connection: fall back to the
           plain blocking connect, which works and is merely unbounded. */
        ESP_LOGW(TAG, "FIONBIO refused (errno=%d); connecting unbounded", errno);
        return connect(fd, addr, len) == 0 ? 0 : -1;
    }

    result = lwip_connect(fd, addr, len);
    if (result != 0 && errno == EINPROGRESS) {
        fd_set writable, failed;
        struct timeval tv;
        if (timeout_ms == 0) timeout_ms = 15000;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        FD_ZERO(&writable);
        FD_SET(fd, &writable);
        FD_ZERO(&failed);
        FD_SET(fd, &failed);
        result = lwip_select(fd + 1, NULL, &writable, &failed, &tv);
        if (result == 0) {
            ESP_LOGW(TAG, "connect timed out after %u ms", (unsigned)timeout_ms);
            errno = ETIMEDOUT;
            result = -1;
        } else if (result > 0) {
            /* Writable can also mean "finished, with an error". */
            int so_error = 0;
            socklen_t so_len = sizeof(so_error);
            if (lwip_getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &so_len) < 0) so_error = EIO;
            if (so_error != 0) {
                errno = so_error;
                result = -1;
            } else {
                result = 0;
            }
        }
    }

    (void)lwip_ioctl(fd, FIONBIO, &off);     /* the session wants blocking */
    return result == 0 ? 0 : -1;
}

/* ============================================================================
 * Public API
 * ========================================================================== */

microlink_tcp_socket_t *microlink_tcp_connect(microlink_t *ml, uint32_t dest_ip,
                                                uint16_t dest_port,
                                                uint32_t timeout_ms) {
    if (!ml || dest_ip == 0 || dest_port == 0) {
        ESP_LOGE(TAG, "Invalid args");
        return NULL;
    }
    if (ml->state != ML_STATE_CONNECTED) {
        ESP_LOGE(TAG, "Not connected to Tailscale (state=%d)", ml->state);
        return NULL;
    }

    char ip_str[16];
    microlink_ip_to_str(dest_ip, ip_str);
    ESP_LOGI(TAG, "TCP connect to %s:%u (timeout=%lums)", ip_str, dest_port,
             (unsigned long)timeout_ms);

    /* Trigger WG handshake + DISCO to wake up the peer connection.
     * The tunnel may already be up if there's been recent traffic (wg_mgr
     * then leaves the session alone). */
    tcp_request_handshake(ml, dest_ip);

    /* Wait for WG tunnel to establish with this peer.
     * Poll ml_wg_mgr_peer_is_up() — once the WG handshake completes,
     * lwIP can route TCP through the tunnel. Without a valid session,
     * connect() will fail with EHOSTUNREACH. */
    uint32_t wait_ms = 0;
    uint32_t max_wait = timeout_ms > 0 ? timeout_ms : 15000;
    bool tunnel_up = ml_wg_mgr_peer_is_up(ml, dest_ip);

    while (!tunnel_up && wait_ms < max_wait) {
        vTaskDelay(pdMS_TO_TICKS(500));
        wait_ms += 500;

        tunnel_up = ml_wg_mgr_peer_is_up(ml, dest_ip);

        /* Re-trigger handshake every 5s in case the first was dropped */
        if (!tunnel_up && (wait_ms % 5000) == 0) {
            ESP_LOGI(TAG, "WG tunnel not up after %lums, re-triggering handshake",
                     (unsigned long)wait_ms);
            tcp_request_handshake(ml, dest_ip);
        }
    }

    if (tunnel_up) {
        ESP_LOGI(TAG, "WG tunnel up after %lums", (unsigned long)wait_ms);
    } else {
        ESP_LOGW(TAG, "WG tunnel not up after %lums, attempting connect anyway",
                 (unsigned long)max_wait);
    }

    /* Create TCP socket */
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        ESP_LOGE(TAG, "socket() failed: %d", errno);
        return NULL;
    }

    /* Bind to our VPN IP so lwIP routes through the WG netif.
     * Without this, the PPP default route may catch 100.x.x.x traffic
     * and send it out the cellular modem raw (unroutable). */
    if (ml->vpn_ip != 0) {
        struct sockaddr_in src = {
            .sin_family = AF_INET,
            .sin_port = 0,  /* ephemeral */
            .sin_addr.s_addr = htonl(ml->vpn_ip),
        };
        if (bind(fd, (struct sockaddr *)&src, sizeof(src)) != 0) {
            ESP_LOGW(TAG, "bind() to VPN IP failed: errno=%d (continuing)", errno);
        } else {
            char src_str[16];
            microlink_ip_to_str(ml->vpn_ip, src_str);
            ESP_LOGI(TAG, "TCP socket bound to VPN IP %s", src_str);
        }
    }

    /* Set connect/send timeout */
    uint32_t remaining = (timeout_ms > wait_ms) ? (timeout_ms - wait_ms) : 10000;
    struct timeval tv = {
        .tv_sec = remaining / 1000,
        .tv_usec = (remaining % 1000) * 1000,
    };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* Set recv timeout */
    struct timeval rtv = { .tv_sec = 10, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));

    /* TCP keepalive for long-lived connections over VPN */
    int keepalive = 1;
    int keepidle = 30;
    int keepintvl = 10;
    int keepcnt = 3;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));

    /* Disable Nagle for low-latency sends */
    int nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    /* Connect */
    struct sockaddr_in dest = {
        .sin_family = AF_INET,
        .sin_port = htons(dest_port),
    };
    /* Convert host byte order IP to network byte order */
    dest.sin_addr.s_addr = htonl(dest_ip);

    int64_t t_start = esp_timer_get_time();

    if (pim_connect_bounded(fd, (struct sockaddr *)&dest, sizeof(dest), remaining) != 0) {
        int err = errno;
        ESP_LOGE(TAG, "connect() to %s:%u failed: errno=%d", ip_str, dest_port, err);

        /* If tunnel wasn't ready, retry once after triggering handshake again */
        if (err == EHOSTUNREACH || err == ENETUNREACH || err == ETIMEDOUT) {
            close(fd);
            ESP_LOGI(TAG, "Retrying after re-triggering WG handshake...");
            tcp_request_handshake(ml, dest_ip);
            vTaskDelay(pdMS_TO_TICKS(3000));

            fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (fd < 0) return NULL;

            /* Bind to VPN IP for routing */
            if (ml->vpn_ip != 0) {
                struct sockaddr_in retry_src = {
                    .sin_family = AF_INET,
                    .sin_port = 0,
                    .sin_addr.s_addr = htonl(ml->vpn_ip),
                };
                bind(fd, (struct sockaddr *)&retry_src, sizeof(retry_src));
            }

            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));
            setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

            if (pim_connect_bounded(fd, (struct sockaddr *)&dest, sizeof(dest), remaining) != 0) {
                ESP_LOGE(TAG, "Retry connect() failed: errno=%d", errno);
                close(fd);
                return NULL;
            }
        } else {
            close(fd);
            return NULL;
        }
    }

    int64_t t_end = esp_timer_get_time();
    ESP_LOGI(TAG, "TCP connected to %s:%u (%ld ms)", ip_str, dest_port,
             (long)((t_end - t_start) / 1000));

    /* Allocate handle */
    microlink_tcp_socket_t *sock = calloc(1, sizeof(microlink_tcp_socket_t));
    if (!sock) {
        close(fd);
        return NULL;
    }
    sock->ml = ml;
    sock->fd = fd;
    sock->peer_ip = dest_ip;
    sock->peer_port = dest_port;
    sock->connected = true;

    return sock;
}

esp_err_t microlink_tcp_send(microlink_tcp_socket_t *sock, const void *data, size_t len) {
    if (!sock || !sock->connected || sock->fd < 0) return ESP_ERR_INVALID_STATE;
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;

    size_t sent = 0;
    while (sent < len) {
        int n = send(sock->fd, (const uint8_t *)data + sent, len - sent, 0);
        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            ESP_LOGE(TAG, "TCP send failed: n=%d errno=%d (sent %d/%d)",
                     n, errno, (int)sent, (int)len);
            sock->connected = false;
            return ESP_FAIL;
        }
        sent += n;
    }

    return ESP_OK;
}

int microlink_tcp_recv(microlink_tcp_socket_t *sock, void *buffer, size_t len,
                        uint32_t timeout_ms) {
    if (!sock || !sock->connected || sock->fd < 0) return -1;
    if (!buffer || len == 0) return -1;

    if (timeout_ms > 0) {
        struct timeval tv = {
            .tv_sec = timeout_ms / 1000,
            .tv_usec = (timeout_ms % 1000) * 1000,
        };
        setsockopt(sock->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    int n = recv(sock->fd, buffer, len, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;  /* Timeout, no data */
        }
        ESP_LOGE(TAG, "TCP recv failed: errno=%d", errno);
        sock->connected = false;
        return -1;
    }
    if (n == 0) {
        /* Peer closed connection */
        ESP_LOGI(TAG, "TCP peer closed connection");
        sock->connected = false;
        return -1;
    }

    return n;
}

bool microlink_tcp_is_connected(const microlink_tcp_socket_t *sock) {
    return sock && sock->connected && sock->fd >= 0;
}

void microlink_tcp_close(microlink_tcp_socket_t *sock) {
    if (!sock) return;

    if (sock->fd >= 0) {
        /* Graceful shutdown */
        shutdown(sock->fd, SHUT_RDWR);
        close(sock->fd);
        sock->fd = -1;
    }

    char ip_str[16];
    microlink_ip_to_str(sock->peer_ip, ip_str);
    ESP_LOGI(TAG, "TCP socket to %s:%u closed", ip_str, sock->peer_port);

    sock->connected = false;
    free(sock);
}
