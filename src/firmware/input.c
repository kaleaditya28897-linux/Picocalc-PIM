#include "pim/ui.h"
#include "i2ckbd.h"
#include "lvgl/lvgl.h"

static volatile bool game_mode;

void pim_input_set_game_mode(bool enabled) { game_mode = enabled; }

static uint32_t map_key(int key) {
    switch (key) {
        case 0xb5: return LV_KEY_UP;
        case 0xb6: return LV_KEY_DOWN;
        case 0xb4: return LV_KEY_LEFT;
        case 0xb7: return LV_KEY_RIGHT;
        case 0xb1: return LV_KEY_ESC;
        case 0x08: return LV_KEY_BACKSPACE;
        case 0x09: return LV_KEY_NEXT;
        case 0x0a:
        case 0x0d: return LV_KEY_ENTER;
        case 0xd2: return LV_KEY_HOME;
        case 0xd4: return LV_KEY_DEL;
        case 0xd5: return LV_KEY_END;
        /* Modifier, function and other special codes are not text; typing them inserts stray bytes. */
        default: return key >= 0x20 && key <= 0x7e ? (uint32_t)key : 0;
    }
}

static void read_keyboard(lv_indev_t *indev, lv_indev_data_t *data) {
    static uint32_t previous;
    static bool release_pending;
    int key;
    (void)indev;
    if (release_pending) {
        data->key = previous;
        data->state = LV_INDEV_STATE_RELEASED;
        release_pending = false;
        return;
    }
    key = read_i2c_kbd();
    if (game_mode && key > 0) {
        /* Drop the keyboard FIFO backlog from held keys; the newest key wins. */
        for (int drained = 0; drained < 32; drained++) {
            int next = read_i2c_kbd();
            if (next <= 0) break;
            key = next;
        }
    }
    if (key <= 0) {
        data->key = 0;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    previous = map_key(key);
    if (previous == 0) {
        data->key = 0;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    data->key = previous;
    data->state = LV_INDEV_STATE_PRESSED;
    release_pending = true;
}

bool pim_input_battery(pim_battery_t *battery) {
    uint8_t reg = 0x0b, reply[2] = {0, 0};
    if (i2c_write_timeout_us(I2C_KBD_MOD, I2C_KBD_ADDR, &reg, 1, false, 50000) != 1) return false;
    sleep_ms(16);
    if (i2c_read_timeout_us(I2C_KBD_MOD, I2C_KBD_ADDR, reply, 2, false, 50000) != 2) return false;
    return pim_battery_decode(reply, battery);
}

void pim_input_init(void) {
    lv_indev_t *indev;
    lv_group_t *group;
    init_i2c_kbd();
    indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, read_keyboard);
    group = lv_group_create();
    lv_group_set_default(group);
    lv_group_set_wrap(group, true);
    lv_indev_set_group(indev, group);
}
