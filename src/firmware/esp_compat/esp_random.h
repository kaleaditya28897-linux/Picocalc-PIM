#ifndef PIM_COMPAT_ESP_RANDOM_H
#define PIM_COMPAT_ESP_RANDOM_H

#include <stddef.h>
#include <stdint.h>

/* WireGuard keys and Noise nonces come from here, so this is the RP2350's
   hardware TRNG and not a PRNG. See ts_platform.c. */
uint32_t esp_random(void);
void esp_fill_random(void *buffer, size_t length);

#endif
