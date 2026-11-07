#include <stdbool.h>

#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#include "lv_port_disp_picocalc_ILI9488.h"
#include "lvgl/lvgl.h"
#include "pim/pim.h"
#include "pim/sound.h"
#include "pim/storage.h"
#include "pim/textfile.h"
#include "pim/ui.h"
#include "pim/wifi.h"

bool fs_init(void);
void pim_input_init(void);
void pim_input_idle_poll(void);

void pim_system_busy(bool busy) {
    if (busy) watchdog_disable();
    else watchdog_enable(8000, true);
}

int main(void) {
    static pim_database_t database;
    bool storage_available;
    stdio_init_all();
    lv_init();
    lv_port_disp_init();
    pim_input_init();
    storage_available = fs_init();
    if (storage_available) {
        pim_storage_load("/PIM/pim.dat", &database);
        if (pim_journal_migrate("/PIM", &database)) pim_storage_save("/PIM/pim.dat", &database);
        pim_storage_export("/PIM", &database);
    }
    else pim_database_init(&database);
    pim_ui_start(&database, storage_available ? "/PIM/pim.dat" : NULL);
    watchdog_enable(8000, true);
    uint32_t last_ms = to_ms_since_boot(get_absolute_time());
    while (true) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        lv_tick_inc(now - last_ms);
        last_ms = now;
        watchdog_update();
        uint32_t wait = lv_timer_handler();
        pim_wifi_poll();
        pim_sound_poll();
        pim_input_idle_poll();
        /* WFE-based sleep; keys and Wi-Fi still get polled at least every 20 ms. */
        sleep_ms(wait < 5 ? 5 : wait > 20 ? 20 : wait);
    }
}
