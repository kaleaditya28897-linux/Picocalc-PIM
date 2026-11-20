/* The Tailscale client, as the rest of the firmware sees it.

   Everything specific to the vendored client in third_party/microlink is here,
   behind the small interface in pim/tailscale.h, so no screen and no transport
   has to know about WireGuard, DERP or control planes. The weak definitions in
   src/core/tsconfig.c answer for a build without the client; these override
   them.

   The client runs its own FreeRTOS tasks once started, so there is nothing to
   drive from the main loop: pim_ts_poll only takes a snapshot of what it
   reports, which is what the screen reads. */

#include "pim/tailscale.h"
#include "pim/wifi.h"

#include <stdio.h>
#include <string.h>

#include <malloc.h>

#include "pim/boottrace.h"

#include "microlink.h"
#include "esp_compat/nvs_flash.h"

static microlink_t *client;
static pim_ts_state_t cached_state = PIM_TS_OFF;
static char cached_address[PIM_TS_ADDRESS_MAX + 1];
static char cached_message[PIM_TS_MESSAGE_MAX + 1];
/* A login URL is long and worth keeping whole: the screen shows it in pieces. */
static char login_url[MICROLINK_AUTH_URL_MAX];
/* The configuration has to outlive the call: microlink keeps the pointers. */
static pim_ts_config_t live;

bool pim_ts_available(void) { return true; }

/* The home relay region, read from the card by pim_ts_start. Upstream asks for
   this through CONFIG_ML_DERP_REGION, so it can be answered at run time. 6 is
   Mumbai, which beats upstream's Dallas for anyone near this device. */
unsigned pim_ts_derp_region(void) {
    return live.derp_region != 0 ? live.derp_region : 6u;
}

/* Everything between the end of the static data and the stack is the heap, and
   FreeRTOS is configured to use the C library's allocator, so what newlib has
   handed out is what is gone. */
size_t pim_ts_free_memory(void) {
    extern char __StackLimit, __bss_end__;
    size_t total = (size_t)(&__StackLimit - &__bss_end__);
    struct mallinfo info = mallinfo();
    return total > info.uordblks ? total - (size_t)info.uordblks : 0;
}

static void say(const char *text) {
    snprintf(cached_message, sizeof(cached_message), "%s", text);
}

bool pim_ts_start(const pim_ts_config_t *config) {
    microlink_config_t ml;
    if (client != NULL) return true;            /* already up or coming up */

    live = *config;
    cached_address[0] = '\0';
    login_url[0] = '\0';

    if (nvs_flash_init() != ESP_OK) {
        cached_state = PIM_TS_FAILED;
        say("Could not open the card to store this device's keys.");
        return false;
    }

    memset(&ml, 0, sizeof(ml));
    /* No key is not an error: the control plane then answers with a login URL,
       which is the friendlier way to add a device to a tailnet. */
    ml.auth_key = live.authkey[0] != '\0' ? live.authkey : NULL;
    ml.device_name = live.name[0] != '\0' ? live.name : "picocalc";
    ml.enable_derp = true;
    ml.enable_stun = true;
    ml.enable_disco = true;
    /* The peer table costs RAM per entry and this tailnet is not large. */
    ml.max_peers = 16;
    ml.streaming_map_fetch = true;

    pim_boot_stage(PIM_BOOT_TS_START);
    printf("tailscale: %u KB free before init\n", (unsigned)(pim_ts_free_memory() / 1024));
    client = microlink_init(&ml);
    printf("tailscale: %u KB free after init\n", (unsigned)(pim_ts_free_memory() / 1024));
    if (client == NULL) {
        cached_state = PIM_TS_FAILED;
        snprintf(cached_message, sizeof(cached_message),
                 "Not enough memory: %u KB free.", (unsigned)(pim_ts_free_memory() / 1024));
        pim_boot_stage(PIM_BOOT_RUNNING);
        return false;
    }
    printf("tailscale: starting, %u KB free\n", (unsigned)(pim_ts_free_memory() / 1024));
    if (microlink_start(client) != ESP_OK) {
        microlink_destroy(client);
        client = NULL;
        cached_state = PIM_TS_FAILED;
        say("Tailscale would not start.");
        pim_boot_stage(PIM_BOOT_RUNNING);
        return false;
    }
    cached_state = PIM_TS_STARTING;
    pim_boot_stage(PIM_BOOT_TS_RUN);
    say("Starting...");
    return true;
}

void pim_ts_stop(void) {
    if (client == NULL) return;
    /* First, while the client still exists: the tunnel socket is the client's,
       and a session left holding it would read freed memory afterwards. */
    pim_wifi_tunnel_drop();
    microlink_stop(client);
    microlink_destroy(client);
    client = NULL;
    pim_boot_stage(PIM_BOOT_RUNNING);
    cached_state = PIM_TS_OFF;
    cached_address[0] = '\0';
    login_url[0] = '\0';
    say("Off.");
}

void pim_ts_poll(void) {
    microlink_state_t state;
    uint32_t ip;
    if (client == NULL) return;

    state = microlink_get_state(client);
    ip = microlink_get_vpn_ip(client);
    if (ip != 0) microlink_ip_to_str(ip, cached_address);

    switch (state) {
        case ML_STATE_IDLE:
            cached_state = PIM_TS_STARTING;
            say("Waiting to start...");
            break;
        case ML_STATE_WIFI_WAIT:
            cached_state = PIM_TS_STARTING;
            say("Waiting for Wi-Fi.");
            break;
        case ML_STATE_CONNECTING:
            cached_state = PIM_TS_STARTING;
            say("Reaching the control plane...");
            break;
        case ML_STATE_REGISTERING:
            cached_state = PIM_TS_REGISTERING;
            say("Registering this device...");
            break;
        case ML_STATE_CONNECTED:
            cached_state = PIM_TS_RUNNING;
            pim_boot_stage(PIM_BOOT_RUNNING);   /* it is up; stop blaming the start */
            snprintf(cached_message, sizeof(cached_message), "On the tailnet, %d peers",
                     microlink_get_peer_count(client));
            break;
        case ML_STATE_RECONNECTING:
            cached_state = PIM_TS_STARTING;
            say("Lost the control plane, trying again...");
            break;
        case ML_STATE_NEEDS_LOGIN:
            cached_state = PIM_TS_REGISTERING;
            if (login_url[0] == '\0') {
                microlink_get_auth_url(client, login_url, sizeof(login_url));
            }
            say("Open the login address below, then wait.");
            break;
        case ML_STATE_AUTH_FAILED:
            cached_state = PIM_TS_FAILED;
            /* The client keeps retrying, but it will not recover without a new
               key, so say the thing that actually needs doing. */
            say("The auth key was refused. It may be used up or expired.");
            break;
        case ML_STATE_ERROR:
        default:
            cached_state = PIM_TS_FAILED;
            say("Tailscale stopped with an error.");
            break;
    }
}

pim_ts_state_t pim_ts_state(void) { return cached_state; }

const char *pim_ts_address(void) { return cached_address; }

const char *pim_ts_message(void) {
    return cached_message[0] != '\0' ? cached_message : "Off.";
}

/* ---- for the terminal's transport ----

   Declared in the firmware-private header rather than pim/tailscale.h: only
   wifi.c needs them, and only in this build. */

const char *pim_ts_login_url(void) { return login_url; }

microlink_t *pim_ts_client(void) {
    return cached_state == PIM_TS_RUNNING ? client : NULL;
}
