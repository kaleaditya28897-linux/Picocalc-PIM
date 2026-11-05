#include <stdbool.h>

#include "pico/stdlib.h"

#include "lv_port_disp_picocalc_ILI9488.h"
#include "lvgl/lvgl.h"
#include "pim/pim.h"
#include "pim/storage.h"
#include "pim/ui.h"

bool fs_init(void);
void pim_input_init(void);

int main(void) {
    pim_database_t database;
    bool storage_available;
    stdio_init_all();
    lv_init();
    lv_port_disp_init();
    pim_input_init();
    storage_available = fs_init();
    if (storage_available) pim_storage_load("/PIM/pim.dat", &database);
    else pim_database_init(&database);
    pim_ui_start(&database, storage_available ? "/PIM/pim.dat" : NULL);
    while (true) {
        lv_timer_handler();
        sleep_ms(5);
        lv_tick_inc(5);
    }
}
