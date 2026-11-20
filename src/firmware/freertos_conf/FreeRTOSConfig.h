#ifndef PIM_FREERTOS_CONFIG_H
#define PIM_FREERTOS_CONFIG_H

/* FreeRTOS for the Tailscale build only (PIM_ENABLE_TAILSCALE). The default
   firmware still runs lwIP with NO_SYS=1 on the cooperative LVGL loop.

   One core, on purpose: LVGL is not thread safe, so everything that touches it
   stays on the UI task, and a single core keeps that easy to reason about. The
   pico interop options matter more than they look - they make sleep_ms() and
   the SDK's mutexes yield to the scheduler, so the existing Wi-Fi and SD code
   keeps working unchanged inside a task. */

/* The ARMv8-M port insists on being told about all three, and the RP2350 runs
   the no-TrustZone port with the FPU on (the calculator and LVGL use floats). */
#define configENABLE_FPU                        1
#define configENABLE_MPU                        0
#define configENABLE_TRUSTZONE                  0
#define configRUN_FREERTOS_SECURE_ONLY          1

#define configNUMBER_OF_CORES                   1
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_TICKLESS_IDLE                 0
#define configCPU_CLOCK_HZ                      150000000
#define configTICK_RATE_HZ                      1000
#define configMAX_PRIORITIES                   	8
#define configMINIMAL_STACK_SIZE                512     /* words, so 2 KB */
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1       /* lwIP's core lock needs these */
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               8
#define configUSE_QUEUE_SETS                    0
/* Each task gets its own newlib state, which above all means its own errno.
   Sharing one is a real bug here, not a nicety: the DERP read path sets a
   100 ms receive timeout and decides whether a failed read was a timeout or a
   dead connection by looking at errno afterwards. lwIP's own thread runs at a
   higher priority and writes errno too, so a benign timeout could be read as a
   fatal error - the relay then dropped and reconnected in a loop, and peers
   behind it were unreachable. Costs a few hundred bytes per task. */
#define configUSE_NEWLIB_REENTRANT              1
#define configENABLE_BACKWARD_COMPATIBILITY     1  /* lwIP's FreeRTOS port still says portTICK_RATE_MS */
#define configSTACK_DEPTH_TYPE                  uint32_t
#define configMESSAGE_BUFFER_LENGTH_TYPE        size_t

/* One heap, not two: heap_3 hands out newlib malloc, which is whatever the
   linker left over after .bss. Reserving a second fixed pool would waste the
   little headroom this board has. */
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configAPPLICATION_ALLOCATED_HEAP        0

#define configUSE_IDLE_HOOK                     1   /* feeds the watchdog; see freertos_hooks.c */
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0
#define configCHECK_FOR_STACK_OVERFLOW          2       /* kept on: the watchdog cannot explain a stack overflow */

#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    0

#define configUSE_CO_ROUTINES                   0
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 2)
#define configTIMER_QUEUE_LENGTH                16
#define configTIMER_TASK_STACK_DEPTH            512

/* The SDK's sleep_ms, mutexes and semaphores become scheduler-aware. */
#define configSUPPORT_PICO_SYNC_INTEROP         1
#define configSUPPORT_PICO_TIME_INTEROP         1

#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xTaskAbortDelay                 1
#define INCLUDE_xTaskGetHandle                  1
#define INCLUDE_xSemaphoreGetMutexHolder        1

/* A failed assert stops rather than carrying on with a wrong answer; the
   hardware watchdog reboots the board eight seconds later. This header is also
   read by the port's assembly, so it must stay free of C calls. */
#if !defined(configASSERT) && !defined(__ASSEMBLER__)
/* Freezing with interrupts off leaves no way to find out what failed, so this
   records the line and reboots; the next start reports it. Declared here rather
   than included, because this header is also read by the assembler. */
void pim_assert_failed(unsigned line);
#define configASSERT(x) if ((x) == 0) pim_assert_failed(__LINE__)
#endif

#define configKERNEL_INTERRUPT_PRIORITY         (255)
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (0x20)

#endif /* PIM_FREERTOS_CONFIG_H */
