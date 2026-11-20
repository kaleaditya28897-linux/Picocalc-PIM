#include "pim/boottrace.h"

#include <stdio.h>

/* The naming and the wording, which the UI tests exercise on the host. The
   registers themselves are hardware, so reading and writing them lives in
   src/firmware/boottrace.c and replaces the weak definitions at the bottom. */

const char *pim_boot_stage_text(pim_boot_stage_t stage) {
    switch (stage) {
        case PIM_BOOT_START:      return "startup";
        case PIM_BOOT_DISPLAY:    return "display";
        case PIM_BOOT_INPUT:      return "keyboard";
        case PIM_BOOT_CARD:       return "SD card";
        case PIM_BOOT_DATABASE:   return "database";
        case PIM_BOOT_UI:         return "interface";
        case PIM_BOOT_SCHEDULER:  return "scheduler";
        case PIM_BOOT_RUNNING:    return "running";
        case PIM_BOOT_WIFI_INIT:  return "Wi-Fi start";
        case PIM_BOOT_WIFI_SCAN:  return "Wi-Fi scan";
        case PIM_BOOT_WIFI_JOIN:  return "Wi-Fi join";
        case PIM_BOOT_TS_START:   return "Tailscale start";
        case PIM_BOOT_TS_RUN:     return "Tailscale";
        case PIM_BOOT_NONE:
        default:                  return "unknown";
    }
}

const char *pim_boot_fault_text(pim_fault_t kind) {
    switch (kind) {
        case PIM_FAULT_ASSERT:   return "failed check";
        case PIM_FAULT_STACK:    return "stack overflow";
        case PIM_FAULT_MEMORY:   return "out of memory";
        case PIM_FAULT_HARD:     return "hard fault";
        case PIM_FAULT_WATCHDOG: return "stopped responding";
        case PIM_FAULT_NONE:
        default:                 return "unknown";
    }
}

void pim_boot_failure_text(char *out, unsigned out_size) {
    pim_boot_stage_t stage;
    pim_fault_t kind;
    unsigned detail;
    if (out_size == 0) return;
    if (!pim_boot_last_failure(&stage, &kind, &detail)) { out[0] = '\0'; return; }
    if (kind == PIM_FAULT_STACK) {
        /* The detail is the start of the task name, which is what identifies
           the stack that wants more room. */
        char name[5];
        name[0] = (char)(detail & 0xff);
        name[1] = (char)((detail >> 8) & 0xff);
        name[2] = (char)((detail >> 16) & 0xff);
        name[3] = (char)((detail >> 24) & 0xff);
        name[4] = '\0';
        snprintf(out, out_size, "%s stopped: stack overflow in %s",
                 pim_boot_stage_text(stage), name);
        return;
    }
    if (kind == PIM_FAULT_MEMORY) {
        snprintf(out, out_size, "%s stopped: out of memory, %u KB free",
                 pim_boot_stage_text(stage), detail / 1024u);
        return;
    }
    if (kind == PIM_FAULT_HARD && detail == 0xa5a5a5a5u) {
        /* The byte FreeRTOS fills a new stack with. A jump to it means the
           return address came from stack that was never written: an overflow,
           whatever the fault register says. */
        snprintf(out, out_size, "%s stopped: stack overflow (ran off the end)",
                 pim_boot_stage_text(stage));
        return;
    }
    if (kind == PIM_FAULT_HARD && detail != 0) {
        snprintf(out, out_size, "%s stopped: hard fault at 0x%08x",
                 pim_boot_stage_text(stage), detail);
        return;
    }
    if (detail != 0 && kind != PIM_FAULT_WATCHDOG) {
        snprintf(out, out_size, "%s stopped: %s (%u)",
                 pim_boot_stage_text(stage), pim_boot_fault_text(kind), detail);
        return;
    }
    snprintf(out, out_size, "%s stopped: %s",
             pim_boot_stage_text(stage), pim_boot_fault_text(kind));
}

/* ---- no registers on a build host ---- */

__attribute__((weak)) void pim_boot_stage(pim_boot_stage_t stage) { (void)stage; }

__attribute__((weak)) void pim_boot_fault(pim_fault_t kind, unsigned detail) {
    (void)kind; (void)detail;
}

__attribute__((weak)) bool pim_boot_last_failure(pim_boot_stage_t *stage, pim_fault_t *kind,
                                                 unsigned *detail) {
    (void)stage; (void)kind; (void)detail;
    return false;
}

__attribute__((weak)) bool pim_boot_skip_wifi(void) { return false; }
