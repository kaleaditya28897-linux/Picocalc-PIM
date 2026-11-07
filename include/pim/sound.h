#ifndef PIM_SOUND_H
#define PIM_SOUND_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { PIM_SOUND_KEY, PIM_SOUND_SELECT, PIM_SOUND_CLEAR, PIM_SOUND_HIT, PIM_SOUND_GAME_OVER, PIM_SOUND_ALERT,
    PIM_SOUND_MOVE, PIM_SOUND_PICKUP, PIM_SOUND_SHOOT, PIM_SOUND_LEVEL_UP, PIM_SOUND_WIN, PIM_SOUND_HURT, PIM_SOUND_COUNT } pim_sound_event_t;

/* Tone for an event; returns false for unknown events. Pure data, so it is host-testable. */
bool pim_sound_tone(pim_sound_event_t event, uint16_t *frequency_hz, uint16_t *duration_ms);

/* Firmware-provided hooks (no-ops on the host). */
void pim_sound_set_enabled(bool enabled);
bool pim_sound_enabled(void);
void pim_sound_play(pim_sound_event_t event);
void pim_sound_poll(void);

#endif
