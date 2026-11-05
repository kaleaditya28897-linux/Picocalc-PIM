#include "pim/ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lvgl/lvgl.h"
#include "pico/time.h"
#include "pim/games.h"
#include "pim/storage.h"

typedef enum { GAME_NONE, GAME_TETRIS, GAME_DUNGEON, GAME_INVADERS } game_t;

typedef struct {
    pim_database_t *database;
    const char *storage_path;
    pim_date_t calendar;
    size_t selected;
    lv_obj_t *date, *time, *title, *content, *choice, *game_label;
    lv_timer_t *timer;
    game_t game;
    pim_tetris_t tetris;
    pim_dungeon_t dungeon;
    pim_invaders_t invaders;
} ui_t;

static ui_t ui;

#define CRT_AMBER 0xffb000

static void home(lv_event_t *event);
static void calendar(lv_event_t *event);
static void appointments(lv_event_t *event);
static void appointment_list(lv_event_t *event);
static void todos(lv_event_t *event);
static void todo_list(lv_event_t *event);
static void notes(lv_event_t *event);
static void note_list(lv_event_t *event);
static void journal(lv_event_t *event);
static void journal_list(lv_event_t *event);
static void tetris(lv_event_t *event);
static void dungeon(lv_event_t *event);
static void invaders(lv_event_t *event);

static uint32_t now(void) { return (uint32_t)(to_ms_since_boot(get_absolute_time()) / 1000U); }
static void save(void) { if (ui.storage_path) pim_storage_save(ui.storage_path, ui.database); }
static void crt_style(lv_obj_t *object, bool border) {
    lv_obj_set_style_bg_color(object, lv_color_black(), 0);
    lv_obj_set_style_text_color(object, lv_color_hex(CRT_AMBER), 0);
    lv_obj_set_style_border_color(object, lv_color_hex(CRT_AMBER), 0);
    lv_obj_set_style_border_width(object, border ? 1 : 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
}
static void clear(void) {
    if (ui.timer) { lv_timer_delete(ui.timer); ui.timer = NULL; }
    ui.game = GAME_NONE;
    lv_obj_clean(lv_screen_active());
    crt_style(lv_screen_active(), false);
}
static void focus(lv_obj_t *object) { lv_group_focus_obj(object); }
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
    crt_style(object, true); crt_style(caption, false);
    return object;
}
static lv_obj_t *text(const char *hint, int x, int y, int width, int height, bool one_line) {
    lv_obj_t *object = lv_textarea_create(lv_screen_active());
    lv_textarea_set_placeholder_text(object, hint); lv_textarea_set_one_line(object, one_line);
    lv_obj_set_size(object, width, height); lv_obj_set_pos(object, x, y); crt_style(object, true); return object;
}
static size_t index_from_event(lv_event_t *event) {
    return (size_t)((uintptr_t)lv_event_get_user_data(event) - 1U);
}
static bool date_from_text(const char *value, pim_date_t *date) {
    unsigned int year, month, day;
    if (sscanf(value, "%u-%u-%u", &year, &month, &day) != 3 ||
        year > UINT16_MAX || month > UINT8_MAX || day > UINT8_MAX) return false;
    *date = (pim_date_t){(uint16_t)year, (uint8_t)month, (uint8_t)day};
    return pim_date_is_valid(*date);
}
static bool minutes_from_text(const char *value, uint16_t *minutes) {
    unsigned int hours, minute;
    if (sscanf(value, "%u:%u", &hours, &minute) != 2 || hours > 23 || minute > 59) return false;
    *minutes = (uint16_t)(hours * 60 + minute); return true;
}
static void message(const char *title, const char *body) {
    lv_obj_t *box = lv_msgbox_create(NULL);
    lv_msgbox_add_title(box, title); lv_msgbox_add_text(box, body); lv_msgbox_add_close_button(box);
    crt_style(box, true); lv_obj_center(box);
}
static void set_date_text(lv_obj_t *field, pim_date_t date) {
    char value[16]; snprintf(value, sizeof(value), "%04u-%02u-%02u", date.year, date.month, date.day);
    lv_textarea_set_text(field, value);
}

static void home(lv_event_t *event) {
    lv_obj_t *first; (void)event; clear();
    label("PicoCalc PIM", 102, 8);
    first = button("Calendar", 20, 42, 130, calendar, NULL);
    button("Appointments", 170, 42, 130, appointments, NULL);
    button("To-Do List", 20, 82, 130, todos, NULL);
    button("Notes", 170, 82, 130, notes, NULL);
    button("Journal", 20, 122, 130, journal, NULL);
    button("Tetris", 170, 122, 130, tetris, NULL);
    button("Dungeon RPG", 20, 162, 130, dungeon, NULL);
    button("Space Invaders", 170, 162, 130, invaders, NULL);
    label(ui.storage_path ? "SD storage: /PIM/pim.dat" : "SD storage unavailable", 18, 220);
    label("TAB/arrow keys select  ENTER opens  ESC returns", 12, 286);
    focus(first);
}

static void change_month(lv_event_t *event) {
    intptr_t delta = (intptr_t)lv_event_get_user_data(event);
    if (delta < 0 && --ui.calendar.month == 0) { ui.calendar.month = 12; ui.calendar.year--; }
    if (delta > 0 && ++ui.calendar.month == 13) { ui.calendar.month = 1; ui.calendar.year++; }
    calendar(NULL);
}
static void calendar(lv_event_t *event) {
    static const char *const names[] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
    lv_obj_t *table, *first; char heading[20]; (void)event; clear();
    snprintf(heading, sizeof(heading), "%04u-%02u", ui.calendar.year, ui.calendar.month);
    label(heading, 132, 8);
    first = button("<", 20, 36, 44, change_month, (void *)(intptr_t)-1);
    button(">", 256, 36, 44, change_month, (void *)(intptr_t)1);
    button("Back", 112, 280, 95, home, NULL);
    table = lv_table_create(lv_screen_active()); lv_obj_set_pos(table, 8, 66); lv_obj_set_size(table, 304, 210); crt_style(table, true);
    for (uint8_t column = 0; column < 7; column++) {
        lv_table_set_cell_value(table, 0, column, names[column]); lv_table_set_col_width(table, column, 42);
    }
    uint8_t first_day = pim_first_day_of_month(ui.calendar.year, ui.calendar.month);
    for (uint8_t day = 1; day <= pim_days_in_month(ui.calendar.year, ui.calendar.month); day++) {
        char number[4]; uint8_t cell = (uint8_t)(first_day + day - 1);
        snprintf(number, sizeof(number), "%u", day);
        lv_table_set_cell_value(table, (uint16_t)(cell / 7 + 1), cell % 7, number);
    }
    focus(first);
}

static void save_appointment(lv_event_t *event) {
    pim_appointment_t value = {0}; (void)event;
    if (!date_from_text(lv_textarea_get_text(ui.date), &value.date) ||
        !minutes_from_text(lv_textarea_get_text(ui.time), &value.minutes)) {
        message("Invalid", "Date: YYYY-MM-DD, time: HH:MM"); return;
    }
    snprintf(value.title, sizeof(value.title), "%s", lv_textarea_get_text(ui.title));
    snprintf(value.description, sizeof(value.description), "%s", lv_textarea_get_text(ui.content));
    if (!pim_add_appointment(ui.database, &value)) { message("Not saved", "Add a title or remove an old record."); return; }
    save(); appointments(NULL);
}
static void delete_appointment(lv_event_t *event) {
    pim_delete_appointment(ui.database, index_from_event(event)); save(); appointment_list(NULL);
}
static void appointment_detail(lv_event_t *event) {
    ui.selected = index_from_event(event); pim_appointment_t *value = &ui.database->appointments[ui.selected];
    char detail[240]; lv_obj_t *first; clear();
    snprintf(detail, sizeof(detail), "%04u-%02u-%02u %02u:%02u\n%s\n\n%s",
             value->date.year, value->date.month, value->date.day, value->minutes / 60,
             value->minutes % 60, value->title, value->description);
    label(detail, 12, 28);
    first = button("Delete", 20, 260, 100, delete_appointment, (void *)(uintptr_t)(ui.selected + 1));
    button("Back", 190, 260, 100, appointment_list, NULL); focus(first);
}
static void appointment_list(lv_event_t *event) {
    lv_obj_t *list, *first; (void)event; clear(); label("Appointments", 108, 8);
    first = button("Back", 112, 280, 95, appointments, NULL);
    list = lv_list_create(lv_screen_active()); lv_obj_set_pos(list, 8, 30); lv_obj_set_size(list, 304, 246); crt_style(list, true);
    for (size_t i = 0; i < ui.database->appointment_count; i++) {
        pim_appointment_t *value = &ui.database->appointments[i]; char row[100];
        snprintf(row, sizeof(row), "%04u-%02u-%02u %02u:%02u %s", value->date.year, value->date.month,
                 value->date.day, value->minutes / 60, value->minutes % 60, value->title);
        lv_obj_t *item = lv_list_add_button(list, NULL, row);
        lv_obj_add_event_cb(item, appointment_detail, LV_EVENT_CLICKED, (void *)(uintptr_t)(i + 1)); crt_style(item, true);
    }
    focus(first);
}
static void appointments(lv_event_t *event) {
    lv_obj_t *first; (void)event; clear(); label("New appointment", 92, 6);
    ui.date = text("YYYY-MM-DD", 12, 32, 140, 28, true); ui.time = text("HH:MM", 168, 32, 92, 28, true);
    ui.title = text("Title", 12, 70, 296, 28, true); ui.content = text("Description", 12, 108, 296, 84, false);
    first = button("Save", 12, 208, 90, save_appointment, NULL);
    button("View", 116, 208, 90, appointment_list, NULL); button("Back", 220, 208, 90, home, NULL);
    focus(first);
}

static void save_todo(lv_event_t *event) {
    pim_todo_t value = {0}; (void)event;
    snprintf(value.title, sizeof(value.title), "%s", lv_textarea_get_text(ui.title));
    value.priority = (pim_priority_t)lv_dropdown_get_selected(ui.choice); value.created_at = now();
    if (!pim_add_todo(ui.database, &value)) { message("Not saved", "Add a title or remove an old record."); return; }
    save(); todos(NULL);
}
static void todo_detail(lv_event_t *event);
static void toggle_todo(lv_event_t *event) { pim_toggle_todo(ui.database, index_from_event(event)); save(); todo_list(NULL); }
static void delete_todo(lv_event_t *event) { pim_delete_todo(ui.database, index_from_event(event)); save(); todo_list(NULL); }
static void todo_detail(lv_event_t *event) {
    ui.selected = index_from_event(event); pim_todo_t *value = &ui.database->todos[ui.selected]; char detail[128]; lv_obj_t *first; clear();
    snprintf(detail, sizeof(detail), "%s\nPriority: %s\nStatus: %s", value->title, pim_priority_name(value->priority),
             value->completed ? "Complete" : "Pending");
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
        snprintf(row, sizeof(row), "%s %s [%s]", value->completed ? "[x]" : "[ ]", value->title,
                 pim_priority_name(value->priority));
        lv_obj_t *item = lv_list_add_button(list, NULL, row);
        lv_obj_add_event_cb(item, todo_detail, LV_EVENT_CLICKED, (void *)(uintptr_t)(i + 1)); crt_style(item, true);
    }
    focus(first);
}
static void todos(lv_event_t *event) {
    lv_obj_t *first; (void)event; clear(); label("New to-do", 118, 12);
    ui.title = text("Task", 12, 48, 296, 32, true); ui.choice = lv_dropdown_create(lv_screen_active());
    lv_dropdown_set_options(ui.choice, "Low\nNormal\nHigh"); lv_dropdown_set_selected(ui.choice, PIM_PRIORITY_NORMAL, LV_ANIM_OFF); crt_style(ui.choice, true);
    lv_obj_set_pos(ui.choice, 12, 94); lv_obj_set_size(ui.choice, 140, 32);
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
static void delete_note(lv_event_t *event) { (void)event; pim_delete_note(ui.database, ui.selected); save(); note_list(NULL); }
static void select_note(lv_event_t *event) { ui.selected = index_from_event(event); note_editor(NULL); }
static void note_list(lv_event_t *event) {
    lv_obj_t *list, *first; (void)event; clear(); label("Notes", 140, 8);
    first = button("New", 12, 280, 90, notes, NULL); button("Back", 218, 280, 90, home, NULL);
    list = lv_list_create(lv_screen_active()); lv_obj_set_pos(list, 8, 30); lv_obj_set_size(list, 304, 246); crt_style(list, true);
    for (size_t i = 0; i < ui.database->note_count; i++) {
        lv_obj_t *item = lv_list_add_button(list, NULL, ui.database->notes[i].title);
        lv_obj_add_event_cb(item, select_note, LV_EVENT_CLICKED, (void *)(uintptr_t)(i + 1)); crt_style(item, true);
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

static void save_journal(lv_event_t *event) {
    pim_journal_entry_t value = {0}; (void)event;
    if (!date_from_text(lv_textarea_get_text(ui.date), &value.date)) { message("Invalid date", "Use YYYY-MM-DD."); return; }
    value.mood = (pim_mood_t)lv_dropdown_get_selected(ui.choice); value.modified_at = now();
    snprintf(value.content, sizeof(value.content), "%s", lv_textarea_get_text(ui.content));
    if (!pim_add_or_update_journal(ui.database, &value)) { message("Not saved", "Enter text or use a valid date."); return; }
    save(); journal(NULL);
}
static void delete_journal(lv_event_t *event) { pim_delete_journal(ui.database, index_from_event(event)); save(); journal_list(NULL); }
static void journal_detail(lv_event_t *event) {
    ui.selected = index_from_event(event); pim_journal_entry_t *value = &ui.database->journal[ui.selected]; char detail[450]; lv_obj_t *first; clear();
    snprintf(detail, sizeof(detail), "%04u-%02u-%02u  %s\n\n%s", value->date.year, value->date.month,
             value->date.day, pim_mood_name(value->mood), value->content);
    label(detail, 12, 24); first = button("Delete", 20, 260, 100, delete_journal, (void *)(uintptr_t)(ui.selected + 1));
    button("Back", 190, 260, 100, journal_list, NULL); focus(first);
}
static void journal_list(lv_event_t *event) {
    lv_obj_t *list, *first; (void)event; clear(); label("Journal", 132, 8);
    first = button("Back", 112, 280, 95, journal, NULL);
    list = lv_list_create(lv_screen_active()); lv_obj_set_pos(list, 8, 30); lv_obj_set_size(list, 304, 246); crt_style(list, true);
    for (size_t i = 0; i < ui.database->journal_count; i++) {
        pim_journal_entry_t *value = &ui.database->journal[i]; char row[80];
        snprintf(row, sizeof(row), "%04u-%02u-%02u [%s]", value->date.year, value->date.month,
                 value->date.day, pim_mood_name(value->mood));
        lv_obj_t *item = lv_list_add_button(list, NULL, row);
        lv_obj_add_event_cb(item, journal_detail, LV_EVENT_CLICKED, (void *)(uintptr_t)(i + 1)); crt_style(item, true);
    }
    focus(first);
}
static void journal(lv_event_t *event) {
    lv_obj_t *first; (void)event; clear(); label("New journal entry", 90, 6);
    ui.date = text("YYYY-MM-DD", 12, 32, 140, 28, true); set_date_text(ui.date, ui.calendar);
    ui.choice = lv_dropdown_create(lv_screen_active()); lv_dropdown_set_options(ui.choice, "Great\nGood\nOkay\nBad\nTerrible");
    lv_dropdown_set_selected(ui.choice, PIM_MOOD_OKAY, LV_ANIM_OFF); lv_obj_set_pos(ui.choice, 168, 32); lv_obj_set_size(ui.choice, 140, 28); crt_style(ui.choice, true);
    ui.content = text("What's on your mind?", 12, 72, 296, 110, false);
    first = button("Save", 12, 198, 90, save_journal, NULL);
    button("View", 116, 198, 90, journal_list, NULL); button("Back", 220, 198, 90, home, NULL); focus(first);
}

static void render_tetris(void) {
    char board[(PIM_TETRIS_WIDTH + 1) * PIM_TETRIS_HEIGHT + 48]; size_t at = 0;
    for (uint8_t y = 0; y < PIM_TETRIS_HEIGHT; y++) {
        for (uint8_t x = 0; x < PIM_TETRIS_WIDTH; x++)
            board[at++] = (ui.tetris.cells[y][x] || pim_tetris_active_cell(&ui.tetris, x, y)) ? '#' : '.';
        board[at++] = '\n';
    }
    snprintf(&board[at], sizeof(board) - at, "Score:%u Lvl:%u", ui.tetris.score, ui.tetris.level);
    lv_label_set_text(ui.game_label, board);
}
static void render_dungeon(void) {
    char board[(PIM_DUNGEON_WIDTH + 1) * PIM_DUNGEON_HEIGHT + 64]; size_t at = 0;
    for (int8_t y = 0; y < PIM_DUNGEON_HEIGHT; y++) {
        for (int8_t x = 0; x < PIM_DUNGEON_WIDTH; x++)
            board[at++] = (x == ui.dungeon.player_x && y == ui.dungeon.player_y) ? '@' : ui.dungeon.cells[y][x];
        board[at++] = '\n';
    }
    snprintf(&board[at], sizeof(board) - at, "HP:%u Gold:%u %s", ui.dungeon.health, ui.dungeon.gold,
             ui.dungeon.game_over ? "DEFEATED" : (ui.dungeon.won ? "VICTORY" : ""));
    lv_label_set_text(ui.game_label, board);
}
static void render_invaders(void) {
    char board[(PIM_INVADERS_WIDTH + 1) * PIM_INVADERS_HEIGHT + 56]; size_t at = 0;
    for (int8_t y = 0; y < PIM_INVADERS_HEIGHT; y++) {
        for (int8_t x = 0; x < PIM_INVADERS_WIDTH; x++) {
            int8_t sx = x - ui.invaders.invader_offset_x, sy = y - ui.invaders.invader_offset_y;
            char cell = (y == PIM_INVADERS_HEIGHT - 1 && x == ui.invaders.player_x) ? 'A' : '.';
            if (ui.invaders.bullet_active && x == ui.invaders.bullet_x && y == ui.invaders.bullet_y) cell = '|';
            if (sx >= 0 && sx < PIM_INVADERS_WIDTH && sy >= 0 && sy < PIM_INVADERS_HEIGHT && ui.invaders.alive[sy][sx]) cell = 'W';
            board[at++] = cell;
        }
        board[at++] = '\n';
    }
    snprintf(&board[at], sizeof(board) - at, "Score:%u %s", ui.invaders.score,
             ui.invaders.game_over ? "GAME OVER" : (ui.invaders.won ? "WAVE CLEAR" : ""));
    lv_label_set_text(ui.game_label, board);
}
static void game_key(lv_event_t *event) {
    uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_ESC) { home(NULL); return; }
    if (ui.game == GAME_TETRIS) {
        if (key == LV_KEY_LEFT) pim_tetris_move(&ui.tetris, -1, 0);
        if (key == LV_KEY_RIGHT) pim_tetris_move(&ui.tetris, 1, 0);
        if (key == LV_KEY_DOWN) pim_tetris_move(&ui.tetris, 0, 1);
        if (key == LV_KEY_UP) pim_tetris_rotate(&ui.tetris);
        if (key == LV_KEY_ENTER) pim_tetris_drop(&ui.tetris);
        render_tetris();
    } else if (ui.game == GAME_DUNGEON) {
        if (key == LV_KEY_UP) pim_dungeon_move(&ui.dungeon, PIM_UP, now());
        if (key == LV_KEY_DOWN) pim_dungeon_move(&ui.dungeon, PIM_DOWN, now());
        if (key == LV_KEY_LEFT) pim_dungeon_move(&ui.dungeon, PIM_LEFT, now());
        if (key == LV_KEY_RIGHT) pim_dungeon_move(&ui.dungeon, PIM_RIGHT, now());
        if (key == LV_KEY_ENTER && (ui.dungeon.game_over || ui.dungeon.won)) pim_dungeon_reset(&ui.dungeon, now());
        render_dungeon();
    } else if (ui.game == GAME_INVADERS) {
        if (key == LV_KEY_LEFT) pim_invaders_move(&ui.invaders, -1);
        if (key == LV_KEY_RIGHT) pim_invaders_move(&ui.invaders, 1);
        if (key == LV_KEY_ENTER) pim_invaders_fire(&ui.invaders);
        render_invaders();
    }
}
static void game_tick(lv_timer_t *timer) {
    (void)timer;
    if (ui.game == GAME_TETRIS && !ui.tetris.game_over) { pim_tetris_move(&ui.tetris, 0, 1); render_tetris(); }
    if (ui.game == GAME_INVADERS) { pim_invaders_tick(&ui.invaders); render_invaders(); }
}
static void start_game(game_t game) {
    lv_obj_t *input; clear(); ui.game = game; ui.game_label = label("", 12, 6);
    input = lv_button_create(lv_screen_active()); lv_obj_set_pos(input, 318, 318); lv_obj_set_size(input, 1, 1);
    lv_obj_add_event_cb(input, game_key, LV_EVENT_KEY, NULL); focus(input);
    if (game == GAME_TETRIS) { pim_tetris_reset(&ui.tetris, now()); ui.timer = lv_timer_create(game_tick, 500, NULL); render_tetris(); }
    else if (game == GAME_DUNGEON) { pim_dungeon_reset(&ui.dungeon, now()); render_dungeon(); }
    else { pim_invaders_reset(&ui.invaders); ui.timer = lv_timer_create(game_tick, 300, NULL); render_invaders(); }
}
static void tetris(lv_event_t *event) { (void)event; start_game(GAME_TETRIS); }
static void dungeon(lv_event_t *event) { (void)event; start_game(GAME_DUNGEON); }
static void invaders(lv_event_t *event) { (void)event; start_game(GAME_INVADERS); }

void pim_ui_start(pim_database_t *database, const char *storage_path) {
    ui = (ui_t){.database = database, .storage_path = storage_path, .calendar = {2026, 1, 1}};
    home(NULL);
}
