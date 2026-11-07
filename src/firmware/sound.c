#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

#include "pim/sound.h"

#define SOUND_LEFT 26
#define SOUND_RIGHT 27

static bool enabled = true;
static bool ready;
static bool playing;
static absolute_time_t stop_at;

static void silence(void) {
    pwm_set_gpio_level(SOUND_LEFT, 0);
    pwm_set_gpio_level(SOUND_RIGHT, 0);
    playing = false;
}

void pim_sound_set_enabled(bool value) {
    enabled = value;
    if (!enabled && ready) silence();
}
bool pim_sound_enabled(void) { return enabled; }

void pim_sound_play(pim_sound_event_t event) {
    uint16_t hz, ms;
    uint slice;
    uint32_t wrap;
    if (!enabled || !pim_sound_tone(event, &hz, &ms)) return;
    gpio_set_function(SOUND_LEFT, GPIO_FUNC_PWM);
    gpio_set_function(SOUND_RIGHT, GPIO_FUNC_PWM);
    slice = pwm_gpio_to_slice_num(SOUND_LEFT);
    if (!ready) { pwm_set_clkdiv(slice, 150.0f); pwm_set_enabled(slice, true); ready = true; }
    wrap = 1000000U / hz - 1U;
    pwm_set_wrap(slice, (uint16_t)wrap);
    pwm_set_gpio_level(SOUND_LEFT, (uint16_t)(wrap / 10U));
    pwm_set_gpio_level(SOUND_RIGHT, (uint16_t)(wrap / 10U));
    stop_at = make_timeout_time_ms(ms);
    playing = true;
}

void pim_sound_poll(void) {
    if (playing && time_reached(stop_at)) silence();
}
