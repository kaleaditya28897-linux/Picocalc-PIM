#ifndef PIM_COMPAT_ESP_WIFI_H
#define PIM_COMPAT_ESP_WIFI_H

#include "esp_err.h"

/* Upstream turns the radio down a little to save power. The CYW43 has no
   equivalent knob exposed here, so this reports "not supported" and upstream
   carries on; it only logs the result. */
esp_err_t esp_wifi_set_max_tx_power(int8_t power);

#endif
