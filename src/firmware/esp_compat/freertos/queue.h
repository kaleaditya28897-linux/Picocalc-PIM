#ifndef PIM_COMPAT_FREERTOS_queue_H
#define PIM_COMPAT_FREERTOS_queue_H
/* ESP-IDF keeps the kernel headers under freertos/; the kernel itself does not.
   Resolves to third_party/FreeRTOS-Kernel/include/queue.h, because only the
   esp_compat directory is on the include path, not this one. */
#include <queue.h>
#endif
