#ifndef PIM_COMPAT_FREERTOS_task_H
#define PIM_COMPAT_FREERTOS_task_H
/* ESP-IDF keeps the kernel headers under freertos/; the kernel itself does not.
   Resolves to third_party/FreeRTOS-Kernel/include/task.h, because only the
   esp_compat directory is on the include path, not this one. */
#include "FreeRTOS.h"       /* ours, for the critical-section wrappers below */
#include <task.h>

/* ESP-IDF's SMP additions, reduced to what one core means. Pinning a task to a
   core is a no-op when there is only one.

   The stack size is the part that matters: ESP-IDF counts it in bytes, while
   the kernel counts it in words and multiplies by four. Passing the number
   through unchanged gave every task four times the stack it asked for - 184 KB
   across the client's five tasks instead of 46 KB - which is most of this
   board's memory spent on stacks nobody uses. The rounding up keeps a stack
   that is not a multiple of four words from being short. */
#define PIM_STACK_WORDS(bytes) ((configSTACK_DEPTH_TYPE)(((bytes) + 3u) / 4u))
#define xTaskCreatePinnedToCore(fn, name, stack, arg, prio, handle, core) \
    ((void)(core), xTaskCreate((fn), (name), PIM_STACK_WORDS(stack), (arg), (prio), (handle)))

/* The spinlock-taking forms, like their port-level counterparts. These call the
   wrapper FreeRTOS.h captured rather than the kernel's taskENTER_CRITICAL,
   because that one expands to portENTER_CRITICAL(), whose name has by now been
   taken over by the one-argument version. */
#undef taskENTER_CRITICAL
#undef taskEXIT_CRITICAL
#define taskENTER_CRITICAL(...) do { pim_port_enter_critical(); } while (0)
#define taskEXIT_CRITICAL(...)  do { pim_port_exit_critical(); } while (0)
#endif
