/* Headless simulation of the PicoCalc UI: runs src/firmware/ui.c on LVGL with
 * synthetic key presses and inspects widgets, pixels, the SD sandbox and the database. */
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lvgl/lvgl.h"
#include "pico/time.h"
#include "pim/pim.h"
#include "pim/storage.h"
#include "pim/ui.h"
#include "pim/wifi.h"

#define ROOT "pim_sim_root"
#define WIDTH 320
#define HEIGHT 320

static int failures, checks;
static const char *scenario = "init";
static int sim_battery = 87;
static uint32_t sim_ms;
static uint16_t framebuffer[WIDTH * HEIGHT];
static uint32_t key_queue[512];
static size_t key_head, key_tail;
static pim_database_t database;
static bool wifi_result = true;
static char wifi_ssid[64], wifi_password[64];
static int wifi_calls;

#define CHECK(condition) check((condition), #condition, __LINE__)
static void check(bool condition, const char *text, int line) {
    checks++;
    if (!condition) { failures++; printf("FAIL [%s] line %d: %s\n", scenario, line, text); }
}

absolute_time_t get_absolute_time(void) { return (absolute_time_t)sim_ms * 1000U; }
uint32_t to_ms_since_boot(absolute_time_t time) { return (uint32_t)(time / 1000U); }
static bool sim_up, sim_ntp_ok = true, sim_scan_ok = true, sim_telnet_ok = true, sim_telnet_open;
static uint32_t sim_ntp_seconds = 1773570000U; /* 2026-03-15 10:20:00 UTC */
static char sim_rx[512], sim_tx[128], sim_telnet_host[64];
static size_t sim_rx_length, sim_tx_length;
static unsigned sim_telnet_port;
static pim_wifi_hooks_t sim_hooks;
bool pim_wifi_connect(const char *ssid, const char *password, char *address, size_t address_size) {
    wifi_calls++;
    snprintf(wifi_ssid, sizeof(wifi_ssid), "%s", ssid);
    snprintf(wifi_password, sizeof(wifi_password), "%s", password);
    snprintf(address, address_size, "192.168.1.50");
    sim_up = wifi_result;
    return wifi_result;
}
bool pim_wifi_connected(void) { return sim_up; }
const char *pim_wifi_error(void) { return ""; }
void pim_wifi_set_progress(void (*hook)(const char *stage)) { (void)hook; }
const char *pim_wifi_address(void) { return "192.168.1.50"; }
const char *pim_wifi_pin(void) { return "4821"; }
int pim_wifi_scan(pim_wifi_ap_t *list, int max) {
    if (!sim_scan_ok || max < 2) return -1;
    snprintf(list[0].ssid, sizeof(list[0].ssid), "HomeNet"); list[0].rssi = -55;
    snprintf(list[1].ssid, sizeof(list[1].ssid), "CafeWifi"); list[1].rssi = -70;
    return 2;
}
bool pim_wifi_ntp(uint32_t *seconds) { *seconds = sim_ntp_seconds; return sim_ntp_ok; }
void pim_wifi_attach(pim_database_t *db, const char *storage_path, const char *root, const pim_wifi_hooks_t *hooks) {
    (void)db; (void)storage_path; (void)root; sim_hooks = *hooks;
}
bool pim_telnet_open(const char *host, uint16_t port) {
    snprintf(sim_telnet_host, sizeof(sim_telnet_host), "%s", host); sim_telnet_port = port;
    sim_telnet_open = sim_telnet_ok; sim_rx_length = sim_tx_length = 0;
    return sim_telnet_ok;
}
void pim_telnet_close(void) { sim_telnet_open = false; }
bool pim_telnet_is_open(void) { return sim_telnet_open; }
size_t pim_telnet_read(uint8_t *data, size_t max) {
    size_t count = sim_rx_length < max ? sim_rx_length : max;
    memcpy(data, sim_rx, count);
    memmove(sim_rx, sim_rx + count, sim_rx_length - count);
    sim_rx_length -= count;
    return count;
}
bool pim_telnet_write(const uint8_t *data, size_t length) {
    if (sim_tx_length + length >= sizeof(sim_tx)) return false;
    memcpy(sim_tx + sim_tx_length, data, length); sim_tx_length += length; sim_tx[sim_tx_length] = '\0';
    return true;
}
static void server_says(const char *text) {
    size_t length = strlen(text);
    memcpy(sim_rx + sim_rx_length, text, length); sim_rx_length += length;
}
void pim_wifi_poll(void) {}

/* Mirrors src/firmware/input.c: one press then one release per key. */
static void read_keyboard(lv_indev_t *indev, lv_indev_data_t *data) {
    static uint32_t previous;
    static bool release_pending;
    (void)indev;
    if (release_pending) {
        data->key = previous; data->state = LV_INDEV_STATE_RELEASED; release_pending = false; return;
    }
    if (key_head == key_tail) { data->key = 0; data->state = LV_INDEV_STATE_RELEASED; return; }
    previous = key_queue[key_head++ % 512];
    data->key = previous; data->state = LV_INDEV_STATE_PRESSED; release_pending = true;
}
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    (void)area; (void)pixels;
    lv_display_flush_ready(display);
}

static void pump(int steps) {
    for (int i = 0; i < steps; i++) { sim_ms += 10; lv_tick_inc(10); lv_timer_handler(); }
}
static void key(uint32_t code) {
    key_queue[key_tail++ % 512] = code;
    for (int i = 0; i < 40 && key_head != key_tail; i++) pump(1);
    pump(8);
}
static void keys(uint32_t code, int count) { while (count-- > 0) key(code); }
static void type(const char *text) {
    for (; *text; text++) key(*text == '\n' ? LV_KEY_ENTER : (uint32_t)(unsigned char)*text);
}

static void collect(lv_obj_t *object, char *out, size_t capacity) {
    size_t length = strlen(out);
    const char *text = NULL;
    char selected[64];
    if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_check_type(object, &lv_label_class)) text = lv_label_get_text(object);
    else if (lv_obj_check_type(object, &lv_dropdown_class)) {
        lv_dropdown_get_selected_str(object, selected, sizeof(selected)); text = selected;
    }
    if (text != NULL && length + strlen(text) + 2 < capacity) { strcat(out, text); strcat(out, "\n"); }
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); i++) collect(lv_obj_get_child(object, (int32_t)i), out, capacity);
}
static const char *screen(void) {
    static char text[16384];
    text[0] = '\0';
    collect(lv_screen_active(), text, sizeof(text));
    collect(lv_layer_top(), text, sizeof(text));
    return text;
}
static bool see(const char *needle) { return strstr(screen(), needle) != NULL; }
static bool see_line(const char *needle) {
    const char *text = screen(), *at;
    size_t length = strlen(needle);
    for (at = text; (at = strstr(at, needle)) != NULL; at++)
        if ((at == text || at[-1] == '\n') && at[length] == '\n') return true;
    return false;
}
static lv_obj_t *focused(void) { return lv_group_get_focused(lv_group_get_default()); }
static const char *first_label(lv_obj_t *object) {
    if (lv_obj_check_type(object, &lv_label_class)) return lv_label_get_text(object);
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); i++) {
        const char *text = first_label(lv_obj_get_child(object, (int32_t)i));
        if (text != NULL) return text;
    }
    return NULL;
}
static const char *focus_text(void) {
    lv_obj_t *object = focused();
    if (object == NULL) return "<none>";
    if (lv_obj_check_type(object, &lv_textarea_class)) return "<textarea>";
    if (lv_obj_check_type(object, &lv_dropdown_class)) return "<dropdown>";
    const char *text = first_label(object);
    return text ? text : "<unnamed>";
}
static bool focus_is(const char *text) { return strcmp(focus_text(), text) == 0; }
static bool goto_focus(const char *text) {
    for (int i = 0; i < 90; i++) {
        if (focus_is(text)) return true;
        key(LV_KEY_NEXT);
    }
    printf("  (could not focus \"%s\"; focus=\"%s\"; screen:\n%s)\n", text, focus_text(), screen());
    return false;
}
static bool activate(const char *text) {
    bool found = goto_focus(text);
    if (found) key(LV_KEY_ENTER);
    return found;
}

/* Every rendered pixel must lie on the black-to-amber gradient. */
static void theme(const char *name) {
    int bad = 0, first_x = -1, first_y = -1, x0 = WIDTH, y0 = HEIGHT, x1 = -1, y1 = -1;
    unsigned first_color = 0;
    uint32_t fg_hex, bg_hex;
    double fg[3], bg[3], span = 0;
    pim_ui_theme_colors(&fg_hex, &bg_hex);
    for (int c = 0; c < 3; c++) {
        fg[c] = (fg_hex >> (16 - 8 * c)) & 255; bg[c] = (bg_hex >> (16 - 8 * c)) & 255;
        span += (fg[c] - bg[c]) * (fg[c] - bg[c]);
    }
    lv_refr_now(NULL);
    for (int y = 0; y < HEIGHT; y++) for (int x = 0; x < WIDTH; x++) {
        uint16_t pixel = framebuffer[y * WIDTH + x];
        double p[3] = {((pixel >> 11) & 31) * 255.0 / 31, ((pixel >> 5) & 63) * 255.0 / 63, (pixel & 31) * 255.0 / 31};
        double t = 0, worst = 0;
        for (int c = 0; c < 3; c++) t += (p[c] - bg[c]) * (fg[c] - bg[c]);
        t /= span;
        if (t < -0.05 || t > 1.05) worst = 99;
        for (int c = 0; c < 3; c++) {
            double error = p[c] - (bg[c] + t * (fg[c] - bg[c]));
            if (error < 0) error = -error;
            if (error > worst) worst = error;
        }
        if (worst > 14) { if (bad++ == 0) { first_x = x; first_y = y; first_color = pixel; } if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    }
    checks++;
    if (bad) {
        failures++;
        printf("FAIL [%s] theme at \"%s\": %d off-palette pixels (first %d,%d = 0x%04x, box %d,%d-%d,%d)\n  focus=\"%s\"\n",
               scenario, name, bad, first_x, first_y, first_color, x0, y0, x1, y1, focus_text());
    }
}

static int count_timers(void) {
    int count = 0;
    for (lv_timer_t *timer = lv_timer_get_next(NULL); timer; timer = lv_timer_get_next(timer)) count++;
    return count;
}
static void make_dirs(void) {
    static const char *const names[] = {"", "/appointments", "/calendar", "/journal", "/notes", "/todos"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        char path[64]; snprintf(path, sizeof(path), "%s%s", ROOT, names[i]); mkdir(path, 0777);
    }
}
static const char *forced_theme = "Amber";
static void confirm_startup(void) {
    pump(10);
    if (see("Set date and time")) activate("Start");
    pump(10);
}
static void reset_ui(const char *name) {
    int status;
    scenario = name;
    lv_obj_clean(lv_layer_top());
    lv_obj_clean(lv_screen_active());
    status = system("rm -rf " ROOT);
    (void)status;
    make_dirs();
    {
        FILE *file = fopen(ROOT "/theme.cfg", "wb");
        if (file != NULL) { fprintf(file, "%s\n", forced_theme); fclose(file); }
    }
    pim_database_init(&database);
    pim_ui_start(&database, ROOT "/pim.dat");
    confirm_startup();
    wifi_result = true; wifi_calls = 0; sim_up = false; sim_ntp_ok = true; sim_scan_ok = true; sim_telnet_ok = true;
    sim_telnet_open = false; sim_rx_length = sim_tx_length = 0; sim_ntp_seconds = 1773570000U;
}
static bool file_contains(const char *path, const char *needle) {
    char buffer[8192] = {0};
    FILE *file = fopen(path, "rb");
    size_t length;
    if (file == NULL) return false;
    length = fread(buffer, 1, sizeof(buffer) - 1, file);
    buffer[length] = '\0';
    fclose(file);
    return strstr(buffer, needle) != NULL;
}
static bool exists(const char *path) { struct stat s; return stat(path, &s) == 0; }
static bool database_persisted(void) {
    static pim_database_t loaded;
    return pim_storage_load(ROOT "/pim.dat", &loaded) && loaded.appointment_count == database.appointment_count &&
           loaded.todo_count == database.todo_count && loaded.note_count == database.note_count &&
           loaded.journal_count == database.journal_count;
}

static void test_home(void) {
    static const char *const entries[] = {"Calendar", "To-Do List", "Notes", "Journal", "Files",
                                          "Text editor", "Wi-Fi & web", "Tetris", "Dungeon RPG", "Space Invaders", "Solitaire", "Theme", "Telnet (BBS)"};
    reset_ui("home");
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) CHECK(see_line(entries[i]));
    CHECK(!see("Appointments"));
    CHECK(see("Battery: 87%"));
    sim_battery = 15 + 128; pump(1600); CHECK(see("Battery: 15% charging"));
    sim_battery = 9; pump(1600); CHECK(see("Battery: 9% LOW"));
    sim_battery = 0; pump(1600); CHECK(see("Battery: no data"));
    sim_battery = 87; pump(1600); CHECK(see("Battery: 87%"));
    theme("home");
    CHECK(focus_is("Calendar"));
    key(LV_KEY_RIGHT); CHECK(focus_is("To-Do List"));
    key(LV_KEY_DOWN); CHECK(focus_is("Notes"));
    key(LV_KEY_LEFT); CHECK(focus_is("To-Do List"));
    key(LV_KEY_UP); CHECK(focus_is("Calendar"));
    key(LV_KEY_PREV); CHECK(focus_is("Telnet (BBS)"));
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        if (!activate(entries[i])) continue;
        key(LV_KEY_ESC);
        CHECK(see_line("PicoCalc PIM"));
    }
}

static void test_startup(void) {
    reset_ui("startup");
    pim_ui_start(&database, ROOT "/pim.dat"); pump(10);
    CHECK(see("Set date and time")); CHECK(see("2026"));
    CHECK(see("Start")); CHECK(!see("Calendar")); theme("startup");
    key(LV_KEY_ESC); CHECK(see("Set date and time")); /* cannot be skipped */
    key('1'); key('5'); key(LV_KEY_RIGHT); key('0'); key('3'); key(LV_KEY_RIGHT);
    key(LV_KEY_UP); key(LV_KEY_RIGHT); key('1'); key('4'); key(LV_KEY_RIGHT); key('3'); key('0');
    CHECK(see("15") && see("03") && see("2027") && see("14") && see("30"));
    CHECK(activate("Start")); CHECK(see_line("PicoCalc PIM")); CHECK(see("15-03-2027  14:30"));
    CHECK(file_contains(ROOT "/today.cfg", "15-03-2027 14:30"));
    pump(6000); CHECK(see("15-03-2027  14:31")); /* clock advances */
    CHECK(activate("Calendar")); CHECK(see("03-2027")); CHECK(focus_is("15"));
    key(LV_KEY_ESC);
    pim_ui_start(&database, ROOT "/pim.dat"); pump(10);
    CHECK(see("Set date and time")); CHECK(see("14") && see("2027")); /* prefilled from the saved clock */
    CHECK(activate("Start")); CHECK(see_line("PicoCalc PIM"));
}

static void test_calendar_navigation(void) {
    reset_ui("calendar-nav");
    CHECK(activate("Calendar"));
    CHECK(see("01-2026")); theme("calendar");
    CHECK(focus_is("1"));
    key(LV_KEY_RIGHT); CHECK(focus_is("2"));
    key(LV_KEY_DOWN); CHECK(focus_is("9"));
    key(LV_KEY_LEFT); CHECK(focus_is("8"));
    key(LV_KEY_UP); CHECK(focus_is("1"));
    key(LV_KEY_UP); CHECK(see("12-2025")); CHECK(focus_is("25"));
    key(LV_KEY_DOWN); CHECK(see("01-2026")); CHECK(focus_is("1"));
    key(LV_KEY_LEFT); CHECK(see("12-2025")); CHECK(focus_is("31"));
    key(LV_KEY_ENTER); CHECK(see("31-12-2025")); theme("day-view");
    CHECK(see("No appointments"));
    key(LV_KEY_ESC); CHECK(see_line("PicoCalc PIM"));
    reset_ui("calendar-keys");
    CHECK(activate("Calendar"));
    key(','); CHECK(see("12-2025"));
    key('.'); key('.'); CHECK(see("02-2026"));
    key('>'); CHECK(see("02-2027"));
    key('<'); key('<'); CHECK(see("02-2025"));
    key('t'); CHECK(see("01-2026")); CHECK(focus_is("1"));
    keys(LV_KEY_DOWN, 4); CHECK(focus_is("29"));
    key(LV_KEY_RIGHT); key(LV_KEY_RIGHT); CHECK(focus_is("31"));
    key(LV_KEY_RIGHT); CHECK(see("02-2026")); CHECK(focus_is("1"));
    CHECK(goto_focus("<")); key(LV_KEY_ENTER); CHECK(see("01-2026"));
    CHECK(goto_focus(">")); key(LV_KEY_ENTER); CHECK(see("02-2026"));
    CHECK(goto_focus("Go to")); key(LV_KEY_RIGHT); CHECK(focus_is("Back")); key(LV_KEY_RIGHT); CHECK(focus_is("Today"));
    key(LV_KEY_UP); CHECK(focus_is("1"));
    CHECK(activate("Today")); CHECK(see("01-2026"));
    CHECK(activate("Back")); CHECK(see_line("PicoCalc PIM"));
}

static void test_date_picker(void) {
    reset_ui("date-picker");
    CHECK(activate("Calendar"));
    key('g'); CHECK(see("Choose a date")); CHECK(see("2026")); theme("date picker");
    key('1'); key('5'); CHECK(see("15"));
    key(LV_KEY_RIGHT); key('0'); key('3');
    key(LV_KEY_RIGHT); key('2'); key('0'); key('2'); key('7');
    CHECK(activate("Go")); CHECK(see("03-2027")); CHECK(focus_is("15"));
    CHECK(activate("Go to")); CHECK(see("Choose a date"));
    key('3'); key('1'); key(LV_KEY_ENTER); key('0'); key('2'); key(LV_KEY_ENTER);
    CHECK(see("28"));
    key(LV_KEY_UP); key(LV_KEY_UP); CHECK(see("2029"));
    key(LV_KEY_LEFT); key(LV_KEY_DOWN); CHECK(see("01"));
    key(LV_KEY_LEFT); key(LV_KEY_DOWN); CHECK(see("27"));
    key(LV_KEY_ESC); CHECK(see("03-2027")); /* Esc leaves without applying */
    CHECK(activate("Go to")); CHECK(activate("Back")); CHECK(see("03-2027"));
    reset_ui("date-picker-today");
    CHECK(activate("Calendar"));
    CHECK(activate("Go to")); key('2'); key('0');
    CHECK(activate("Set today")); CHECK(see("Today set")); CHECK(see("Today is 20-01-2026"));
    key(LV_KEY_ESC);
    CHECK(file_contains(ROOT "/today.cfg", "20-01-2026"));
    CHECK(activate("Go")); CHECK(focus_is("20"));
    key('.'); CHECK(see("02-2026")); key('t'); CHECK(see("01-2026")); CHECK(focus_is("20"));
    pim_ui_start(&database, ROOT "/pim.dat"); confirm_startup();
    CHECK(activate("Calendar")); CHECK(see("01-2026")); CHECK(focus_is("20"));
    key(LV_KEY_ESC); CHECK(activate("Journal")); CHECK(see("20-01-2026"));
    key(LV_KEY_ESC); key('x');
    reset_ui("today-from-picker");
    CHECK(activate("Calendar")); CHECK(activate("Go to")); key('2'); key('0');
    CHECK(activate("Set today")); key(LV_KEY_ESC); /* closes the message */
    key(LV_KEY_ESC); CHECK(see_line("PicoCalc PIM"));
    CHECK(activate("Calendar")); CHECK(see("01-2026")); CHECK(focus_is("20"));
    key(LV_KEY_ESC); CHECK(activate("Journal")); CHECK(see("20-01-2026"));
    reset_ui("date-picker-journal");
    CHECK(activate("Journal")); CHECK(see("01-01-2026"));
    CHECK(activate("01-01-2026")); CHECK(see("Choose a date"));
    key('0'); key('7'); CHECK(activate("Go")); CHECK(see("07-01-2026")); theme("journal date");
    CHECK(activate("07-01-2026")); CHECK(activate("Back")); CHECK(see("07-01-2026"));
    key(LV_KEY_ESC); CHECK(see_line("PicoCalc PIM"));
}

static void test_time_entry(void) {
    reset_ui("time-entry");
    CHECK(activate("Calendar")); key(LV_KEY_ENTER); CHECK(activate("New"));
    CHECK(see("09:00-10:00"));
    key('1'); key('4'); CHECK(see("14:00-15:00"));
    key(LV_KEY_RIGHT); key('3'); key('0'); CHECK(see("14:30-15:30"));
    key(LV_KEY_UP); CHECK(see("14:35"));
    key(LV_KEY_DOWN); key(LV_KEY_DOWN); CHECK(see("14:25"));
    key('7'); key('5'); CHECK(see("14:05"));
    key(LV_KEY_LEFT); key('9'); key('9'); CHECK(see("09:05"));
    theme("time entry");
    key(LV_KEY_ESC); CHECK(see("01-2026") || see("01-01-2026"));
}

static void test_appointment_flow(void) {
    reset_ui("appointment");
    CHECK(activate("Calendar"));
    keys(LV_KEY_DOWN, 2); CHECK(focus_is("15"));
    key(LV_KEY_ENTER); CHECK(see("15-01-2026")); theme("empty day");
    CHECK(activate("New"));
    CHECK(see("Hour") && see("Minute") && see("Length") && see("09:00-10:00")); theme("picker");
    CHECK(focus_is("09"));
    key(LV_KEY_UP); CHECK(focus_is("10"));
    key(LV_KEY_RIGHT); key(LV_KEY_UP); key(LV_KEY_UP); CHECK(focus_is("10"));
    key(LV_KEY_RIGHT); CHECK(focus_is("1h"));
    key(LV_KEY_UP); CHECK(focus_is("1h30"));
    CHECK(see("10:10-11:40"));
    key(LV_KEY_ENTER); CHECK(focus_is("Next"));
    key(LV_KEY_ENTER);
    CHECK(see("15-01-2026 10:10-11:40")); theme("details");
    CHECK(focus_is("<textarea>"));
    key(LV_KEY_NEXT); key(LV_KEY_NEXT); CHECK(focus_is("Save"));
    key(LV_KEY_ENTER);
    CHECK(see("Title needed")); theme("title needed message");
    key(LV_KEY_ENTER);
    CHECK(!see("Title needed"));
    CHECK(goto_focus("<textarea>"));
    type("Dentist");
    key(LV_KEY_NEXT); type("Bring card\nsecond");
    keys(LV_KEY_LEFT, 6); type("X");
    CHECK(activate("Save"));
    CHECK(see("10:10-11:40 Dentist")); theme("day with appointment");
    CHECK(database.appointment_count == 1);
    if (database.appointment_count == 1) {
        pim_appointment_t *value = &database.appointments[0];
        CHECK(value->date.year == 2026 && value->date.month == 1 && value->date.day == 15);
        CHECK(value->minutes == 10 * 60 + 10 && value->duration == 90);
        CHECK(strcmp(value->title, "Dentist") == 0);
        CHECK(strcmp(value->description, "Bring card\nXsecond") == 0);
    }
    CHECK(database_persisted());
    CHECK(file_contains(ROOT "/appointments/appointments.csv", "15-01-2026,10:10,90,\"Dentist\""));
    CHECK(file_contains(ROOT "/calendar/calendar.md", "15-01-2026 10:10-11:40 - Dentist"));
    CHECK(activate("Back"));
    CHECK(goto_focus("15*"));
    key(LV_KEY_ENTER);
    CHECK(activate("10:10-11:40 Dentist"));
    CHECK(see("Dentist") && see("Bring card")); theme("appointment detail");
    CHECK(activate("Delete"));
    CHECK(database.appointment_count == 0 && see("No appointments"));
    CHECK(database_persisted());
    CHECK(!file_contains(ROOT "/appointments/appointments.csv", "Dentist"));
}

static void test_appointment_limits(void) {
    reset_ui("appointment-limits");
    CHECK(activate("Calendar")); key(LV_KEY_ENTER);
    CHECK(activate("New"));
    keys(LV_KEY_DOWN, 10); CHECK(focus_is("23"));
    key(LV_KEY_RIGHT); keys(LV_KEY_UP, 6); CHECK(focus_is("30"));
    key(LV_KEY_RIGHT); CHECK(see("23:30-24:30"));
    key(LV_KEY_ENTER); key(LV_KEY_ENTER);
    CHECK(goto_focus("<textarea>")); type("Night");
    CHECK(activate("Save"));
    CHECK(see("Not saved")); theme("not saved message");
    key(LV_KEY_ESC);
    CHECK(!see("Not saved") && see("Night"));
    CHECK(database.appointment_count == 0);
    CHECK(activate("Back")); CHECK(see("Hour"));
    CHECK(activate("Back")); CHECK(see("01-01-2026"));
    for (int i = 0; i < PIM_MAX_APPOINTMENTS; i++) {
        pim_appointment_t value = {.date = {2026, 1, 1}, .minutes = (uint16_t)(i * 20), .duration = 15};
        snprintf(value.title, sizeof(value.title), "Item %d", i);
        CHECK(pim_add_appointment(&database, &value));
    }
    CHECK(activate("Back")); key(LV_KEY_ESC); CHECK(activate("Calendar")); key(LV_KEY_ENTER);
    CHECK(see("Item 0")); theme("full day list");
    CHECK(activate("New")); key(LV_KEY_ENTER); key(LV_KEY_ENTER); key(LV_KEY_ENTER); key(LV_KEY_ENTER);
    CHECK(goto_focus("<textarea>")); type("Overflow");
    CHECK(activate("Save")); CHECK(see("Not saved"));
    key(LV_KEY_ENTER);
}

static void test_todos(void) {
    reset_ui("todos");
    CHECK(activate("To-Do List")); theme("todo form");
    CHECK(focus_is("Add"));
    key(LV_KEY_ENTER); CHECK(see("Not saved")); theme("todo message"); key(LV_KEY_ENTER);
    CHECK(goto_focus("<textarea>")); type("Buy milk");
    CHECK(goto_focus("<dropdown>")); key(LV_KEY_ENTER); theme("dropdown open");
    key(LV_KEY_DOWN); key(LV_KEY_ENTER);
    CHECK(activate("Add"));
    CHECK(database.todo_count == 1);
    if (database.todo_count) CHECK(database.todos[0].priority == PIM_PRIORITY_HIGH && strcmp(database.todos[0].title, "Buy milk") == 0);
    CHECK(database_persisted());
    CHECK(activate("View")); CHECK(see("[ ] Buy milk [High]")); theme("todo list");
    CHECK(activate("[ ] Buy milk [High]")); CHECK(see("Priority: High")); theme("todo detail");
    CHECK(activate("Mark complete")); CHECK(see("[x] Buy milk"));
    CHECK(file_contains(ROOT "/todos/todos.csv", "done,High"));
    CHECK(activate("[x] Buy milk [High]")); CHECK(activate("Delete"));
    CHECK(database.todo_count == 0);
    CHECK(activate("Back")); CHECK(see_line("New to-do"));
}

static void test_notes(void) {
    char path[96];
    reset_ui("notes");
    CHECK(activate("Notes")); theme("note editor");
    CHECK(goto_focus("Save")); key(LV_KEY_ENTER); CHECK(see("Not saved")); key(LV_KEY_ENTER);
    CHECK(goto_focus("<textarea>")); type("Ideas");
    key(LV_KEY_NEXT); type("Line one\nLine two");
    CHECK(activate("Save"));
    CHECK(see("Ideas")); theme("note list");
    CHECK(database.note_count == 1);
    snprintf(path, sizeof(path), ROOT "/notes/note-%08u.md", database.notes[0].id);
    CHECK(file_contains(path, "# Ideas") && file_contains(path, "Line two"));
    CHECK(activate("Ideas")); CHECK(see("Edit note"));
    CHECK(goto_focus("<textarea>")); key(LV_KEY_NEXT); type("!");
    CHECK(activate("Save"));
    CHECK(strstr(database.notes[0].content, "!") != NULL);
    CHECK(activate("Ideas")); CHECK(activate("Delete"));
    CHECK(database.note_count == 0 && !exists(path));
    CHECK(database_persisted());
}

static void test_journal(void) {
    reset_ui("journal");
    CHECK(activate("Journal")); CHECK(see("01-01-2026")); theme("journal");
    CHECK(activate(">")); CHECK(see("02-01-2026"));
    CHECK(activate("<")); CHECK(activate("<")); CHECK(see("31-12-2025"));
    CHECK(activate(">")); CHECK(see("01-01-2026"));
    CHECK(goto_focus("<dropdown>"));
    key(LV_KEY_ENTER); key(LV_KEY_UP); key(LV_KEY_ENTER);
    CHECK(goto_focus("<textarea>")); type("Good day");
    CHECK(activate("Save"));
    CHECK(database.journal_count == 1);
    if (database.journal_count) CHECK(database.journal[0].mood == PIM_MOOD_GOOD);
    CHECK(file_contains(ROOT "/journal/01-01-2026.md", "Good day"));
    CHECK(see("Good day"));
    CHECK(activate("View")); CHECK(see("01-01-2026 [Good]")); theme("journal list");
    CHECK(activate("Back")); CHECK(see("Journal"));
}

static void test_journal_detail_delete(void) {
    pim_journal_entry_t entry = {.date = {2026, 3, 4}, .mood = PIM_MOOD_GREAT, .content = "Hello"};
    reset_ui("journal-delete");
    CHECK(pim_add_or_update_journal(&database, &entry));
    pim_storage_save(ROOT "/pim.dat", &database); pim_storage_export(ROOT, &database);
    CHECK(activate("Journal")); CHECK(activate("View"));
    CHECK(activate("04-03-2026 [Great]")); CHECK(see("Hello")); theme("journal detail");
    CHECK(activate("Delete"));
    CHECK(database.journal_count == 0 && !exists(ROOT "/journal/04-03-2026.md"));
}

static void test_files(void) {
    reset_ui("files");
    CHECK(activate("Files")); CHECK(see(ROOT)); theme("file list");
    CHECK(see("[DIR] notes") && see("[DIR] todos"));
    CHECK(activate("New")); theme("new file");
    CHECK(activate("Create")); CHECK(see("Invalid name")); theme("invalid name"); key(LV_KEY_ENTER);
    CHECK(goto_focus("<textarea>")); type("bad name");
    CHECK(activate("Create")); CHECK(see("Invalid name")); key(LV_KEY_ENTER);
    CHECK(goto_focus("<textarea>")); keys(LV_KEY_BACKSPACE, 8);
    type("hello.txt");
    CHECK(activate("Create")); CHECK(see("hello.txt")); theme("editor");
    CHECK(exists(ROOT "/hello.txt"));
    CHECK(goto_focus("<textarea>"));
    type("Line one\nLine two\n");
    for (int i = 0; i < 3; i++) type("0123456789abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789abcdefg");
    key(LV_KEY_UP); key(LV_KEY_LEFT); type("Q");
    CHECK(activate("Save")); CHECK(see("hello.txt"));
    CHECK(file_contains(ROOT "/hello.txt", "Line one\nLine two\n"));
    CHECK(file_contains(ROOT "/hello.txt", "Q"));
    CHECK(activate("hello.txt")); CHECK(see("Line two")); theme("editor with text");
    CHECK(activate("Delete")); CHECK(!exists(ROOT "/hello.txt"));
    CHECK(activate("Dir"));
    CHECK(goto_focus("<textarea>")); type("stuff");
    CHECK(activate("Create")); CHECK(see("[DIR] stuff")); CHECK(exists(ROOT "/stuff"));
    CHECK(activate("Dir")); CHECK(goto_focus("<textarea>")); type("stuff");
    CHECK(activate("Create")); CHECK(see("Not created")); theme("exists"); key(LV_KEY_ENTER);
    CHECK(activate("Files"));
    CHECK(activate("[DIR] stuff")); CHECK(see(ROOT "/stuff")); CHECK(!see("[DIR] notes"));
    CHECK(activate("New")); CHECK(goto_focus("<textarea>")); type("a.md"); CHECK(activate("Create"));
    CHECK(goto_focus("<textarea>")); type("# hi"); CHECK(activate("Save"));
    CHECK(exists(ROOT "/stuff/a.md") && see("a.md"));
    CHECK(activate("Up")); CHECK(see("[DIR] stuff") && see("[DIR] notes"));
    CHECK(activate("Delete")); CHECK(goto_focus("<textarea>")); type("stuff");
    CHECK(activate("Delete")); CHECK(see("Not deleted")); key(LV_KEY_ENTER); CHECK(exists(ROOT "/stuff"));
    CHECK(activate("Files")); CHECK(activate("[DIR] stuff"));
    CHECK(activate("Delete")); CHECK(goto_focus("<textarea>")); type("a.md"); CHECK(activate("Delete"));
    CHECK(!exists(ROOT "/stuff/a.md"));
    CHECK(activate("Up"));
    CHECK(activate("Delete")); CHECK(goto_focus("<textarea>")); type("stuff"); CHECK(activate("Delete"));
    CHECK(!exists(ROOT "/stuff"));
    CHECK(activate("Delete")); CHECK(goto_focus("<textarea>")); type("notes"); CHECK(activate("Delete"));
    CHECK(see("Protected")); theme("protected"); key(LV_KEY_ENTER); CHECK(exists(ROOT "/notes"));
    CHECK(activate("Files"));
    CHECK(activate("Home")); CHECK(see_line("PicoCalc PIM"));
}

static void test_text_editor_entry(void) {
    reset_ui("text-editor");
    CHECK(activate("Text editor")); CHECK(see("Create text file"));
    CHECK(goto_focus("<textarea>")); type("todo.txt"); CHECK(activate("Create"));
    CHECK(see("todo.txt")); CHECK(exists(ROOT "/todo.txt"));
    key(LV_KEY_ESC); CHECK(see_line("PicoCalc PIM"));
}

static void test_wifi(void) {
    reset_ui("wifi");
    CHECK(activate("Wi-Fi & web")); theme("wifi menu");
    CHECK(see("Not connected") && see("UTC+05:30"));
    CHECK(activate("Web address")); CHECK(see("Not connected")); key(LV_KEY_ESC);
    CHECK(activate("Manual entry")); theme("wifi manual");
    CHECK(goto_focus("<textarea>")); type("HomeNet"); key(LV_KEY_NEXT); type("secret pass");
    CHECK(activate("Connect"));
    CHECK(wifi_calls == 1 && strcmp(wifi_ssid, "HomeNet") == 0 && strcmp(wifi_password, "secret pass") == 0);
    CHECK(see("192.168.1.50:8080") && see("PIN: 4821") && see("Cookie: pim=4821")); theme("wifi success"); key(LV_KEY_ESC);
    CHECK(see("Connected") && !see("Not connected"));
    CHECK(file_contains(ROOT "/wifi.cfg", "net=HomeNet\tsecret pass"));
    CHECK(activate("Web address")); CHECK(see("PIN: 4821")); key(LV_KEY_ESC);
    /* failure keeps the screen usable and does not store the network */
    wifi_result = false;
    CHECK(activate("Manual entry"));
    CHECK(goto_focus("<textarea>")); type("Bad"); key(LV_KEY_NEXT); type("nope");
    CHECK(activate("Connect")); CHECK(see("Wi-Fi failed")); theme("wifi failure"); key(LV_KEY_ESC);
    CHECK(!file_contains(ROOT "/wifi.cfg", "Bad"));
    CHECK(activate("Back")); CHECK(see_line("PicoCalc PIM"));
}

static void test_wifi_networks(void) {
    reset_ui("wifi networks");
    CHECK(activate("Wi-Fi & web")); CHECK(activate("Join saved")); CHECK(see("No saved networks")); theme("no saved");
    CHECK(activate("Back"));
    CHECK(activate("Scan networks")); CHECK(see("HomeNet") && see("CafeWifi") && see("-55dBm")); theme("scan list");
    CHECK(activate("CafeWifi  -70dBm"));
    CHECK(see("Join network")); CHECK(see("CafeWifi"));
    CHECK(goto_focus("<textarea>")); type("pw1"); CHECK(activate("Connect"));
    CHECK(wifi_calls == 1 && strcmp(wifi_ssid, "CafeWifi") == 0 && strcmp(wifi_password, "pw1") == 0);
    key(LV_KEY_ESC);
    /* a scanned network that is already saved connects without asking again */
    CHECK(activate("Scan networks")); CHECK(activate("CafeWifi  -70dBm"));
    CHECK(wifi_calls == 2 && strcmp(wifi_password, "pw1") == 0); key(LV_KEY_ESC);
    CHECK(activate("Join saved")); CHECK(see("CafeWifi")); theme("saved list");
    CHECK(activate("CafeWifi")); CHECK(wifi_calls == 3); key(LV_KEY_ESC);
    CHECK(activate("Forget saved")); CHECK(activate("CafeWifi")); CHECK(see("Forgotten")); key(LV_KEY_ESC);
    CHECK(!file_contains(ROOT "/wifi.cfg", "CafeWifi"));
    sim_scan_ok = false;
    CHECK(activate("Scan networks")); CHECK(see("Scan failed")); CHECK(activate("Back"));
    CHECK(activate("Back")); CHECK(see_line("PicoCalc PIM"));
}

static void test_wifi_time(void) {
    reset_ui("wifi time");
    CHECK(activate("Wi-Fi & web"));
    CHECK(activate("Sync time")); CHECK(see("No network")); key(LV_KEY_ESC);
    CHECK(activate("Manual entry")); CHECK(goto_focus("<textarea>")); type("HomeNet"); key(LV_KEY_NEXT); type("pw");
    CHECK(activate("Connect")); key(LV_KEY_ESC);
    CHECK(activate("Sync time")); CHECK(see("Clock set") && see("15-03-2026 15:50")); theme("clock set"); key(LV_KEY_ESC);
    CHECK(file_contains(ROOT "/today.cfg", "15-03-2026 15:50"));
    CHECK(activate("Time zone")); CHECK(see("UTC+05:30")); theme("time zone");
    CHECK(activate("+1 hour")); CHECK(see("UTC+06:30")); CHECK(activate("-15 min")); CHECK(see("UTC+06:15"));
    CHECK(file_contains(ROOT "/wifi.cfg", "tz=375"));
    for (int i = 0; i < 9; i++) CHECK(activate("+1 hour"));
    CHECK(see("UTC+13:15")); /* stops at the +14:00 limit */
    CHECK(activate("Back"));
    CHECK(activate("Sync time")); CHECK(see("15-03-2026 23:35")); key(LV_KEY_ESC);
    sim_ntp_ok = false;
    CHECK(activate("Sync time")); CHECK(see("Sync failed")); theme("sync failed"); key(LV_KEY_ESC);
    CHECK(activate("Back")); CHECK(see("15-03-2026  23:3"));
    /* the web server asks the UI for the clock and for a refresh after edits */
    {
        uint16_t minutes = 0;
        pim_date_t date = sim_hooks.today(&minutes);
        CHECK(date.year == 2026 && date.month == 3 && minutes < 1440);
        sim_hooks.changed(); CHECK(see_line("PicoCalc PIM"));
    }
    /* sync from the startup screen */
    reset_ui("startup sync");
    wifi_result = true;
    CHECK(activate("Wi-Fi & web")); CHECK(activate("Manual entry")); CHECK(goto_focus("<textarea>")); type("HomeNet"); key(LV_KEY_NEXT); type("pw");
    CHECK(activate("Connect")); key(LV_KEY_ESC);
    pim_ui_start(&database, ROOT "/pim.dat"); pump(10);
    CHECK(see("Set date and time")); theme("startup");
    CHECK(activate("Sync (Wi-Fi)")); CHECK(see("Time synced")); key(LV_KEY_ESC);
    CHECK(see("15") && see("03") && see("2026")); theme("startup synced");
    CHECK(activate("Start")); CHECK(see("15-03-2026  15:50"));
}

static void test_telnet(void) {
    reset_ui("telnet");
    CHECK(activate("Telnet (BBS)")); theme("telnet setup");
    CHECK(goto_focus("<textarea>")); type("bbs.example.org"); key(LV_KEY_NEXT);
    keys(LV_KEY_BACKSPACE, 2); type("2323");
    CHECK(activate("Connect")); CHECK(see("Not connected")); key(LV_KEY_ESC);
    sim_up = true;
    CHECK(goto_focus("<textarea>")); key(LV_KEY_NEXT); keys(LV_KEY_BACKSPACE, 4); type("99999");
    CHECK(activate("Connect")); CHECK(see("Check the address")); key(LV_KEY_ESC);
    CHECK(goto_focus("<textarea>")); key(LV_KEY_NEXT); keys(LV_KEY_BACKSPACE, 5); type("2323");
    sim_telnet_ok = false;
    CHECK(activate("Connect")); CHECK(see("Telnet failed")); theme("telnet failed"); key(LV_KEY_ESC);
    CHECK(see("bbs.example.org") || see_line("Connect"));
    sim_telnet_ok = true;
    CHECK(activate("Connect"));
    CHECK(strcmp(sim_telnet_host, "bbs.example.org") == 0 && sim_telnet_port == 2323);
    server_says("Welcome to the BBS\r\n\x1b[1;31mLogin:\x1b[0m "); pump(10);
    CHECK(see("Welcome to the BBS") && see("Login:") && !see("[1;31m")); theme("telnet terminal");
    type("bob"); key(LV_KEY_ENTER);
    CHECK(strcmp(sim_tx, "bob\r\n") == 0);
    CHECK(see("Login: bob")); /* no remote echo, so the terminal echoes locally */
    for (int i = 0; i < 40; i++) { char line[24]; snprintf(line, sizeof(line), "line %d\r\n", i); server_says(line); pump(5); }
    CHECK(see("line 39") && !see("Welcome to the BBS"));
    keys(LV_KEY_UP, 30); CHECK(see("line 9\n") || see("line 10")); CHECK(!see("line 39"));
    key('x'); CHECK(see("line 39")); /* typing returns to the newest output */
    server_says("\xff\xfb\x01"); pump(10); /* WILL ECHO: server echoes from now on */
    sim_tx_length = 0; key('q'); CHECK(strcmp(sim_tx, "q") == 0);
    sim_telnet_open = false; pump(10);
    CHECK(see("disconnected")); theme("telnet closed");
    sim_hooks.changed(); CHECK(see("disconnected")); /* web edits never kick you out of the terminal */
    key(LV_KEY_ESC); CHECK(see_line("PicoCalc PIM")); CHECK(!sim_telnet_open);
    /* leaving with ESC while still connected closes the socket */
    CHECK(activate("Telnet (BBS)")); CHECK(activate("Connect")); CHECK(sim_telnet_open);
    key(LV_KEY_ESC); CHECK(!sim_telnet_open); CHECK(see_line("PicoCalc PIM"));
}

void pim_input_set_game_mode(bool enabled) { (void)enabled; }

bool pim_input_battery(pim_battery_t *battery) {
    if (sim_battery == 0) return false;
    battery->percent = (uint8_t)(sim_battery & 0x7f);
    battery->charging = sim_battery > 100;
    return true;
}
static bool status_has(const char *needle) { return strstr(pim_ui_game_status(), needle) != NULL; }
static unsigned status_number(const char *prefix) {
    const char *at = strstr(pim_ui_game_status(), prefix);
    return at ? (unsigned)strtoul(at + strlen(prefix), NULL, 10) : 0;
}
static bool board_is_fullscreen(void) {
    lv_obj_t *board = lv_obj_get_child(lv_screen_active(), 0);
    return board != NULL && lv_obj_get_width(board) == 320 && lv_obj_get_height(board) == 320;
}
static int lit_pixels(int x0, int y0, int x1, int y1) {
    int lit = 0;
    uint32_t fg, bg_hex;
    uint16_t bg;
    pim_ui_theme_colors(&fg, &bg_hex);
    bg = (uint16_t)(((bg_hex >> 19) & 31) << 11 | ((bg_hex >> 10) & 63) << 5 | ((bg_hex >> 3) & 31));
    lv_refr_now(NULL);
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) if (framebuffer[y * WIDTH + x] != bg) lit++;
    return lit;
}
static void test_games(void) {
    int baseline;
    reset_ui("games");
    baseline = count_timers();

    CHECK(activate("Tetris")); CHECK(board_is_fullscreen()); CHECK(status_has("Tetris score 0"));
    CHECK(count_timers() == baseline); theme("tetris");
    CHECK(lit_pixels(0, 0, 175, 320) > 800 && lit_pixels(175, 0, 320, 320) > 800);
    key(LV_KEY_LEFT); key(LV_KEY_RIGHT); key(LV_KEY_UP); key(LV_KEY_DOWN); theme("tetris moved");
    key(LV_KEY_ENTER); CHECK(status_number("score ") > 0); theme("tetris dropped");
    pump(120); /* gravity timer keeps running */
    for (int i = 0; i < 300 && !status_has("GAME OVER"); i++) key(LV_KEY_ENTER);
    CHECK(status_has("GAME OVER")); theme("tetris over");
    key(LV_KEY_ENTER); CHECK(!status_has("GAME OVER") && status_has("score 0"));
    key(LV_KEY_ESC); CHECK(see_line("PicoCalc PIM")); CHECK(count_timers() == baseline);

    CHECK(activate("Dungeon RPG")); CHECK(board_is_fullscreen()); CHECK(status_has("floor 1")); theme("dungeon");
    CHECK(lit_pixels(0, 0, 320, 320) > 1500);
    {
        const uint32_t moves[] = {LV_KEY_RIGHT, LV_KEY_DOWN, LV_KEY_LEFT, LV_KEY_UP};
        for (int i = 0; i < 300 && !status_has("DEFEATED") && !status_has("VICTORY"); i++) key(moves[(i * 5 + i / 4) % 4]);
    }
    theme("dungeon played");
    key(LV_KEY_ENTER); theme("dungeon again");
    key(LV_KEY_ESC); CHECK(see_line("PicoCalc PIM"));

    CHECK(activate("Space Invaders")); CHECK(board_is_fullscreen()); CHECK(status_has("score 0")); theme("invaders");
    CHECK(count_timers() == baseline);
    for (int i = 0; i < 6; i++) key(LV_KEY_LEFT);
    key(LV_KEY_ENTER); pump(150);
    theme("invaders shot");
    for (int i = 0; i < 80; i++) { key(i % 2 ? LV_KEY_RIGHT : LV_KEY_LEFT); key(LV_KEY_ENTER); pump(20); }
    CHECK(status_number("score ") > 0); theme("invaders played");
    for (int i = 0; i < 1500 && !status_has("GAME OVER"); i++) pump(10);
    CHECK(status_has("GAME OVER")); theme("invaders over");
    key(LV_KEY_ENTER); CHECK(!status_has("GAME OVER"));
    key(LV_KEY_ESC); CHECK(see_line("PicoCalc PIM")); CHECK(count_timers() == baseline);

    CHECK(activate("Solitaire")); CHECK(board_is_fullscreen()); CHECK(status_has("stock 24")); theme("solitaire");
    CHECK(lit_pixels(0, 0, 320, 320) > 2500);
    key(LV_KEY_ENTER); CHECK(status_has("stock 23 waste 1")); theme("solitaire drawn");
    key(LV_KEY_RIGHT); key(LV_KEY_ENTER); theme("solitaire picked");
    key(LV_KEY_DOWN); key(LV_KEY_UP); key(LV_KEY_UP); key(LV_KEY_BACKSPACE);
    key(LV_KEY_DOWN); keys(LV_KEY_RIGHT, 3); key(LV_KEY_UP); key(LV_KEY_DOWN); key(LV_KEY_ENTER); theme("solitaire tableau");
    key(LV_KEY_ENTER); key('f'); theme("solitaire foundation");
    key('n'); CHECK(status_has("moves 0") && status_has("stock 24"));
    key(LV_KEY_ESC); CHECK(see_line("PicoCalc PIM")); CHECK(count_timers() == baseline);
}

static void test_theme_switcher(void) {
    static const char *const names[] = {"Amber", "Green", "Manila"};
    reset_ui("theme switcher");
    CHECK(activate("Theme")); CHECK(see_line("Theme")); CHECK(see("Saved to /PIM/theme.cfg")); theme("theme screen");
    for (size_t i = 0; i < 3; i++) {
        uint32_t fg, bg;
        CHECK(activate(names[i]));
        { char current[32]; snprintf(current, sizeof(current), "Current: %s", names[i]); CHECK(see(current)); }
        pim_ui_theme_colors(&fg, &bg);
        CHECK(file_contains(ROOT "/theme.cfg", names[i]));
        CHECK(i == 0 ? fg == 0xffb000 : i == 1 ? (fg == 0x33ff33 && bg == 0) : (fg == 0 && bg == 0xefe0b3));
        theme(names[i]);
        key(LV_KEY_ESC); theme("home after switch"); CHECK(activate("Theme"));
    }
    key(LV_KEY_ESC);
    pim_ui_start(&database, ROOT "/pim.dat"); confirm_startup();
    { uint32_t fg, bg; pim_ui_theme_colors(&fg, &bg); CHECK(bg == 0xefe0b3); }
    forced_theme = "Amber";
}

static void test_navigation_leaks(void) {
    static const char *const entries[] = {"Calendar", "To-Do List", "Notes", "Journal", "Files",
                                          "Text editor", "Wi-Fi & web", "Tetris", "Dungeon RPG", "Space Invaders", "Solitaire", "Theme", "Telnet (BBS)"};
    lv_mem_monitor_t before, after;
    reset_ui("memory");
    for (int round = 0; round < 3; round++)
        for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) { activate(entries[i]); key(LV_KEY_ESC); }
    lv_mem_monitor(&before);
    for (int round = 0; round < 25; round++)
        for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) { activate(entries[i]); key(LV_KEY_ESC); }
    lv_mem_monitor(&after);
    printf("  LVGL pool (host 64-bit): free %zu -> %zu of %zu, biggest %zu, frag %u%%\n",
           (size_t)before.free_size, (size_t)after.free_size, (size_t)before.total_size,
           (size_t)after.free_biggest_size, (unsigned)after.frag_pct);
    CHECK(after.free_size + 64 >= before.free_size);
}

static uint32_t rng_state = 0xC0FFEEu;
static uint32_t rnd(void) { rng_state = rng_state * 1664525u + 1013904223u; return rng_state >> 8; }
static void test_fuzz(void) {
    static const uint32_t special[] = {LV_KEY_UP, LV_KEY_DOWN, LV_KEY_LEFT, LV_KEY_RIGHT, LV_KEY_ENTER, LV_KEY_ESC,
                                       LV_KEY_NEXT, LV_KEY_PREV, LV_KEY_BACKSPACE, LV_KEY_ENTER, LV_KEY_NEXT, LV_KEY_DOWN};
    reset_ui("fuzz");
    for (int i = 0; i < 12000; i++) {
        uint32_t pick = rnd() % 100, code;
        if (pick < 70) code = special[rnd() % (sizeof(special) / sizeof(special[0]))];
        else if (pick < 90) code = 'a' + rnd() % 26;
        else if (pick < 97) code = '0' + rnd() % 10;
        else code = (uint32_t)" -._/:"[rnd() % 6];
        key_queue[key_tail++ % 512] = code;
        pump((int)(rnd() % 4 + 1));
        if (key_tail - key_head > 200) pump(40);
        if (i % 600 == 599) {
            pump(40);
            theme("fuzz sample");
            CHECK(database.appointment_count <= PIM_MAX_APPOINTMENTS && database.todo_count <= PIM_MAX_TODOS);
            CHECK(database.note_count <= PIM_MAX_NOTES && database.journal_count <= PIM_MAX_JOURNAL_ENTRIES);
        }
    }
    pump(100);
    for (size_t i = 0; i < database.appointment_count; i++) {
        const pim_appointment_t *value = &database.appointments[i];
        CHECK(pim_date_is_valid(value->date) && value->minutes + value->duration <= 1440 && value->title[0]);
    }
    for (size_t i = 0; i < database.journal_count; i++) CHECK(pim_date_is_valid(database.journal[i].date));
    CHECK(database_persisted());
    CHECK(lv_group_get_focused(lv_group_get_default()) != NULL);
}

static int run_suite(const char *theme_name) {
    lv_display_t *display;
    lv_indev_t *indev;
    lv_group_t *group;
    lv_init();
    display = lv_display_create(WIDTH, HEIGHT);
    lv_display_set_buffers(display, framebuffer, NULL, sizeof(framebuffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, read_keyboard);
    group = lv_group_create();
    lv_group_set_default(group);
    lv_group_set_wrap(group, true);
    lv_indev_set_group(indev, group);

    forced_theme = theme_name;
    printf("-- theme %s\n", forced_theme);
    test_home();
    test_startup(); test_calendar_navigation();
    test_date_picker();
    test_time_entry();
    test_appointment_flow();
    test_appointment_limits();
    test_todos();
    test_notes();
    test_journal();
    test_journal_detail_delete();
    test_files();
    test_text_editor_entry();
    test_wifi();
    test_wifi_networks();
    test_wifi_time();
    test_telnet();
    test_games();
    if (strcmp(theme_name, "Amber") == 0) test_theme_switcher();
    test_navigation_leaks();
    test_fuzz();
    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

/* Each theme runs the whole suite in a fresh process so no LVGL state leaks between runs. */
int main(void) {
    static const char *const theme_names[] = {"Amber", "Green", "Manila"};
    int status_total = 0;
    fflush(stdout);
    for (size_t t = 0; t < 3; t++) {
        pid_t child = fork();
        int status = 0;
        if (child == 0) _exit(run_suite(theme_names[t]));
        if (child < 0 || waitpid(child, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) status_total = 1;
    }
    return status_total;
}
