#ifndef PIM_COMPAT_ESP_HEAP_CAPS_H
#define PIM_COMPAT_ESP_HEAP_CAPS_H

#include <stddef.h>

/* There is no PSRAM and only one heap, so the capability flags are accepted and
   ignored. Upstream already falls back to malloc() when a PSRAM request fails,
   which is the path this takes. */
#define MALLOC_CAP_8BIT     (1 << 2)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_SPIRAM   (1 << 10)

void *heap_caps_malloc(size_t size, uint32_t caps);
void *heap_caps_calloc(size_t count, size_t size, uint32_t caps);
size_t heap_caps_get_free_size(uint32_t caps);

#endif
