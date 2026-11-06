#ifndef PIM_SIM_PICO_TIME_H
#define PIM_SIM_PICO_TIME_H
#include <stdint.h>
typedef uint64_t absolute_time_t;
absolute_time_t get_absolute_time(void);
uint32_t to_ms_since_boot(absolute_time_t time);
#endif
