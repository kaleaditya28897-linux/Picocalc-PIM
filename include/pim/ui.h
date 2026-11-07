#ifndef PIM_UI_H
#define PIM_UI_H

#include "pim/pim.h"

void pim_ui_start(pim_database_t *database, const char *storage_path);
/* Text summary of the running game, used by the headless UI tests. */
const char *pim_ui_game_status(void);
/* In game mode the keyboard driver discards queued key repeats so input never lags behind. */
/* Active theme colours as 0xRRGGBB, used by the headless UI tests. */
void pim_ui_theme_colors(uint32_t *foreground, uint32_t *background);
void pim_input_set_game_mode(bool enabled);
/* Reads the PicoCalc battery gauge; false when no battery data is available. */
bool pim_input_battery(pim_battery_t *battery);
/* Wakes the backlight and restarts the idle timers (used by reminders). */
void pim_input_activity(void);
/* Long SD operations call this so the hardware watchdog does not reset the device; false re-arms it. */
void pim_system_busy(bool busy);

#endif
