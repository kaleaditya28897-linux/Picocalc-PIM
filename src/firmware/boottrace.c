#include "pim/boottrace.h"

#include <stdio.h>

#if PIM_TAILSCALE
/* Only this build has a kernel to ask which task was running. */
#include "FreeRTOS.h"
#include "task.h"
#endif

#include "hardware/watchdog.h"
#include "pico/stdlib.h"

/* Watchdog scratch registers survive a reset, and the SDK only uses 4 to 7
   (watchdog_enable and watchdog_reboot put their markers there), so 0 to 3 are
   ours. A power cycle clears them, which is what we want: a report should
   describe the start you just watched.

      scratch[0]  a magic, so uninitialised noise is not read as a report
      scratch[1]  the stage startup had reached
      scratch[2]  what went wrong, if anything
      scratch[3]  the detail that goes with it */

#define TRACE_MAGIC 0x50494d31u        /* "PIM1" */

static pim_boot_stage_t failed_stage;
static pim_fault_t failed_kind;
static unsigned failed_detail;
static bool have_failure;

/* Read once, before anything starts overwriting the registers. */
void pim_boot_trace_init(void) {
    if (watchdog_hw->scratch[0] == TRACE_MAGIC) {
        pim_boot_stage_t stage = (pim_boot_stage_t)watchdog_hw->scratch[1];
        pim_fault_t kind = (pim_fault_t)watchdog_hw->scratch[2];
        unsigned detail = watchdog_hw->scratch[3];
        /* Reaching the UI loop is a successful start, whatever happened later;
           anything else means the last start did not finish. */
        if (stage != PIM_BOOT_NONE && stage < PIM_BOOT_RUNNING) {
            failed_stage = stage;
            failed_kind = kind != PIM_FAULT_NONE ? kind : PIM_FAULT_WATCHDOG;
            failed_detail = detail;
            have_failure = true;
        } else if (kind != PIM_FAULT_NONE) {
            /* It was running, then something fatal happened - a Wi-Fi join or
               a Tailscale start, which are the interesting ones. */
            failed_stage = stage;
            failed_kind = kind;
            failed_detail = detail;
            have_failure = true;
        }
    }
    watchdog_hw->scratch[0] = TRACE_MAGIC;
    watchdog_hw->scratch[1] = PIM_BOOT_START;
    if (have_failure) {
        char report[96];
        pim_boot_failure_text(report, sizeof(report));
        printf("\nprevious start: %s\n", report);
        if (failed_kind == PIM_FAULT_ASSERT) {
            char task[5];
            unsigned packed = watchdog_hw->scratch[6];
            task[0] = (char)(packed & 0xff);
            task[1] = (char)((packed >> 8) & 0xff);
            task[2] = (char)((packed >> 16) & 0xff);
            task[3] = (char)((packed >> 24) & 0xff);
            task[4] = '\0';
            printf("  in task %s\n", task);
        }
        if (failed_kind == PIM_FAULT_HARD) {
            /* Left by the fault handler, which could not print them itself:
               cfsr says what kind of access failed, and r3 and r0 are the
               registers most often holding the pointer that caused it. */
            printf("  cfsr %08lx  r3 %08lx  r0 %08lx\n",
                   (unsigned long)watchdog_hw->scratch[5],
                   (unsigned long)watchdog_hw->scratch[6],
                   (unsigned long)watchdog_hw->scratch[7]);
        }
    } else {
        printf("\nprevious start: finished\n");
    }
    watchdog_hw->scratch[2] = PIM_FAULT_NONE;
    watchdog_hw->scratch[3] = 0;
}

void pim_boot_stage(pim_boot_stage_t stage) {
    static pim_boot_stage_t previous = PIM_BOOT_NONE;
    if (stage != previous) {
        /* Printed as it happens: whatever the last line is, that is where it
           stopped. Costs nothing on a build with no console attached. */
        printf("stage: %s\n", pim_boot_stage_text(stage));
        previous = stage;
    }
    watchdog_hw->scratch[1] = (unsigned)stage;
    /* Clearing the fault as each stage is entered means a report always belongs
       to the stage it is stored against. */
    watchdog_hw->scratch[2] = PIM_FAULT_NONE;
    watchdog_hw->scratch[3] = 0;
}

void pim_boot_fault(pim_fault_t kind, unsigned detail) {
    watchdog_hw->scratch[2] = (unsigned)kind;
    watchdog_hw->scratch[3] = detail;
    /* Reboot rather than spin with interrupts disabled. A frozen device cannot
       be asked what happened; one that comes back can say so on its screen. */
    watchdog_reboot(0, 0, 0);
    for (;;) { }
}

bool pim_boot_last_failure(pim_boot_stage_t *stage, pim_fault_t *kind, unsigned *detail) {
    if (!have_failure) return false;
    if (stage != NULL) *stage = failed_stage;
    if (kind != NULL) *kind = failed_kind;
    if (detail != NULL) *detail = failed_detail;
    return true;
}

bool pim_boot_skip_wifi(void) {
    return have_failure && (failed_stage == PIM_BOOT_WIFI_INIT ||
                            failed_stage == PIM_BOOT_WIFI_SCAN ||
                            failed_stage == PIM_BOOT_WIFI_JOIN);
}

/* What FreeRTOS's configASSERT calls. Separate from pim_boot_fault so that
   FreeRTOSConfig.h, which the assembler also reads and so cannot include this
   module's header, needs no knowledge of its types. Only the build with a
   kernel ever reaches it. */
void pim_assert_failed(unsigned line) {
#if PIM_TAILSCALE
    /* Carried through the reboot alongside the line: which task was running. */
    const char *name = pcTaskGetName(NULL);
    unsigned packed = 0;
    int i;
    for (i = 0; i < 4 && name != NULL && name[i] != '\0'; i++) {
        packed |= (unsigned)(unsigned char)name[i] << (8 * i);
    }
    watchdog_hw->scratch[6] = packed;
#endif
    pim_boot_fault(PIM_FAULT_ASSERT, line);
}
