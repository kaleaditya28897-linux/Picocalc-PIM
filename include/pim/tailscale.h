#ifndef PIM_TAILSCALE_H
#define PIM_TAILSCALE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Joining a Tailscale network.

   Two halves live here. The configuration - what is needed to register with a
   control plane - is plain file handling and is tested on the build host. The
   client itself is only present in the firmware built with
   PIM_ENABLE_TAILSCALE; everything else talks to it through the small interface
   at the bottom, which reports "not in this build" when it is absent and is
   simulated in the headless UI tests. That way the screen is the same code
   everywhere and gets tested, without the UI knowing anything about WireGuard.

   The auth key is kept on the SD card rather than compiled in: a key baked into
   the firmware would travel in the UF2 and in every backup of it. */

#define PIM_TS_AUTHKEY_MAX 128
#define PIM_TS_NAME_MAX 32
#define PIM_TS_ADDRESS_MAX 40          /* "100.x.y.z", with room for a v6 one */
#define PIM_TS_MESSAGE_MAX 64

typedef struct {
    char authkey[PIM_TS_AUTHKEY_MAX + 1];
    char name[PIM_TS_NAME_MAX + 1];     /* the name on the tailnet */
    uint16_t derp_region;               /* 0 lets the client keep its default */
} pim_ts_config_t;

void pim_ts_config_init(pim_ts_config_t *config);
/* True when the file held a usable configuration. */
bool pim_ts_config_load(const char *path, pim_ts_config_t *config);
bool pim_ts_config_save(const char *path, const pim_ts_config_t *config);
/* An auth key has a recognisable shape; a typo is worth catching before a
   registration attempt fails somewhere far away. */
bool pim_ts_authkey_ok(const char *key);
/* "tskey-auth-kXXXX...": enough to recognise, with the secret part hidden, for
   showing on screen. */
void pim_ts_authkey_summary(const char *key, char *out, size_t out_size);

/* ---- the client, as the rest of the firmware sees it ---- */

typedef enum {
    PIM_TS_OFF = 0,
    PIM_TS_STARTING,        /* bringing up the interface and reaching the control plane */
    PIM_TS_REGISTERING,     /* presenting the auth key */
    PIM_TS_RUNNING,         /* on the tailnet, with an address */
    PIM_TS_FAILED
} pim_ts_state_t;

/* False in a firmware built without the client, so a screen can say so rather
   than offering something that cannot happen. */
bool pim_ts_available(void);
bool pim_ts_start(const pim_ts_config_t *config);
void pim_ts_stop(void);
pim_ts_state_t pim_ts_state(void);
/* The tailnet address once there is one, otherwise empty. */
const char *pim_ts_address(void);
/* One short line for the screen: what it is doing, or why it stopped. */
const char *pim_ts_message(void);
/* Called from the main loop; cheap when the client is off. */
void pim_ts_poll(void);
/* When the control plane wants an interactive login rather than a key, the
   address to open; empty otherwise. */
const char *pim_ts_login_url(void);
/* True for an address Tailscale hands out - 100.64.0.0/10. Such an address is
   not routable over the local network, so it only means anything through the
   tunnel; it is also how a saved connection is recognised as a tailnet one. */
bool pim_ts_is_tailnet_address(const char *address);
/* Bytes of heap left, or 0 where that cannot be told. The client needs a large
   contiguous run of memory to parse a network map, so when it fails to start
   this is the number that explains why. */
size_t pim_ts_free_memory(void);

#endif
