#include "i2ckbd.h"
#include "lvgl/lvgl.h"

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
        default: return key > 0 ? (uint32_t)key : 0;
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
    if (key < 0) {
        data->key = 0;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    previous = map_key(key);
    data->key = previous;
    data->state = LV_INDEV_STATE_PRESSED;
    release_pending = true;
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
