/* The Pico side of the ESP-IDF compatibility layer in esp_compat/, plus the four
   hooks wireguard-lwip asks of its platform. This file is the entire port: the
   vendored Tailscale client in third_party/microlink is unmodified.

   Only built when PIM_ENABLE_TAILSCALE is on. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/cyw43_arch.h"
#include "pico/rand.h"
#include "pico/stdlib.h"

#include "lwip/netif.h"
#include "lwip/sockets.h"

#include "esp_compat/esp_crc.h"
#include "esp_compat/esp_crt_bundle.h"
#include "esp_compat/esp_err.h"
#include "esp_compat/esp_heap_caps.h"
#include "esp_compat/esp_log.h"
#include "esp_compat/esp_mac.h"
#include "esp_compat/esp_netif.h"
#include "esp_compat/esp_random.h"
#include "esp_compat/esp_timer.h"
#include "esp_compat/esp_vfs_eventfd.h"
#include "esp_compat/esp_wifi.h"
#include <sys/stat.h>
#include <errno.h>
#include "esp_compat/nvs_flash.h"

#include "wireguard-platform.h"

#ifndef PIM_ROOT
#define PIM_ROOT "/PIM"
#endif

/* ------------------------------------------------------------------ errors */

const char *esp_err_to_name(esp_err_t code) {
    switch (code) {
    case ESP_OK: return "ESP_OK";
    case ESP_FAIL: return "ESP_FAIL";
    case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_INVALID_SIZE: return "ESP_ERR_INVALID_SIZE";
    case ESP_ERR_NOT_FOUND: return "ESP_ERR_NOT_FOUND";
    case ESP_ERR_NOT_SUPPORTED: return "ESP_ERR_NOT_SUPPORTED";
    case ESP_ERR_TIMEOUT: return "ESP_ERR_TIMEOUT";
    case ESP_ERR_INVALID_MAC: return "ESP_ERR_INVALID_MAC";
    default: return "ESP_ERR";
    }
}

/* --------------------------------------------------------------------- log */

static esp_log_level_t log_level = ESP_LOG_INFO;

void esp_log_level_set(const char *tag, esp_log_level_t level) {
    (void)tag;                  /* one level for everything; per-tag filtering is not worth the table */
    log_level = level;
}

esp_log_level_t esp_log_level_get(const char *tag) {
    (void)tag;
    return log_level;
}

void pim_esp_log(esp_log_level_t level, const char *tag, const char *format, ...) {
    static const char letter[] = {'-', 'E', 'W', 'I', 'D', 'V'};
    va_list arguments;
    if (level > log_level || level <= ESP_LOG_NONE) return;
    printf("%c (%lu) %s: ", letter[level], (unsigned long)to_ms_since_boot(get_absolute_time()),
           tag ? tag : "?");
    va_start(arguments, format);
    vprintf(format, arguments);
    va_end(arguments);
    /* Upstream's messages mostly carry their own newline; add one when not. */
    {
        size_t length = strlen(format);
        if (length == 0 || format[length - 1] != '\n') printf("\n");
    }
}

/* ------------------------------------------------------------------- clock */

int64_t esp_timer_get_time(void) {
    return (int64_t)time_us_64();
}

/* ------------------------------------------------------------------ random */

/* WireGuard's keys and Noise nonces come from here.

   get_rand_64() is seeded from the RP2350's hardware TRNG, which is the right
   source. One caveat worth keeping in view: the SDK samples the ring oscillator
   with the sample period set to zero and does not run the entropy checks the
   datasheet describes. That is fine for nonces. Before trusting it for a
   long-lived node key, either check that path against the TRNG chapter of the
   datasheet, or sidestep it entirely by generating the key on a PC and loading
   it from the card - which is how the SSH keys already get here. */
uint32_t esp_random(void) {
    uint32_t value;
    esp_fill_random(&value, sizeof(value));
    return value;
}

void esp_fill_random(void *buffer, size_t length) {
    uint8_t *out = buffer;
    while (length > 0) {
        uint64_t word = get_rand_64();
        size_t chunk = length < sizeof(word) ? length : sizeof(word);
        memcpy(out, &word, chunk);
        out += chunk;
        length -= chunk;
    }
}

/* mbedTLS's entropy hook is not here on purpose: the SDK's pico_mbedtls
   provides mbedtls_hardware_poll already, from the same hardware source, and two
   definitions do not link. */

/* --------------------------------------------------------------------- mac */

esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type) {
    if (mac == NULL) return ESP_ERR_INVALID_ARG;
    if (type != ESP_MAC_WIFI_STA) return ESP_ERR_NOT_SUPPORTED;
    cyw43_hal_get_mac(CYW43_HAL_MAC_WLAN0, mac);   /* returns void; the driver has it either way */
    return ESP_OK;
}

/* --------------------------------------------------------------------- crc */

uint32_t esp_crc32_le(uint32_t crc, const uint8_t *data, size_t length) {
    crc = ~crc;
    while (length-- > 0) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return ~crc;
}

/* -------------------------------------------------------------------- heap */

void *heap_caps_malloc(size_t size, uint32_t caps) {
    (void)caps;
    return malloc(size);
}

void *heap_caps_calloc(size_t count, size_t size, uint32_t caps) {
    (void)caps;
    return calloc(count, size);
}

size_t heap_caps_get_free_size(uint32_t caps) {
    /* Reported only in log messages. There is no cheap exact answer for newlib's
       heap, so this returns what is between the heap end and the stack. */
    extern char __StackLimit, __bss_end__;
    (void)caps;
    return (size_t)(&__StackLimit - &__bss_end__);
}

/* ------------------------------------------------------------------- netif */

esp_netif_t *esp_netif_get_default_netif(void) {
    return netif_default;
}

esp_netif_t *esp_netif_find_if(bool (*predicate)(esp_netif_t *netif, void *arg), void *arg) {
    struct netif *netif;
    if (predicate == NULL) return NULL;
    NETIF_FOREACH(netif) {
        if (predicate(netif, arg)) return netif;
    }
    return NULL;
}

/* The cyw43 driver names its interfaces 'w' plus the interface number, and
   wireguard-lwip names its tunnel "wg". Upstream only advertises addresses
   belonging to a LAN interface, so the tunnel must not look like one. */
const char *esp_netif_get_ifkey(esp_netif_t *netif) {
    if (netif == NULL) return NULL;
    if (netif->name[0] == 'w' && netif->name[1] == 'g') return "WG_DEF";
    if (netif->name[0] == 'w' && netif->name[1] == '0') return "WIFI_STA_DEF";
    if (netif->name[0] == 'w' && netif->name[1] == '1') return "WIFI_AP_DEF";
    return "LWIP_DEF";
}

esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key) {
    struct netif *netif;
    if (key == NULL) return NULL;
    NETIF_FOREACH(netif) {
        const char *found = esp_netif_get_ifkey(netif);
        if (found != NULL && strcmp(found, key) == 0) return netif;
    }
    return NULL;
}

uint32_t esp_netif_get_flags(esp_netif_t *netif) {
    (void)netif;
    return 0;                   /* nothing here is PPP */
}

bool esp_netif_is_netif_up(esp_netif_t *netif) {
    return netif != NULL && netif_is_up(netif) && netif_is_link_up(netif);
}

esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info) {
    if (netif == NULL || info == NULL) return ESP_ERR_INVALID_ARG;
    info->ip.addr = netif_ip4_addr(netif)->addr;
    info->netmask.addr = netif_ip4_netmask(netif)->addr;
    info->gw.addr = netif_ip4_gw(netif)->addr;
    return ESP_OK;
}

esp_err_t esp_netif_get_ip6_linklocal(esp_netif_t *netif, esp_ip6_addr_t *address) {
    (void)netif;
    (void)address;
    return ESP_ERR_NOT_SUPPORTED;   /* IPv6 is off in lwipopts.h */
}

esp_err_t esp_netif_create_ip6_linklocal(esp_netif_t *netif) {
    (void)netif;
    return ESP_ERR_NOT_SUPPORTED;
}

/* ------------------------------------------------------------------- stubs */

esp_err_t esp_wifi_set_max_tx_power(int8_t power) {
    (void)power;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_crt_bundle_attach(void *conf) {
    (void)conf;
    return ESP_ERR_NOT_SUPPORTED;   /* no CA bundle; see sdkconfig.h */
}

esp_err_t esp_vfs_eventfd_register(const esp_vfs_eventfd_config_t *config) {
    (void)config;
    return ESP_OK;                  /* eventfd() below needs no registration */
}

/* A loopback UDP socket stands in for an eventfd: it can sit in the same
   select() set, and one byte sent to itself wakes the loop. */
int eventfd(unsigned int initial, int flags) {
    struct sockaddr_in address;
    socklen_t length = sizeof(address);
    int fd;
    (void)initial;
    (void)flags;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = PP_HTONL(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        getsockname(fd, (struct sockaddr *)&address, &length) != 0 ||
        connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* -------------------------------------------------------- peer cache (NVS) */

/* One namespace, one blob, one file. Upstream brackets batches of peer updates
   so this is written rarely, which matters as much for an SD card as it did for
   flash. */

#define NVS_MAX_HANDLES 2

typedef struct {
    bool used;
    char name[24];
} nvs_entry_t;

static nvs_entry_t nvs_table[NVS_MAX_HANDLES];

static void nvs_path(char *out, size_t size, const char *namespace_name, const char *key) {
    snprintf(out, size, PIM_ROOT "/ts/%.12s-%.12s.bin", namespace_name, key);
}

esp_err_t nvs_flash_init(void) {
    /* The card is mounted before any of this runs. The identity lives in its
       own folder, which the web server refuses to list or serve: these are the
       machine, WireGuard and DISCO private keys. */
    if (mkdir(PIM_ROOT "/ts", 0777) != 0 && errno != EEXIST) return ESP_FAIL;
    return ESP_OK;
}

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t mode, nvs_handle_t *out) {
    (void)mode;
    if (namespace_name == NULL || out == NULL) return ESP_ERR_INVALID_ARG;
    for (uint32_t i = 0; i < NVS_MAX_HANDLES; i++) {
        if (nvs_table[i].used) continue;
        nvs_table[i].used = true;
        snprintf(nvs_table[i].name, sizeof(nvs_table[i].name), "%s", namespace_name);
        *out = i + 1;
        return ESP_OK;
    }
    return ESP_ERR_NO_MEM;
}

void nvs_close(nvs_handle_t handle) {
    if (handle >= 1 && handle <= NVS_MAX_HANDLES) nvs_table[handle - 1].used = false;
}

static const char *nvs_namespace(nvs_handle_t handle) {
    if (handle < 1 || handle > NVS_MAX_HANDLES || !nvs_table[handle - 1].used) return NULL;
    return nvs_table[handle - 1].name;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *length) {
    const char *space = nvs_namespace(handle);
    char path[96];
    FILE *file;
    long size;
    if (space == NULL || key == NULL || length == NULL) return ESP_ERR_INVALID_ARG;
    nvs_path(path, sizeof(path), space, key);
    file = fopen(path, "rb");
    if (file == NULL) return ESP_ERR_NOT_FOUND;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0) { fclose(file); return ESP_FAIL; }
    /* A null `out` is how a caller asks for the size first. */
    if (out == NULL) {
        *length = (size_t)size;
        fclose(file);
        return ESP_OK;
    }
    if ((size_t)size > *length) { fclose(file); return ESP_ERR_INVALID_SIZE; }
    *length = fread(out, 1, (size_t)size, file);
    fclose(file);
    return *length == (size_t)size ? ESP_OK : ESP_FAIL;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length) {
    const char *space = nvs_namespace(handle);
    char path[96], temporary[112];
    FILE *file;
    if (space == NULL || key == NULL || value == NULL) return ESP_ERR_INVALID_ARG;
    nvs_path(path, sizeof(path), space, key);
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    /* Written aside and renamed, so a card pulled mid-write leaves the previous
       cache intact rather than a half-written one. */
    file = fopen(temporary, "wb");
    if (file == NULL) return ESP_FAIL;
    if (fwrite(value, 1, length, file) != length || fclose(file) != 0) {
        remove(temporary);
        return ESP_FAIL;
    }
    remove(path);
    return rename(temporary, path) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t nvs_commit(nvs_handle_t handle) {
    (void)handle;
    return ESP_OK;              /* nvs_set_blob already renamed into place */
}

esp_err_t nvs_erase_all(nvs_handle_t handle) {
    const char *space = nvs_namespace(handle);
    char path[96];
    if (space == NULL) return ESP_ERR_INVALID_ARG;
    nvs_path(path, sizeof(path), space, "tbl");
    remove(path);
    return ESP_OK;
}

/* ------------------------------------------------- wireguard-lwip platform */

uint32_t wireguard_sys_now(void) {
    return to_ms_since_boot(get_absolute_time());
}

void wireguard_random_bytes(void *bytes, size_t size) {
    esp_fill_random(bytes, size);
}

/* The wall clock, as Unix seconds, or 0 when nothing has set it. Defined weak so
   the UI can publish the time the user entered at startup. Without it the
   timestamp below restarts from zero after a reboot, and a peer that remembers a
   higher one rejects the first handshake until its own state ages out. Wiring
   this up is a small job and worth doing before relying on reconnects. */
__attribute__((weak)) uint32_t pim_wall_clock_seconds(void) {
    return 0;
}

/* TAI64N: seconds since 1970 plus the TAI offset, then nanoseconds. A handshake
   only needs this to move forward, which the uptime does within one session. */
void wireguard_tai64n_now(uint8_t *output) {
    uint64_t micros = time_us_64();
    uint64_t seconds = 0x400000000000000aULL + (micros / 1000000u) + pim_wall_clock_seconds();
    uint32_t nanos = (uint32_t)((micros % 1000000u) * 1000u);
    for (int i = 0; i < 8; i++) output[i] = (uint8_t)(seconds >> (56 - 8 * i));
    for (int i = 0; i < 4; i++) output[8 + i] = (uint8_t)(nanos >> (24 - 8 * i));
}

bool wireguard_is_under_load(void) {
    return false;               /* one peer set on a private tailnet; no cookie storms */
}

/* ---- two upstream modules that are not in the vendored tree ----

   third_party/microlink carries the client's own sources, but two of its
   components live in the ESP-IDF project around it rather than in src/, so they
   are supplied here against the contracts their headers document.

   ml_at_validate: parameter checks for a cellular modem's AT commands. This
   device has no modem, but microlink_init() uses the host-name check on a
   custom control plane's address, which goes into a TLS server name and an HTTP
   Host header - worth keeping exactly as strict as upstream intends rather than
   waving through. */

#include "ml_at_validate.h"

bool ml_at_is_safe_param(const char *s, size_t maxlen) {
    size_t i;
    if (s == NULL) return false;
    for (i = 0; s[i] != '\0'; i++) {
        unsigned char c = (unsigned char)s[i];
        if (maxlen != 0 && i >= maxlen) return false;
        if (c < 0x20 || c > 0x7e) return false;     /* controls and 8-bit bytes */
        if (c == '"' || c == '\\') return false;    /* would escape the argument */
    }
    return maxlen == 0 || i <= maxlen;
}

/* Letters, digits and '-' in dot-separated labels, with no label empty, none
   starting or ending in '-', and each at most 63 bytes. */
static bool ldh_labels(const char *s, bool allow_empty) {
    size_t label = 0, i;
    if (s[0] == '\0') return allow_empty;
    for (i = 0; s[i] != '\0'; i++) {
        char c = s[i];
        if (c == '.') {
            if (label == 0 || s[i - 1] == '-') return false;
            label = 0;
            continue;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-')) return false;
        if (label == 0 && c == '-') return false;
        if (++label > 63) return false;
    }
    return label > 0 && s[i - 1] != '-';
}

bool ml_at_param_valid(ml_at_param_kind_t kind, const char *s, size_t maxlen) {
    size_t limit;
    size_t length;
    switch (kind) {
        case ML_AT_PARAM_APN:        limit = ML_AT_APN_MAX; break;
        case ML_AT_PARAM_PIN:        limit = ML_AT_PIN_MAX; break;
        case ML_AT_PARAM_HOSTNAME:   limit = ML_AT_HOSTNAME_MAX; break;
        case ML_AT_PARAM_CREDENTIAL: limit = ML_AT_CRED_MAX; break;
        default: return false;
    }
    if (maxlen != 0 && maxlen < limit) limit = maxlen;
    if (!ml_at_is_safe_param(s, limit)) return false;
    length = strlen(s);

    switch (kind) {
        case ML_AT_PARAM_APN:
            return ldh_labels(s, true);
        case ML_AT_PARAM_PIN: {
            size_t i;
            if (length < ML_AT_PIN_MIN || length > ML_AT_PIN_MAX) return false;
            for (i = 0; i < length; i++) if (s[i] < '0' || s[i] > '9') return false;
            return true;
        }
        case ML_AT_PARAM_HOSTNAME:
            return ldh_labels(s, false);
        case ML_AT_PARAM_CREDENTIAL:
            return true;            /* the baseline check is the whole rule */
        default:
            return false;
    }
}

/* ml_ota: keeping or rolling back a firmware image delivered over the tailnet.
   This device is flashed over USB and has no second slot to roll back to, so
   there is nothing to arm and nothing to confirm. */
void ml_ota_check_pending(void) { }
void ml_ota_confirm(void) { }

/* ---- the allocator the kernel uses ----

   FreeRTOS's heap_3 wraps every malloc and free in vTaskSuspendAll() /
   xTaskResumeAll(), to keep two tasks out of newlib's allocator at once. That
   window is a trap: while the scheduler is suspended a task may not block, and
   the Tailscale client waits on an event group with a timeout as soon as it has
   registered. It asserted in the net I/O task, inside vPortFree, and took the
   board down with it.

   So the serialising is moved to where it belongs. PICO_USE_MALLOC_MUTEX makes
   the SDK guard malloc with a real mutex, which with FreeRTOS blocks the
   calling task and leaves every other task running - and these then hand
   straight through, with no suspension anywhere. The allocator is still
   serialised; what has gone is the scheduler being stopped to do it. */

void *pvPortMalloc(size_t size);
void vPortFree(void *pv);
void vPortInitialiseBlocks(void);
size_t xPortGetFreeHeapSize(void);

void *pvPortMalloc(size_t size) {
    void *p = malloc(size);
    if (p == NULL) {
        extern void vApplicationMallocFailedHook(void);
        vApplicationMallocFailedHook();
    }
    return p;
}

void vPortFree(void *pv) {
    free(pv);
}

void vPortInitialiseBlocks(void) { }

size_t xPortGetFreeHeapSize(void) {
    extern size_t pim_ts_free_memory(void);
    return pim_ts_free_memory();
}
