#ifndef PIM_BOOTTRACE_H
#define PIM_BOOTTRACE_H

#include <stdbool.h>

/* Where startup got to, and what stopped it.

   A device with no serial console that freezes during startup tells you
   nothing: the screen holds whatever was last drawn and the cause is gone. So
   each stage of startup leaves a breadcrumb in a watchdog scratch register,
   which survives a reset, and anything fatal - a failed assertion, a stack
   overflow, an allocation that could not be met, a hard fault - records itself
   and reboots instead of spinning with interrupts off. The next boot then says
   what happened, on screen, and skips the stage that killed the last one so the
   device stays usable.

   The registers are cleared by a power cycle, which is the right behaviour: a
   report describes the start you just watched, not one from last week. */

typedef enum {
    PIM_BOOT_NONE = 0,
    PIM_BOOT_START,
    PIM_BOOT_DISPLAY,
    PIM_BOOT_INPUT,
    PIM_BOOT_CARD,
    PIM_BOOT_DATABASE,
    PIM_BOOT_UI,
    PIM_BOOT_SCHEDULER,
    PIM_BOOT_RUNNING,        /* the UI loop is going; nothing below is startup */
    PIM_BOOT_WIFI_INIT,
    PIM_BOOT_WIFI_SCAN,
    PIM_BOOT_WIFI_JOIN,
    PIM_BOOT_TS_START,
    PIM_BOOT_TS_RUN
} pim_boot_stage_t;

typedef enum {
    PIM_FAULT_NONE = 0,
    PIM_FAULT_ASSERT,        /* detail: the line in FreeRTOS that asserted */
    PIM_FAULT_STACK,         /* detail: first four characters of the task name */
    PIM_FAULT_MEMORY,        /* detail: bytes of heap still free */
    PIM_FAULT_HARD,          /* detail: the faulting address, where known */
    PIM_FAULT_WATCHDOG       /* nothing recorded; the watchdog simply fired */
} pim_fault_t;

/* Reads the previous start's report and begins a new one. Called first, before
   anything else can overwrite the registers. */
void pim_boot_trace_init(void);
/* Leaves a breadcrumb. Cheap: one store to a register. */
void pim_boot_stage(pim_boot_stage_t stage);
/* Records the failure against the current stage and reboots. Does not return. */
void pim_boot_fault(pim_fault_t kind, unsigned detail);
/* What the previous start was doing when it stopped, if it stopped. False when
   the last start reached the UI loop, which is the normal case. */
bool pim_boot_last_failure(pim_boot_stage_t *stage, pim_fault_t *kind, unsigned *detail);
/* True when the previous start died joining Wi-Fi, so this one should not do it
   automatically and strand the device again. */
bool pim_boot_skip_wifi(void);
/* One short line for the screen, e.g. "Wi-Fi join stopped: stack overflow". */
void pim_boot_failure_text(char *out, unsigned out_size);

const char *pim_boot_stage_text(pim_boot_stage_t stage);
const char *pim_boot_fault_text(pim_fault_t kind);

#endif
