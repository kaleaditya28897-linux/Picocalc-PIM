#ifndef PIM_UI_H
#define PIM_UI_H

#include "pim/pim.h"

void pim_ui_start(pim_database_t *database, const char *storage_path);
/* Text summary of the running game, used by the headless UI tests. */
const char *pim_ui_game_status(void);
/* In game mode the keyboard driver discards queued key repeats so input never lags behind. */
/* The terminal screen as text, rows separated by newlines: the cell grid is
   drawn, not built from labels, so the headless UI tests read it through here. */
const char *pim_ui_terminal_text(void);
/* Active theme colours as 0xRRGGBB, used by the headless UI tests. */
void pim_ui_theme_colors(uint32_t *foreground, uint32_t *background);
void pim_input_set_game_mode(bool enabled);
/* Keys a terminal needs - control codes, function keys, Page Up - are not LVGL
   keys, so in terminal mode the driver passes the raw keyboard code through with
   this bit set instead of dropping it. */
#define PIM_UI_TERM_KEY 0xE000u
void pim_input_set_terminal_mode(bool enabled);
/* Reads the PicoCalc battery gauge; false when no battery data is available. */
bool pim_input_battery(pim_battery_t *battery);
/* Wakes the backlight and restarts the idle timers (used by reminders). */
void pim_input_activity(void);
/* Long SD operations call this so the hardware watchdog does not reset the device; false re-arms it. */
void pim_system_busy(bool busy);

#endif
