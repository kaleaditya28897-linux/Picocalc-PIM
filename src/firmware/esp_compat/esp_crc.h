#ifndef PIM_COMPAT_ESP_CRC_H
#define PIM_COMPAT_ESP_CRC_H

#include <stddef.h>
#include <stdint.h>

/* Reflected CRC-32 with the usual polynomial, as ESP-IDF's version computes it.
   STUN's fingerprint attribute depends on getting this exactly right. */
uint32_t esp_crc32_le(uint32_t crc, const uint8_t *data, size_t length);

#endif
