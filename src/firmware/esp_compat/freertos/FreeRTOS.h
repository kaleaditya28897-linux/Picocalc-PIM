#ifndef PIM_COMPAT_FREERTOS_FreeRTOS_H
#define PIM_COMPAT_FREERTOS_FreeRTOS_H
/* ESP-IDF keeps the kernel headers under freertos/; the kernel itself does not.
   Resolves to third_party/FreeRTOS-Kernel/include/FreeRTOS.h, because only the
   esp_compat directory is on the include path, not this one. */
#include <FreeRTOS.h>

/* ESP-IDF's port exposes this; the kernel's does not. This build runs one core. */
static inline int xPortGetCoreID(void) { return 0; }

/* ESP-IDF's SMP port guards critical sections with a per-object spinlock, which
   vanilla FreeRTOS has no equivalent of: its critical sections are global. On a
   single core a global section is a correct superset of a per-spinlock one, just
   coarser, so the spinlock becomes a tag that is accepted and ignored.

   The two inline wrappers capture the kernel's no-argument macros before the
   names are taken over below. Only files that include "freertos/FreeRTOS.h"
   (which is to say the vendored client) see the replacement; the kernel and lwIP
   are compiled against their own headers. */
typedef struct { unsigned int unused; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED { 0 }
#define portMUX_INITIALIZE(mux) do { (void)(mux); } while (0)

static inline void pim_port_enter_critical(void) { portENTER_CRITICAL(); }
static inline void pim_port_exit_critical(void) { portEXIT_CRITICAL(); }

#undef portENTER_CRITICAL
#undef portEXIT_CRITICAL
/* Variadic so both spellings work: lwIP and the kernel call these with no
   argument, upstream calls them with a spinlock. */
#define portENTER_CRITICAL(...) do { pim_port_enter_critical(); } while (0)
#define portEXIT_CRITICAL(...)  do { pim_port_exit_critical(); } while (0)
#endif
