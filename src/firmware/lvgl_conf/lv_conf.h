#ifndef PIM_LV_CONF_H
#define PIM_LV_CONF_H
#include PIM_DEVICE_LV_CONF
/* The vendor 48 KiB pool overflows when listing 96 to-dos; the RP2350 has RAM to spare.
 * Host builds use 64-bit pointers, so they set PIM_LV_MEM_KB higher. */
#ifndef PIM_LV_MEM_KB
#define PIM_LV_MEM_KB 160
#endif
#undef LV_MEM_SIZE
#define LV_MEM_SIZE (PIM_LV_MEM_KB * 1024U)
#endif
