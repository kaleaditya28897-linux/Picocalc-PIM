#ifndef PIM_COMPAT_ESP_TIMER_H
#define PIM_COMPAT_ESP_TIMER_H

#include <stdint.h>

/* Microseconds since boot, which is time_us_64() by another name. */
int64_t esp_timer_get_time(void);

#endif
