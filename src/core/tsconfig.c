#include "pim/tailscale.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* /PIM/tailscale.cfg, one setting per line:
 *
 *   authkey=tskey-auth-k...        what registers this device with the tailnet
 *   name=picocalc                  the name it appears under
 *   derp=9                         preferred relay region, 0 to leave it alone
 *   control=https://headscale...   only for a self-hosted control plane
 *   verifyderp=0                   whether to check relay certificates
 */

#define DEFAULT_NAME "picocalc"

void pim_ts_config_init(pim_ts_config_t *config) {
    memset(config, 0, sizeof(*config));
    snprintf(config->name, sizeof(config->name), "%s", DEFAULT_NAME);
}

/* A key is "tskey-" then a kind, then a secret. Only the shape is checked:
   whether the control plane likes it is its business. */
bool pim_ts_authkey_ok(const char *key) {
    size_t length = key != NULL ? strlen(key) : 0;
    size_t i;
    if (length < 20 || length > PIM_TS_AUTHKEY_MAX) return false;
    if (strncmp(key, "tskey-", 6) != 0) return false;
    for (i = 0; i < length; i++) {
        char c = key[i];
        bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!plain) return false;
    }
    return true;
}

void pim_ts_authkey_summary(const char *key, char *out, size_t out_size) {
    if (out_size == 0) return;
    if (!pim_ts_authkey_ok(key)) {
        snprintf(out, out_size, "no key on the card");
        return;
    }
    /* Enough to tell one key from another without putting the secret on a
       screen somebody could be looking over. */
    snprintf(out, out_size, "%.16s... (%u characters)", key, (unsigned)strlen(key));
}

static void copy_field(char *out, size_t size, const char *value) {
    size_t length = strlen(value);
    if (length >= size) length = size - 1;
    memcpy(out, value, length);
    out[length] = '\0';
}

bool pim_ts_config_load(const char *path, pim_ts_config_t *config) {
    char line[256];
    FILE *file;
    pim_ts_config_init(config);
    file = fopen(path, "rb");
    if (file == NULL) return false;
    while (fgets(line, sizeof(line), file) != NULL) {
        size_t length = strlen(line);
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[--length] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;
        if (strncmp(line, "authkey=", 8) == 0) {
            if (pim_ts_authkey_ok(line + 8)) copy_field(config->authkey, sizeof(config->authkey), line + 8);
        } else if (strncmp(line, "name=", 5) == 0 && line[5] != '\0') {
            copy_field(config->name, sizeof(config->name), line + 5);
        } else if (strncmp(line, "control=", 8) == 0) {
            copy_field(config->control, sizeof(config->control), line + 8);
        } else if (strncmp(line, "derp=", 5) == 0) {
            unsigned long value = strtoul(line + 5, NULL, 10);
            if (value <= 65535u) config->derp_region = (uint16_t)value;
        } else if (strncmp(line, "verifyderp=", 11) == 0) {
            config->verify_derp_tls = line[11] == '1';
        }
    }
    fclose(file);
    /* Without a key there is nothing to register with, so this is not a usable
       configuration even though the file parsed. */
    return config->authkey[0] != '\0';
}

bool pim_ts_config_save(const char *path, const pim_ts_config_t *config) {
    char temporary[160];
    FILE *file;
    bool ok;
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    file = fopen(temporary, "wb");
    if (file == NULL) return false;
    ok = fprintf(file, "# PicoDeck Tailscale settings. The auth key is a secret.\n") > 0;
    if (ok && config->authkey[0] != '\0') ok = fprintf(file, "authkey=%s\n", config->authkey) > 0;
    if (ok) ok = fprintf(file, "name=%s\n", config->name) > 0;
    if (ok && config->control[0] != '\0') ok = fprintf(file, "control=%s\n", config->control) > 0;
    if (ok) ok = fprintf(file, "derp=%u\n", config->derp_region) > 0;
    if (ok) ok = fprintf(file, "verifyderp=%d\n", config->verify_derp_tls ? 1 : 0) > 0;
    if (fclose(file) != 0 || !ok) { remove(temporary); return false; }
    remove(path);
    return rename(temporary, path) == 0;
}

/* Tailscale allocates from 100.64.0.0/10, the shared address space set aside
   for carrier NAT. Only a dotted quad counts here: a name has to be asked of
   the tailnet's own DNS, which needs the client, and that happens when the
   connection is dialled rather than here. */
bool pim_ts_is_tailnet_address(const char *address) {
    unsigned part[4];
    char extra;
    if (address == NULL) return false;
    if (sscanf(address, "%u.%u.%u.%u%c", &part[0], &part[1], &part[2], &part[3], &extra) != 4) {
        return false;
    }
    if (part[0] > 255u || part[1] > 255u || part[2] > 255u || part[3] > 255u) return false;
    return part[0] == 100u && part[1] >= 64u && part[1] <= 127u;
}

/* ---- the default client: there isn't one ----

   A firmware built without PIM_ENABLE_TAILSCALE still has the screen, so these
   answer honestly and are overridden by src/firmware/tailscale.c in the build
   that carries the real client. The headless UI tests provide their own. */

__attribute__((weak)) bool pim_ts_available(void) { return false; }

__attribute__((weak)) bool pim_ts_start(const pim_ts_config_t *config) {
    (void)config;
    return false;
}

__attribute__((weak)) void pim_ts_stop(void) { }

__attribute__((weak)) pim_ts_state_t pim_ts_state(void) { return PIM_TS_OFF; }

__attribute__((weak)) const char *pim_ts_address(void) { return ""; }

__attribute__((weak)) const char *pim_ts_message(void) {
    return "This firmware was built without Tailscale.";
}

__attribute__((weak)) void pim_ts_poll(void) { }

__attribute__((weak)) const char *pim_ts_login_url(void) { return ""; }

__attribute__((weak)) size_t pim_ts_free_memory(void) { return 0; }
