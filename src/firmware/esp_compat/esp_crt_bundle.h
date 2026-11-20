#ifndef PIM_COMPAT_ESP_CRT_BUNDLE_H
#define PIM_COMPAT_ESP_CRT_BUNDLE_H

#include "esp_err.h"
#include "mbedtls/ssl.h"

/* ESP-IDF ships a bundle of CA roots in flash. There is none here, so this
   fails and the caller is expected to have CONFIG_ML_DERP_VERIFY_CERT off.
   Relayed traffic is still WireGuard-encrypted end to end; what is lost is
   detecting something impersonating a relay to drop or delay packets. */
esp_err_t esp_crt_bundle_attach(void *conf);

#endif
