#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#if PIM_TAILSCALE
#include "FreeRTOS.h"
#include "task.h"
#endif

#include "lv_port_disp_picocalc_ILI9488.h"
#include "lvgl/lvgl.h"
#include "pim/pim.h"
#include "pim/sound.h"
#include "pim/storage.h"
#include "pim/textfile.h"
#include "pim/ui.h"
#if PIM_TAILSCALE
#include "pico/stdio_usb.h"      /* only the bring-up build serves stdio over USB */
#endif
#include "pim/boottrace.h"
#include "pim/tailscale.h"
#include "pim/wifi.h"

bool fs_init(void);
void pim_input_init(void);
void pim_input_idle_poll(void);

void pim_system_busy(bool busy) {
    if (busy) watchdog_disable();
    else watchdog_enable(8000, true);
}

/* The one loop that may touch LVGL. Under FreeRTOS everything else - the lwIP
   thread, and later the Tailscale tasks - runs beside it, so no other task may
   call into LVGL or the UI. */
static void ui_loop(void) {
    uint32_t last_ms = to_ms_since_boot(get_absolute_time());
    /* Past this point a failure is not a failure to start, so the report that
       survives a reset stops blaming startup for it. */
    pim_boot_stage(PIM_BOOT_RUNNING);
    while (true) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        lv_tick_inc(now - last_ms);
        last_ms = now;
        watchdog_update();
        uint32_t wait = lv_timer_handler();
        pim_wifi_poll();
        /* Keeps the Tailscale state fresh whether or not its screen is open,
           so the terminal can see the tunnel come up. Cheap when it is off. */
        pim_ts_poll();
        pim_sound_poll();
        pim_input_idle_poll();
        /* WFE-based sleep; keys and Wi-Fi still get polled at least every 20 ms.
           With pico time interop this yields to the scheduler instead of spinning. */
        sleep_ms(wait < 5 ? 5 : wait > 20 ? 20 : wait);
    }
}

/* Everything from the display to the interface, in one place so that both
   builds run it at the same point: in the polled build straight from main, in
   the FreeRTOS build from inside the task.

   That distinction is the whole reason this is a function. The SDK's drivers -
   the SD card over SPI, the radio - wait on timers and locks that only work
   once the scheduler is running, so calling them from main() in the FreeRTOS
   build hangs on the first one that waits for anything. Startup used to run
   here and the device stopped dead at the card, every time. */
static void startup(void) {
    static pim_database_t database;
    bool storage_available;

    pim_boot_stage(PIM_BOOT_DISPLAY);
    lv_init();
    lv_port_disp_init();
    pim_boot_stage(PIM_BOOT_INPUT);
    pim_input_init();
    pim_boot_stage(PIM_BOOT_CARD);
    storage_available = fs_init();
    pim_boot_stage(PIM_BOOT_DATABASE);
    if (storage_available) {
        pim_storage_load("/PIM/pim.dat", &database);
        if (pim_journal_migrate("/PIM", &database)) pim_storage_save("/PIM/pim.dat", &database);
        pim_storage_export("/PIM", &database);
    }
    else pim_database_init(&database);
    pim_boot_stage(PIM_BOOT_UI);
    pim_ui_start(&database, storage_available ? "/PIM/pim.dat" : NULL);
    watchdog_enable(8000, true);
}

#if PIM_TAILSCALE
static void ui_task(void *argument) {
    (void)argument;
    startup();
    ui_loop();
}
#endif

int main(void) {
    stdio_init_all();
#if PIM_TAILSCALE
    /* A bring-up build: the opening lines are the ones worth having, and USB
       output goes nowhere until a host has attached. Two seconds at most, and
       only when nothing is listening does it wait the whole time. */
    for (int i = 0; i < 40 && !stdio_usb_connected(); i++) sleep_ms(50);
#endif
    /* Before anything else can overwrite the registers; it also prints what
       the previous start managed. */
    pim_boot_trace_init();
#if PIM_TAILSCALE
    pim_boot_stage(PIM_BOOT_SCHEDULER);
    /* Below lwIP and the radio driver: a responsive screen matters less than
       not starving the thing holding the link up, and LVGL only needs to run
       fifty times a second. The stack is 12 KB because LVGL renders on it, and
       all of startup now runs on it too. */
    /* 32 KB. Everything the person does runs on this one stack: LVGL's
       rendering, the SD card through FatFS, and an SSH key exchange - which
       means curve25519, ed25519 verification and SHA-256, each with frames of
       its own. Accepting a host key did all three at once and ran off the end,
       and a stack that has run off the end shows up as a jump to 0xa5a5a5a5,
       the pattern FreeRTOS fills a fresh stack with. */
    if (xTaskCreate(ui_task, "ui", 6144, NULL, 2, NULL) != pdPASS) {
        pim_boot_fault(PIM_FAULT_MEMORY, 0);
    }
    vTaskStartScheduler();
    pim_boot_fault(PIM_FAULT_MEMORY, 0);     /* the scheduler only returns for want of memory */
#else
    startup();
    ui_loop();
#endif
}
