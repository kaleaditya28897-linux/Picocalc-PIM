#include "pim/sound.h"

bool pim_sound_tone(pim_sound_event_t event, uint16_t *frequency_hz, uint16_t *duration_ms) {
    static const struct { uint16_t hz, ms; } tones[PIM_SOUND_COUNT] = {
        {1800, 6}, {1200, 25}, {1500, 90}, {300, 50}, {200, 400}, {1000, 300},
        {900, 12}, {2400, 60}, {2000, 25}, {2600, 180}, {2200, 350}, {150, 120},
    };
    if ((unsigned)event >= PIM_SOUND_COUNT) return false;
    *frequency_hz = tones[event].hz;
    *duration_ms = tones[event].ms;
    return true;
}
