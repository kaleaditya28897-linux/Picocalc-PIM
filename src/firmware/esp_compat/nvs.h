#ifndef PIM_COMPAT_NVS_H
#define PIM_COMPAT_NVS_H

#include "esp_err.h"
#include <stddef.h>

/* The peer cache is one blob that survives a reboot. On this device that is a
   file on the SD card rather than a flash partition, so a namespace becomes a
   file name under /PIM and a key becomes a suffix. Upstream's batching keeps
   writes rare, which suits a card as well as it suited flash. */

typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t mode, nvs_handle_t *out);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *length);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_erase_all(nvs_handle_t handle);

#endif
