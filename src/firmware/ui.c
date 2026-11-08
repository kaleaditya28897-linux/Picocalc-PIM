#include "pim/ui.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lvgl/lvgl.h"
#include "pim/sound.h"
#include "pico/time.h"
#include "pim/games.h"
#include "pim/net.h"
#include "pim/sdgames.h"
#include "pim/storage.h"
#include "pim/telnet.h"
#include "pim/textfile.h"
#include "pim/tools.h"
#include "pim/wifi.h"

typedef enum { GAME_NONE, GAME_TETRIS, GAME_DUNGEON, GAME_INVADERS, GAME_SOLITAIRE, GAME_SNAKE, GAME_2048, GAME_MINES } game_t;

#define FILE_LIST_MAX 32
#define FILE_NAME_MAX 48
#define FILE_PATH_MAX 128
#define FILE_TEXT_MAX 4096

typedef struct {
    pim_database_t *database;
    const char *storage_path;
    pim_date_t calendar;
    size_t selected;
    uint8_t spin[3], spin_digits[2], fld_digits[5], date_origin;
    uint16_t fld[5], base_minutes;
    uint32_t base_ms;
    pim_date_t today;
    lv_obj_t *nav[5], *days[31], *spin_obj[3], *fld_obj[5], *fld_hint, *preview, *title, *content, *choice, *board, *battery_obj, *clock_obj;
    lv_timer_t *timer;
    game_t game;
    pim_tetris_t tetris;
    pim_dungeon_t dungeon;
    pim_invaders_t invaders;
    pim_solitaire_t solitaire;
    pim_snake_t snake;
    pim_2048_t g2048;
    pim_mines_t mines;
    bool terminal;
    uint8_t cursor_col, cursor_row, cursor_depth, sel_index, sel_count;
    pim_solitaire_zone_t sel_zone;
    bool sel_active;
    char sol_message[32];
    char file_names[FILE_LIST_MAX][FILE_NAME_MAX];
    bool file_is_directory[FILE_LIST_MAX];
    char current_directory[FILE_PATH_MAX];
    char file_name[FILE_NAME_MAX];
    char file_text[2 * FILE_TEXT_MAX + 1];
} ui_t;

static ui_t ui;

#ifndef PIM_ROOT
#define PIM_ROOT "/PIM"
#endif
typedef struct { const char *name; uint32_t fg, dim, dark, bg; } theme_t;
static const theme_t themes[] = {
    {"Amber", 0xffb000, 0x805800, 0x302100, 0x000000},
    {"Green", 0x33ff33, 0x1a801a, 0x0a330a, 0x000000},
    {"Manila", 0x000000, 0x837b62, 0xd2c59d, 0xefe0b3},
    {"Cyan", 0x40e0ff, 0x207080, 0x0c2a30, 0x000000},
    {"Violet", 0xc080ff, 0x604080, 0x251830, 0x000000},
    {"Red", 0xff4040, 0x802020, 0x300c0c, 0x000000},
    {"Mono", 0xe0e0e0, 0x707070, 0x2a2a2a, 0x000000},
    {"Paper", 0x1a1a1a, 0x868686, 0xd6d6d6, 0xf2f2f2},
};
#define THEME_COUNT (sizeof(themes) / sizeof(themes[0]))
#define TODAY_FILE PIM_ROOT "/today.cfg"
#define THEME_FILE PIM_ROOT "/theme.cfg"
#define SOUND_FILE PIM_ROOT "/sound.cfg"
static size_t theme_index;
#define CRT_AMBER (themes[theme_index].fg)
#define CRT_BG (themes[theme_index].bg)

extern DIR *opendir(const char *path);
extern struct dirent *readdir(DIR *directory);
extern int closedir(DIR *directory);

static void home(lv_event_t *event);
static void calendar(lv_event_t *event);
static void day_view(lv_event_t *event);
static void time_picker(lv_event_t *event);
static bool today_save(void);
static void date_picker(lv_event_t *event);
static void appointment_details(lv_event_t *event);
static void todos(lv_event_t *event);
static void today_screen(lv_event_t *event);
static void todo_list(lv_event_t *event);
static void notes(lv_event_t *event);
static void theme_screen(lv_event_t *event);
static void note_list(lv_event_t *event);
static void journal(lv_event_t *event);
static void journal_list(lv_event_t *event);
static void tetris(lv_event_t *event);
static void dungeon(lv_event_t *event);
static void games_screen(lv_event_t *event);
static void invaders(lv_event_t *event);
static void solitaire(lv_event_t *event);
static void file_list(lv_event_t *event);
static void file_editor(lv_event_t *event);
static void new_file(lv_event_t *event);
static void create_file(lv_event_t *event);
static void new_directory(lv_event_t *event);
static void create_directory(lv_event_t *event);
static void delete_entry(lv_event_t *event);
static void delete_named(lv_event_t *event);
static void directory_up(lv_event_t *event);
static void wifi(lv_event_t *event);
static void telnet(lv_event_t *event);
static void tools_screen(lv_event_t *event);
static void journal_write(lv_event_t *event);
static void recur_reload(void);
static void note_editor(lv_event_t *event);

static uint32_t now(void) { return (uint32_t)(to_ms_since_boot(get_absolute_time()) / 1000U); }
static void save(void) {
    if (ui.storage_path) {
        pim_storage_save(ui.storage_path, ui.database);
        pim_storage_export(PIM_ROOT, ui.database);
    }
}
static void crt_style(lv_obj_t *object, bool border) {
    lv_obj_set_style_bg_color(object, lv_color_hex(CRT_BG), 0);
    lv_obj_set_style_text_color(object, lv_color_hex(CRT_AMBER), 0);
    lv_obj_set_style_border_color(object, lv_color_hex(CRT_AMBER), 0);
    lv_obj_set_style_border_width(object, border ? 1 : 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_set_style_outline_width(object, 0, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(CRT_AMBER), LV_PART_CURSOR);
    lv_obj_set_style_border_color(object, lv_color_hex(CRT_AMBER), LV_PART_CURSOR);
    lv_obj_set_style_border_color(object, lv_color_hex(CRT_AMBER), LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(object, lv_color_hex(CRT_AMBER), LV_PART_CURSOR | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_color(object, lv_color_hex(CRT_AMBER), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_grad_dir(object, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_outline_color(object, lv_color_hex(CRT_AMBER), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(object, lv_color_hex(CRT_AMBER), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(object, border ? 1 : 0, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(object, border ? 1 : 0, LV_STATE_FOCUS_KEY);
}
static void crt_style_children(lv_obj_t *object) {
    if (object == NULL) return;
    uint32_t count = lv_obj_get_child_count(object);
    crt_style(object, false);
    for (uint32_t i = 0; i < count; i++) crt_style_children(lv_obj_get_child(object, (int32_t)i));
}
static lv_obj_t *message_return;
static void clear(void) {
    message_return = NULL;
    ui.battery_obj = NULL; ui.clock_obj = NULL;
    if (ui.timer) { lv_timer_delete(ui.timer); ui.timer = NULL; }
    ui.game = GAME_NONE; ui.terminal = false;
    pim_input_set_game_mode(false);
    lv_obj_clean(lv_screen_active());
    crt_style(lv_screen_active(), false);
}
static void focus(lv_obj_t *object) { lv_group_focus_obj(object); }
static void navigate(lv_event_t *event) {
    uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_ESC) { if (ui.date_origin != 2) home(NULL); return; }
    if (key == LV_KEY_UP || key == LV_KEY_LEFT) lv_group_focus_prev(lv_group_get_default());
    if (key == LV_KEY_DOWN || key == LV_KEY_RIGHT) lv_group_focus_next(lv_group_get_default());
}
static lv_obj_t *label(const char *text, int x, int y) {
    lv_obj_t *object = lv_label_create(lv_screen_active());
    lv_label_set_text(object, text); lv_obj_set_pos(object, x, y); crt_style(object, false); return object;
}
static lv_obj_t *button(const char *text, int x, int y, int width, lv_event_cb_t callback,
                        void *data) {
    lv_obj_t *object = lv_button_create(lv_screen_active());
    lv_obj_t *caption = lv_label_create(object);
    lv_label_set_text(caption, text); lv_obj_center(caption);
    lv_obj_set_size(object, width, 28); lv_obj_set_pos(object, x, y);
    lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, data);
    lv_obj_add_event_cb(object, navigate, LV_EVENT_KEY, NULL);
    crt_style(object, true); crt_style(caption, false);
    return object;
}
static lv_obj_t *text(const char *hint, int x, int y, int width, int height, bool one_line) {
    lv_obj_t *object = lv_textarea_create(lv_screen_active());
    lv_obj_set_style_text_color(object, lv_color_hex(CRT_AMBER), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_textarea_set_placeholder_text(object, hint); lv_textarea_set_one_line(object, one_line);
    lv_obj_set_size(object, width, height); lv_obj_set_pos(object, x, y); crt_style(object, true); return object;
}
static void style_dropdown_list(lv_event_t *event) {
    lv_obj_t *list = lv_dropdown_get_list(lv_event_get_target(event));
    if (list == NULL) return;
    crt_style(list, true);
    lv_obj_set_style_bg_color(list, lv_color_hex(CRT_AMBER), LV_PART_SELECTED);
    lv_obj_set_style_text_color(list, lv_color_hex(CRT_BG), LV_PART_SELECTED);
    lv_obj_set_style_bg_color(list, lv_color_hex(CRT_AMBER), LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(list, lv_color_hex(CRT_BG), LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_SELECTED | LV_STATE_CHECKED);
}
static void crt_dropdown(lv_obj_t *object) {
    crt_style(object, true);
    lv_obj_set_style_text_color(object, lv_color_hex(CRT_AMBER), LV_PART_INDICATOR);
    lv_obj_add_event_cb(object, style_dropdown_list, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(object, style_dropdown_list, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(object, style_dropdown_list, LV_EVENT_READY, NULL);
}
static bool safe_file_name(const char *name) {
    size_t length = strlen(name);
    if (length == 0 || length >= FILE_NAME_MAX || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0 || strcmp(name, "pim.dat") == 0 || strcmp(name, "wifi.cfg") == 0 || strcmp(name, "theme.cfg") == 0 || strcmp(name, "today.cfg") == 0 || strcmp(name, "sound.cfg") == 0 || strcmp(name, "recurring.cfg") == 0) return false;
    for (size_t i = 0; i < length; i++)
        if (!((name[i] >= 'a' && name[i] <= 'z') || (name[i] >= 'A' && name[i] <= 'Z') ||
              (name[i] >= '0' && name[i] <= '9') || name[i] == '.' || name[i] == '_' || name[i] == '-')) return false;
    return true;
}
static bool join_path(char *path, size_t path_size, const char *directory, const char *name) {
    size_t directory_length = strlen(directory);
    size_t name_length = strlen(name);
    if (directory_length == 0 || directory_length + 1 + name_length + 1 > path_size) return false;
    memcpy(path, directory, directory_length);
    path[directory_length] = '/';
    memcpy(&path[directory_length + 1], name, name_length + 1);
    return true;
}
static void style_list_item(lv_obj_t *item) {
    crt_style(item, true);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_color(item, lv_color_hex(CRT_AMBER), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(item, lv_color_hex(CRT_AMBER), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_text_color(item, lv_color_hex(CRT_BG), LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(item, lv_color_hex(CRT_BG), LV_STATE_FOCUS_KEY);
    lv_obj_add_event_cb(item, navigate, LV_EVENT_KEY, NULL);
}
static size_t index_from_event(lv_event_t *event) {
    return (size_t)((uintptr_t)lv_event_get_user_data(event) - 1U);
}
static void message_key(lv_event_t *event) {
    if (lv_event_get_key(event) == LV_KEY_ESC) lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(event));
}
static void message_closed(lv_event_t *event) {
    (void)event;
    if (message_return != NULL && lv_obj_is_valid(message_return)) focus(message_return);
    message_return = NULL;
}
static void message(const char *title, const char *body) {
    lv_obj_t *box = lv_msgbox_create(NULL);
    lv_obj_t *close;
    lv_obj_t *previous = lv_group_get_focused(lv_group_get_default());
    lv_msgbox_add_title(box, title); lv_msgbox_add_text(box, body); close = lv_msgbox_add_close_button(box);
    lv_obj_set_width(box, 304);
    crt_style(lv_obj_get_parent(box), false);
    crt_style(box, true);
    crt_style_children(lv_msgbox_get_header(box));
    crt_style_children(lv_msgbox_get_content(box));
    crt_style_children(lv_msgbox_get_footer(box));
    crt_style_children(close); crt_style(close, true);
    lv_obj_center(box);
    if (message_return == NULL) message_return = previous;
    lv_obj_add_event_cb(close, message_key, LV_EVENT_KEY, box);
    lv_obj_add_event_cb(box, message_closed, LV_EVENT_DELETE, NULL);
    focus(close);
}
/* The device has no RTC: the date and time chosen at startup advance with the uptime. */
static uint32_t clock_total_minutes(void) {
    return ui.base_minutes + (to_ms_since_boot(get_absolute_time()) - ui.base_ms) / 60000U;
}
static pim_date_t current_today(void) { return pim_date_shift(ui.today, (int)(clock_total_minutes() / 1440U)); }
static uint16_t current_minutes(void) { return (uint16_t)(clock_total_minutes() % 1440U); }
/* Opening these from Home starts on today; returning from sub-screens keeps the chosen date. */
static void home_calendar(lv_event_t *event) { (void)event; ui.calendar = current_today(); calendar(NULL); }
static void home_journal(lv_event_t *event) { (void)event; ui.calendar = current_today(); journal(NULL); }
static void battery_refresh(lv_timer_t *timer) {
    pim_battery_t battery;
    char text_value[40];
    (void)timer;
    if (ui.clock_obj != NULL) {
        pim_date_t today = current_today();
        snprintf(text_value, sizeof(text_value), "%02u-%02u-%04u  %02u:%02u", today.day, today.month, today.year,
                 current_minutes() / 60, current_minutes() % 60);
        lv_label_set_text(ui.clock_obj, text_value);
    }
    if (ui.battery_obj == NULL) return;
    if (!pim_input_battery(&battery)) snprintf(text_value, sizeof(text_value), "Battery: no data");
    else snprintf(text_value, sizeof(text_value), "Battery: %u%%%s", battery.percent,
                  battery.charging ? " charging" : battery.percent <= 15 ? " LOW" : "");
    lv_label_set_text(ui.battery_obj, text_value);
}
static lv_obj_t *sound_button;
static void sound_save(void) {
    FILE *file = fopen(SOUND_FILE, "wb");
    if (file == NULL) return;
    fprintf(file, "%d\n", pim_sound_enabled() ? 1 : 0);
    fclose(file);
}
static void sound_load(void) {
    int value;
    FILE *file = fopen(SOUND_FILE, "rb");
    if (file == NULL) return;
    if (fscanf(file, "%d", &value) == 1) pim_sound_set_enabled(value != 0);
    fclose(file);
}
static void sound_toggle(lv_event_t *event) {
    (void)event;
    pim_sound_set_enabled(!pim_sound_enabled());
    sound_save();
    lv_label_set_text(lv_obj_get_child(sound_button, 0), pim_sound_enabled() ? "Sound: ON" : "Sound: OFF");
    pim_sound_play(PIM_SOUND_SELECT);
}
static void home(lv_event_t *event) {
    lv_obj_t *first; (void)event; clear();
    label("PicoDeck OS", 105, 2);
    first = button("Calendar", 10, 22, 145, home_calendar, NULL);
    button("To-Do List", 165, 22, 145, todos, NULL);
    button("Notes", 10, 52, 145, notes, NULL);
    button("Journal", 165, 52, 145, home_journal, NULL);
    button("Files", 10, 82, 145, file_list, NULL);
    button("Text editor", 165, 82, 145, new_file, NULL);
    button("Tools", 10, 112, 145, tools_screen, NULL);
    button("Wi-Fi & web", 165, 112, 145, wifi, NULL);
    button("Today", 10, 142, 145, today_screen, NULL);
    button("Games", 165, 142, 145, games_screen, NULL);
    button("Theme", 10, 172, 145, theme_screen, NULL);
    button("Telnet (BBS)", 165, 172, 145, telnet, NULL);
    sound_button = button(pim_sound_enabled() ? "Sound: ON" : "Sound: OFF", 10, 202, 145, sound_toggle, NULL);
    {
        lv_obj_t *rule = lv_obj_create(lv_screen_active());
        lv_obj_t *hint = label("Arrows move  Enter open  Esc home", 0, 282);
        lv_obj_t *credit = label("Created by Aditya Kale", 0, 298);
        lv_obj_set_size(rule, 300, 1); lv_obj_set_pos(rule, 10, 238);
        lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0); lv_obj_set_style_border_width(rule, 0, 0);
        lv_obj_set_style_pad_all(rule, 0, 0); lv_obj_set_style_radius(rule, 0, 0);
        lv_obj_set_style_bg_color(rule, lv_color_hex(CRT_AMBER), 0);
        lv_obj_remove_flag(rule, LV_OBJ_FLAG_SCROLLABLE);
        ui.clock_obj = label("", 0, 246);
        ui.battery_obj = label("Battery: ...", 0, 263);
        lv_obj_t *centered[] = {hint, credit, ui.clock_obj, ui.battery_obj};
        for (size_t i = 0; i < 4; i++) {
            lv_obj_set_width(centered[i], 320);
            lv_obj_set_style_text_align(centered[i], LV_TEXT_ALIGN_CENTER, 0);
        }
    }
    battery_refresh(NULL);
    ui.timer = lv_timer_create(battery_refresh, 15000, NULL);
    focus(first);
}

static void format_date(char *out, size_t size, pim_date_t date) {
    snprintf(out, size, "%02u-%02u-%04u", date.day, date.month, date.year);
}
static void format_span(char *out, size_t size, const pim_appointment_t *value) {
    unsigned int end = value->minutes + value->duration;
    if (value->duration > 0)
        snprintf(out, size, "%02u:%02u-%02u:%02u", value->minutes / 60, value->minutes % 60, end / 60, end % 60);
    else snprintf(out, size, "%02u:%02u", value->minutes / 60, value->minutes % 60);
}
static void clamp_calendar_day(void) {
    uint8_t last = pim_days_in_month(ui.calendar.year, ui.calendar.month);
    if (ui.calendar.day > last) ui.calendar.day = last;
}
static void change_month(lv_event_t *event) {
    intptr_t delta = (intptr_t)lv_event_get_user_data(event);
    if (delta < 0 && --ui.calendar.month == 0) { ui.calendar.month = 12; ui.calendar.year--; }
    if (delta > 0 && ++ui.calendar.month == 13) { ui.calendar.month = 1; ui.calendar.year++; }
    if (ui.calendar.year < 2000) { ui.calendar.year = 2000; ui.calendar.month = 1; }
    clamp_calendar_day();
    calendar(NULL);
}
static void open_day(lv_event_t *event) {
    ui.calendar.day = (uint8_t)((uintptr_t)lv_event_get_user_data(event));
    day_view(NULL);
}
static void calendar_open(pim_date_t date) { ui.calendar = date; calendar(NULL); }
static void today_button(lv_event_t *event) { (void)event; calendar_open(current_today()); }
static void date_picker_calendar(lv_event_t *event) { (void)event; ui.date_origin = 0; date_picker(NULL); }
static void date_picker_journal(lv_event_t *event) { (void)event; ui.date_origin = 1; date_picker(NULL); }
/* Arrows scroll continuously through dates; , . change month, < > change year, T is today, G is go-to. */
static void day_key(lv_event_t *event) {
    uint32_t key = lv_event_get_key(event);
    pim_date_t target = ui.calendar;
    if (key == LV_KEY_ESC) { home(NULL); return; }
    if (key == LV_KEY_LEFT) target = pim_date_shift(target, -1);
    else if (key == LV_KEY_RIGHT) target = pim_date_shift(target, 1);
    else if (key == LV_KEY_UP) target = pim_date_shift(target, -7);
    else if (key == LV_KEY_DOWN) target = pim_date_shift(target, 7);
    else if (key == ',') target = pim_date_add_months(target, -1);
    else if (key == '.') target = pim_date_add_months(target, 1);
    else if (key == '<') target = pim_date_add_months(target, -12);
    else if (key == '>') target = pim_date_add_months(target, 12);
    else if (key == 't' || key == 'T') target = current_today();
    else if (key == 'g' || key == 'G') { date_picker_calendar(NULL); return; }
    else return;
    if (target.year > 2099) return;
    if (target.year == ui.calendar.year && target.month == ui.calendar.month) {
        ui.calendar = target;
        focus(ui.days[target.day - 1]);
    } else calendar_open(target);
}
static void nav_key(lv_event_t *event) {
    uint32_t key = lv_event_get_key(event);
    intptr_t which = (intptr_t)lv_event_get_user_data(event);
    if (key == LV_KEY_ESC) { home(NULL); return; }
    if (which >= 2) {
        if (key == LV_KEY_UP) focus(ui.days[ui.calendar.day - 1]);
        else if (key == LV_KEY_LEFT) focus(ui.nav[which == 4 ? 2 : 3]);
        else if (key == LV_KEY_RIGHT) focus(ui.nav[which == 3 ? 2 : 4]);
        return;
    }
    if (key == LV_KEY_DOWN) focus(ui.days[ui.calendar.day - 1]);
    else if (key == LV_KEY_LEFT) focus(ui.nav[which == 1 ? 0 : 2]);
    else if (key == LV_KEY_RIGHT) focus(ui.nav[which == 0 ? 1 : 2]);
}
static void calendar(lv_event_t *event) {
    static const char *const names[] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
    char heading[20]; (void)event; clear();
    clamp_calendar_day();
    snprintf(heading, sizeof(heading), "%02u-%04u", ui.calendar.month, ui.calendar.year);
    label("Calendar", 120, 4); label(heading, 126, 34);
    ui.nav[0] = button("<", 12, 28, 52, change_month, (void *)(intptr_t)-1);
    ui.nav[1] = button(">", 256, 28, 52, change_month, (void *)(intptr_t)1);
    ui.nav[2] = button("Back", 112, 286, 95, home, NULL);
    ui.nav[3] = button("Go to", 8, 286, 95, date_picker_calendar, NULL);
    ui.nav[4] = button("Today", 217, 286, 95, today_button, NULL);
    for (int i = 0; i < 5; i++) lv_obj_add_event_cb(ui.nav[i], nav_key, LV_EVENT_KEY, (void *)(intptr_t)i);
    for (uint8_t column = 0; column < 7; column++) label(names[column], (int)(14 + column * 43), 62);
    uint8_t first_day = pim_first_day_of_month(ui.calendar.year, ui.calendar.month);
    uint8_t days = pim_days_in_month(ui.calendar.year, ui.calendar.month);
    for (uint8_t day = 1; day <= days; day++) {
        pim_date_t date = {ui.calendar.year, ui.calendar.month, day};
        char number[6]; uint8_t cell = (uint8_t)(first_day + day - 1);
        lv_obj_t *item, *caption;
        snprintf(number, sizeof(number), pim_appointments_on(ui.database, date, NULL, 0) ? "%u*" : "%u", day);
        item = lv_button_create(lv_screen_active()); caption = lv_label_create(item);
        lv_label_set_text(caption, number); lv_obj_center(caption);
        lv_obj_set_size(item, 42, 28); lv_obj_set_pos(item, 8 + (cell % 7) * 43, 80 + (cell / 7) * 34);
        crt_style(item, true); crt_style(caption, false);
        lv_obj_add_event_cb(item, open_day, LV_EVENT_CLICKED, (void *)(uintptr_t)day);
        lv_obj_add_event_cb(item, day_key, LV_EVENT_KEY, (void *)(uintptr_t)day);
        ui.days[day - 1] = item;
    }
    focus(ui.days[ui.calendar.day - 1]);
}

static void delete_appointment(lv_event_t *event) {
    pim_delete_appointment(ui.database, index_from_event(event)); save(); day_view(NULL);
}
static void appointment_detail(lv_event_t *event) {
    ui.selected = index_from_event(event); pim_appointment_t *value = &ui.database->appointments[ui.selected];
    char detail[240], date[16], span[16]; lv_obj_t *first; clear();
    format_date(date, sizeof(date), value->date); format_span(span, sizeof(span), value);
    snprintf(detail, sizeof(detail), "%s %s\n%s\n\n%s", date, span, value->title, value->description);
    label(detail, 12, 28);
    first = button("Delete", 20, 260, 100, delete_appointment, (void *)(uintptr_t)(ui.selected + 1));
    button("Back", 190, 260, 100, day_view, NULL); focus(first);
}
static void new_journal_for_day(lv_event_t *event) { (void)event; journal(NULL); }
static void day_view(lv_event_t *event) {
    size_t indices[PIM_MAX_APPOINTMENTS], count;
    lv_obj_t *list, *first; char heading[24]; (void)event; clear();
    format_date(heading, sizeof(heading), ui.calendar);
    label(heading, 118, 6);
    first = button("New", 12, 284, 90, time_picker, NULL);
    button("Journal", 116, 284, 90, new_journal_for_day, NULL); button("Back", 220, 284, 90, calendar, NULL);
    list = lv_list_create(lv_screen_active()); lv_obj_set_pos(list, 8, 30); lv_obj_set_size(list, 304, 246); crt_style(list, true);
    count = pim_appointments_on(ui.database, ui.calendar, indices, PIM_MAX_APPOINTMENTS);
    if (count == 0) label("No appointments.\nSelect New to add one.", 20, 50);
    for (size_t i = 0; i < count; i++) {
        pim_appointment_t *value = &ui.database->appointments[indices[i]]; char row[100], span[16];
        format_span(span, sizeof(span), value);
        snprintf(row, sizeof(row), "%s %s", span, value->title);
        lv_obj_t *item = lv_list_add_button(list, NULL, row);
        lv_obj_add_event_cb(item, appointment_detail, LV_EVENT_CLICKED, (void *)(uintptr_t)(indices[i] + 1)); style_list_item(item);
    }
    focus(first);
}

static const uint16_t durations[] = {15, 30, 45, 60, 90, 120, 180, 240};
#define DURATION_COUNT (sizeof(durations) / sizeof(durations[0]))
static void spinner_text(char *out, size_t size, int which) {
    if (which == 0) snprintf(out, size, "%02u", ui.spin[0]);
    else if (which == 1) snprintf(out, size, "%02u", ui.spin[1]);
    else if (durations[ui.spin[2]] % 60 == 0) snprintf(out, size, "%uh", durations[ui.spin[2]] / 60);
    else if (durations[ui.spin[2]] > 60) snprintf(out, size, "%uh%02u", durations[ui.spin[2]] / 60, durations[ui.spin[2]] % 60);
    else snprintf(out, size, "%um", durations[ui.spin[2]]);
}
static void refresh_picker(void) {
    char text_value[16], span[24];
    pim_appointment_t preview = {.minutes = (uint16_t)(ui.spin[0] * 60U + ui.spin[1]), .duration = durations[ui.spin[2]]};
    for (int i = 0; i < 3; i++) {
        spinner_text(text_value, sizeof(text_value), i);
        lv_label_set_text(lv_obj_get_child(ui.spin_obj[i], 0), text_value);
    }
    format_span(span, sizeof(span), &preview);
    lv_label_set_text(ui.preview, span);
}
static void spinner_key(lv_event_t *event) {
    static const uint8_t limits[] = {24, 60, DURATION_COUNT};
    uint32_t key = lv_event_get_key(event);
    int which = (int)(intptr_t)lv_event_get_user_data(event);
    if (key == LV_KEY_ESC) { day_view(NULL); return; }
    if (key >= '0' && key <= '9' && which < 2) {
        ui.spin[which] = (uint8_t)pim_entry_digit(ui.spin[which], &ui.spin_digits[which], key - '0', which == 0 ? 23 : 59);
        refresh_picker();
        return;
    }
    ui.spin_digits[0] = ui.spin_digits[1] = 0;
    if (key == LV_KEY_UP || key == LV_KEY_DOWN) {
        bool up = key == LV_KEY_UP;
        if (which == 1) ui.spin[1] = (uint8_t)(up ? (ui.spin[1] / 5 * 5 + 5) % 60 : (ui.spin[1] % 5 ? ui.spin[1] / 5 * 5 : (ui.spin[1] + 55) % 60));
        else ui.spin[which] = (uint8_t)((ui.spin[which] + (up ? 1 : limits[which] - 1)) % limits[which]);
    }
    else if (key == LV_KEY_LEFT) { lv_group_focus_prev(lv_group_get_default()); return; }
    else if (key == LV_KEY_RIGHT) { lv_group_focus_next(lv_group_get_default()); return; }
    else return;
    refresh_picker();
}
static void spinner_enter(lv_event_t *event) { (void)event; ui.spin_digits[0] = ui.spin_digits[1] = 0; lv_group_focus_next(lv_group_get_default()); }
static void time_picker(lv_event_t *event) {
    static const char *const names[] = {"Hour", "Minute", "Length"};
    lv_obj_t *first; char date[16]; (void)event; clear();
    ui.spin_digits[0] = ui.spin_digits[1] = 0;
    format_date(date, sizeof(date), ui.calendar);
    label("New appointment", 92, 6); label(date, 118, 30);
    for (int i = 0; i < 3; i++) {
        lv_obj_t *caption;
        label(names[i], 36 + i * 102, 58);
        ui.spin_obj[i] = lv_button_create(lv_screen_active()); caption = lv_label_create(ui.spin_obj[i]);
        lv_label_set_text(caption, ""); lv_obj_center(caption);
        lv_obj_set_size(ui.spin_obj[i], 90, 40); lv_obj_set_pos(ui.spin_obj[i], 12 + i * 102, 78);
        crt_style(ui.spin_obj[i], true); crt_style(caption, false);
        lv_obj_add_event_cb(ui.spin_obj[i], spinner_key, LV_EVENT_KEY, (void *)(intptr_t)i);
        lv_obj_add_event_cb(ui.spin_obj[i], spinner_enter, LV_EVENT_CLICKED, NULL);
    }
    ui.preview = label("", 108, 130);
    label("UP/DOWN or type digits", 60, 160); label("LEFT/RIGHT/ENTER next", 60, 180);
    first = ui.spin_obj[0];
    button("Next", 12, 220, 90, appointment_details, NULL); button("Back", 220, 220, 90, day_view, NULL);
    refresh_picker(); focus(first);
}

static void save_appointment(lv_event_t *event) {
    pim_appointment_t value = {0}; (void)event;
    value.date = ui.calendar; value.minutes = (uint16_t)(ui.spin[0] * 60U + ui.spin[1]);
    value.duration = durations[ui.spin[2]];
    snprintf(value.title, sizeof(value.title), "%s", lv_textarea_get_text(ui.title));
    snprintf(value.description, sizeof(value.description), "%s", lv_textarea_get_text(ui.content));
    if (value.title[0] == '\0') { message("Title needed", "Enter a title for the appointment."); return; }
    if (!pim_add_appointment(ui.database, &value)) { message("Not saved", "Ends after midnight or list is full."); return; }
    save(); day_view(NULL);
}
static void appointment_details(lv_event_t *event) {
    lv_obj_t *first; char heading[40], span[24]; pim_appointment_t preview = {.minutes = (uint16_t)(ui.spin[0] * 60U + ui.spin[1]), .duration = durations[ui.spin[2]]};
    (void)event; clear();
    format_span(span, sizeof(span), &preview);
    snprintf(heading, sizeof(heading), "%02u-%02u-%04u %s", ui.calendar.day, ui.calendar.month, ui.calendar.year, span);
    label(heading, 60, 6);
    ui.title = text("Title", 12, 32, 296, 28, true); ui.content = text("Notes", 12, 70, 296, 110, false);
    label("TAB moves between fields", 60, 190);
    first = ui.title;
    button("Save", 12, 220, 90, save_appointment, NULL); button("Back", 220, 220, 90, time_picker, NULL);
    focus(first);
}

static void save_todo(lv_event_t *event) {
    pim_todo_t value = {0}; (void)event;
    snprintf(value.title, sizeof(value.title), "%s", lv_textarea_get_text(ui.title));
    value.priority = (pim_priority_t)lv_dropdown_get_selected(ui.choice); value.created_at = now();
    {
        static const int offset[] = {-1, 0, 1, 3, 7, 14};
        uint32_t pick = lv_dropdown_get_selected(ui.fld_obj[0]);
        if (pick > 0 && pick < sizeof(offset) / sizeof(offset[0])) pim_todo_set_due(&value, pim_date_shift(current_today(), offset[pick]));
    }
    if (!pim_add_todo(ui.database, &value)) { message("Not saved", "Add a title or remove an old record."); return; }
    save(); todos(NULL);
}
static void todo_detail(lv_event_t *event);
static void toggle_todo(lv_event_t *event) { pim_toggle_todo(ui.database, index_from_event(event)); save(); todo_list(NULL); }
static void delete_todo(lv_event_t *event) { pim_delete_todo(ui.database, index_from_event(event)); save(); todo_list(NULL); }
static void todo_detail(lv_event_t *event) {
    ui.selected = index_from_event(event); pim_todo_t *value = &ui.database->todos[ui.selected]; char detail[128]; lv_obj_t *first; clear();
    pim_date_t due; char due_text[24] = "none";
    if (pim_todo_due(value, &due)) format_date(due_text, sizeof(due_text), due);
    snprintf(detail, sizeof(detail), "%s\nPriority: %s\nDue: %s\nStatus: %s", value->title, pim_priority_name(value->priority),
             due_text, value->completed ? "Complete" : "Pending");
    label(detail, 20, 50);
    first = button(value->completed ? "Mark pending" : "Mark complete", 18, 242, 125, toggle_todo,
                   (void *)(uintptr_t)(ui.selected + 1));
    button("Delete", 153, 242, 70, delete_todo, (void *)(uintptr_t)(ui.selected + 1));
    button("Back", 233, 242, 70, todo_list, NULL); focus(first);
}
static void todo_list(lv_event_t *event) {
    lv_obj_t *list, *first; (void)event; clear(); label("To-Do List", 120, 8);
    first = button("Back", 112, 280, 95, todos, NULL);
    list = lv_list_create(lv_screen_active()); lv_obj_set_pos(list, 8, 30); lv_obj_set_size(list, 304, 246); crt_style(list, true);
    for (size_t i = 0; i < ui.database->todo_count; i++) {
        pim_todo_t *value = &ui.database->todos[i]; char row[96];
        pim_date_t due; char due_text[16] = "";
        if (pim_todo_due(value, &due)) snprintf(due_text, sizeof(due_text), " %02u-%02u", due.day, due.month);
        snprintf(row, sizeof(row), "%s %s [%s]%s", value->completed ? "[x]" : "[ ]", value->title,
                 pim_priority_name(value->priority), due_text);
        lv_obj_t *item = lv_list_add_button(list, NULL, row);
        lv_obj_add_event_cb(item, todo_detail, LV_EVENT_CLICKED, (void *)(uintptr_t)(i + 1)); style_list_item(item);
    }
    focus(first);
}
static void todos(lv_event_t *event) {
    lv_obj_t *first; (void)event; clear(); label("New to-do", 118, 12);
    ui.title = text("Task", 12, 48, 296, 32, true); ui.choice = lv_dropdown_create(lv_screen_active());
    lv_dropdown_set_options(ui.choice, "Low\nNormal\nHigh"); lv_dropdown_set_selected(ui.choice, PIM_PRIORITY_NORMAL, LV_ANIM_OFF); crt_dropdown(ui.choice);
    lv_obj_set_pos(ui.choice, 12, 94); lv_obj_set_size(ui.choice, 140, 32);
    ui.fld_obj[0] = lv_dropdown_create(lv_screen_active());
    lv_dropdown_set_options(ui.fld_obj[0], "No due date\nToday\nTomorrow\nIn 3 days\nIn a week\nIn 2 weeks"); crt_dropdown(ui.fld_obj[0]);
    lv_obj_set_pos(ui.fld_obj[0], 160, 94); lv_obj_set_size(ui.fld_obj[0], 148, 32);
    first = button("Add", 12, 148, 90, save_todo, NULL);
    button("View", 116, 148, 90, todo_list, NULL); button("Back", 220, 148, 90, home, NULL); focus(first);
}

static void note_editor(lv_event_t *event);
static void save_note(lv_event_t *event) {
    pim_note_t value = {0}; (void)event;
    snprintf(value.title, sizeof(value.title), "%s", lv_textarea_get_text(ui.title));
    snprintf(value.content, sizeof(value.content), "%s", lv_textarea_get_text(ui.content));
    value.modified_at = now();
    bool saved;
    if (ui.selected < ui.database->note_count) {
        value.created_at = ui.database->notes[ui.selected].created_at;
        saved = pim_update_note(ui.database, ui.selected, &value);
    } else { value.created_at = value.modified_at; saved = pim_add_note(ui.database, &value); }
    if (!saved) { message("Not saved", "Add a title or remove an old record."); return; }
    save(); note_list(NULL);
}
static void delete_note(lv_event_t *event) {
    char path[64];
    uint32_t id = ui.database->notes[ui.selected].id;
    (void)event;
    pim_delete_note(ui.database, ui.selected);
    if (ui.storage_path) {
        snprintf(path, sizeof(path), PIM_ROOT "/notes/note-%08lu.md", (unsigned long)id);
        remove(path);
    }
    save();
    note_list(NULL);
}
static void select_note(lv_event_t *event) { ui.selected = index_from_event(event); note_editor(NULL); }
static void note_list(lv_event_t *event) {
    lv_obj_t *list, *first; (void)event; clear(); label("Notes", 140, 8);
    first = button("New", 12, 280, 90, notes, NULL); button("Back", 218, 280, 90, home, NULL);
    list = lv_list_create(lv_screen_active()); lv_obj_set_pos(list, 8, 30); lv_obj_set_size(list, 304, 246); crt_style(list, true);
    for (size_t i = 0; i < ui.database->note_count; i++) {
        lv_obj_t *item = lv_list_add_button(list, NULL, ui.database->notes[i].title);
        lv_obj_add_event_cb(item, select_note, LV_EVENT_CLICKED, (void *)(uintptr_t)(i + 1)); style_list_item(item);
    }
    focus(first);
}
static void note_editor(lv_event_t *event) {
    lv_obj_t *first; (void)event; clear(); label(ui.selected < ui.database->note_count ? "Edit note" : "New note", 120, 6);
    ui.title = text("Title", 12, 32, 296, 28, true); ui.content = text("Content", 12, 72, 296, 124, false);
    if (ui.selected < ui.database->note_count) {
        lv_textarea_set_text(ui.title, ui.database->notes[ui.selected].title);
        lv_textarea_set_text(ui.content, ui.database->notes[ui.selected].content);
    }
    first = button("Save", 12, 212, 90, save_note, NULL);
    if (ui.selected < ui.database->note_count) button("Delete", 116, 212, 90, delete_note, NULL);
    button("Back", 220, 212, 90, note_list, NULL); focus(first);
}
static void notes(lv_event_t *event) { (void)event; ui.selected = ui.database->note_count; note_editor(NULL); }

/* ---- Paged text editor: files of any size, one page at a time ---- */
#define PAGE_STACK 256
static struct {
    char path[FILE_PATH_MAX], title[FILE_NAME_MAX];
    uint32_t offset, stack[PAGE_STACK];
    size_t length;
    unsigned depth;
    bool journal;
    lv_event_cb_t back;
} ed;

static void editor_show(void);
static void editor_open(const char *path, const char *title, bool journal, lv_event_cb_t back) {
    snprintf(ed.path, sizeof(ed.path), "%s", path);
    snprintf(ed.title, sizeof(ed.title), "%s", title);
    ed.offset = 0; ed.depth = 0; ed.length = 0; ed.journal = journal; ed.back = back;
    editor_show();
}
/* Writes the page back only when it changed, so browsing a large file never rewrites it. */
static bool editor_commit(void) {
    const char *current = lv_textarea_get_text(ui.content);
    size_t length = strlen(current);
    uint32_t size;
    if (strcmp(current, ui.file_text) == 0) return true;
    if (length >= sizeof(ui.file_text) ||
        !pim_text_replace(ed.path, ed.offset, (uint32_t)ed.length, current, length)) {
        char why[64];
        snprintf(why, sizeof(why), "Write failed (error %d).\nTry again or free space.", pim_text_last_errno());
        message("Not saved", why);
        return false;
    }
    memcpy(ui.file_text, current, length + 1);
    ed.length = length;
    if (ed.journal && pim_text_size(ed.path, &size) && size == 0) remove(ed.path);
    return true;
}
static void editor_save(lv_event_t *event) { (void)event; if (editor_commit()) ed.back(NULL); }
static void editor_cancel(lv_event_t *event) { (void)event; ed.back(NULL); }
static void editor_delete(lv_event_t *event) {
    (void)event;
    if (remove(ed.path) != 0) { message("Not deleted", "The file may not exist yet."); return; }
    ed.back(NULL);
}
static void editor_next(lv_event_t *event) {
    uint32_t size = 0, next;
    (void)event;
    if (!editor_commit()) return;
    pim_text_size(ed.path, &size);
    next = ed.offset + (uint32_t)ed.length;
    if (next >= size && ed.length == 0) return;
    if (ed.depth < PAGE_STACK) ed.stack[ed.depth] = ed.offset;
    ed.depth++;
    ed.offset = next;
    editor_show();
}
static void editor_previous(lv_event_t *event) {
    (void)event;
    if (ed.depth == 0 || !editor_commit()) return;
    ed.depth--;
    ed.offset = ed.depth < PAGE_STACK ? ed.stack[ed.depth] : pim_text_previous(ed.path, ed.offset);
    editor_show();
}
static void editor_show(void) {
    char head[64];
    size_t length = 0;
    uint32_t size = 0;
    bool more = false;
    lv_obj_t *first;
    clear();
    if (!pim_text_page(ed.path, ed.offset, ui.file_text, PIM_PAGE_BYTES, &length, &more)) { ui.file_text[0] = '\0'; length = 0; }
    ed.length = length;
    pim_text_size(ed.path, &size);
    snprintf(head, sizeof(head), "%.22s  page %u  %u%%", ed.title, ed.depth + 1,
             size == 0 ? 0U : (unsigned)(((uint64_t)ed.offset + length) * 100U / size));
    label(head, 8, 6);
    ui.content = text("Type here", 0, 24, 320, 206, false);
    lv_obj_set_style_pad_hor(ui.content, 0, 0);
    lv_obj_set_style_border_width(ui.content, 0, 0);
    lv_textarea_set_max_length(ui.content, 2 * PIM_PAGE_BYTES - 1);
    lv_obj_set_style_text_font(ui.content, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_line_space(ui.content, 0, 0);
    lv_obj_set_scrollbar_mode(ui.content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(ui.content, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_textarea_set_text(ui.content, ui.file_text);
    first = button("Save", 12, 234, 96, editor_save, NULL);
    button("Prev", 112, 234, 96, editor_previous, NULL);
    button("Next", 212, 234, 96, editor_next, NULL);
    button("Delete", 12, 266, 140, editor_delete, NULL);
    button(ed.journal ? "Journal" : "Files", 168, 266, 140, editor_cancel, NULL);
    focus(first);
}

/* ---- Journal: one free-form text file per day ---- */
static void journal_write(lv_event_t *event) {
    char path[FILE_PATH_MAX], title[FILE_NAME_MAX];
    (void)event;
    if (ui.storage_path == NULL) { message("No SD card", "The journal needs SD storage."); return; }
    mkdir(PIM_ROOT "/journal", 0777);
    if (!pim_journal_path(PIM_ROOT, ui.calendar, path, sizeof(path))) return;
    format_date(title, sizeof(title), ui.calendar);
    editor_open(path, title, true, journal);
}
static void journal_open_entry(lv_event_t *event) {
    pim_date_t *dates = (pim_date_t *)lv_event_get_user_data(event);
    ui.calendar = *dates;
    journal_write(NULL);
}
static void journal_remove(lv_event_t *event) {
    (void)event;
    if (!pim_journal_delete(PIM_ROOT, ui.calendar)) message("Not deleted", "There is no entry for this day.");
    journal(NULL);
}
#define JOURNAL_LIST_MAX 40
static void journal_list(lv_event_t *event) {
    static pim_date_t dates[JOURNAL_LIST_MAX];
    lv_obj_t *list, *first;
    size_t total = 0, count;
    (void)event; clear(); label("Journal entries", 100, 8);
    first = button("Back", 112, 280, 95, journal, NULL);
    list = lv_list_create(lv_screen_active()); lv_obj_set_pos(list, 8, 30); lv_obj_set_size(list, 304, 246); crt_style(list, true);
    count = pim_journal_list(PIM_ROOT, dates, JOURNAL_LIST_MAX, &total);
    for (size_t i = 0; i < count; i++) {
        char row[80], date[16], preview[32];
        format_date(date, sizeof(date), dates[i]);
        pim_journal_preview(PIM_ROOT, dates[i], preview, sizeof(preview));
        snprintf(row, sizeof(row), "%s %s", date, preview);
        lv_obj_t *item = lv_list_add_button(list, NULL, row);
        lv_obj_add_event_cb(item, journal_open_entry, LV_EVENT_CLICKED, &dates[i]); style_list_item(item);
    }
    if (total > count) {
        char more[40];
        snprintf(more, sizeof(more), "%u older entries not shown", (unsigned)(total - count));
        label(more, 12, 262);
    }
    focus(first);
}
static void journal_shift(lv_event_t *event) {
    ui.calendar = pim_date_shift(ui.calendar, (int)(intptr_t)lv_event_get_user_data(event));
    journal(NULL);
}
static void journal(lv_event_t *event) {
    lv_obj_t *first, *preview; char date[16], path[FILE_PATH_MAX], body[200] = "";
    size_t length; bool more, exists; (void)event; clear(); label("Journal", 132, 6);
    format_date(date, sizeof(date), ui.calendar);
    first = button("<", 12, 32, 52, journal_shift, (void *)(intptr_t)-1);
    button(date, 76, 32, 168, date_picker_journal, NULL); button(">", 256, 32, 52, journal_shift, (void *)(intptr_t)1);
    exists = pim_journal_exists(PIM_ROOT, ui.calendar);
    if (exists && pim_journal_path(PIM_ROOT, ui.calendar, path, sizeof(path))) pim_text_page(path, 0, body, sizeof(body), &length, &more);
    preview = label(exists ? body : "No entry for this day.\nSelect Write to start one.", 12, 76);
    lv_obj_set_size(preview, 296, 130); lv_label_set_long_mode(preview, LV_LABEL_LONG_DOT);
    button(exists ? "Edit" : "Write", 12, 222, 90, journal_write, NULL);
    button("View", 116, 222, 90, journal_list, NULL); button("Back", 220, 222, 90, home, NULL);
    if (exists) button("Delete", 12, 254, 90, journal_remove, NULL);
    focus(first);
}

static void file_editor(lv_event_t *event) {
    char path[FILE_PATH_MAX];
    (void)event;
    if (!join_path(path, sizeof(path), ui.current_directory, ui.file_name)) {
        message("Path too long", "Use a shallower directory.");
        return;
    }
    editor_open(path, ui.file_name, false, file_list);
}
static void open_file(lv_event_t *event) {
    char path[FILE_PATH_MAX];
    size_t index = index_from_event(event);
    if (index >= FILE_LIST_MAX) return;
    if (ui.file_is_directory[index]) {
        char path[FILE_PATH_MAX];
        if (!join_path(path, sizeof(path), ui.current_directory, ui.file_names[index])) {
            message("Path too long", "Use a shallower directory.");
            return;
        }
        memcpy(ui.current_directory, path, strlen(path) + 1);
        file_list(NULL);
        return;
    }
    snprintf(ui.file_name, sizeof(ui.file_name), "%s", ui.file_names[index]);
    if (!join_path(path, sizeof(path), ui.current_directory, ui.file_name)) {
        message("Path too long", "Use a shallower directory.");
        return;
    }
    file_editor(NULL);
}
static void create_file(lv_event_t *event) {
    char path[FILE_PATH_MAX];
    FILE *file;
    const char *name = lv_textarea_get_text(ui.title);
    (void)event;
    if (!safe_file_name(name)) {
        message("Invalid name", "Use letters, numbers, dots, dashes, or underscores.");
        return;
    }
    if (!join_path(path, sizeof(path), ui.current_directory, name)) {
        message("Path too long", "Use a shallower directory.");
        return;
    }
    file = fopen(path, "rb");
    if (file != NULL) {
        fclose(file);
        message("Already exists", "Choose another name or open the existing file.");
        return;
    }
    file = fopen(path, "wb");
    if (file == NULL || fclose(file) != 0) {
        message("Not created", "Check that the SD card is available.");
        return;
    }
    snprintf(ui.file_name, sizeof(ui.file_name), "%s", name);
    file_editor(NULL);
}
static void new_file(lv_event_t *event) {
    lv_obj_t *first;
    (void)event;
    clear();
    if (ui.current_directory[0] == '\0') snprintf(ui.current_directory, sizeof(ui.current_directory), "%s", PIM_ROOT);
    label("Create text file", 92, 12);
    ui.title = text("name.txt", 12, 52, 296, 28, true);
    first = button("Create", 12, 102, 110, create_file, NULL);
    button("Files", 190, 102, 110, file_list, NULL);
    focus(first);
}
static void create_directory(lv_event_t *event) {
    char path[FILE_PATH_MAX];
    const char *name = lv_textarea_get_text(ui.title);
    (void)event;
    if (!safe_file_name(name)) {
        message("Invalid name", "Use letters, numbers, dots, dashes, or underscores.");
        return;
    }
    if (!join_path(path, sizeof(path), ui.current_directory, name)) {
        message("Path too long", "Use a shallower directory.");
        return;
    }
    if (mkdir(path, 0777) != 0) {
        message("Not created", "The directory may already exist.");
        return;
    }
    file_list(NULL);
}
static void new_directory(lv_event_t *event) {
    lv_obj_t *first;
    (void)event;
    clear();
    label("Create directory", 86, 12);
    ui.title = text("directory-name", 12, 52, 296, 28, true);
    first = button("Create", 12, 102, 110, create_directory, NULL);
    button("Files", 190, 102, 110, file_list, NULL);
    focus(first);
}
static bool protected_directory(const char *name) {
    static const char *const names[] = {"appointments", "backup", "calendar", "games", "journal", "notes", "todos"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (strcmp(name, names[i]) == 0) return true;
    return false;
}
static void delete_named(lv_event_t *event) {
    char path[FILE_PATH_MAX];
    struct stat status;
    const char *name = lv_textarea_get_text(ui.title);
    (void)event;
    if (!safe_file_name(name) ||
        (strcmp(ui.current_directory, PIM_ROOT) == 0 && protected_directory(name))) {
        message("Protected", "This name cannot be deleted here.");
        return;
    }
    if (!join_path(path, sizeof(path), ui.current_directory, name)) {
        message("Path too long", "Use a shallower directory.");
        return;
    }
    if (stat(path, &status) != 0 ||
        (S_ISDIR(status.st_mode) ? rmdir(path) : unlink(path)) != 0) {
        message("Not deleted", "Directories must be empty.");
        return;
    }
    file_list(NULL);
}
static void delete_entry(lv_event_t *event) {
    lv_obj_t *first;
    (void)event;
    clear();
    label("Delete file or empty directory", 42, 12);
    ui.title = text("exact name", 12, 52, 296, 28, true);
    first = button("Delete", 12, 102, 110, delete_named, NULL);
    button("Files", 190, 102, 110, file_list, NULL);
    focus(first);
}
static void directory_up(lv_event_t *event) {
    char *slash;
    (void)event;
    if (strcmp(ui.current_directory, PIM_ROOT) == 0) {
        home(NULL);
        return;
    }
    slash = strrchr(ui.current_directory, '/');
    if (slash != NULL && slash != ui.current_directory) *slash = '\0';
    file_list(NULL);
}
static void file_list(lv_event_t *event) {
    DIR *directory;
    struct dirent *entry;
    lv_obj_t *list, *first;
    size_t count = 0;
    (void)event;
    clear();
    if (ui.current_directory[0] == '\0') snprintf(ui.current_directory, sizeof(ui.current_directory), "%s", PIM_ROOT);
    label(ui.current_directory, 8, 8);
    first = button("New", 8, 274, 72, new_file, NULL);
    button("Dir", 84, 274, 72, new_directory, NULL);
    button("Delete", 160, 274, 72, delete_entry, NULL);
    button(strcmp(ui.current_directory, PIM_ROOT) == 0 ? "Home" : "Up", 236, 274, 76,
           directory_up, NULL);
    list = lv_list_create(lv_screen_active());
    lv_obj_set_pos(list, 8, 30);
    lv_obj_set_size(list, 304, 234);
    crt_style(list, true);
    directory = opendir(ui.current_directory);
    if (directory != NULL) {
        while (count < FILE_LIST_MAX && (entry = readdir(directory)) != NULL) {
            lv_obj_t *item;
            size_t name_length = strlen(entry->d_name);
            if ((entry->d_type != DT_REG && entry->d_type != DT_DIR) ||
                name_length >= FILE_NAME_MAX || !safe_file_name(entry->d_name)) continue;
            memcpy(ui.file_names[count], entry->d_name, name_length + 1);
            ui.file_is_directory[count] = entry->d_type == DT_DIR;
            char row[FILE_NAME_MAX + 7];
            snprintf(row, sizeof(row), "%s%s", ui.file_is_directory[count] ? "[DIR] " : "",
                     ui.file_names[count]);
            item = lv_list_add_button(list, NULL, row);
            lv_obj_add_event_cb(item, open_file, LV_EVENT_CLICKED, (void *)(uintptr_t)(count + 1));
            style_list_item(item);
            count++;
        }
        closedir(directory);
    }
    focus(first);
}

/* ---- Tools: search, calculator, timer, recurring appointments, backup ---- */
static void tools_screen(lv_event_t *event);
static void search_screen(lv_event_t *event);
static void calc_screen(lv_event_t *event);
static void recur_screen(lv_event_t *event);
static void timer_screen(lv_event_t *event);
static void backup_screen(lv_event_t *event);
static void busy(const char *title, const char *detail);

static void tools_screen(lv_event_t *event) {
    lv_obj_t *first; (void)event; clear(); label("Tools", 134, 6);
    first = button("Search", 10, 34, 145, search_screen, NULL);
    button("Calculator", 165, 34, 145, calc_screen, NULL);
    button("Timer", 10, 68, 145, timer_screen, NULL);
    button("Recurring", 165, 68, 145, recur_screen, NULL);
    button("Backup", 10, 102, 145, backup_screen, NULL);
    button("Back", 165, 102, 145, home, NULL);
    focus(first);
}

/* Search */
#define SEARCH_MAX 24
static pim_hit_t search_hits[SEARCH_MAX];
static size_t search_count;
static char search_query[40];
static bool search_done;
static void search_open_hit(lv_event_t *event) {
    const pim_hit_t *hit = &search_hits[index_from_event(event)];
    const char *name;
    switch (hit->kind) {
    case PIM_HIT_APPOINTMENT: ui.calendar = hit->date; day_view(NULL); break;
    case PIM_HIT_TODO: todo_list(NULL); break;
    case PIM_HIT_NOTE: ui.selected = hit->index; note_editor(NULL); break;
    case PIM_HIT_JOURNAL: ui.calendar = hit->date; journal_write(NULL); break;
    default:
        name = strrchr(hit->path, '/');
        editor_open(hit->path, name != NULL ? name + 1 : hit->path, false, search_screen);
        break;
    }
}
static void search_run(lv_event_t *event) {
    (void)event;
    snprintf(search_query, sizeof(search_query), "%s", lv_textarea_get_text(ui.title));
    if (search_query[0] == '\0') { message("Search", "Type a word to look for."); return; }
    busy("Searching", "Please wait...");
    pim_system_busy(true);
    search_count = pim_search(PIM_ROOT, ui.database, search_query, search_hits, SEARCH_MAX);
    pim_system_busy(false);
    search_done = true;
    search_screen(NULL);
}
static void search_enter(lv_event_t *event) { if (lv_event_get_code(event) == LV_EVENT_READY) search_run(NULL); }
static void search_screen(lv_event_t *event) {
    lv_obj_t *list, *input;
    (void)event; clear(); label("Search", 134, 4);
    input = text("word or phrase", 8, 22, 304, 30, true);
    ui.title = input;
    lv_textarea_set_max_length(input, sizeof(search_query) - 1);
    lv_textarea_set_text(input, search_query);
    lv_obj_add_event_cb(input, search_enter, LV_EVENT_READY, NULL);
    button("Search", 8, 56, 145, search_run, NULL); button("Back", 167, 56, 145, tools_screen, NULL);
    list = lv_list_create(lv_screen_active()); lv_obj_set_pos(list, 8, 90); lv_obj_set_size(list, 304, 220); crt_style(list, true);
    if (search_done && search_count == 0) label("No matches.", 16, 100);
    for (size_t i = 0; search_done && i < search_count; i++) {
        static const char tags[] = "ATNJF";
        char row[120];
        snprintf(row, sizeof(row), "%c %s: %s", tags[search_hits[i].kind], search_hits[i].title, search_hits[i].snippet);
        lv_obj_t *item = lv_list_add_button(list, NULL, row);
        lv_obj_add_event_cb(item, search_open_hit, LV_EVENT_CLICKED, (void *)(uintptr_t)(i + 1)); style_list_item(item);
    }
    focus(input);
}

/* Calculator */
#define CALC_LINES 5
static char calc_log[CALC_LINES][2][40];
static size_t calc_used;
static double calc_answer;
static bool calc_degrees = true, calc_have_answer;
static lv_obj_t *calc_view, *calc_mode, *calc_preview;
static size_t calc_recall;
static void calc_refresh(void) {
    char body[CALC_LINES * 90] = "";
    for (size_t i = 0; i < calc_used; i++) {
        size_t length = strlen(body);
        snprintf(body + length, sizeof(body) - length, "%s\n   = %s\n", calc_log[i][0], calc_log[i][1]);
    }
    lv_label_set_text(calc_view, body);
}
/* Shows the answer while you type, so you can see it before pressing Enter. */
static void calc_changed(lv_event_t *event) {
    char error[32], shown[32], line[48];
    double value;
    const char *expression = lv_textarea_get_text(ui.title);
    (void)event;
    if (calc_have_answer && expression[0] != '\0' && expression[1] == '\0' && strchr("+-*/^%", expression[0]) != NULL) {
        char continued[8];
        snprintf(continued, sizeof(continued), "ans%c", expression[0]);
        lv_textarea_set_text(ui.title, continued);
        return;
    }
    if (expression[0] != '\0' && pim_calc_eval(expression, calc_answer, calc_degrees, &value, error, sizeof(error))) {
        pim_calc_format(value, shown, sizeof(shown));
        snprintf(line, sizeof(line), "= %s", shown);
    } else line[0] = '\0';
    lv_label_set_text(calc_preview, line);
}
static void calc_enter(lv_event_t *event) {
    char error[32], shown[32];
    double value;
    const char *expression = lv_textarea_get_text(ui.title);
    if (lv_event_get_code(event) != LV_EVENT_READY || expression[0] == '\0') return;
    if (calc_used == CALC_LINES) { memmove(calc_log[0], calc_log[1], sizeof(calc_log[0]) * (CALC_LINES - 1)); calc_used--; }
    snprintf(calc_log[calc_used][0], sizeof(calc_log[0][0]), "%s", expression);
    if (pim_calc_eval(expression, calc_answer, calc_degrees, &value, error, sizeof(error))) {
        pim_calc_format(value, shown, sizeof(shown));
        calc_answer = value; calc_have_answer = true;
        lv_textarea_set_text(ui.title, "");
    } else snprintf(shown, sizeof(shown), "%s", error);
    snprintf(calc_log[calc_used][1], sizeof(calc_log[0][1]), "%s", shown);
    calc_used++;
    calc_recall = calc_used;
    calc_refresh();
}
/* UP/DOWN step through earlier expressions so they can be corrected and re-run. */
static void calc_key(lv_event_t *event) {
    uint32_t key = lv_event_get_key(event);
    if (calc_used == 0 || (key != LV_KEY_UP && key != LV_KEY_DOWN)) return;
    if (key == LV_KEY_UP && calc_recall > 0) calc_recall--;
    else if (key == LV_KEY_DOWN && calc_recall < calc_used) calc_recall++;
    lv_textarea_set_text(ui.title, calc_recall < calc_used ? calc_log[calc_recall][0] : "");
}
static void calc_toggle(lv_event_t *event) {
    (void)event;
    calc_degrees = !calc_degrees;
    lv_label_set_text(lv_obj_get_child(calc_mode, 0), calc_degrees ? "Deg" : "Rad");
    calc_changed(NULL);
}
static void calc_clear(lv_event_t *event) {
    (void)event; calc_used = calc_recall = 0; calc_answer = 0; calc_have_answer = false;
    calc_refresh(); lv_textarea_set_text(ui.title, "");
}
static void calc_screen(lv_event_t *event) {
    lv_obj_t *input;
    (void)event; clear(); label("Calculator", 120, 4);
    input = text("type e.g. 2+3*4", 8, 22, 304, 30, true);
    ui.title = input;
    lv_textarea_set_max_length(input, 60);
    lv_obj_add_event_cb(input, calc_enter, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(input, calc_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(input, calc_key, LV_EVENT_KEY, NULL);
    calc_preview = label("", 12, 56);
    calc_view = label("", 12, 78);
    lv_obj_set_width(calc_view, 296);
    label("ENTER = solve   UP = recall\n+ - * / after a result continues it\nsin cos tan sqrt ln log abs  pi e ans\n^ power  % remainder  ( ) brackets", 8, 232);
    calc_mode = button(calc_degrees ? "Deg" : "Rad", 8, 290, 90, calc_toggle, NULL);
    button("Clear", 115, 290, 90, calc_clear, NULL); button("Back", 222, 290, 90, tools_screen, NULL);
    calc_recall = calc_used;
    calc_refresh();
    focus(input);
}

/* Basic stopwatch and countdown; the time keeps running when you leave the screen */
static bool sw_running, cd_running, timer_countdown;
static uint32_t sw_base_ms, sw_start, cd_left_ms, cd_end;
static lv_obj_t *timer_view, *timer_start;
static uint32_t ticks_ms(void) { return (uint32_t)to_ms_since_boot(get_absolute_time()); }
static uint32_t stopwatch_ms(void) { return sw_running ? sw_base_ms + (ticks_ms() - sw_start) : sw_base_ms; }
static uint32_t countdown_ms(void) {
    int32_t left = (int32_t)(cd_end - ticks_ms());
    if (!cd_running) return cd_left_ms;
    if (left > 0) return (uint32_t)left;
    cd_running = false; cd_left_ms = 0;
    return 0;
}
static void timer_refresh(lv_timer_t *timer) {
    char shown[32], line[64];
    uint32_t ms;
    (void)timer;
    if (timer_view == NULL || timer_start == NULL) return;
    ms = timer_countdown ? countdown_ms() : stopwatch_ms();
    pim_clock_format(ms, !timer_countdown, shown, sizeof(shown));
    snprintf(line, sizeof(line), "%s\n%s", timer_countdown ? (ms == 0 && !cd_running ? "COUNTDOWN  DONE" : "COUNTDOWN") : "STOPWATCH", shown);
    lv_label_set_text(timer_view, line);
    lv_label_set_text(lv_obj_get_child(timer_start, 0), (timer_countdown ? cd_running : sw_running) ? "Stop" : "Start");
}
static void timer_start_stop(lv_event_t *event) {
    (void)event;
    if (timer_countdown) {
        if (cd_running) { cd_left_ms = countdown_ms(); cd_running = false; }
        else if (cd_left_ms > 0) { cd_end = ticks_ms() + cd_left_ms; cd_running = true; }
    } else if (sw_running) { sw_base_ms = stopwatch_ms(); sw_running = false; }
    else { sw_start = ticks_ms(); sw_running = true; }
    timer_refresh(NULL);
}
static void timer_reset(lv_event_t *event) {
    (void)event;
    if (timer_countdown) { cd_running = false; cd_left_ms = 0; } else { sw_running = false; sw_base_ms = 0; }
    timer_refresh(NULL);
}
static void timer_mode(lv_event_t *event) { (void)event; timer_countdown = !timer_countdown; timer_refresh(NULL); }
static void timer_add(lv_event_t *event) {
    uint32_t add = (uint32_t)(uintptr_t)lv_event_get_user_data(event) * 60000U;
    if (!timer_countdown) return;
    if (cd_running) cd_end += add; else cd_left_ms += add;
    if (cd_left_ms > 99U * 3600000U) cd_left_ms = 99U * 3600000U;
    timer_refresh(NULL);
}
static void timer_gone(lv_event_t *event) {
    lv_obj_t *object = lv_event_get_target_obj(event);
    if (object == timer_view) timer_view = NULL;
    if (object == timer_start) timer_start = NULL;
}
static void timer_screen(lv_event_t *event) {
    lv_obj_t *first; (void)event; clear(); label("Timer", 134, 4);
    timer_view = label("", 60, 40);
    lv_obj_set_width(timer_view, 200); lv_obj_set_style_text_align(timer_view, LV_TEXT_ALIGN_CENTER, 0);
    first = timer_start = button("Start", 12, 110, 145, timer_start_stop, NULL);
    button("Reset", 165, 110, 145, timer_reset, NULL);
    button("Mode", 12, 148, 145, timer_mode, NULL);
    button("+1 min", 165, 148, 70, timer_add, (void *)(uintptr_t)1);
    button("+5", 240, 148, 70, timer_add, (void *)(uintptr_t)5);
    button("Back", 12, 186, 145, tools_screen, NULL);
    label("Mode switches stopwatch/countdown.", 12, 230);
    lv_obj_add_event_cb(timer_view, timer_gone, LV_EVENT_DELETE, NULL);
    lv_obj_add_event_cb(timer_start, timer_gone, LV_EVENT_DELETE, NULL);
    timer_refresh(NULL);
    ui.timer = lv_timer_create(timer_refresh, 100, NULL);
    focus(first);
}

/* Recurring appointments (stored in /PIM/recurring.cfg) */
static pim_recur_t recur[PIM_RECUR_MAX];
static size_t recur_count;
static void recur_reload(void) { recur_count = pim_recur_load(PIM_ROOT, recur, PIM_RECUR_MAX); }
static void recur_detail(lv_event_t *event);
static void recur_add(lv_event_t *event);
/* Today: this day's appointments (including recurring ones) and open to-dos that are due or overdue. */
static void today_screen(lv_event_t *event) {
    pim_date_t today = current_today();
    size_t indices[PIM_MAX_TODOS];
    char *text = ui.file_text;
    size_t used = 0, count;
    lv_obj_t *first, *view;
    (void)event; clear();
    recur_reload();
    text[0] = '\0';
#define TODAY_ADD(...) do { if (used < 900) used += (size_t)snprintf(text + used, sizeof(ui.file_text) - used, __VA_ARGS__); } while (0)
    TODAY_ADD("Appointments\n");
    count = pim_appointments_on(ui.database, today, indices, PIM_MAX_APPOINTMENTS);
    for (size_t i = 0; i < count; i++) {
        const pim_appointment_t *item = &ui.database->appointments[indices[i]];
        TODAY_ADD(" %02u:%02u %s\n", item->minutes / 60, item->minutes % 60, item->title);
    }
    for (size_t i = 0; i < recur_count; i++)
        if (pim_recur_occurs(&recur[i], today)) { count++; TODAY_ADD(" %02u:%02u %s (%s)\n", recur[i].minutes / 60, recur[i].minutes % 60, recur[i].title, pim_recur_name(recur[i].freq)); }
    if (count == 0) TODAY_ADD(" none\n");
    TODAY_ADD("\nTo-dos due\n");
    count = pim_todos_due_by(ui.database, today, indices, PIM_MAX_TODOS);
    for (size_t i = 0; i < count; i++) {
        const pim_todo_t *item = &ui.database->todos[indices[i]];
        pim_date_t due;
        pim_todo_due(item, &due);
        TODAY_ADD(" %s%s\n", pim_date_compare(due, today) < 0 ? "! " : "", item->title);
    }
    if (count == 0) TODAY_ADD(" none\n");
#undef TODAY_ADD
    label("Today", 130, 4);
    view = label(text, 10, 26);
    lv_obj_set_width(view, 300);
    lv_label_set_long_mode(view, LV_LABEL_LONG_CLIP);
    first = button("Back", 112, 284, 95, home, NULL);
    focus(first);
}
static void recur_open(lv_event_t *event) { ui.selected = index_from_event(event); recur_detail(NULL); }
static void recur_remove(lv_event_t *event) {
    (void)event;
    if (ui.selected < recur_count) {
        memmove(&recur[ui.selected], &recur[ui.selected + 1], (recur_count - ui.selected - 1) * sizeof(recur[0]));
        recur_count--;
        if (!pim_recur_save(PIM_ROOT, recur, recur_count)) message("Not saved", "Check that the SD card is available.");
    }
    recur_screen(NULL);
}
static void recur_detail(lv_event_t *event) {
    char body[160], date[16];
    lv_obj_t *first; (void)event; clear();
    if (ui.selected >= recur_count) { recur_screen(NULL); return; }
    format_date(date, sizeof(date), recur[ui.selected].start);
    snprintf(body, sizeof(body), "%s\n\nRepeats %s at %02u:%02u\nstarting %s", recur[ui.selected].title,
             pim_recur_name(recur[ui.selected].freq), recur[ui.selected].minutes / 60, recur[ui.selected].minutes % 60, date);
    label(body, 12, 24);
    first = button("Delete", 20, 260, 100, recur_remove, NULL);
    button("Back", 190, 260, 100, recur_screen, NULL); focus(first);
}
static void recur_screen(lv_event_t *event) {
    lv_obj_t *list, *first; (void)event; clear(); label("Recurring appointments", 70, 8);
    first = button("Add", 12, 280, 90, recur_add, NULL); button("Back", 218, 280, 90, tools_screen, NULL);
    list = lv_list_create(lv_screen_active()); lv_obj_set_pos(list, 8, 30); lv_obj_set_size(list, 304, 246); crt_style(list, true);
    for (size_t i = 0; i < recur_count; i++) {
        char row[80];
        snprintf(row, sizeof(row), "%c %02u:%02u %s", recur[i].freq, recur[i].minutes / 60, recur[i].minutes % 60, recur[i].title);
        lv_obj_t *item = lv_list_add_button(list, NULL, row);
        lv_obj_add_event_cb(item, recur_open, LV_EVENT_CLICKED, (void *)(uintptr_t)(i + 1)); style_list_item(item);
    }
    focus(first);
}
static lv_obj_t *recur_date, *recur_time;
static void recur_save_form(lv_event_t *event) {
    unsigned day, month, year, hour, minute;
    char tail, title[sizeof(recur[0].title)];
    pim_recur_t item = {0};
    (void)event;
    snprintf(title, sizeof(title), "%s", lv_textarea_get_text(ui.title));
    if (title[0] == '\0') { message("Not saved", "Enter a title."); return; }
    if (sscanf(lv_textarea_get_text(recur_date), "%u-%u-%u%c", &day, &month, &year, &tail) != 3 || year < 2000 || year > 2099 ||
        !pim_date_is_valid((pim_date_t){(uint16_t)year, (uint8_t)month, (uint8_t)day})) { message("Not saved", "Date must be DD-MM-YYYY."); return; }
    if (sscanf(lv_textarea_get_text(recur_time), "%u:%u%c", &hour, &minute, &tail) != 2 || hour > 23 || minute > 59) { message("Not saved", "Time must be HH:MM."); return; }
    if (recur_count >= PIM_RECUR_MAX) { message("Not saved", "Too many recurring items."); return; }
    item.start = (pim_date_t){(uint16_t)year, (uint8_t)month, (uint8_t)day};
    item.minutes = (uint16_t)(hour * 60 + minute);
    item.freq = "DWMY"[lv_dropdown_get_selected(ui.choice)];
    snprintf(item.title, sizeof(item.title), "%s", title);
    recur[recur_count++] = item;
    if (!pim_recur_save(PIM_ROOT, recur, recur_count)) { recur_count--; message("Not saved", "Check that the SD card is available."); return; }
    recur_screen(NULL);
}
static void recur_add(lv_event_t *event) {
    char date[16];
    lv_obj_t *first; (void)event; clear(); label("New recurring item", 90, 4);
    first = ui.title = text("Title", 12, 24, 296, 30, true);
    format_date(date, sizeof(date), current_today());
    label("Start date", 12, 62); label("Time", 168, 62);
    recur_date = text("DD-MM-YYYY", 12, 80, 140, 30, true); lv_textarea_set_text(recur_date, date);
    recur_time = text("HH:MM", 168, 80, 140, 30, true); lv_textarea_set_text(recur_time, "09:00");
    ui.choice = lv_dropdown_create(lv_screen_active());
    lv_dropdown_set_options(ui.choice, "Daily\nWeekly\nMonthly\nYearly"); crt_dropdown(ui.choice);
    lv_obj_set_pos(ui.choice, 12, 124); lv_obj_set_size(ui.choice, 140, 32);
    button("Save", 12, 176, 100, recur_save_form, NULL); button("Cancel", 190, 176, 100, recur_screen, NULL);
    focus(first);
}

/* Backup and restore of everything under /PIM except derived exports */
static char backup_names[PIM_BACKUP_KEEP + 1][PIM_BACKUP_NAME];
static char backup_pick[PIM_BACKUP_NAME];
static void theme_load(void);
static void backup_create(lv_event_t *event) {
    char name[PIM_BACKUP_NAME];
    pim_date_t today = current_today();
    bool ok;
    (void)event;
    snprintf(name, sizeof(name), "%04u-%02u-%02u_%02u%02u", today.year, today.month, today.day, current_minutes() / 60, current_minutes() % 60);
    busy("Backup", "Copying data...");
    pim_system_busy(true);
    ok = pim_backup_create(PIM_ROOT, name);
    pim_system_busy(false);
    backup_screen(NULL);
    message(ok ? "Backup done" : "Backup failed", ok ? name : "Check that the SD card has free space.");
}
static void backup_restore(lv_event_t *event) {
    bool ok;
    (void)event;
    busy("Restore", "Copying data...");
    pim_system_busy(true);
    ok = pim_backup_restore(PIM_ROOT, backup_pick);
    pim_system_busy(false);
    if (ok && ui.storage_path != NULL) {
        pim_storage_load(ui.storage_path, ui.database);
        sound_load(); theme_load(); recur_reload();
    }
    backup_screen(NULL);
    message(ok ? "Restored" : "Restore failed", ok ? "Data restored. Restart to apply Wi-Fi settings." : "The backup could not be read.");
}
static void backup_pick_item(lv_event_t *event) {
    char body[160];
    lv_obj_t *first;
    size_t index = index_from_event(event);
    if (index > PIM_BACKUP_KEEP) return;
    snprintf(backup_pick, sizeof(backup_pick), "%s", backup_names[index]);
    clear(); label("Restore backup?", 100, 24);
    snprintf(body, sizeof(body), "%s\n\nThis replaces your current\nappointments, to-dos, notes,\njournal and settings.", backup_pick);
    label(body, 12, 60);
    first = button("Cancel", 20, 200, 120, backup_screen, NULL);
    button("Restore", 170, 200, 120, backup_restore, NULL);
    focus(first);
}
static void backup_screen(lv_event_t *event) {
    lv_obj_t *list, *first;
    size_t count;
    (void)event; clear(); label("Backup", 130, 4);
    first = button("Create backup", 8, 24, 190, backup_create, NULL); button("Back", 212, 24, 100, tools_screen, NULL);
    label("Newest 5 are kept. Select one to restore.", 8, 58);
    list = lv_list_create(lv_screen_active()); lv_obj_set_pos(list, 8, 80); lv_obj_set_size(list, 304, 220); crt_style(list, true);
    count = pim_backup_list(PIM_ROOT, backup_names, PIM_BACKUP_KEEP + 1);
    if (count == 0) label("No backups yet.", 16, 90);
    for (size_t i = 0; i < count; i++) {
        lv_obj_t *item = lv_list_add_button(list, NULL, backup_names[i]);
        lv_obj_add_event_cb(item, backup_pick_item, LV_EVENT_CLICKED, (void *)(uintptr_t)(i + 1)); style_list_item(item);
    }
    focus(first);
}

/* ---- Wi-Fi: saved networks, scanner, NTP sync, time zone, web address ---- */

#define WIFI_FILE PIM_ROOT "/wifi.cfg"
#define SCAN_MAX 12
static pim_wifi_config_t wifi_cfg;
static bool wifi_cfg_ready;
static pim_wifi_ap_t wifi_aps[SCAN_MAX];
static char wifi_pending[PIM_WIFI_SSID_MAX + 1];
static char telnet_host[64] = "";
static char telnet_port[8] = "23";
static unsigned term_scroll;
static bool term_dirty, term_notified;
static pim_term_t term;

static void wifi_cfg_load(void) {
    if (wifi_cfg_ready) return;
    pim_wifi_config_init(&wifi_cfg);
    pim_wifi_config_load(WIFI_FILE, &wifi_cfg);
    wifi_cfg_ready = true;
}
static void busy(const char *title, const char *detail) {
    clear();
    label(title, 96, 120);
    label(detail, 40, 148);
    lv_refr_now(NULL);
}
static void tz_text(char *out, size_t size) {
    int minutes = wifi_cfg.tz_minutes, absolute = minutes < 0 ? -minutes : minutes;
    snprintf(out, size, "UTC%c%02d:%02d", minutes < 0 ? '-' : '+', absolute / 60, absolute % 60);
}
static void wifi_ready_message(const char *address) {
    snprintf(ui.file_text, sizeof(ui.file_text),
             "Open http://%s:8080\nin a phone or PC browser.\nPIN: %s\n\n"
             "curl -H 'Cookie: pim=%s' \\\n --data-binary @file http://%s:8080/upload/name\n\n"
             "Use a trusted network (plain HTTP).",
             address, pim_wifi_pin(), pim_wifi_pin(), address);
    message("Wi-Fi ready", ui.file_text);
}
static void wifi_progress(const char *stage) { busy("Wi-Fi", stage); }
static bool wifi_join(const char *ssid, const char *password, bool remember) {
    char address[16];
    busy("Connecting...", ssid);
    pim_wifi_set_progress(wifi_progress);
    if (!pim_wifi_connect(ssid, password, address, sizeof(address))) {
        wifi(NULL);
        message("Wi-Fi failed", pim_wifi_error()[0] != '\0' ? pim_wifi_error() : "Check the network name and password.");
        return false;
    }
    busy("Connected", "Saving network");
    wifi_cfg_load();
    if (remember && pim_wifi_config_remember(&wifi_cfg, ssid, password)) pim_wifi_config_save(WIFI_FILE, &wifi_cfg);
    wifi(NULL);
    wifi_ready_message(address);
    return true;
}
static void wifi_info(lv_event_t *event) {
    (void)event;
    if (!pim_wifi_connected()) { message("Not connected", "Join a network first."); return; }
    wifi_ready_message(pim_wifi_address());
}
/* Typing can leave stray spaces around a password; SSIDs may legitimately end in a space. */
static void trim_spaces(char *text) {
    size_t length = strlen(text), start = 0;
    while (length > 0 && (unsigned char)text[length - 1] <= ' ') length--;
    while (start < length && (unsigned char)text[start] <= ' ') start++;
    memmove(text, text + start, length - start);
    text[length - start] = '\0';
}
static void wifi_manual_connect(lv_event_t *event) {
    char ssid[PIM_WIFI_SSID_MAX + 1], password[PIM_WIFI_PASSWORD_MAX + 1];
    (void)event;
    snprintf(ssid, sizeof(ssid), "%s", lv_textarea_get_text(ui.title));
    snprintf(password, sizeof(password), "%s", lv_textarea_get_text(ui.content));
    trim_spaces(password);
    if (ssid[0] == '\0' || password[0] == '\0') { message("Missing details", "Enter a network name and password."); return; }
    wifi_join(ssid, password, true);
}
static void wifi_manual(lv_event_t *event) {
    lv_obj_t *first;
    (void)event;
    clear();
    label("Join network", 106, 8);
    ui.title = text("Network name", 12, 38, 296, 28, true);
    ui.content = text("Password", 12, 78, 296, 28, true);
    if (wifi_pending[0] != '\0') lv_textarea_set_text(ui.title, wifi_pending);
    first = button("Connect", 12, 126, 120, wifi_manual_connect, NULL);
    button("Back", 176, 126, 120, wifi, NULL);
    label("Passwords are saved on the SD", 28, 178);
    label("card in /PIM/wifi.cfg", 66, 196);
    focus(wifi_pending[0] != '\0' ? ui.content : first);
    wifi_pending[0] = '\0';
}
static void wifi_scan_pick(lv_event_t *event) {
    size_t index = index_from_event(event);
    const pim_wifi_network_t *saved;
    wifi_cfg_load();
    snprintf(wifi_pending, sizeof(wifi_pending), "%s", wifi_aps[index].ssid);
    saved = pim_wifi_config_find(&wifi_cfg, wifi_pending);
    if (saved != NULL) {
        char ssid[PIM_WIFI_SSID_MAX + 1], password[PIM_WIFI_PASSWORD_MAX + 1];
        snprintf(ssid, sizeof(ssid), "%s", saved->ssid);
        snprintf(password, sizeof(password), "%s", saved->password);
        wifi_pending[0] = '\0';
        wifi_join(ssid, password, true);
    } else wifi_manual(NULL);
}
static void wifi_scan(lv_event_t *event) {
    lv_obj_t *list, *first = NULL;
    int count;
    (void)event;
    busy("Scanning...", "About five seconds");
    count = pim_wifi_scan(wifi_aps, SCAN_MAX);
    clear();
    label("Networks (strongest first)", 52, 8);
    list = lv_list_create(lv_screen_active());
    lv_obj_set_pos(list, 8, 30); lv_obj_set_size(list, 304, 226); crt_style(list, true);
    for (int i = 0; i < count; i++) {
        char row[PIM_WIFI_SSID_MAX + 24];
        lv_obj_t *item;
        snprintf(row, sizeof(row), "%.32s  %ddBm", wifi_aps[i].ssid, wifi_aps[i].rssi);
        item = lv_list_add_button(list, NULL, row);
        lv_obj_add_event_cb(item, wifi_scan_pick, LV_EVENT_CLICKED, (void *)(uintptr_t)(i + 1));
        style_list_item(item);
        if (first == NULL) first = item;
    }
    if (count <= 0) label(count < 0 ? "Scan failed" : "No networks found", 96, 120);
    focus(button("Back", 10, 262, 90, wifi, NULL));
    if (first != NULL) focus(first);
}
static void wifi_saved_pick(lv_event_t *event) {
    size_t index = index_from_event(event);
    wifi_cfg_load();
    if (index < wifi_cfg.count) {
        pim_wifi_network_t net = wifi_cfg.networks[index];
        wifi_join(net.ssid, net.password, true);
    }
}
static void wifi_forget_pick(lv_event_t *event) {
    size_t index = index_from_event(event);
    wifi_cfg_load();
    if (pim_wifi_config_forget(&wifi_cfg, index)) pim_wifi_config_save(WIFI_FILE, &wifi_cfg);
    wifi(NULL);
    message("Forgotten", "The saved network was removed.");
}
static void wifi_saved(lv_event_t *event) {
    bool forget = lv_event_get_user_data(event) != NULL;
    lv_obj_t *list, *first = NULL;
    wifi_cfg_load();
    clear();
    label(forget ? "Forget which network?" : "Saved networks", forget ? 78 : 100, 8);
    list = lv_list_create(lv_screen_active());
    lv_obj_set_pos(list, 8, 30); lv_obj_set_size(list, 304, 226); crt_style(list, true);
    for (size_t i = 0; i < wifi_cfg.count; i++) {
        lv_obj_t *item = lv_list_add_button(list, NULL, wifi_cfg.networks[i].ssid);
        lv_obj_add_event_cb(item, forget ? wifi_forget_pick : wifi_saved_pick, LV_EVENT_CLICKED, (void *)(uintptr_t)(i + 1));
        style_list_item(item);
        if (first == NULL) first = item;
    }
    if (wifi_cfg.count == 0) label("No saved networks yet", 80, 120);
    focus(button("Back", 10, 262, 90, wifi, NULL));
    if (first != NULL) focus(first);
}
static void apply_clock(pim_date_t date, uint16_t minutes) {
    ui.today = date;
    ui.base_minutes = minutes;
    ui.base_ms = to_ms_since_boot(get_absolute_time());
    ui.calendar = ui.today;
    today_save();
}
/* Joins the most recent saved network if needed, then reads the time from NTP. */
static bool ntp_clock(pim_date_t *date, uint16_t *minutes) {
    uint32_t seconds;
    wifi_cfg_load();
    if (!pim_wifi_connected()) {
        char address[16];
        if (wifi_cfg.count == 0) { message("No network", "Join a network first (Scan or Manual)."); return false; }
        busy("Connecting...", wifi_cfg.networks[0].ssid);
        if (!pim_wifi_connect(wifi_cfg.networks[0].ssid, wifi_cfg.networks[0].password, address, sizeof(address))) {
            return false;
        }
    }
    busy("Syncing time...", "Asking pool.ntp.org");
    pim_wifi_set_progress(wifi_progress);
    return pim_wifi_ntp(&seconds) && pim_time_from_unix(seconds, wifi_cfg.tz_minutes, date, minutes);
}
static void wifi_sync(lv_event_t *event) {
    pim_date_t date;
    uint16_t minutes;
    char body[64];
    (void)event;
    if (!ntp_clock(&date, &minutes)) {
        wifi(NULL);
        message("Sync failed", pim_wifi_error()[0] != '\0' ? pim_wifi_error() : "Could not reach the network or time server.");
        return;
    }
    apply_clock(date, minutes);
    wifi(NULL);
    snprintf(body, sizeof(body), "%02u-%02u-%04u %02u:%02u", date.day, date.month, date.year, minutes / 60, minutes % 60);
    message("Clock set", body);
}
static void tz_show(lv_obj_t *target) {
    char value[24];
    tz_text(value, sizeof(value));
    lv_label_set_text(target, value);
}
static lv_obj_t *tz_label;
static void tz_step(lv_event_t *event) {
    int step = (int)(intptr_t)lv_event_get_user_data(event), value = wifi_cfg.tz_minutes + step;
    if (!pim_tz_valid(value)) return;
    wifi_cfg.tz_minutes = (int16_t)value;
    pim_wifi_config_save(WIFI_FILE, &wifi_cfg);
    tz_show(tz_label);
}
static void wifi_zone(lv_event_t *event) {
    lv_obj_t *first;
    (void)event;
    wifi_cfg_load();
    clear();
    label("Time zone", 112, 8);
    tz_label = label("", 112, 52);
    tz_show(tz_label);
    first = button("-1 hour", 10, 90, 145, tz_step, (void *)(intptr_t)-60);
    button("+1 hour", 165, 90, 145, tz_step, (void *)(intptr_t)60);
    button("-15 min", 10, 126, 145, tz_step, (void *)(intptr_t)-15);
    button("+15 min", 165, 126, 145, tz_step, (void *)(intptr_t)15);
    button("Back", 110, 170, 100, wifi, NULL);
    label("Used by NTP time sync", 76, 220);
    focus(first);
}
static void wifi_off(lv_event_t *event) {
    (void)event;
    pim_wifi_off();
    wifi(NULL);
}
static void wifi(lv_event_t *event) {
    char status[48], zone[24];
    lv_obj_t *first;
    (void)event;
    wifi_cfg_load();
    clear();
    label("Wi-Fi", 136, 4);
    tz_text(zone, sizeof(zone));
    snprintf(status, sizeof(status), "%s  %s", pim_wifi_connected() ? "Connected" : "Not connected", zone);
    label(status, 60, 26);
    first = button("Join saved", 10, 52, 145, wifi_saved, NULL);
    button("Scan networks", 165, 52, 145, wifi_scan, NULL);
    button("Manual entry", 10, 84, 145, wifi_manual, NULL);
    button("Sync time", 165, 84, 145, wifi_sync, NULL);
    button("Time zone", 10, 116, 145, wifi_zone, (void *)NULL);
    button("Forget saved", 165, 116, 145, wifi_saved, (void *)(intptr_t)1);
    button("Web address", 10, 148, 145, wifi_info, NULL);
    button("Wi-Fi off", 165, 148, 145, wifi_off, NULL);
    button("Back", 10, 180, 145, home, NULL);
    label("Once connected, open the web", 36, 220);
    label("address from a phone or PC.", 44, 238);
    label("PIN shown there. HTTP only,", 40, 262);
    label("so use a trusted network.", 52, 280);
    focus(first);
}

/* ---- Telnet client for BBSs ---- */

#define TERM_ROWS 24
static void term_refresh(void) {
    char view[(PIM_TERM_COLS + 1) * TERM_ROWS + 1];
    pim_term_view(&term, TERM_ROWS, term_scroll, view, sizeof(view));
    lv_label_set_text(ui.preview, view);
    term_dirty = false;
}
static void telnet_leave(lv_event_t *event) {
    (void)event;
    pim_telnet_close();
    home(NULL);
}
static void telnet_send(const uint8_t *bytes, size_t length) {
    if (pim_telnet_is_open()) pim_telnet_write(bytes, length);
}
static void telnet_key(lv_event_t *event) {
    uint32_t key = lv_event_get_key(event);
    uint8_t out[2];
    if (key == LV_KEY_ESC) { telnet_leave(NULL); return; }
    if (!pim_telnet_is_open()) return;
    if (key == LV_KEY_UP) { if (term_scroll < pim_term_max_scroll(&term, TERM_ROWS)) term_scroll++; term_dirty = true; return; }
    if (key == LV_KEY_DOWN) { if (term_scroll > 0) term_scroll--; term_dirty = true; return; }
    if (key == LV_KEY_BACKSPACE) key = 0x08;
    else if (key == LV_KEY_ENTER) key = '\r';
    else if (key >= 128 || key < 32) return;
    term_scroll = 0;
    telnet_send(out, pim_term_encode((uint8_t)key, out));
    if (!term.remote_echo && key >= 32) {
        uint8_t echo = (uint8_t)key, none[4];
        pim_term_feed(&term, &echo, 1, none, sizeof(none));
    } else if (!term.remote_echo && key == '\r') {
        pim_term_feed(&term, (const uint8_t *)"\r\n", 2, out, sizeof(out));
    }
    term_dirty = true;
}
static void telnet_tick(lv_timer_t *timer) {
    uint8_t data[256], reply[64];
    size_t count;
    (void)timer;
    while ((count = pim_telnet_read(data, sizeof(data))) > 0) {
        size_t used = pim_term_feed(&term, data, count, reply, sizeof(reply));
        if (used > 0) telnet_send(reply, used);
        term_dirty = true;
    }
    if (!pim_telnet_is_open() && !term_notified) {
        const uint8_t note[] = "\r\n[disconnected - ESC to leave]\r\n";
        pim_term_feed(&term, note, sizeof(note) - 1, reply, sizeof(reply));
        term_notified = true;
        term_dirty = true;
    }
    if (term_dirty) term_refresh();
}
static void telnet_screen(void) {
    clear();
    pim_term_init(&term);
    term_scroll = 0;
    term_notified = false;
    ui.terminal = true;
    ui.board = lv_obj_create(lv_screen_active());
    lv_obj_set_size(ui.board, 320, 320); lv_obj_set_pos(ui.board, 0, 0);
    lv_obj_remove_flag(ui.board, LV_OBJ_FLAG_SCROLLABLE);
    crt_style(ui.board, false);
    lv_obj_set_style_pad_all(ui.board, 0, 0);
    lv_obj_set_style_outline_width(ui.board, 0, LV_STATE_ANY);
    ui.preview = lv_label_create(ui.board);
    lv_obj_set_pos(ui.preview, 0, 2); lv_obj_set_width(ui.preview, 320);
    lv_label_set_long_mode(ui.preview, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(ui.preview, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_line_space(ui.preview, 3, 0);
    crt_style(ui.preview, false);
    lv_obj_set_style_text_font(ui.preview, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_line_space(ui.preview, 3, 0);
    label("ESC leave   UP/DN scroll", 60, 300);
    lv_obj_add_event_cb(ui.board, telnet_key, LV_EVENT_KEY, NULL);
    if (lv_obj_get_group(ui.board) == NULL) lv_group_add_obj(lv_group_get_default(), ui.board);
    focus(ui.board);
    ui.timer = lv_timer_create(telnet_tick, 40, NULL);
    term_refresh();
}
static void telnet_connect(lv_event_t *event) {
    unsigned long port;
    char *end;
    (void)event;
    snprintf(telnet_host, sizeof(telnet_host), "%s", lv_textarea_get_text(ui.title));
    snprintf(telnet_port, sizeof(telnet_port), "%s", lv_textarea_get_text(ui.content));
    port = strtoul(telnet_port, &end, 10);
    if (telnet_host[0] == '\0' || telnet_port[0] == '\0' || *end != '\0' || port == 0 || port > 65535) {
        message("Check the address", "Enter a host name and a port from 1 to 65535.");
        return;
    }
    if (!pim_wifi_connected()) { message("Not connected", "Join a network from the Wi-Fi screen first."); return; }
    busy("Connecting...", telnet_host);
    if (!pim_telnet_open(telnet_host, (uint16_t)port)) {
        telnet(NULL);
        message("Telnet failed", "Could not reach that host. Check the name and port.");
        return;
    }
    telnet_screen();
}
static void telnet(lv_event_t *event) {
    lv_obj_t *first;
    (void)event;
    clear();
    label("Telnet (BBS)", 106, 8);
    label("Host", 12, 34);
    ui.title = text("bbs.example.com", 12, 52, 296, 28, true);
    label("Port", 12, 88);
    ui.content = text("23", 12, 106, 120, 28, true);
    lv_textarea_set_text(ui.title, telnet_host);
    lv_textarea_set_text(ui.content, telnet_port);
    first = button("Connect", 12, 148, 120, telnet_connect, NULL);
    button("Back", 176, 148, 120, home, NULL);
    label("Plain text terminal, 40 columns.", 28, 200);
    label("Colours and cursor codes are", 36, 218);
    label("ignored. Needs a Wi-Fi network.", 28, 236);
    focus(first);
}

/* ---- Graphical games: drawn straight into an LVGL object, amber-on-black palette only ---- */

#define G_AMBER (themes[theme_index].fg)
#define G_DIM (themes[theme_index].dim)
#define G_DARK (themes[theme_index].dark)
#define G_BLACK (themes[theme_index].bg)
#define G_NONE 0xffffffffu

static lv_layer_t *gl;
static char game_status[96];
static const lv_font_t *const font_small = &lv_font_montserrat_12;
static const lv_font_t *const font_mid = &lv_font_montserrat_14;
static const lv_font_t *const font_big = &lv_font_montserrat_14;

const char *pim_ui_game_status(void) { return game_status; }
void pim_ui_theme_colors(uint32_t *foreground, uint32_t *background) {
    *foreground = themes[theme_index].fg;
    *background = themes[theme_index].bg;
}

static void gbox(int x, int y, int w, int h, uint32_t fill_color, int border, uint32_t border_color) {
    lv_draw_rect_dsc_t dsc;
    lv_area_t area = {x, y, x + w - 1, y + h - 1};
    if (w <= 0 || h <= 0) return;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = fill_color == G_NONE ? LV_OPA_TRANSP : LV_OPA_COVER;
    dsc.bg_color = lv_color_hex(fill_color == G_NONE ? 0 : fill_color);
    dsc.border_width = border;
    dsc.border_opa = border ? LV_OPA_COVER : LV_OPA_TRANSP;
    dsc.border_color = lv_color_hex(border_color);
    lv_draw_rect(gl, &dsc, &area);
}
static void gfill(int x, int y, int w, int h, uint32_t color) { gbox(x, y, w, h, color, 0, 0); }
static void gframe(int x, int y, int w, int h, int thickness, uint32_t color) { gbox(x, y, w, h, G_NONE, thickness, color); }
static void gcircle(int x, int y, int size, uint32_t color) {
    lv_draw_rect_dsc_t dsc;
    lv_area_t area = {x, y, x + size - 1, y + size - 1};
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(color); dsc.radius = LV_RADIUS_CIRCLE;
    lv_draw_rect(gl, &dsc, &area);
}
static void gtext(const char *text, int x, int y, int w, lv_text_align_t align, uint32_t color, const lv_font_t *font) {
    lv_draw_label_dsc_t dsc;
    lv_area_t area = {x, y, x + w - 1, y + (int)font->line_height + 2};
    lv_draw_label_dsc_init(&dsc);
    dsc.text = text; dsc.text_local = 1; dsc.font = font; dsc.align = align; dsc.color = lv_color_hex(color);
    lv_draw_label(gl, &dsc, &area);
}
static void gnumber(const char *label_text, unsigned value, int x, int y, int w) {
    char text[16];
    gtext(label_text, x, y, w, LV_TEXT_ALIGN_LEFT, G_DIM, font_small);
    snprintf(text, sizeof(text), "%u", value);
    gtext(text, x, y + 14, w, LV_TEXT_ALIGN_LEFT, G_AMBER, font_big);
}
static bool score_saved;
static unsigned score_rank;
static void gover(const char *title, const char *line, int cx, int cy, const char *game, uint32_t score) {
    char best[24];
    if (!score_saved) { score_saved = true; score_rank = pim_scores_add(game, score); }
    gbox(cx - 100, cy - 36, 200, 72, G_BLACK, 2, G_AMBER);
    gtext(title, cx - 100, cy - 28, 200, LV_TEXT_ALIGN_CENTER, G_AMBER, font_big);
    if (score_rank > 0) snprintf(best, sizeof(best), "BEST #%u", score_rank);
    else snprintf(best, sizeof(best), "ENTER to restart");
    gtext(score_rank > 0 ? best : line, cx - 100, cy + 8, 200, LV_TEXT_ALIGN_CENTER, G_DIM, font_mid);
}
static void game_sounds(void) {
    static uint16_t lines, score, level, gold, kills, floor, moves;
    static uint8_t health, lives;
    static bool over, won, fired;
    uint16_t now_lines = 0, now_score = 0, now_level = 0, now_gold = 0, now_kills = 0, now_floor = 0, now_moves = 0;
    uint8_t now_health = 0, now_lives = 0;
    bool now_over = false, now_won = false, now_fired = false;
    if (ui.game == GAME_TETRIS) { now_over = ui.tetris.game_over; now_lines = ui.tetris.lines; now_level = ui.tetris.level; }
    else if (ui.game == GAME_INVADERS) {
        now_over = ui.invaders.game_over; now_score = ui.invaders.score; now_lives = ui.invaders.lives;
        now_level = ui.invaders.wave; now_fired = ui.invaders.bullet_active;
    } else if (ui.game == GAME_DUNGEON) {
        now_over = ui.dungeon.game_over; now_won = ui.dungeon.won; now_gold = ui.dungeon.gold; now_kills = ui.dungeon.kills;
        now_floor = ui.dungeon.floor; now_health = ui.dungeon.health;
    } else if (ui.game == GAME_SOLITAIRE) { now_won = ui.solitaire.won; now_moves = ui.solitaire.moves; }
    else if (ui.game == GAME_SNAKE) { now_over = ui.snake.game_over && !ui.snake.won; now_won = ui.snake.won; now_score = ui.snake.score; }
    else if (ui.game == GAME_2048) { now_over = ui.g2048.game_over; now_score = (uint16_t)(ui.g2048.score > 65535 ? 65535 : ui.g2048.score); now_won = ui.g2048.won; }
    else if (ui.game == GAME_MINES) { now_over = ui.mines.game_over && !ui.mines.won; now_won = ui.mines.won; now_moves = ui.mines.revealed; }
    if (now_over && !over) pim_sound_play(PIM_SOUND_GAME_OVER);
    else if (now_won && !won) pim_sound_play(PIM_SOUND_WIN);
    else if (now_level > level && level > 0) pim_sound_play(PIM_SOUND_LEVEL_UP);
    else if (now_floor > floor && floor > 0) pim_sound_play(PIM_SOUND_LEVEL_UP);
    else if (now_lines > lines) pim_sound_play(PIM_SOUND_CLEAR);
    else if (now_lives < lives || (now_health < health && health > 0)) pim_sound_play(PIM_SOUND_HURT);
    else if (now_score > score || now_kills > kills) pim_sound_play(PIM_SOUND_HIT);
    else if (now_gold > gold || (now_health > health && health > 0)) pim_sound_play(PIM_SOUND_PICKUP);
    else if (now_fired && !fired) pim_sound_play(PIM_SOUND_SHOOT);
    else if (now_moves != moves) pim_sound_play(PIM_SOUND_MOVE);
    over = now_over; won = now_won; lines = now_lines; score = now_score; level = now_level; gold = now_gold;
    kills = now_kills; floor = now_floor; moves = now_moves; health = now_health; lives = now_lives; fired = now_fired;
}
static void game_redraw(void) {
    bool ended = ui.game == GAME_TETRIS ? ui.tetris.game_over : ui.game == GAME_INVADERS ? ui.invaders.game_over :
                 ui.game == GAME_DUNGEON ? ui.dungeon.game_over || ui.dungeon.won : ui.game == GAME_SOLITAIRE ? ui.solitaire.won :
                 ui.game == GAME_SNAKE ? ui.snake.game_over : ui.game == GAME_2048 ? ui.g2048.game_over : ui.game == GAME_MINES ? ui.mines.game_over : false;
    if (!ended) { score_saved = false; score_rank = 0; }
    game_sounds(); if (ui.board != NULL) lv_obj_invalidate(ui.board); }

/* Tetris: 150x300 well with a ghost piece, next-piece preview and score panel. */
#define T_CELL 15
#define T_X 14
#define T_Y 10
static void tetris_block(int x, int y, int size, uint8_t shape) {
    uint32_t tone = shape % 2 ? G_DIM : G_AMBER;
    switch (shape / 2) {
        case 1: gframe(x + 1, y + 1, size - 2, size - 2, 2, tone); break;
        case 2: gfill(x + 1, y + 1, size - 2, size - 2, tone); gfill(x + size / 2 - 1, y + size / 2 - 1, 3, 3, G_BLACK); break;
        case 3: gfill(x + 1, y + 1, size - 2, size - 2, tone); gframe(x + 3, y + 3, size - 6, size - 6, 1, G_BLACK); break;
        default: gfill(x + 1, y + 1, size - 2, size - 2, tone); break;
    }
}
static bool tetris_fits(int8_t x, int8_t y) {
    for (uint8_t row = 0; row < 4; row++) for (uint8_t col = 0; col < 4; col++) {
        int gx = x + col, gy = y + row;
        if (!pim_tetris_shape_cell(ui.tetris.shape, ui.tetris.rotation, row, col)) continue;
        if (gx < 0 || gx >= PIM_TETRIS_WIDTH || gy >= PIM_TETRIS_HEIGHT || (gy >= 0 && ui.tetris.cells[gy][gx])) return false;
    }
    return true;
}
static void draw_tetris(void) {
    int8_t ghost = ui.tetris.y;
    int min_row = 4, max_row = -1, min_col = 4, max_col = -1;
    gframe(T_X - 3, T_Y - 3, PIM_TETRIS_WIDTH * T_CELL + 6, PIM_TETRIS_HEIGHT * T_CELL + 6, 2, G_AMBER);
    for (int i = 1; i < PIM_TETRIS_WIDTH; i++) gfill(T_X + i * T_CELL, T_Y, 1, PIM_TETRIS_HEIGHT * T_CELL, G_DARK);
    for (int i = 1; i < PIM_TETRIS_HEIGHT; i++) gfill(T_X, T_Y + i * T_CELL, PIM_TETRIS_WIDTH * T_CELL, 1, G_DARK);
    for (int y = 0; y < PIM_TETRIS_HEIGHT; y++) for (int x = 0; x < PIM_TETRIS_WIDTH; x++)
        if (ui.tetris.cells[y][x]) tetris_block(T_X + x * T_CELL, T_Y + y * T_CELL, T_CELL, (uint8_t)(ui.tetris.cells[y][x] - 1));
    if (!ui.tetris.game_over) {
        while (tetris_fits(ui.tetris.x, (int8_t)(ghost + 1))) ghost++;
        for (uint8_t row = 0; row < 4; row++) for (uint8_t col = 0; col < 4; col++) {
            if (!pim_tetris_shape_cell(ui.tetris.shape, ui.tetris.rotation, row, col)) continue;
            if (ghost + row >= 0) gframe(T_X + (ui.tetris.x + col) * T_CELL + 2, T_Y + (ghost + row) * T_CELL + 2, T_CELL - 4, T_CELL - 4, 1, G_DIM);
            if (ui.tetris.y + row >= 0)
                tetris_block(T_X + (ui.tetris.x + col) * T_CELL, T_Y + (ui.tetris.y + row) * T_CELL, T_CELL, ui.tetris.shape);
        }
    }
    gtext("TETRIS", 178, 8, 136, LV_TEXT_ALIGN_LEFT, G_AMBER, font_big);
    gtext("NEXT", 178, 44, 136, LV_TEXT_ALIGN_LEFT, G_DIM, font_small);
    gframe(178, 60, 76, 60, 1, G_DIM);
    for (uint8_t row = 0; row < 4; row++) for (uint8_t col = 0; col < 4; col++)
        if (pim_tetris_shape_cell(ui.tetris.next, 0, row, col)) {
            if (row < min_row) min_row = row;
            if (row > max_row) max_row = row;
            if (col < min_col) min_col = col;
            if (col > max_col) max_col = col;
        }
    for (uint8_t row = 0; row < 4; row++) for (uint8_t col = 0; col < 4; col++)
        if (pim_tetris_shape_cell(ui.tetris.next, 0, row, col))
            tetris_block(178 + 38 - (max_col - min_col + 1) * 6 + (col - min_col) * 12,
                         60 + 30 - (max_row - min_row + 1) * 6 + (row - min_row) * 12, 12, ui.tetris.next);
    gnumber("SCORE", ui.tetris.score, 178, 136, 136);
    gnumber("LINES", ui.tetris.lines, 178, 180, 136);
    gnumber("LEVEL", ui.tetris.level, 178, 224, 136);
    gtext("< > move   ^ turn", 178, 270, 140, LV_TEXT_ALIGN_LEFT, G_DIM, font_small);
    gtext("v soft  ENTER drop", 178, 284, 140, LV_TEXT_ALIGN_LEFT, G_DIM, font_small);
    gtext("ESC quit", 178, 298, 140, LV_TEXT_ALIGN_LEFT, G_DIM, font_small);
    if (ui.tetris.game_over) gover("GAME OVER", "ENTER to play again", T_X + 75, 150, "tetris", ui.tetris.score);
    snprintf(game_status, sizeof(game_status), "Tetris score %u lines %u level %u%s", ui.tetris.score, ui.tetris.lines,
             ui.tetris.level, ui.tetris.game_over ? " GAME OVER" : "");
}

/* Dungeon: 16x12 tiles of 20 px, HUD underneath. */
#define D_TILE 20
static void draw_dungeon(void) {
    const pim_dungeon_t *d = &ui.dungeon;
    char text[48];
    for (int y = 0; y < PIM_DUNGEON_HEIGHT; y++) for (int x = 0; x < PIM_DUNGEON_WIDTH; x++) {
        int px = x * D_TILE, py = y * D_TILE;
        char cell = d->cells[y][x];
        if (cell == '#') { gfill(px, py, D_TILE, D_TILE, G_DIM); gframe(px, py, D_TILE, D_TILE, 1, G_DARK); gfill(px + 4, py + 9, 12, 2, G_DARK); }
        else gfill(px + 9, py + 9, 2, 2, G_DARK);
        if (cell == '>') { gframe(px + 2, py + 2, 16, 16, 2, G_AMBER); gtext(">", px, py + 1, D_TILE, LV_TEXT_ALIGN_CENTER, G_AMBER, font_mid); }
        else if (cell == '$') { gcircle(px + 4, py + 4, 12, G_AMBER); gcircle(px + 7, py + 7, 6, G_BLACK); }
        else if (cell == '/') { gfill(px + 9, py + 2, 3, 12, G_AMBER); gfill(px + 5, py + 13, 11, 2, G_AMBER); gfill(px + 9, py + 15, 3, 3, G_DIM); }
        else if (cell == '[') { gframe(px + 4, py + 3, 12, 14, 2, G_AMBER); gfill(px + 9, py + 6, 2, 8, G_DIM); }
        else if (cell == '!') { gfill(px + 8, py + 2, 4, 4, G_AMBER); gframe(px + 5, py + 6, 10, 12, 2, G_AMBER); gfill(px + 7, py + 11, 6, 5, G_DIM); }
    }
    for (int i = 0; i < PIM_DUNGEON_MAX_ENEMIES; i++) {
        const pim_enemy_t *e = &d->enemy[i];
        int px = e->x * D_TILE, py = e->y * D_TILE;
        if (!e->alive) continue;
        if (e->kind == 2) { gfill(px + 1, py + 1, 18, 18, G_DIM); gframe(px + 1, py + 1, 18, 18, 2, G_AMBER); gfill(px + 4, py + 2, 3, 3, G_AMBER); gfill(px + 13, py + 2, 3, 3, G_AMBER); }
        else if (e->kind == 1) { gfill(px + 3, py + 4, 14, 14, G_AMBER); gfill(px + 3, py + 2, 3, 3, G_AMBER); gfill(px + 14, py + 2, 3, 3, G_AMBER); }
        else { gfill(px + 3, py + 4, 14, 14, G_DIM); gframe(px + 3, py + 4, 14, 14, 1, G_AMBER); }
        gfill(px + 6, py + 8, 3, 3, e->kind == 1 ? G_BLACK : G_AMBER); gfill(px + 11, py + 8, 3, 3, e->kind == 1 ? G_BLACK : G_AMBER); gfill(px + 6, py + 14, 8, 2, G_BLACK);
        for (int pip = 0; pip < e->hp && pip < 6; pip++) gfill(px + 3 + pip * 3, py + 1, 2, 2, G_AMBER);
    }
    gcircle(d->player_x * D_TILE + 3, d->player_y * D_TILE + 3, 14, G_AMBER);
    gfill(d->player_x * D_TILE + 7, d->player_y * D_TILE + 8, 2, 3, G_BLACK);
    gfill(d->player_x * D_TILE + 11, d->player_y * D_TILE + 8, 2, 3, G_BLACK);
    gfill(0, 240, 320, 2, G_AMBER);
    gtext("HP", 8, 250, 24, LV_TEXT_ALIGN_LEFT, G_DIM, font_mid);
    gframe(30, 250, 130, 16, 2, G_AMBER);
    gfill(33, 253, d->max_health ? d->health * 124 / d->max_health : 0, 10, G_AMBER);
    snprintf(text, sizeof(text), "%u/%u", d->health, d->max_health);
    gtext(text, 168, 250, 60, LV_TEXT_ALIGN_LEFT, G_AMBER, font_mid);
    snprintf(text, sizeof(text), "FLOOR %u/%u", d->floor, d->floors);
    gtext(text, 232, 250, 84, LV_TEXT_ALIGN_LEFT, G_AMBER, font_mid);
    snprintf(text, sizeof(text), "GOLD %u KILLS %u FOES %u ATK+%u DEF+%u", d->gold, d->kills, d->enemies, d->attack, d->defense);
    gtext(text, 8, 272, 304, LV_TEXT_ALIGN_LEFT, G_AMBER, font_mid);
    gtext(d->message, 8, 292, 150, LV_TEXT_ALIGN_LEFT, G_AMBER, font_mid);
    gtext("Move into foes to fight", 150, 281, 166, LV_TEXT_ALIGN_RIGHT, G_DIM, font_small);
    gtext("ESC quit", 150, 297, 166, LV_TEXT_ALIGN_RIGHT, G_DIM, font_small);
    if (d->game_over) gover("YOU DIED", "ENTER to try again", 160, 120, "dungeon", d->gold + d->kills * 10U);
    if (d->won) gover("YOU ESCAPED!", "ENTER to play again", 160, 120, "dungeon", d->gold + d->kills * 10U);
    snprintf(game_status, sizeof(game_status), "Dungeon floor %u HP %u gold %u%s%s", d->floor, d->health, d->gold,
             d->game_over ? " DEFEATED" : "", d->won ? " VICTORY" : "");
}

/* Snake: 20x15 grid of 16 px. */
static void draw_snake(void) {
    const pim_snake_t *g = &ui.snake;
    char text[40];
    gframe(0, 0, 320, 240, 2, G_AMBER);
    gcircle(g->food_x * 16 + 2, g->food_y * 16 + 2, 12, G_AMBER);
    gcircle(g->food_x * 16 + 6, g->food_y * 16 + 6, 4, G_BLACK);
    for (uint16_t i = g->length; i > 0; i--) {
        int px = g->x[i - 1] * 16, py = g->y[i - 1] * 16;
        gfill(px + 1, py + 1, 14, 14, i == 1 ? G_AMBER : G_DIM);
        if (i == 1) { gfill(px + 4, py + 5, 3, 3, G_BLACK); gfill(px + 9, py + 5, 3, 3, G_BLACK); }
    }
    snprintf(text, sizeof(text), "SCORE %u   LENGTH %u", g->score, g->length);
    gtext(text, 8, 250, 304, LV_TEXT_ALIGN_LEFT, G_AMBER, font_mid);
    gtext("ARROWS steer", 8, 276, 150, LV_TEXT_ALIGN_LEFT, G_DIM, font_small);
    gtext("ESC quit", 8, 296, 150, LV_TEXT_ALIGN_LEFT, G_DIM, font_small);
    if (g->game_over) gover(g->won ? "YOU WIN!" : "GAME OVER", "ENTER to play again", 160, 120, "snake", g->score);
    snprintf(game_status, sizeof(game_status), "Snake score %u length %u%s", g->score, g->length, g->game_over ? " GAME OVER" : "");
}

/* 2048: 4x4 tiles of 64 px. */
static void draw_2048(void) {
    const pim_2048_t *g = &ui.g2048;
    char text[40];
    int best = 0;
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
        int px = 24 + x * 68, py = 8 + y * 68, e = g->cells[y][x];
        if (e > best) best = e;
        if (e == 0) { gframe(px, py, 64, 64, 1, G_DARK); continue; }
        gfill(px, py, 64, 64, e < 4 ? G_DARK : e < 7 ? G_DIM : G_AMBER);
        gframe(px, py, 64, 64, 1, G_AMBER);
        snprintf(text, sizeof(text), "%u", 1u << e);
        gtext(text, px, py + 24, 64, LV_TEXT_ALIGN_CENTER, e < 7 ? G_AMBER : G_BLACK, font_big);
    }
    snprintf(text, sizeof(text), "SCORE %u   BEST TILE %u", (unsigned)g->score, best ? 1u << best : 0u);
    gtext(text, 8, 286, 304, LV_TEXT_ALIGN_LEFT, G_AMBER, font_mid);
    gtext("ARROWS slide  ESC quit", 8, 302, 304, LV_TEXT_ALIGN_LEFT, G_DIM, font_small);
    if (g->game_over) gover("NO MOVES", "ENTER to play again", 160, 140, "2048", g->score);
    snprintf(game_status, sizeof(game_status), "2048 score %u best %u%s%s", (unsigned)g->score, best ? 1u << best : 0u,
             g->won ? " REACHED2048" : "", g->game_over ? " GAME OVER" : "");
}

/* Minesweeper: 16x12 cells of 18 px. */
static uint32_t mines_start_ms;
static void draw_mines(void) {
    const pim_mines_t *g = &ui.mines;
    char text[40];
    for (int y = 0; y < PIM_MINES_HEIGHT; y++) for (int x = 0; x < PIM_MINES_WIDTH; x++) {
        int px = 16 + x * 18, py = 4 + y * 18;
        uint8_t f = g->flags[y][x];
        if (f & PIM_MINE_REVEALED) {
            gframe(px, py, 18, 18, 1, G_DARK);
            if (f & PIM_MINE_MINE) { gcircle(px + 4, py + 4, 10, G_AMBER); }
            else if (pim_mines_around(g, (int8_t)x, (int8_t)y) > 0) {
                snprintf(text, sizeof(text), "%u", pim_mines_around(g, (int8_t)x, (int8_t)y));
                gtext(text, px, py + 1, 18, LV_TEXT_ALIGN_CENTER, G_AMBER, font_mid);
            }
        } else {
            gfill(px + 1, py + 1, 16, 16, G_DIM); gframe(px, py, 18, 18, 1, G_DARK);
            if (f & PIM_MINE_FLAGGED) gtext("F", px, py + 1, 18, LV_TEXT_ALIGN_CENTER, G_BLACK, font_mid);
        }
    }
    gframe(16 + g->cursor_x * 18, 4 + g->cursor_y * 18, 18, 18, 2, G_AMBER);
    snprintf(text, sizeof(text), "MINES LEFT %u", pim_mines_flags_left(g));
    gtext(text, 8, 232, 304, LV_TEXT_ALIGN_LEFT, G_AMBER, font_mid);
    gtext("ENTER open   F flag", 8, 262, 304, LV_TEXT_ALIGN_LEFT, G_DIM, font_small);
    gtext("ESC quit", 8, 278, 304, LV_TEXT_ALIGN_LEFT, G_DIM, font_small);
    if (g->game_over) {
        uint32_t seconds = (to_ms_since_boot(get_absolute_time()) - mines_start_ms) / 1000;
        if (!g->won) score_saved = true;
        gover(g->won ? "CLEARED!" : "BOOM!", "ENTER to play again", 160, 120, "mines", seconds > 9999 ? 1 : 10000 - seconds);
    }
    snprintf(game_status, sizeof(game_status), "Mines left %u revealed %u%s%s", pim_mines_flags_left(g), g->revealed,
             g->won ? " CLEARED" : "", g->game_over && !g->won ? " BOOM" : "");
}

/* Space Invaders: 20x16 grid of 16 px with pixel-art sprites. */
#define I_CELL 16
static const char *const invader_art[3][2][5] = {
    {{"..XXX..", ".XXXXX.", "XX.X.XX", "XXXXXXX", ".X...X."}, {"..XXX..", ".XXXXX.", "XX.X.XX", "XXXXXXX", "X.....X"}},
    {{".X...X.", "..XXX..", ".XXXXX.", "X.XXX.X", "X.....X"}, {".X...X.", "X.XXX.X", "XXXXXXX", ".XXXXX.", ".X...X."}},
    {{"X.XXX.X", "XXXXXXX", "XX.X.XX", ".XXXXX.", "..X.X.."}, {"X.XXX.X", "XXXXXXX", "XX.X.XX", ".XXXXX.", ".X...X."}},
};
static const char *const ship_art[4] = {"...X...", "..XXX..", ".XXXXX.", "XXXXXXX"};
static void sprite(const char *const *rows, int count, int x, int y, int pixel, uint32_t color) {
    for (int r = 0; r < count; r++)
        for (int c = 0; rows[r][c] != '\0'; c++) {
            int start = c;
            if (rows[r][c] != 'X') continue;
            while (rows[r][c + 1] == 'X') c++;
            gfill(x + start * pixel, y + r * pixel, (c - start + 1) * pixel, pixel, color);
        }
}
static void draw_invaders(void) {
    const pim_invaders_t *g = &ui.invaders;
    char text[32];
    for (int i = 0; i < 28; i++) gfill((i * 97 + 13) % 316, (i * 53 + 7) % 250, 1, 1, G_DARK);
    for (int row = 0; row < PIM_INVADERS_ROWS; row++) for (int col = 0; col < PIM_INVADERS_COLUMNS; col++) {
        if (!g->alive[row][col]) continue;
        sprite(invader_art[row % 3][(g->tick / 5) % 2], 5, (g->offset_x + 2 * col) * I_CELL + 1, (g->offset_y + row) * I_CELL + 3, 2,
               row % 2 ? G_DIM : G_AMBER);
    }
    sprite(ship_art, 4, g->player_x * I_CELL + 1, (PIM_INVADERS_HEIGHT - 1) * I_CELL + 5, 2, G_AMBER);
    if (g->bullet_active) gfill(g->bullet_x * I_CELL + 7, g->bullet_y * I_CELL + 1, 2, 12, G_AMBER);
    if (g->bomb_active) gfill(g->bomb_x * I_CELL + (g->tick % 2 ? 5 : 8), g->bomb_y * I_CELL + 2, 3, 12, G_DIM);
    gfill(0, 256, 320, 2, G_AMBER);
    gnumber("SCORE", g->score, 8, 262, 100);
    gtext("LIVES", 120, 262, 60, LV_TEXT_ALIGN_LEFT, G_DIM, font_small);
    for (int i = 0; i < g->lives; i++) sprite(ship_art, 4, 120 + i * 20, 282, 2, G_AMBER);
    snprintf(text, sizeof(text), "WAVE %u", g->wave);
    gtext(text, 232, 262, 84, LV_TEXT_ALIGN_LEFT, G_AMBER, font_mid);
    gtext("< > move  ENTER fire  ESC quit", 8, 301, 304, LV_TEXT_ALIGN_LEFT, G_DIM, font_small);
    if (g->game_over) gover("GAME OVER", "ENTER to play again", 160, 120, "invaders", g->score);
    snprintf(game_status, sizeof(game_status), "Invaders score %u lives %u wave %u%s", g->score, g->lives, g->wave,
             g->game_over ? " GAME OVER" : "");
}

/* Solitaire (Klondike, draw one). */
#define S_W 40
#define S_H 48
#define S_PITCH 45
#define S_TOP_Y 6
#define S_TABLE_Y 62
static const char *const rank_names[13] = {"A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};
static const char suit_letters[4] = {'S', 'H', 'D', 'C'};
static void solitaire_card(int x, int y, uint8_t card, bool face_up, bool full) {
    char text[8];
    bool red = pim_solitaire_is_red(card);
    uint32_t ink = red ? G_BLACK : G_AMBER;
    if (!face_up) {
        gfill(x, y, S_W, S_H, G_DIM); gframe(x, y, S_W, S_H, 2, G_AMBER); gframe(x + 6, y + 6, S_W - 12, S_H - 12, 1, G_DARK);
        return;
    }
    gbox(x, y, S_W, S_H, red ? G_AMBER : G_BLACK, 2, G_AMBER);
    snprintf(text, sizeof(text), "%s%c", rank_names[card % 13], suit_letters[card / 13]);
    gtext(text, x + 3, y + 2, S_W - 4, LV_TEXT_ALIGN_LEFT, ink, font_mid);
    if (full) {
        text[0] = suit_letters[card / 13]; text[1] = '\0';
        gtext(text, x, y + 20, S_W, LV_TEXT_ALIGN_CENTER, ink, font_big);
    }
}
static void solitaire_slot(int x, int y, const char *caption) {
    gframe(x, y, S_W, S_H, 1, G_DIM);
    gtext(caption, x, y + 16, S_W, LV_TEXT_ALIGN_CENTER, G_DIM, font_mid);
}
static void solitaire_ring(int x, int y, int w, int h, int offset, uint32_t color) {
    gframe(x - offset, y - offset, w + 2 * offset, h + 2 * offset, 2, color);
}
static int sol_x(int column) { return 5 + column * S_PITCH; }
static int sol_card_y(int pile, int index) {
    int y = S_TABLE_Y;
    for (int i = 0; i < index; i++) y += i < ui.solitaire.tableau_count[pile] - ui.solitaire.face_up[pile] ? 4 : 14;
    return y;
}
static void draw_solitaire(void) {
    const pim_solitaire_t *g = &ui.solitaire;
    char text[64];
    int selected_x = -1, selected_y = 0, selected_h = S_H;
    if (g->stock_count) solitaire_card(sol_x(0), S_TOP_Y, 0, false, true); else solitaire_slot(sol_x(0), S_TOP_Y, "O");
    if (g->stock_count) { snprintf(text, sizeof(text), "%u", g->stock_count); gtext(text, sol_x(0), S_TOP_Y + 16, S_W, LV_TEXT_ALIGN_CENTER, G_AMBER, font_mid); }
    if (g->waste_count) solitaire_card(sol_x(1), S_TOP_Y, g->waste[g->waste_count - 1], true, true); else solitaire_slot(sol_x(1), S_TOP_Y, "");
    for (int suit = 0; suit < 4; suit++) {
        char caption[2] = {suit_letters[suit], '\0'};
        if (g->foundation[suit]) solitaire_card(sol_x(3 + suit), S_TOP_Y, (uint8_t)(suit * 13 + g->foundation[suit] - 1), true, true);
        else solitaire_slot(sol_x(3 + suit), S_TOP_Y, caption);
    }
    for (int pile = 0; pile < PIM_SOLITAIRE_PILES; pile++) {
        int first_up = g->tableau_count[pile] - g->face_up[pile];
        if (g->tableau_count[pile] == 0) solitaire_slot(sol_x(pile), S_TABLE_Y, "");
        for (int i = 0; i < g->tableau_count[pile]; i++)
            solitaire_card(sol_x(pile), sol_card_y(pile, i), g->tableau[pile][i], i >= first_up, i == g->tableau_count[pile] - 1);
    }
    if (ui.sel_active) {
        if (ui.sel_zone == PIM_SOL_WASTE) { selected_x = sol_x(1); selected_y = S_TOP_Y; }
        else if (ui.sel_zone == PIM_SOL_FOUNDATION) { selected_x = sol_x(3 + ui.sel_index); selected_y = S_TOP_Y; }
        else {
            int first = g->tableau_count[ui.sel_index] - ui.sel_count;
            selected_x = sol_x(ui.sel_index); selected_y = sol_card_y(ui.sel_index, first);
            selected_h = sol_card_y(ui.sel_index, g->tableau_count[ui.sel_index] - 1) + S_H - selected_y;
        }
        solitaire_ring(selected_x, selected_y, S_W, selected_h, 5, G_DIM);
    }
    if (ui.cursor_row == 0) solitaire_ring(sol_x(ui.cursor_col), S_TOP_Y, S_W, S_H, 3, G_AMBER);
    else {
        int pile = ui.cursor_col, count = g->tableau_count[pile], depth = ui.cursor_depth > count ? count : ui.cursor_depth, first = count - depth;
        int y = count ? sol_card_y(pile, first) : S_TABLE_Y;
        int h = count ? sol_card_y(pile, count - 1) + S_H - y : S_H;
        solitaire_ring(sol_x(pile), y, S_W, h, 3, G_AMBER);
    }
    gfill(0, 302, 320, 1, G_DARK);
    if (ui.sol_message[0]) snprintf(text, sizeof(text), "%s", ui.sol_message);
    else snprintf(text, sizeof(text), "ENTER pick/place  F found.  N new");
    gtext(text, 4, 304, 240, LV_TEXT_ALIGN_LEFT, ui.sol_message[0] ? G_AMBER : G_DIM, font_small);
    snprintf(text, sizeof(text), "Moves %u", g->moves);
    gtext(text, 250, 304, 66, LV_TEXT_ALIGN_RIGHT, G_DIM, font_small);
    if (g->won) gover("YOU WIN!", "ENTER for a new deal", 160, 160, "solitaire", g->moves < 10000 ? 10000U - g->moves : 0U);
    snprintf(game_status, sizeof(game_status), "Solitaire moves %u stock %u waste %u%s", g->moves, g->stock_count, g->waste_count,
             g->won ? " WON" : "");
}

static void board_draw(lv_event_t *event) {
    gl = lv_event_get_layer(event);
    if (ui.game == GAME_TETRIS) draw_tetris();
    else if (ui.game == GAME_DUNGEON) draw_dungeon();
    else if (ui.game == GAME_INVADERS) draw_invaders();
    else if (ui.game == GAME_SOLITAIRE) draw_solitaire();
    else if (ui.game == GAME_SNAKE) draw_snake();
    else if (ui.game == GAME_2048) draw_2048();
    else if (ui.game == GAME_MINES) draw_mines();
}

static void solitaire_clear_selection(void) { ui.sel_active = false; ui.cursor_depth = 1; }
static void solitaire_key(uint32_t key) {
    pim_solitaire_t *g = &ui.solitaire;
    bool ok;
    ui.sol_message[0] = '\0';
    if (g->won) { if (key == LV_KEY_ENTER || key == 'n') { pim_solitaire_new(g, to_ms_since_boot(get_absolute_time())); solitaire_clear_selection(); } return; }
    if (key == LV_KEY_LEFT && ui.cursor_col > 0) {
        ui.cursor_col--;
        if (ui.cursor_row == 0 && ui.cursor_col == 2) ui.cursor_col = 1;
        ui.cursor_depth = 1;
    } else if (key == LV_KEY_RIGHT && ui.cursor_col < 6) {
        ui.cursor_col++;
        if (ui.cursor_row == 0 && ui.cursor_col == 2) ui.cursor_col = 3;
        ui.cursor_depth = 1;
    } else if (key == LV_KEY_DOWN) {
        if (ui.cursor_row == 0) { ui.cursor_row = 1; ui.cursor_depth = 1; }
        else if (ui.cursor_depth > 1) ui.cursor_depth--;
    } else if (key == LV_KEY_UP) {
        if (ui.cursor_row == 1 && ui.cursor_depth < g->face_up[ui.cursor_col]) ui.cursor_depth++;
        else if (ui.cursor_row == 1) { ui.cursor_row = 0; ui.cursor_depth = 1; if (ui.cursor_col == 2) ui.cursor_col = 1; }
    } else if (key == LV_KEY_BACKSPACE) {
        solitaire_clear_selection();
    } else if (key == 'n' || key == 'N') {
        pim_solitaire_new(g, to_ms_since_boot(get_absolute_time())); solitaire_clear_selection();
        snprintf(ui.sol_message, sizeof(ui.sol_message), "New deal");
    } else if (key == 'f' || key == 'F' || key == ' ') {
        pim_solitaire_zone_t zone = PIM_SOL_TABLEAU;
        uint8_t index = ui.cursor_col;
        if (ui.sel_active) { zone = ui.sel_zone; index = ui.sel_index; }
        else if (ui.cursor_row == 0 && ui.cursor_col == 1) zone = PIM_SOL_WASTE;
        else if (ui.cursor_row == 0) zone = PIM_SOL_FOUNDATION;
        if (zone == PIM_SOL_FOUNDATION || (zone == PIM_SOL_TABLEAU && ui.sel_active && ui.sel_count != 1) ||
            !pim_solitaire_to_foundation(g, zone, index)) snprintf(ui.sol_message, sizeof(ui.sol_message), "Can't go to foundation");
        else solitaire_clear_selection();
    } else if (key == LV_KEY_ENTER) {
        if (!ui.sel_active) {
            if (ui.cursor_row == 0 && ui.cursor_col == 0) {
                if (!pim_solitaire_draw(g)) snprintf(ui.sol_message, sizeof(ui.sol_message), "Nothing to draw");
            } else if (ui.cursor_row == 0 && ui.cursor_col == 1 && g->waste_count) {
                ui.sel_active = true; ui.sel_zone = PIM_SOL_WASTE; ui.sel_index = 0; ui.sel_count = 1;
            } else if (ui.cursor_row == 0 && ui.cursor_col >= 3 && g->foundation[ui.cursor_col - 3]) {
                ui.sel_active = true; ui.sel_zone = PIM_SOL_FOUNDATION; ui.sel_index = (uint8_t)(ui.cursor_col - 3); ui.sel_count = 1;
            } else if (ui.cursor_row == 1 && g->tableau_count[ui.cursor_col]) {
                ui.sel_active = true; ui.sel_zone = PIM_SOL_TABLEAU; ui.sel_index = ui.cursor_col; ui.sel_count = ui.cursor_depth;
            } else snprintf(ui.sol_message, sizeof(ui.sol_message), "Nothing to pick up");
        } else if (ui.cursor_row == 0 && ui.cursor_col < 3) {
            solitaire_clear_selection();
        } else if (ui.sel_zone == PIM_SOL_TABLEAU && ui.cursor_row == 1 && ui.sel_index == ui.cursor_col) {
            solitaire_clear_selection();
        } else {
            ok = ui.cursor_row == 0
                     ? pim_solitaire_move(g, ui.sel_zone, ui.sel_index, ui.sel_count, PIM_SOL_FOUNDATION, (uint8_t)(ui.cursor_col - 3))
                     : pim_solitaire_move(g, ui.sel_zone, ui.sel_index, ui.sel_count, PIM_SOL_TABLEAU, ui.cursor_col);
            if (ok) solitaire_clear_selection();
            else snprintf(ui.sol_message, sizeof(ui.sol_message), "Can't move there");
        }
    }
    if (ui.cursor_row == 1 && ui.cursor_depth > g->face_up[ui.cursor_col]) ui.cursor_depth = g->face_up[ui.cursor_col] ? g->face_up[ui.cursor_col] : 1;
}

static void game_key(lv_event_t *event) {
    uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_ESC) { home(NULL); return; }
    if (key >= 'A' && key <= 'Z' && key != 'F' && key != 'N') key = key - 'A' + 'a';
    if (ui.game == GAME_TETRIS) {
        if (ui.tetris.game_over) {
            if (key == LV_KEY_ENTER) pim_tetris_reset(&ui.tetris, to_ms_since_boot(get_absolute_time()));
        } else {
            if (key == LV_KEY_LEFT || key == 'a') pim_tetris_move(&ui.tetris, -1, 0);
            else if (key == LV_KEY_RIGHT || key == 'd') pim_tetris_move(&ui.tetris, 1, 0);
            else if (key == LV_KEY_DOWN || key == 's') pim_tetris_move(&ui.tetris, 0, 1);
            else if (key == LV_KEY_UP || key == 'w') pim_tetris_rotate(&ui.tetris);
            else if (key == LV_KEY_ENTER || key == ' ') pim_tetris_drop(&ui.tetris);
            lv_timer_set_period(ui.timer, pim_tetris_interval_ms(&ui.tetris));
        }
    } else if (ui.game == GAME_DUNGEON) {
        if (key == LV_KEY_UP || key == 'w') pim_dungeon_move(&ui.dungeon, PIM_UP);
        else if (key == LV_KEY_DOWN || key == 's') pim_dungeon_move(&ui.dungeon, PIM_DOWN);
        else if (key == LV_KEY_LEFT || key == 'a') pim_dungeon_move(&ui.dungeon, PIM_LEFT);
        else if (key == LV_KEY_RIGHT || key == 'd') pim_dungeon_move(&ui.dungeon, PIM_RIGHT);
        else if (key == LV_KEY_ENTER && (ui.dungeon.game_over || ui.dungeon.won)) pim_dungeon_reset(&ui.dungeon, to_ms_since_boot(get_absolute_time()));
    } else if (ui.game == GAME_INVADERS) {
        if (ui.invaders.game_over) {
            if (key == LV_KEY_ENTER) pim_invaders_reset(&ui.invaders, to_ms_since_boot(get_absolute_time()));
        } else if (key == LV_KEY_LEFT || key == 'a') pim_invaders_move(&ui.invaders, -1);
        else if (key == LV_KEY_RIGHT || key == 'd') pim_invaders_move(&ui.invaders, 1);
        else if (key == LV_KEY_ENTER || key == LV_KEY_UP || key == ' ' || key == 'w') pim_invaders_fire(&ui.invaders);
    } else if (ui.game == GAME_SOLITAIRE) {
        solitaire_key(key);
    } else if (ui.game == GAME_SNAKE) {
        if (ui.snake.game_over) { if (key == LV_KEY_ENTER) { pim_snake_reset(&ui.snake, to_ms_since_boot(get_absolute_time())); lv_timer_set_period(ui.timer, pim_snake_interval_ms(&ui.snake)); } }
        else if (key == LV_KEY_UP || key == 'w') pim_snake_turn(&ui.snake, PIM_UP);
        else if (key == LV_KEY_DOWN || key == 's') pim_snake_turn(&ui.snake, PIM_DOWN);
        else if (key == LV_KEY_LEFT || key == 'a') pim_snake_turn(&ui.snake, PIM_LEFT);
        else if (key == LV_KEY_RIGHT || key == 'd') pim_snake_turn(&ui.snake, PIM_RIGHT);
    } else if (ui.game == GAME_2048) {
        if (key == LV_KEY_UP || key == 'w') pim_2048_move(&ui.g2048, PIM_UP);
        else if (key == LV_KEY_DOWN || key == 's') pim_2048_move(&ui.g2048, PIM_DOWN);
        else if (key == LV_KEY_LEFT || key == 'a') pim_2048_move(&ui.g2048, PIM_LEFT);
        else if (key == LV_KEY_RIGHT || key == 'd') pim_2048_move(&ui.g2048, PIM_RIGHT);
        else if (key == LV_KEY_ENTER && ui.g2048.game_over) pim_2048_reset(&ui.g2048, to_ms_since_boot(get_absolute_time()));
    } else if (ui.game == GAME_MINES) {
        pim_mines_t *m = &ui.mines;
        if (m->game_over) { if (key == LV_KEY_ENTER) { pim_mines_reset(m, to_ms_since_boot(get_absolute_time())); mines_start_ms = to_ms_since_boot(get_absolute_time()); } }
        else if (key == LV_KEY_UP || key == 'w') pim_mines_move_cursor(m, 0, -1);
        else if (key == LV_KEY_DOWN || key == 's') pim_mines_move_cursor(m, 0, 1);
        else if (key == LV_KEY_LEFT || key == 'a') pim_mines_move_cursor(m, -1, 0);
        else if (key == LV_KEY_RIGHT || key == 'd') pim_mines_move_cursor(m, 1, 0);
        else if (key == 'f' || key == 'F') pim_mines_flag(m, m->cursor_x, m->cursor_y);
        else if (key == LV_KEY_ENTER || key == ' ') {
            if (!m->placed) mines_start_ms = to_ms_since_boot(get_absolute_time());
            pim_mines_reveal(m, m->cursor_x, m->cursor_y);
        }
    }
    game_redraw();
}
static void game_tick(lv_timer_t *timer) {
    (void)timer;
    if (ui.game == GAME_TETRIS && !ui.tetris.game_over) pim_tetris_move(&ui.tetris, 0, 1);
    else if (ui.game == GAME_INVADERS && !ui.invaders.game_over) pim_invaders_tick(&ui.invaders);
    else if (ui.game == GAME_SNAKE && !ui.snake.game_over) { pim_snake_tick(&ui.snake); lv_timer_set_period(ui.timer, pim_snake_interval_ms(&ui.snake)); }
    else return;
    game_redraw();
}
static void start_game(game_t game) {
    uint32_t seed = to_ms_since_boot(get_absolute_time());
    clear();
    ui.game = game;
    pim_input_set_game_mode(true);
    ui.board = lv_obj_create(lv_screen_active());
    lv_obj_set_size(ui.board, 320, 320); lv_obj_set_pos(ui.board, 0, 0);
    lv_obj_remove_flag(ui.board, LV_OBJ_FLAG_SCROLLABLE);
    crt_style(ui.board, false);
    lv_obj_set_style_pad_all(ui.board, 0, 0);
    lv_obj_set_style_outline_width(ui.board, 0, LV_STATE_ANY);
    lv_obj_add_event_cb(ui.board, board_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(ui.board, game_key, LV_EVENT_KEY, NULL);
    if (lv_obj_get_group(ui.board) == NULL) lv_group_add_obj(lv_group_get_default(), ui.board);
    focus(ui.board);
    if (game == GAME_TETRIS) { pim_tetris_reset(&ui.tetris, seed); ui.timer = lv_timer_create(game_tick, pim_tetris_interval_ms(&ui.tetris), NULL); }
    else if (game == GAME_DUNGEON) pim_dungeon_reset(&ui.dungeon, seed);
    else if (game == GAME_INVADERS) { pim_invaders_reset(&ui.invaders, seed); ui.timer = lv_timer_create(game_tick, pim_invaders_interval_ms(&ui.invaders), NULL); }
    else if (game == GAME_SNAKE) { pim_snake_reset(&ui.snake, seed); ui.timer = lv_timer_create(game_tick, pim_snake_interval_ms(&ui.snake), NULL); }
    else if (game == GAME_2048) pim_2048_reset(&ui.g2048, seed);
    else if (game == GAME_MINES) { pim_mines_reset(&ui.mines, seed); mines_start_ms = to_ms_since_boot(get_absolute_time()); }
    else {
        pim_solitaire_new(&ui.solitaire, seed);
        ui.sel_active = false; ui.cursor_row = 0; ui.cursor_col = 0; ui.cursor_depth = 1; ui.sol_message[0] = '\0';
    }
    game_redraw();
    lv_refr_now(NULL);
}
static void tetris(lv_event_t *event) { (void)event; start_game(GAME_TETRIS); }
static void dungeon(lv_event_t *event) { (void)event; start_game(GAME_DUNGEON); }
static void invaders(lv_event_t *event) { (void)event; start_game(GAME_INVADERS); }
static void solitaire(lv_event_t *event) { (void)event; start_game(GAME_SOLITAIRE); }
static void snake(lv_event_t *event) { (void)event; start_game(GAME_SNAKE); }
static void game_2048(lv_event_t *event) { (void)event; start_game(GAME_2048); }
static void minesweeper(lv_event_t *event) { (void)event; start_game(GAME_MINES); }
static void games_screen(lv_event_t *event) {
    lv_obj_t *first; (void)event; clear();
    label("Games", 135, 8);
    first = button("Tetris", 10, 40, 145, tetris, NULL);
    button("Dungeon RPG", 165, 40, 145, dungeon, NULL);
    button("Space Invaders", 10, 80, 145, invaders, NULL);
    button("Solitaire", 165, 80, 145, solitaire, NULL);
    button("Snake", 10, 120, 145, snake, NULL);
    button("Minesweeper", 165, 120, 145, minesweeper, NULL);
    button("2048", 10, 160, 145, game_2048, NULL);
    button("Back", 165, 160, 145, home, NULL);
    label("Top scores are saved on the SD card", 20, 215);
    focus(first);
}

static void theme_load(void) {
    char name[16] = "";
    FILE *file = fopen(THEME_FILE, "rb");
    if (file == NULL) return;
    if (fgets(name, sizeof(name), file) != NULL)
        for (size_t i = 0; i < THEME_COUNT; i++)
            if (strncmp(name, themes[i].name, strlen(themes[i].name)) == 0) theme_index = i;
    fclose(file);
}
static void theme_save(void) {
    FILE *file = fopen(THEME_FILE, "wb");
    if (file == NULL) return;
    fprintf(file, "%s\n", themes[theme_index].name);
    fclose(file);
}
static void theme_pick(lv_event_t *event) {
    size_t index = index_from_event(event);
    if (index >= THEME_COUNT) return;
    theme_index = index;
    theme_save();
    theme_screen(NULL);
    focus(lv_obj_get_child(lv_screen_active(), (int32_t)index + 1));
}
static void theme_screen(lv_event_t *event) {
    (void)event;
    clear();
    label("Theme", 138, 8);
    for (size_t i = 0; i < THEME_COUNT; i++) {
        button(themes[i].name, i % 2 ? 165 : 10, 36 + (int)(i / 2) * 34, 145, theme_pick, (void *)(uintptr_t)(i + 1));
    }
    {
        char current[32];
        snprintf(current, sizeof(current), "Current: %s", themes[theme_index].name);
        label(current, 100, 190);
    }
    label("Saved to /PIM/theme.cfg", 66, 210);
    button("Back", 10, 262, 90, home, NULL);
    focus(lv_obj_get_child(lv_screen_active(), (int32_t)theme_index + 1));
}

static void today_load(void) {
    unsigned day, month, year, hour = 9, minute = 0;
    pim_date_t date;
    FILE *file = fopen(TODAY_FILE, "rb");
    if (file == NULL) return;
    if (fscanf(file, "%u-%u-%u %u:%u", &day, &month, &year, &hour, &minute) >= 3) {
        date = (pim_date_t){(uint16_t)year, (uint8_t)month, (uint8_t)day};
        if (year <= 2099 && pim_date_is_valid(date)) ui.today = date;
        if (hour < 24 && minute < 60) ui.base_minutes = (uint16_t)(hour * 60 + minute);
    }
    fclose(file);
}
static bool today_save(void) {
    FILE *file = fopen(TODAY_FILE, "wb");
    bool written;
    pim_date_t today = current_today();
    if (file == NULL) return false;
    written = fprintf(file, "%02u-%02u-%04u %02u:%02u\n", today.day, today.month, today.year,
                      current_minutes() / 60, current_minutes() % 60) > 0;
    return fclose(file) == 0 && written;
}

static void date_field_text(char *out, size_t size, int which) {
    if (which == 2) snprintf(out, size, "%04u", ui.fld[2]);
    else snprintf(out, size, "%02u", ui.fld[which]);
}
static void date_refresh(void) {
    char text_value[8];
    for (int i = 0; i < (ui.date_origin == 2 ? 5 : 3); i++) {
        date_field_text(text_value, sizeof(text_value), i);
        lv_label_set_text(lv_obj_get_child(ui.fld_obj[i], 0), text_value);
    }
}
/* Pulls typed values into range; the day follows the month length. */
static void date_normalise(void) {
    if (ui.fld[2] < 2000) ui.fld[2] = 2000;
    if (ui.fld[2] > 2099) ui.fld[2] = 2099;
    if (ui.fld[1] < 1) ui.fld[1] = 1;
    if (ui.fld[1] > 12) ui.fld[1] = 12;
    if (ui.fld[0] < 1) ui.fld[0] = 1;
    if (ui.fld[0] > pim_days_in_month(ui.fld[2], (uint8_t)ui.fld[1])) ui.fld[0] = pim_days_in_month(ui.fld[2], (uint8_t)ui.fld[1]);
    if (ui.fld[3] > 23) ui.fld[3] = 23;
    if (ui.fld[4] > 59) ui.fld[4] = 59;
    for (int i = 0; i < 5; i++) ui.fld_digits[i] = 0;
}
static void date_leave(void) { if (ui.date_origin == 2) return; if (ui.date_origin == 1) journal(NULL); else calendar(NULL); }
static void date_go(lv_event_t *event) {
    (void)event;
    date_normalise();
    ui.calendar = (pim_date_t){ui.fld[2], (uint8_t)ui.fld[1], (uint8_t)ui.fld[0]};
    date_leave();
}
static void date_set_today(lv_event_t *event) {
    char body[32];
    (void)event;
    date_normalise();
    ui.base_minutes = current_minutes(); ui.base_ms = to_ms_since_boot(get_absolute_time());
    ui.today = (pim_date_t){ui.fld[2], (uint8_t)ui.fld[1], (uint8_t)ui.fld[0]};
    ui.calendar = ui.today;
    date_refresh();
    if (!today_save()) { message("Not saved", "Today works until reboot; check the SD card."); return; }
    snprintf(body, sizeof(body), "Today is %02u-%02u-%04u", ui.today.day, ui.today.month, ui.today.year);
    message("Today set", body);
}
static void date_back(lv_event_t *event) { (void)event; date_leave(); }
static void date_field_enter(lv_event_t *event) {
    (void)event;
    date_normalise(); date_refresh();
    lv_group_focus_next(lv_group_get_default());
}
static void date_key(lv_event_t *event) {
    static const uint16_t maxima[] = {31, 12, 2099, 23, 59};
    uint32_t key = lv_event_get_key(event);
    int which = (int)(intptr_t)lv_event_get_user_data(event);
    if (key == LV_KEY_ESC) { date_leave(); return; }
    if (key >= '0' && key <= '9') {
        ui.fld[which] = pim_entry_digit(ui.fld[which], &ui.fld_digits[which], key - '0', maxima[which]);
        date_refresh();
        return;
    }
    if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
        date_normalise(); date_refresh();
        if (key == LV_KEY_LEFT) lv_group_focus_prev(lv_group_get_default()); else lv_group_focus_next(lv_group_get_default());
        return;
    }
    if (key != LV_KEY_UP && key != LV_KEY_DOWN) return;
    date_normalise();
    if (which == 0) {
        uint16_t last = pim_days_in_month(ui.fld[2], (uint8_t)ui.fld[1]);
        ui.fld[0] = key == LV_KEY_UP ? (ui.fld[0] % last) + 1 : (ui.fld[0] == 1 ? last : ui.fld[0] - 1);
    } else if (which == 1) {
        ui.fld[1] = key == LV_KEY_UP ? (ui.fld[1] % 12) + 1 : (ui.fld[1] == 1 ? 12 : ui.fld[1] - 1);
    } else if (which == 3) {
        ui.fld[3] = key == LV_KEY_UP ? (ui.fld[3] + 1) % 24 : (ui.fld[3] + 23) % 24;
    } else if (which == 4) {
        ui.fld[4] = key == LV_KEY_UP ? (ui.fld[4] + 1) % 60 : (ui.fld[4] + 59) % 60;
    } else if (key == LV_KEY_UP) { if (ui.fld[2] < 2099) ui.fld[2]++; }
    else if (ui.fld[2] > 2000) ui.fld[2]--;
    date_normalise(); date_refresh();
}
static void date_picker(lv_event_t *event) {
    static const char *const names[] = {"Day", "Month", "Year"};
    static const int widths[] = {80, 80, 110}, xs[] = {20, 112, 204};
    (void)event; clear();
    ui.fld[0] = ui.calendar.day; ui.fld[1] = ui.calendar.month; ui.fld[2] = ui.calendar.year;
    ui.fld_digits[0] = ui.fld_digits[1] = ui.fld_digits[2] = 0;
    label("Choose a date", 108, 6);
    for (int i = 0; i < 3; i++) {
        lv_obj_t *caption;
        label(names[i], xs[i] + widths[i] / 2 - 16, 40);
        ui.fld_obj[i] = lv_button_create(lv_screen_active()); caption = lv_label_create(ui.fld_obj[i]);
        lv_label_set_text(caption, ""); lv_obj_center(caption);
        lv_obj_set_size(ui.fld_obj[i], widths[i], 40); lv_obj_set_pos(ui.fld_obj[i], xs[i], 60);
        crt_style(ui.fld_obj[i], true); crt_style(caption, false);
        lv_obj_add_event_cb(ui.fld_obj[i], date_key, LV_EVENT_KEY, (void *)(intptr_t)i);
        lv_obj_add_event_cb(ui.fld_obj[i], date_field_enter, LV_EVENT_CLICKED, NULL);
    }
    label("DD-MM-YYYY", 116, 112);
    label("UP/DOWN or type digits", 60, 140); label("LEFT/RIGHT/ENTER next", 60, 160);
    label("Set today saves the current date", 22, 184);
    button("Go", 12, 220, 90, date_go, NULL); button("Set today", 108, 220, 104, date_set_today, NULL);
    button("Back", 220, 220, 90, date_back, NULL);
    date_refresh(); focus(ui.fld_obj[0]);
}

static void startup_screen(void);
static void startup_done(lv_event_t *event) {
    (void)event;
    date_normalise();
    ui.today = (pim_date_t){ui.fld[2], (uint8_t)ui.fld[1], (uint8_t)ui.fld[0]};
    ui.base_minutes = (uint16_t)(ui.fld[3] * 60U + ui.fld[4]);
    ui.base_ms = to_ms_since_boot(get_absolute_time());
    ui.calendar = ui.today;
    ui.date_origin = 0;
    today_save();
    home(NULL);
}
static void startup_sync(lv_event_t *event) {
    pim_date_t date;
    uint16_t minutes;
    (void)event;
    if (!ntp_clock(&date, &minutes)) {
        startup_screen();
        message("Sync failed", "Use Wi-Fi > Scan after Start, or set the time by hand.");
        return;
    }
    ui.today = date;
    ui.base_minutes = minutes;
    startup_screen();
    message("Time synced", "Check the fields, then press Start.");
}
static void startup_screen(void) {
    static const char *const names[] = {"Day", "Month", "Year", "Hour", "Min"};
    static const int widths[] = {52, 52, 76, 52, 52}, xs[] = {8, 66, 124, 208, 266};
    clear();
    ui.date_origin = 2;
    ui.fld[0] = ui.today.day; ui.fld[1] = ui.today.month; ui.fld[2] = ui.today.year;
    ui.fld[3] = ui.base_minutes / 60; ui.fld[4] = ui.base_minutes % 60;
    for (int i = 0; i < 5; i++) ui.fld_digits[i] = 0;
    label("Welcome! Set date and time", 50, 6);
    for (int i = 0; i < 5; i++) {
        lv_obj_t *caption;
        label(names[i], xs[i] + widths[i] / 2 - 14, 40);
        ui.fld_obj[i] = lv_button_create(lv_screen_active()); caption = lv_label_create(ui.fld_obj[i]);
        lv_label_set_text(caption, ""); lv_obj_center(caption);
        lv_obj_set_size(ui.fld_obj[i], widths[i], 40); lv_obj_set_pos(ui.fld_obj[i], xs[i], 60);
        crt_style(ui.fld_obj[i], true); crt_style(caption, false);
        lv_obj_add_event_cb(ui.fld_obj[i], date_key, LV_EVENT_KEY, (void *)(intptr_t)i);
        lv_obj_add_event_cb(ui.fld_obj[i], date_field_enter, LV_EVENT_CLICKED, NULL);
    }
    label("DD-MM-YYYY   HH:MM (24 hour)", 40, 112);
    label("UP/DOWN or type digits", 60, 140); label("LEFT/RIGHT/ENTER next", 60, 160);
    label("The clock runs from this moment", 34, 184);
    button("Sync (Wi-Fi)", 20, 220, 130, startup_sync, NULL);
    button("Start", 170, 220, 130, startup_done, NULL);
    date_refresh(); focus(ui.fld_obj[0]);
}

/* At boot: if a saved network is in range, join it, sync the clock and skip the set-time screen. */
static bool auto_start(void) {
    static pim_wifi_ap_t found[16];
    char address[16];
    uint32_t seconds;
    pim_date_t date;
    uint16_t minutes;
    int count, best = -1, best_rssi = -1000;
    if (ui.storage_path == NULL) return false;
    wifi_cfg_load();
    if (wifi_cfg.count == 0) return false;
    pim_system_busy(true);
    busy("Looking for Wi-Fi...", "Press nothing, please wait");
    count = pim_wifi_scan(found, 16);
    for (int i = 0; i < count; i++)
        for (size_t n = 0; n < wifi_cfg.count; n++)
            if (strcmp(found[i].ssid, wifi_cfg.networks[n].ssid) == 0 && found[i].rssi > best_rssi) { best = (int)n; best_rssi = found[i].rssi; }
    if (best >= 0) {
        busy("Connecting...", wifi_cfg.networks[best].ssid);
        pim_wifi_set_progress(wifi_progress);
        if (pim_wifi_connect(wifi_cfg.networks[best].ssid, wifi_cfg.networks[best].password, address, sizeof(address)) &&
            pim_wifi_ntp(&seconds) && pim_time_from_unix(seconds, wifi_cfg.tz_minutes, &date, &minutes)) {
            apply_clock(date, minutes);
            pim_system_busy(false);
            return true;
        }
    }
    pim_system_busy(false);
    return false;
}

static pim_date_t web_today(uint16_t *minutes) {
    *minutes = current_minutes();
    return current_today();
}
static void web_changed(void) {
    if (ui.date_origin != 2 && ui.game == GAME_NONE && !ui.terminal) home(NULL);
}

/* Critical battery: a banner on the top layer (any screen), shown once per discharge. Data is already saved after every change. */
static lv_obj_t *low_banner;
static bool low_timer_started;
static bool low_warned;
static void low_banner_remove(lv_timer_t *timer) {
    (void)timer;
    if (low_banner != NULL) { lv_obj_delete(low_banner); low_banner = NULL; }
}
static void low_battery_check(lv_timer_t *timer) {
    pim_battery_t battery;
    char text_value[48];
    (void)timer;
    if (!pim_input_battery(&battery)) return;
    if (battery.charging || battery.percent > 15) { low_warned = false; return; }
    if (low_warned || battery.percent > 10) return;
    low_warned = true;
    pim_input_activity();
    pim_sound_play(PIM_SOUND_HURT);
    if (low_banner != NULL) lv_obj_delete(low_banner);
    low_banner = lv_label_create(lv_layer_top());
    snprintf(text_value, sizeof(text_value), "LOW BATTERY %u%% - plug in", battery.percent);
    lv_label_set_text(low_banner, text_value);
    crt_style(low_banner, true);
    lv_obj_set_style_pad_all(low_banner, 6, 0);
    lv_obj_set_style_bg_opa(low_banner, LV_OPA_COVER, 0);
    lv_obj_set_pos(low_banner, 40, 150);
    lv_timer_set_repeat_count(lv_timer_create(low_banner_remove, 8000, NULL), 1);
}

void pim_ui_start(pim_database_t *database, const char *storage_path) {
    static const pim_wifi_hooks_t hooks = {web_today, web_changed};
    ui = (ui_t){.database = database, .storage_path = storage_path, .calendar = {2026, 1, 1}, .today = {2026, 1, 1}, .base_minutes = 540, .spin = {9, 0, 3}};
    wifi_cfg_ready = false;
    low_banner = NULL;
    low_warned = false;
    if (!low_timer_started) { low_timer_started = true; lv_timer_create(low_battery_check, 10000, NULL); }
    telnet_host[0] = '\0';
    theme_load();
    sound_load();
    today_load();
    ui.calendar = ui.today;
    ui.base_ms = to_ms_since_boot(get_absolute_time());
    pim_wifi_attach(database, storage_path, PIM_ROOT, &hooks);
    if (storage_path != NULL) { pim_games_set_root(PIM_ROOT); pim_games_prepare(PIM_ROOT); }
    recur_reload();
    if (auto_start()) home(NULL); else startup_screen();
}
