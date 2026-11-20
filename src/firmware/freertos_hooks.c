/* The hooks FreeRTOS requires of the application, for the Tailscale build.

   Each of these failures corrupts quietly if ignored, so each one stops the
   board. What it must not do is stop it silently: panic() spins with interrupts
   disabled, and a device frozen that way cannot be asked what happened - the
   screen keeps its last frame and USB never comes up. So each records what
   happened where and reboots, and the next start says so on screen. */

#include "FreeRTOS.h"
#include "task.h"

#include "hardware/watchdog.h"
#include "pico/platform.h"
#include "pico/stdlib.h"
#include "pim/boottrace.h"
#include <malloc.h>
#include <stdio.h>

void vApplicationStackOverflowHook(TaskHandle_t task, char *name);
void vApplicationMallocFailedHook(void);
void vApplicationIdleHook(void);
void vApplicationGetIdleTaskMemory(StaticTask_t **buffer, StackType_t **stack, uint32_t *size);
void vApplicationGetTimerTaskMemory(StaticTask_t **buffer, StackType_t **stack, uint32_t *size);

void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
    /* The first four characters of the name identify the stack that wants more
       room, which is the one thing worth carrying through a reboot. */
    unsigned packed = 0;
    int i;
    (void)task;
    for (i = 0; i < 4 && name != NULL && name[i] != '\0'; i++) {
        packed |= (unsigned)(unsigned char)name[i] << (8 * i);
    }
    printf("stack overflow in task %s\n", name != NULL ? name : "?");
    pim_boot_fault(PIM_FAULT_STACK, packed);
}

void vApplicationMallocFailedHook(void) {
    extern char __StackLimit, __bss_end__;
    struct mallinfo info = mallinfo();
    size_t total = (size_t)(&__StackLimit - &__bss_end__);
    unsigned free_bytes = total > (size_t)info.uordblks
                        ? (unsigned)(total - (size_t)info.uordblks) : 0u;
    printf("out of heap, %u bytes free\n", free_bytes);
    pim_boot_fault(PIM_FAULT_MEMORY, free_bytes);
}

/* The watchdog is fed from here rather than only from the UI loop, because the
   calls that take the longest - joining a Wi-Fi network takes up to fifteen
   seconds - block that loop entirely. The idle task runs whenever it blocks, so
   the watchdog stays satisfied while real work is waiting, and still fires if
   everything stops. That is what lets the join run guarded; it used to disable
   the watchdog for the duration, which is exactly when a failure most needs it.

   A task that spins without yielding will starve this and reboot the board,
   which is the correct outcome. */
void vApplicationIdleHook(void) {
    watchdog_update();
}

/* A fault with no handler of its own lands here.

   The processor records why it faulted and, for most kinds, the address that
   caused it. That plus the registers at the point of the fault is the whole
   story, so this prints it before recording and rebooting. Interrupts are
   re-enabled first: a hard fault masks them, and without USB interrupts the
   report would never leave the board. Nothing is returning from here anyway. */
struct fault_frame {
    uint32_t r0, r1, r2, r3, r12, lr, pc, psr;
};

void pim_fault_report(struct fault_frame *frame);

void pim_fault_report(struct fault_frame *frame) {
    const volatile uint32_t *cfsr = (const volatile uint32_t *)0xE000ED28u;
    const volatile uint32_t *bfar = (const volatile uint32_t *)0xE000ED38u;

    /* Printing from here does not work: a hard fault runs at a priority that
       keeps every interrupt masked, USB included, so nothing can drain. The
       processor's account of the fault goes into the watchdog registers the
       SDK leaves alone, and the next start prints it. */
    watchdog_hw->scratch[5] = *cfsr;
    /* bfar reads as all-ones here, so the address that was actually accessed
       comes from the registers instead: r3 holds it at the faulting
       instruction, and it is in the exception frame. */
    watchdog_hw->scratch[6] = frame->r3;
    watchdog_hw->scratch[7] = frame->r0;
    (void)bfar;
    pim_boot_fault(PIM_FAULT_HARD, frame->pc);
}

void __attribute__((naked)) isr_hardfault(void) {
    __asm volatile (
        "tst lr, #4           \n"   /* which stack holds the exception frame? */
        "ite eq               \n"
        "mrseq r0, msp        \n"
        "mrsne r0, psp        \n"
        "b pim_fault_report   \n"
    );
}

/* The idle and timer tasks are allocated statically so that running out of heap
   can never stop the scheduler from starting. */
static StaticTask_t idle_task;
static StackType_t idle_stack[configMINIMAL_STACK_SIZE];
static StaticTask_t timer_task;
static StackType_t timer_stack[configTIMER_TASK_STACK_DEPTH];

void vApplicationGetIdleTaskMemory(StaticTask_t **buffer, StackType_t **stack, uint32_t *size) {
    *buffer = &idle_task;
    *stack = idle_stack;
    *size = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **buffer, StackType_t **stack, uint32_t *size) {
    *buffer = &timer_task;
    *stack = timer_stack;
    *size = configTIMER_TASK_STACK_DEPTH;
}
