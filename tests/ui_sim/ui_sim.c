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
#include "pim/sound.h"
#include "pim/storage.h"
#include "pim/ui.h"
#include "pim/tailscale.h"
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
static bool sim_terminal_mode;
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
static int sim_wifi_off, sim_web_stops, sim_web_starts;
void pim_wifi_off(void) { sim_wifi_off++; }
void pim_wifi_web_stop(void) { sim_web_stops++; }
bool pim_wifi_web_start(void) { sim_web_starts++; return true; }
bool pim_wifi_web_running(void) { return sim_up; }

/* A stand-in for the Tailscale client: the real one wants a radio and a
   tailnet. The screen's only job is to show what the client reports and to
   start and stop it, so the test sets these directly and checks what appears.
   These are strong definitions and so replace the weak ones in tsconfig.c. */
static pim_ts_state_t sim_ts = PIM_TS_OFF;
static char sim_ts_address[PIM_TS_ADDRESS_MAX + 1];
static char sim_ts_url[200];
static char sim_ts_message[PIM_TS_MESSAGE_MAX + 1] = "Off.";
static int sim_ts_starts, sim_ts_stops;
static bool sim_ts_can_start = true;
static pim_ts_config_t sim_ts_started_with;

bool pim_ts_available(void) { return true; }
bool pim_ts_start(const pim_ts_config_t *config) {
    sim_ts_started_with = *config;
    sim_ts_starts++;
    if (!sim_ts_can_start) {
        sim_ts = PIM_TS_FAILED;
        snprintf(sim_ts_message, sizeof(sim_ts_message), "Not enough memory to start Tailscale.");
        return false;
    }
    sim_ts = PIM_TS_STARTING;
    snprintf(sim_ts_message, sizeof(sim_ts_message), "Starting...");
    return true;
}
void pim_ts_stop(void) {
    sim_ts_stops++;
    sim_ts = PIM_TS_OFF;
    sim_ts_address[0] = '\0';
    sim_ts_url[0] = '\0';
    snprintf(sim_ts_message, sizeof(sim_ts_message), "Off.");
}
pim_ts_state_t pim_ts_state(void) { return sim_ts; }
const char *pim_ts_address(void) { return sim_ts_address; }
const char *pim_ts_message(void) { return sim_ts_message; }
const char *pim_ts_login_url(void) { return sim_ts_url; }
void pim_ts_poll(void) { }
static bool sim_sound = true;
void pim_sound_set_enabled(bool enabled) { sim_sound = enabled; }
bool pim_sound_enabled(void) { return sim_sound; }
void pim_sound_play(pim_sound_event_t event) { (void)event; }
void pim_input_activity(void) {}
void pim_system_busy(bool busy) { (void)busy; }
void pim_sound_poll(void) {}

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
static bool goto_focus_contains(const char *needle) {
    for (int i = 0; i < 90; i++) {
        if (strstr(focus_text(), needle) != NULL) return true;
        key(LV_KEY_NEXT);
    }
    printf("  (could not focus anything containing \"%s\"; focus=\"%s\")\n", needle, focus_text());
    return false;
}
/* Esc backs out one screen at a time; forms sometimes need more than one. */
static void go_home(void) {
    for (int i = 0; i < 6 && !see_line("PicoDeck OS"); i++) key(LV_KEY_ESC);
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
static bool file_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;
    fclose(file);
    return true;
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
                                          "Text editor", "Tools", "Wi-Fi & web", "Today", "Games", "Theme", "Terminal"};
    reset_ui("home");
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) CHECK(see_line(entries[i]));
    CHECK(!see("Appointments"));
    CHECK(see("Created by Aditya Kale"));
    CHECK(see("Battery: 87%"));
    sim_battery = 15 + 128; pump(1600); CHECK(see("Battery: 15% charging"));
    sim_battery = 9; pump(1600); CHECK(see("Battery: 9% LOW"));
    sim_battery = 0; pump(1600); CHECK(see("Battery: no data"));
    sim_battery = 87; pump(1600); CHECK(see("Battery: 87%"));
    sim_battery = 87; pump(3200);
    sim_battery = 9;
    for (int i = 0; i < 400 && !see("LOW BATTERY"); i++) pump(10);
    CHECK(see("LOW BATTERY 9%")); theme("low battery banner");
    pump(900); CHECK(!see("LOW BATTERY"));
    sim_battery = 87; pump(1600);
    theme("home");
    CHECK(focus_is("Calendar"));
    key(LV_KEY_RIGHT); CHECK(focus_is("To-Do List"));
    key(LV_KEY_DOWN); CHECK(focus_is("Notes"));
    key(LV_KEY_LEFT); CHECK(focus_is("To-Do List"));
    key(LV_KEY_UP); CHECK(focus_is("Calendar"));
    key(LV_KEY_PREV); CHECK(focus_is("Sound: ON"));
    CHECK(activate("Sound: ON")); CHECK(see_line("Sound: OFF")); CHECK(!pim_sound_enabled());
    CHECK(activate("Sound: OFF")); CHECK(see_line("Sound: ON")); CHECK(pim_sound_enabled());
    CHECK(activate("Wi-Fi & web")); CHECK(activate("Wi-Fi off")); CHECK(sim_wifi_off == 1); key(LV_KEY_ESC);
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        if (!activate(entries[i])) continue;
        key(LV_KEY_ESC);
        CHECK(see_line("PicoDeck OS"));
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
    CHECK(activate("Start")); CHECK(see_line("PicoDeck OS")); CHECK(see("15-03-2027  14:30"));
    CHECK(file_contains(ROOT "/today.cfg", "15-03-2027 14:30"));
    pump(6000); CHECK(see("15-03-2027  14:31")); /* clock advances */
    CHECK(activate("Calendar")); CHECK(see("03-2027")); CHECK(focus_is("15"));
    key(LV_KEY_ESC);
    pim_ui_start(&database, ROOT "/pim.dat"); pump(10);
    CHECK(see("Set date and time")); CHECK(see("14") && see("2027")); /* prefilled from the saved clock */
    CHECK(activate("Start")); CHECK(see_line("PicoDeck OS"));
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
    key(LV_KEY_ESC); CHECK(see_line("PicoDeck OS"));
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
    CHECK(activate("Back")); CHECK(see_line("PicoDeck OS"));
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
    key(LV_KEY_ESC); CHECK(see_line("PicoDeck OS"));
    CHECK(activate("Calendar")); CHECK(see("01-2026")); CHECK(focus_is("20"));
    key(LV_KEY_ESC); CHECK(activate("Journal")); CHECK(see("20-01-2026"));
    reset_ui("date-picker-journal");
    CHECK(activate("Journal")); CHECK(see("01-01-2026"));
    CHECK(activate("01-01-2026")); CHECK(see("Choose a date"));
    key('0'); key('7'); CHECK(activate("Go")); CHECK(see("07-01-2026")); theme("journal date");
    CHECK(activate("07-01-2026")); CHECK(activate("Back")); CHECK(see("07-01-2026"));
    key(LV_KEY_ESC); CHECK(see_line("PicoDeck OS"));
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

static void test_today(void) {
    pim_todo_t todo = {.priority = PIM_PRIORITY_NORMAL};
    reset_ui("today");
    CHECK(activate("To-Do List"));
    CHECK(goto_focus("<textarea>")); type("Pay rent");
    CHECK(goto_focus("<dropdown>")); key(LV_KEY_ENTER); key(LV_KEY_DOWN); key(LV_KEY_ENTER);
    CHECK(activate("Add"));
    CHECK(database.todo_count == 1 && database.todos[0].priority == PIM_PRIORITY_HIGH);
    key(LV_KEY_ESC);
    snprintf(todo.title, sizeof(todo.title), "Later task");
    CHECK(pim_add_todo(&database, &todo));
    for (size_t i = 0; i < database.todo_count; i++) if (strcmp(database.todos[i].title, "Pay rent") == 0) pim_todo_set_due(&database.todos[i], (pim_date_t){2000, 1, 2});
    CHECK(activate("Today")); CHECK(see("Appointments")); CHECK(see("To-dos due")); CHECK(see("! Pay rent")); CHECK(!see("Later task")); theme("today");
    key(LV_KEY_ESC); CHECK(see_line("PicoDeck OS"));
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
    CHECK(activate(">")); CHECK(see("01-01-2026")); CHECK(see("No entry"));
    CHECK(activate("Write")); theme("journal editor");
    CHECK(goto_focus("<textarea>")); type("Good day\nSecond line");
    CHECK(activate("Save"));
    CHECK(file_contains(ROOT "/journal/01-01-2026.md", "Good day"));
    CHECK(see("Good day")); CHECK(see("Edit"));
    CHECK(activate("View")); CHECK(see("01-01-2026")); theme("journal list");
    CHECK(activate("Back")); CHECK(see("Journal"));
    CHECK(activate("Delete")); CHECK(!exists(ROOT "/journal/01-01-2026.md")); CHECK(see("No entry"));
}

static void write_big_file(const char *path, int lines) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) return;
    for (int i = 0; i < lines; i++) fprintf(file, "line %04d of a long file that spans pages\n", i);
    fclose(file);
}
static void test_large_editor(void) {
    reset_ui("large-editor");
    write_big_file(ROOT "/big.txt", 800);
    CHECK(activate("Files")); CHECK(activate("big.txt"));
    CHECK(see("line 0000"));
    CHECK(activate("Next")); CHECK(!see("line 0000")); CHECK(see("line 00"));
    CHECK(goto_focus("<textarea>")); type("EDITED");
    CHECK(activate("Next"));
    CHECK(activate("Prev"));
    CHECK(system("grep -q EDITED " ROOT "/big.txt") == 0);
    CHECK(activate("Prev")); CHECK(see("line 0000"));
    CHECK(activate("Files"));
    CHECK(file_contains(ROOT "/big.txt", "line 0000"));
    {
        FILE *file = fopen(ROOT "/big.txt", "rb");
        long size = 0;
        if (file) { fseek(file, 0, SEEK_END); size = ftell(file); fclose(file); }
        CHECK(size > 30000);
    }
}

static void test_tools(void) {
    reset_ui("tools");
    CHECK(activate("Tools")); theme("tools");
    CHECK(see("Search") && see("Calculator") && see("Backup"));
    CHECK(activate("Back")); CHECK(see("PicoDeck OS"));
    /* calculator */
    CHECK(activate("Tools")); CHECK(activate("Calculator")); theme("calculator");
    CHECK(goto_focus("<textarea>")); type("2+3*4"); CHECK(see("= 14")); type("\n"); CHECK(see("= 14"));
    type("*2"); CHECK(see("ans*2")); CHECK(see("= 28")); type("\n"); CHECK(see("= 28"));
    key(LV_KEY_UP); CHECK(see("ans*2")); key(LV_KEY_DOWN);
    type("ans*2\n"); CHECK(see("= 56"));
    type("sin(30)\n"); CHECK(see("= 0.5"));
    type("1/0\n"); CHECK(see("ivision") || see("rror") || see("zero"));
    CHECK(activate("Deg")); CHECK(see("Rad"));
    CHECK(activate("Clear")); CHECK(!see("= 14"));
    CHECK(activate("Back")); CHECK(see("Timer"));
    /* search */
    {
        FILE *file;
        file = fopen(ROOT "/zebra.txt", "wb"); if (file) { fputs("Zebra plan", file); fclose(file); }
        file = fopen(ROOT "/hello.txt", "wb"); if (file) { fputs("needle in haystack", file); fclose(file); }
    }
    CHECK(activate("Search")); theme("search");
    CHECK(goto_focus("<textarea>")); type("zebra\n"); CHECK(see("Zebra plan")); theme("search results");
    CHECK(activate("Back")); CHECK(activate("Search"));
    CHECK(goto_focus("<textarea>")); key(LV_KEY_BACKSPACE); keys(LV_KEY_BACKSPACE, 10); type("needle\n");
    CHECK(see("hello.txt"));
    CHECK(activate("F hello.txt: needle in haystack") || see("hello"));
    key(LV_KEY_ESC); key(LV_KEY_ESC);
    /* recurring */
    CHECK(activate("Tools")); CHECK(activate("Recurring")); theme("recurring");
    CHECK(activate("Add")); theme("recurring form");
    CHECK(goto_focus("<textarea>")); type("Water plants");
    CHECK(activate("Save")); CHECK(see("Water plants"));
    CHECK(file_contains(ROOT "/recurring.cfg", "Water plants"));
    CHECK(activate("D 09:00 Water plants")); CHECK(see("Repeats")); CHECK(activate("Delete"));
    CHECK(!see("Water plants"));
    CHECK(activate("Add")); CHECK(goto_focus("<textarea>")); key(LV_KEY_NEXT); keys(LV_KEY_BACKSPACE, 12); type("99-99-2026");
    key(LV_KEY_NEXT); CHECK(activate("Save")); CHECK(see("Not saved")); key(LV_KEY_ENTER);
    CHECK(activate("Cancel"));
    /* backup */
    CHECK(activate("Back")); CHECK(activate("Backup")); theme("backup");
    CHECK(see("No backups"));
    CHECK(activate("Create backup")); CHECK(see("Backup done")); key(LV_KEY_ENTER);
    CHECK(!see("No backups")); theme("backup list");
    CHECK(see("20"));
    key(LV_KEY_ESC);
    CHECK(exists(ROOT "/games/dungeon/floor1.txt"));
}

static void test_timer(void) {
    reset_ui("timer");
    CHECK(activate("Tools")); CHECK(activate("Timer")); theme("timer");
    CHECK(see("STOPWATCH")); CHECK(activate("Start")); pump(300); CHECK(see("Stop"));
    CHECK(activate("Stop")); CHECK(activate("Reset")); CHECK(see("00:00"));
    CHECK(activate("Mode")); CHECK(see("COUNTDOWN"));
    CHECK(activate("+1 min")); CHECK(see("01:00"));
    CHECK(activate("Start")); pump(6200); CHECK(see("DONE"));
}

static void test_scores(void) {
    reset_ui("scores");
    CHECK(activate("Games")); CHECK(activate("Tetris"));
    for (int i = 0; i < 60; i++) key(LV_KEY_ENTER);
    key(LV_KEY_ESC);
    CHECK(see("PicoDeck OS"));
    CHECK(exists(ROOT "/games/tetris/scores.txt") || exists(ROOT "/games/tetris/game.cfg"));
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
    CHECK(activate("Home")); CHECK(see_line("PicoDeck OS"));
}

static void test_text_editor_entry(void) {
    reset_ui("text-editor");
    CHECK(activate("Text editor")); CHECK(see("Create text file"));
    CHECK(goto_focus("<textarea>")); type("todo.txt"); CHECK(activate("Create"));
    CHECK(see("todo.txt")); CHECK(exists(ROOT "/todo.txt"));
    key(LV_KEY_ESC); CHECK(see_line("PicoDeck OS"));
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
    CHECK(activate("Back")); CHECK(see_line("PicoDeck OS"));
}

/* The device joining a tailnet on its own: a key off the card, or a login
   address when there is none, and what the screen shows at each stage. */
static void test_tailscale(void) {
    reset_ui("tailscale");
    sim_ts = PIM_TS_OFF;
    sim_ts_starts = sim_ts_stops = 0;
    sim_ts_can_start = true;
    sim_web_stops = sim_web_starts = 0;
    sim_ts_address[0] = sim_ts_url[0] = '\0';
    snprintf(sim_ts_message, sizeof(sim_ts_message), "Off.");

    CHECK(activate("Wi-Fi & web"));
    CHECK(activate("Tailscale"));
    theme("tailscale off");
    CHECK(see("Status: off"));
    CHECK(see("Address: none yet"));
    CHECK(see("no key on the card"));       /* nothing written to the card yet */

    /* The tunnel runs over Wi-Fi, so without it there is nothing to do but
       say so - and not start the client. */
    CHECK(activate("Connect"));
    CHECK(see("Join a Wi-Fi network first"));
    CHECK(sim_ts_starts == 0);
    key(LV_KEY_ESC);

    /* Connect for real. There is no key on the card, which is not an error:
       the client is started anyway and asks for an interactive login. */
    sim_up = true;
    CHECK(activate("Connect"));
    CHECK(sim_ts_starts == 1);
    /* The web server's buffers are handed over for the duration. */
    CHECK(sim_web_stops == 1);
    CHECK(strcmp(sim_ts_started_with.name, "picocalc") == 0);
    CHECK(sim_ts_started_with.authkey[0] == '\0');
    CHECK(see("Status: starting"));

    /* The control plane wants a login: the address is the whole point of the
       screen, so it has to be on it, in full. */
    sim_ts = PIM_TS_REGISTERING;
    snprintf(sim_ts_message, sizeof(sim_ts_message), "Open the login address below, then wait.");
    snprintf(sim_ts_url, sizeof(sim_ts_url), "https://login.tailscale.com/a/0123456789abcdef");
    pump(150);                               /* the screen refreshes each second */
    CHECK(see("Status: registering"));
    CHECK(see("Open the login address below"));
    CHECK(see("https://login.tailscale.com/a/0123456789abcdef"));
    theme("tailscale login");

    /* On the tailnet: the address it was given is what makes this device
       reachable, so that is what the screen leads with. */
    sim_ts = PIM_TS_RUNNING;
    snprintf(sim_ts_address, sizeof(sim_ts_address), "100.64.1.2");
    snprintf(sim_ts_message, sizeof(sim_ts_message), "On the tailnet, 9 peers");
    sim_ts_url[0] = '\0';
    pump(150);
    CHECK(see("Status: on the tailnet"));
    CHECK(see("Address: 100.64.1.2"));
    CHECK(see("On the tailnet, 9 peers"));
    CHECK(!see("login.tailscale.com"));      /* no stale login address left over */
    theme("tailscale running");

    CHECK(activate("Disconnect"));
    CHECK(sim_ts_stops == 1);
    CHECK(sim_web_starts == 1);          /* and handed back */
    CHECK(see("Status: off") && see("Address: none yet"));

    /* A key on the card is used, and never shown in full. */
    {
        FILE *file = fopen(ROOT "/tailscale.cfg", "wb");
        CHECK(file != NULL);
        if (file != NULL) {
            fprintf(file, "authkey=tskey-auth-kSimulated1CNTRL-abcdefghijklmnopqrstuvwx\n"
                          "name=picodeck\nderp=6\n");
            fclose(file);
        }
    }
    CHECK(activate("Back"));                 /* back to Wi-Fi, then in again */
    CHECK(activate("Tailscale"));
    CHECK(see("tskey-auth-kSimu"));
    CHECK(!see("abcdefghijklmnopqrstuvwx"));
    CHECK(activate("Connect"));
    CHECK(sim_ts_starts == 2);
    CHECK(strcmp(sim_ts_started_with.authkey,
                 "tskey-auth-kSimulated1CNTRL-abcdefghijklmnopqrstuvwx") == 0);
    CHECK(strcmp(sim_ts_started_with.name, "picodeck") == 0);
    CHECK(sim_ts_started_with.derp_region == 6);

    /* A client that cannot start says why instead of looking like it worked. */
    pim_ts_stop();
    sim_ts_can_start = false;
    CHECK(activate("Connect"));
    CHECK(see("Not enough memory"));
    key(LV_KEY_ESC);
    CHECK(see("Status: failed"));
    theme("tailscale failed");

    sim_ts_can_start = true;
    pim_ts_stop();
    sim_up = false;
    CHECK(activate("Back"));
    CHECK(see_line("Wi-Fi"));
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
    CHECK(activate("Back")); CHECK(see_line("PicoDeck OS"));
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
        sim_hooks.changed(); CHECK(see_line("PicoDeck OS"));
    }
    /* sync from the startup screen */
    reset_ui("startup sync");
    wifi_result = true;
    CHECK(activate("Wi-Fi & web")); CHECK(activate("Manual entry")); CHECK(goto_focus("<textarea>")); type("HomeNet"); key(LV_KEY_NEXT); type("pw");
    CHECK(activate("Connect")); key(LV_KEY_ESC);
    /* a saved network in range: connect, sync and go straight to Home */
    pim_ui_start(&database, ROOT "/pim.dat"); pump(10);
    CHECK(!see("Set date and time")); CHECK(see("15-03-2026  15:50"));
    key(LV_KEY_ESC);
    /* no scan result or no time: fall back to the manual screen */
    sim_scan_ok = false;
    pim_ui_start(&database, ROOT "/pim.dat"); pump(10);
    CHECK(see("Set date and time")); theme("startup");
    CHECK(activate("Sync (Wi-Fi)")); CHECK(see("Time synced")); key(LV_KEY_ESC);
    CHECK(see("15") && see("03") && see("2026")); theme("startup synced");
    CHECK(activate("Start")); CHECK(see("15-03-2026  15:50"));
    sim_scan_ok = true; sim_ntp_ok = false;
    pim_ui_start(&database, ROOT "/pim.dat"); pump(10);
    CHECK(see("Set date and time"));
    sim_ntp_ok = true;
}

/* Writes a public key onto the simulated card so the key screen has something
   to find. It is a real ed25519 key, so the fingerprint shown is a real one. */
static void make_key_file(void) {
    FILE *file;
    mkdir(ROOT "/ssh", 0777);
    file = fopen(ROOT "/ssh/spare.pub", "wb");
    if (file == NULL) return;
    fprintf(file, "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGb9ECWmEzf8xZ/MGMWyAkWyRcNnZaPtM3SbQ8Nn8PFC spare\n");
    fclose(file);
}

/* A throwaway ed25519 key pair on the simulated card, so the SSH connection
   path can be exercised for real: the key is parsed, the session starts and the
   first bytes go out on the wire. */
static void make_ssh_key(void) {
    FILE *file;
    mkdir(ROOT "/ssh", 0777);
    file = fopen(ROOT "/ssh/picocalc", "wb");
    if (file == NULL) return;
    fputs(
        "-----BEGIN OPENSSH PRIVATE KEY-----\n"
        "b3BlbnNzaC1rZXktdjEAAAAABG5vbmUAAAAEbm9uZQAAAAAAAAABAAAAMwAAAAtzc2gtZW\n"
        "QyNTUxOQAAACCmyWmqkmVgzarRlrmJebqIA97urW2hFzQEAF6og1rSIAAAAIiVCfQFlQn0\n"
        "BQAAAAtzc2gtZWQyNTUxOQAAACCmyWmqkmVgzarRlrmJebqIA97urW2hFzQEAF6og1rSIA\n"
        "AAAEAvuhGXy9ta5Zz8gToaCzOCV+M4xus7PaHZKRnFGqZ4nKbJaaqSZWDNqtGWuYl5uogD\n"
        "3u6tbaEXNAQAXqiDWtIgAAAABHRlc3QB\n"
        "-----END OPENSSH PRIVATE KEY-----\n"
        , file);
    fclose(file);
    file = fopen(ROOT "/ssh/picocalc.pub", "wb");
    if (file != NULL) {
        fprintf(file, "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIKbJaaqSZWDNqtGWuYl5uogD3u6tbaEXNAQAXqiDWtIg test\n");
        fclose(file);
    }
}

static const char *term_text(void) { return pim_ui_terminal_text(); }
static bool term_sees(const char *needle) { return strstr(term_text(), needle) != NULL; }

/* Fills in the connection form, which is a row of text areas followed by the
   two dropdowns, so a Tab walks from one field to the next. */
static bool form_field(int steps, const char *value) {
    /* Start from a button so the walk always lands on the first field, whatever
       was focused before, then step across the fields in creation order. */
    if (!goto_focus("Connect")) return false;
    if (!goto_focus("<textarea>")) return false;
    for (int i = 0; i < steps; i++) key(LV_KEY_NEXT);
    type(value);
    return true;
}


/* The startup PIN. What matters is not only that the right PIN opens it, but
   that nothing else does: Esc, the web hook, and a wrong PIN all have to leave
   the device shut. */
static void test_lock(void) {
    reset_ui("lock");

    /* set one from Tools */
    CHECK(activate("Tools"));
    CHECK(activate("Device PIN"));
    CHECK(see("New six digit PIN"));
    CHECK(goto_focus("<textarea>"));
    type("931724");
    CHECK(activate("Set PIN"));
    CHECK(see("PIN set"));
    key(LV_KEY_ESC);
    /* the screen now offers Change and Remove instead of Set */
    CHECK(see("Current PIN"));
    theme("pin settings");

    /* five digits is not a PIN */
    CHECK(goto_focus("<textarea>"));
    type("931724");                      /* current */
    key(LV_KEY_NEXT);
    type("12345");                       /* new, too short */
    CHECK(activate("Change"));
    CHECK(see("Six digits"));
    key(LV_KEY_ESC);

    /* restarting now asks for it */
    pim_ui_start(&database, ROOT "/pim.dat");
    pump(10);
    CHECK(see("Enter your six digit PIN"));
    theme("locked");

    /* Esc must not get past it, and neither must the web hook */
    key(LV_KEY_ESC);
    pump(5);
    CHECK(see("Enter your six digit PIN"));
    sim_hooks.changed();
    pump(5);
    CHECK(see("Enter your six digit PIN"));
    CHECK(!see_line("Calendar"));        /* no sign of Home */

    /* a wrong PIN says so and stays shut */
    CHECK(goto_focus("<textarea>"));
    type("000000");
    pump(10);
    CHECK(see("Wrong PIN"));
    CHECK(see("Enter your six digit PIN"));

    /* the right one opens it */
    type("931724");
    pump(20);
    CHECK(!see("Enter your six digit PIN"));
    CHECK(see_line("PicoDeck OS") || see("Set date and time"));
    confirm_startup();
    CHECK(see_line("PicoDeck OS"));

    /* removing it needs the current PIN, and then the lock is gone */
    CHECK(activate("Tools"));
    CHECK(activate("Device PIN"));
    CHECK(goto_focus("<textarea>"));
    type("000000");
    CHECK(activate("Remove"));
    CHECK(see("Wrong PIN"));
    key(LV_KEY_ESC);
    CHECK(goto_focus("<textarea>"));
    keys(LV_KEY_BACKSPACE, 8);
    type("931724");
    CHECK(activate("Remove"));
    CHECK(see("PIN removed"));
    key(LV_KEY_ESC);
    pim_ui_start(&database, ROOT "/pim.dat");
    pump(10);
    CHECK(!see("Enter your six digit PIN"));
}

static void test_terminal(void) {
    reset_ui("terminal");
    CHECK(activate("Terminal"));
    CHECK(see("No saved connections"));
    theme("terminal list");

    /* ---- saving a connection ---- */
    CHECK(activate("New"));
    theme("connection form");
    CHECK(form_field(0, "bbs"));          /* name */
    CHECK(form_field(2, "bbs.example.org"));  /* past the kind dropdown to the address */
    CHECK(activate("Save"));
    CHECK(see_line("bbs"));

    /* ---- connecting ---- */
    CHECK(activate("bbs"));
    CHECK(activate("Connect"));
    CHECK(see("Not connected"));          /* no Wi-Fi yet */
    key(LV_KEY_ESC);
    sim_up = true;
    sim_telnet_ok = false;
    CHECK(activate("Connect"));
    CHECK(see("Connection failed"));
    key(LV_KEY_ESC);
    sim_telnet_ok = true;
    CHECK(activate("Connect"));
    CHECK(strcmp(sim_telnet_host, "bbs.example.org") == 0 && sim_telnet_port == 23);
    CHECK(sim_terminal_mode);             /* the keyboard hands over every key now */

    /* the opening offer goes out before anything is typed */
    CHECK(sim_tx_length == 12 && (unsigned char)sim_tx[0] == 255 && (unsigned char)sim_tx[1] == 253);

    /* ---- the screen ---- */
    server_says("Welcome to the BBS\r\n\x1b[1;31mLogin:\x1b[0m "); pump(10);
    CHECK(term_sees("Welcome to the BBS"));
    CHECK(term_sees("Login:"));
    CHECK(!term_sees("[1;31m"));          /* colour is colour, not text */
    theme("terminal session");

    /* ---- typing ---- */
    sim_tx_length = 0; sim_tx[0] = '\0';
    type("bob"); key(LV_KEY_ENTER);
    CHECK(strcmp(sim_tx, "bob\r\n") == 0);  /* NVT ends a line with CR LF */
    CHECK(term_sees("Login: bob"));         /* no remote echo, so we echo */

    /* box drawing arrives as UTF-8 and is painted, not printed */
    server_says("\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x90"); pump(10);
    theme("terminal line drawing");

    /* once the host says it will echo, we stop doing it ourselves */
    server_says("\xff\xfb\x01"); pump(10);
    sim_tx_length = 0; sim_tx[0] = '\0';
    key('q');
    CHECK(strcmp(sim_tx, "q") == 0);
    CHECK(!term_sees("bobq"));

    /* an arrow key becomes an escape sequence, and the application-mode
       request changes which one */
    sim_tx_length = 0; sim_tx[0] = '\0';
    key(LV_KEY_UP);
    CHECK(strcmp(sim_tx, "\033[A") == 0);
    server_says("\033[?1h"); pump(5);
    sim_tx_length = 0; sim_tx[0] = '\0';
    key(LV_KEY_UP);
    CHECK(strcmp(sim_tx, "\033OA") == 0);

    /* function keys and Break, which only exist as raw keyboard codes */
    sim_tx_length = 0; sim_tx[0] = '\0';
    key(PIM_UI_TERM_KEY | 0x85u);
    CHECK(strcmp(sim_tx, "\033[15~") == 0);
    sim_tx_length = 0; sim_tx[0] = '\0';
    key(PIM_UI_TERM_KEY | 0xd0u);
    CHECK(strcmp(sim_tx, "\033") == 0);     /* Break sends a real Escape */
    sim_tx_length = 0; sim_tx[0] = '\0';
    key(PIM_UI_TERM_KEY | 0x03u);           /* Ctrl-C, folded by the driver */
    CHECK(sim_tx_length == 1 && sim_tx[0] == 0x03);

    /* ---- scrollback ---- */
    for (int i = 0; i < 60; i++) { char line[24]; snprintf(line, sizeof(line), "line %d\r\n", i); server_says(line); pump(3); }
    CHECK(term_sees("line 59") && !term_sees("Welcome to the BBS"));
    key(PIM_UI_TERM_KEY | 0xd6u);           /* Page Up */
    key(PIM_UI_TERM_KEY | 0xd6u);
    CHECK(see("back "));                    /* the status line says how far */
    theme("terminal scrollback");
    key(PIM_UI_TERM_KEY | 0xd7u);           /* Page Down */
    key(PIM_UI_TERM_KEY | 0xd7u);
    CHECK(!see("back "));

    /* ---- losing the connection ---- */
    sim_telnet_open = false; pump(10);
    CHECK(term_sees("disconnected"));
    CHECK(!sim_terminal_mode);              /* the keyboard goes back to normal */
    theme("terminal closed");
    sim_hooks.changed();
    CHECK(term_sees("disconnected"));       /* web edits never kick you out */
    key(LV_KEY_ESC);
    CHECK(see_line("bbs"));                      /* back to the list */

    /* ---- a connection dials the address it was given ----

       There used to be a relay here: another computer forwarding a port, for a
       device that could not join a tailnet. This one joins it, so the form no
       longer offers one and what is saved is what gets dialled. */
    CHECK(activate("bbs"));
    CHECK(activate("Connect"));
    CHECK(strcmp(sim_telnet_host, "bbs.example.org") == 0 && sim_telnet_port == 23);
    key(LV_KEY_ESC);
    /* And the form has no relay fields left to fill in. */
    CHECK(activate("bbs"));
    CHECK(!see("Relay"));
    CHECK(see("tunnel"));
    CHECK(activate("Save"));
    CHECK(see_line("bbs"));                 /* saving still works with one gone */
    go_home();

    /* ---- an ssh alias is saved but says so plainly ---- */
    CHECK(activate("Terminal"));
    CHECK(activate("New"));
    CHECK(form_field(0, "nas"));
    key(LV_KEY_NEXT);
    key(LV_KEY_ENTER); key(LV_KEY_DOWN); key(LV_KEY_ENTER);   /* kind: ssh */
    CHECK(form_field(2, "nas.tail1a2b.ts.net"));
    /* An SSH connection needs a login and a key, and says which is missing
       rather than dialling and failing somewhere obscure. */
    CHECK(activate("Connect"));
    CHECK(see("No user name"));
    key(LV_KEY_ESC);
    CHECK(form_field(4, "bob"));                 /* the User field */
    CHECK(activate("Connect"));
    CHECK(see("No key chosen"));
    key(LV_KEY_ESC);
    CHECK(activate("Save"));                     /* keep it; the key comes next */
    CHECK(see_line("nas"));

    /* With a key on the card, attached to the connection, Connect starts a real
       session: the client's identification string goes out on the wire. */
    make_ssh_key();
    CHECK(activate("SSH keys"));                 /* the scan is what finds it */
    CHECK(see("picocalc"));
    CHECK(activate("Back"));
    CHECK(goto_focus("nas"));
    key(LV_KEY_ENTER);
    CHECK(goto_focus("Connect"));
    CHECK(goto_focus("<textarea>"));             /* round to the name field */
    for (int i = 0; i < 5; i++) key(LV_KEY_NEXT);   /* on to the key chooser */
    CHECK(focus_is("<dropdown>"));
    key(LV_KEY_ENTER); key(LV_KEY_DOWN); key(LV_KEY_ENTER);
    sim_tx_length = 0; sim_tx[0] = '\0';
    CHECK(activate("Connect"));
    CHECK(strncmp(sim_tx, "SSH-2.0-", 8) == 0);  /* a real SSH session is under way */
    CHECK(see("greeting") || see("exchanging keys"));
    theme("ssh session starting");
    key(LV_KEY_ESC);                             /* leave: no server answers here */
    CHECK(see_line("nas"));

    /* ---- keys on the card ---- */
    CHECK(activate("SSH keys"));
    make_key_file();
    CHECK(activate("Rescan"));
    /* The list carries names only; the type and fingerprint are on the detail
       screen a row opens, which is checked below. */
    CHECK(see_line("spare"));
    theme("ssh keys");
    /* A key made on the device: named, created, and shown with a real
       fingerprint, with both halves written to the card. */
    CHECK(activate("Create"));
    CHECK(goto_focus("<textarea>"));
    keys(LV_KEY_BACKSPACE, 10);
    type("onboard");
    CHECK(activate("Create"));
    CHECK(see("onboard") && see("ssh-ed25519") && see("256 bits"));
    CHECK(see("SHA256:"));
    CHECK(see("on the card"));                   /* the private half is there */
    CHECK(see("/PIM/ssh/onboard.pub"));          /* and it says how to install it */
    theme("created key");
    CHECK(file_exists(ROOT "/ssh/onboard") && file_exists(ROOT "/ssh/onboard.pub"));
    /* a second key of the same name is refused rather than overwriting it */
    CHECK(activate("Back"));
    CHECK(activate("Create"));
    CHECK(goto_focus("<textarea>"));
    keys(LV_KEY_BACKSPACE, 10);
    type("onboard");
    CHECK(activate("Create"));
    CHECK(see("Already there"));
    key(LV_KEY_ESC);
    CHECK(activate("Back"));

    CHECK(goto_focus_contains("spare"));
    key(LV_KEY_ENTER);
    CHECK(see("256 bits") && see("missing"));   /* this one is a public half only */
    theme("ssh key detail");
    CHECK(activate("Delete"));
    /* The key made earlier for the SSH session is still there, so the list is
       not empty: what matters is that the deleted one has gone. */
    CHECK(!see("spare") && see("picocalc"));
    CHECK(activate("Back"));
    CHECK(see_line("nas"));
    key(LV_KEY_ESC);
    CHECK(see_line("PicoDeck OS"));

    /* connections survive a restart, since they live on the card */
    pim_ui_start(&database, ROOT "/pim.dat");
    confirm_startup();
    CHECK(activate("Terminal"));
    CHECK(see_line("nas") && see_line("bbs"));
    key(LV_KEY_ESC);
}

void pim_input_set_game_mode(bool enabled) { (void)enabled; }
void pim_input_set_terminal_mode(bool enabled) { sim_terminal_mode = enabled; }

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

    CHECK(activate("Games")); CHECK(activate("Tetris")); CHECK(board_is_fullscreen()); CHECK(status_has("Tetris score 0"));
    CHECK(count_timers() == baseline); theme("tetris");
    CHECK(lit_pixels(0, 0, 175, 320) > 800 && lit_pixels(175, 0, 320, 320) > 800);
    key(LV_KEY_LEFT); key(LV_KEY_RIGHT); key(LV_KEY_UP); key(LV_KEY_DOWN); theme("tetris moved");
    key(LV_KEY_ENTER); CHECK(status_number("score ") > 0); theme("tetris dropped");
    pump(120); /* gravity timer keeps running */
    for (int i = 0; i < 300 && !status_has("GAME OVER"); i++) key(LV_KEY_ENTER);
    CHECK(status_has("GAME OVER")); theme("tetris over");
    key(LV_KEY_ENTER); CHECK(!status_has("GAME OVER") && status_has("score 0"));
    key(LV_KEY_ESC); CHECK(see_line("PicoDeck OS")); CHECK(count_timers() == baseline);

    CHECK(activate("Games")); CHECK(activate("Dungeon RPG")); CHECK(board_is_fullscreen()); CHECK(status_has("floor 1")); theme("dungeon");
    CHECK(lit_pixels(0, 0, 320, 320) > 1500);
    {
        const uint32_t moves[] = {LV_KEY_RIGHT, LV_KEY_DOWN, LV_KEY_LEFT, LV_KEY_UP};
        for (int i = 0; i < 300 && !status_has("DEFEATED") && !status_has("VICTORY"); i++) key(moves[(i * 5 + i / 4) % 4]);
    }
    theme("dungeon played");
    key(LV_KEY_ENTER); theme("dungeon again");
    key(LV_KEY_ESC); CHECK(see_line("PicoDeck OS"));

    CHECK(activate("Games")); CHECK(activate("Space Invaders")); CHECK(board_is_fullscreen()); CHECK(status_has("score 0")); theme("invaders");
    CHECK(count_timers() == baseline);
    for (int i = 0; i < 6; i++) key(LV_KEY_LEFT);
    key(LV_KEY_ENTER); pump(150);
    theme("invaders shot");
    for (int i = 0; i < 80; i++) { key(i % 2 ? LV_KEY_RIGHT : LV_KEY_LEFT); key(LV_KEY_ENTER); pump(20); }
    CHECK(status_number("score ") > 0); theme("invaders played");
    for (int i = 0; i < 1500 && !status_has("GAME OVER"); i++) pump(10);
    CHECK(status_has("GAME OVER")); theme("invaders over");
    key(LV_KEY_ENTER); CHECK(!status_has("GAME OVER"));
    key(LV_KEY_ESC); CHECK(see_line("PicoDeck OS")); CHECK(count_timers() == baseline);

    CHECK(activate("Games")); CHECK(activate("Solitaire")); CHECK(board_is_fullscreen()); CHECK(status_has("stock 24")); theme("solitaire");
    CHECK(lit_pixels(0, 0, 320, 320) > 2500);
    key(LV_KEY_ENTER); CHECK(status_has("stock 23 waste 1")); theme("solitaire drawn");
    key(LV_KEY_RIGHT); key(LV_KEY_ENTER); theme("solitaire picked");
    key(LV_KEY_DOWN); key(LV_KEY_UP); key(LV_KEY_UP); key(LV_KEY_BACKSPACE);
    key(LV_KEY_DOWN); keys(LV_KEY_RIGHT, 3); key(LV_KEY_UP); key(LV_KEY_DOWN); key(LV_KEY_ENTER); theme("solitaire tableau");
    key(LV_KEY_ENTER); key('f'); theme("solitaire foundation");
    key('n'); CHECK(status_has("moves 0") && status_has("stock 24"));
    key(LV_KEY_ESC); CHECK(see_line("PicoDeck OS")); CHECK(count_timers() == baseline);

    CHECK(activate("Games")); CHECK(activate("Snake")); CHECK(board_is_fullscreen()); CHECK(status_has("Snake score 0")); theme("snake");
    CHECK(lit_pixels(0, 0, 320, 260) > 300);
    key(LV_KEY_UP); pump(400); key(LV_KEY_LEFT); pump(400); key(LV_KEY_DOWN); pump(400); theme("snake moved");
    for (int i = 0; i < 300 && !status_has("GAME OVER"); i++) pump(100);
    CHECK(status_has("GAME OVER")); theme("snake over");
    key(LV_KEY_ENTER); CHECK(!status_has("GAME OVER") && status_has("score 0"));
    key(LV_KEY_ESC); CHECK(see_line("PicoDeck OS")); CHECK(count_timers() == baseline);

    CHECK(activate("Games")); CHECK(activate("2048")); CHECK(board_is_fullscreen()); CHECK(status_has("2048 score 0")); theme("2048");
    CHECK(lit_pixels(0, 0, 320, 280) > 500);
    for (int i = 0; i < 600 && !status_has("GAME OVER"); i++) key(i % 4 == 0 ? LV_KEY_LEFT : i % 4 == 1 ? LV_KEY_DOWN : i % 4 == 2 ? LV_KEY_RIGHT : LV_KEY_UP);
    CHECK(status_number("2048 score ") > 0); theme("2048 played");
    if (status_has("GAME OVER")) { key(LV_KEY_ENTER); CHECK(!status_has("GAME OVER")); }
    key(LV_KEY_ESC); CHECK(see_line("PicoDeck OS")); CHECK(count_timers() == baseline);

    CHECK(activate("Games")); CHECK(activate("Minesweeper")); CHECK(board_is_fullscreen()); CHECK(status_has("Mines left 24")); theme("mines");
    key('f'); CHECK(status_has("Mines left 23")); key('f'); CHECK(status_has("Mines left 24"));
    key(LV_KEY_ENTER); CHECK(status_has("revealed") && !status_has("revealed 0")); theme("mines opened");
    for (int i = 0; i < 400 && !status_has("BOOM") && !status_has("CLEARED"); i++) {
        key(i % 3 == 0 ? LV_KEY_RIGHT : i % 3 == 1 ? LV_KEY_DOWN : LV_KEY_LEFT); key(LV_KEY_ENTER);
    }
    theme("mines played");
    if (status_has("BOOM") || status_has("CLEARED")) { key(LV_KEY_ENTER); CHECK(status_has("revealed 0")); }
    key(LV_KEY_ESC); CHECK(see_line("PicoDeck OS")); CHECK(count_timers() == baseline);
}

static void test_theme_switcher(void) {
    static const char *const names[] = {"Amber", "Green", "Manila", "Cyan", "Violet", "Red", "Mono", "Paper"};
    static const uint32_t fgs[] = {0xffb000, 0x33ff33, 0x000000, 0x40e0ff, 0xc080ff, 0xff4040, 0xe0e0e0, 0x1a1a1a};
    static const uint32_t bgs[] = {0, 0, 0xefe0b3, 0, 0, 0, 0, 0xf2f2f2};
    reset_ui("theme switcher");
    CHECK(activate("Theme")); CHECK(see_line("Theme")); CHECK(see("Saved to /PIM/theme.cfg")); theme("theme screen");
    for (size_t i = 0; i < 8; i++) {
        uint32_t fg, bg;
        CHECK(activate(names[i]));
        { char current[32]; snprintf(current, sizeof(current), "Current: %s", names[i]); CHECK(see(current)); }
        pim_ui_theme_colors(&fg, &bg);
        CHECK(file_contains(ROOT "/theme.cfg", names[i]));
        CHECK(fg == fgs[i] && bg == bgs[i]);
        theme(names[i]);
        key(LV_KEY_ESC); theme("home after switch"); CHECK(activate("Theme"));
    }
    key(LV_KEY_ESC);
    pim_ui_start(&database, ROOT "/pim.dat"); confirm_startup();
    { uint32_t fg, bg; pim_ui_theme_colors(&fg, &bg); CHECK(bg == 0xf2f2f2); }
    forced_theme = "Amber";
}

static void test_navigation_leaks(void) {
    static const char *const entries[] = {"Calendar", "To-Do List", "Notes", "Journal", "Files",
                                          "Text editor", "Tools", "Wi-Fi & web", "Today", "Games", "Theme", "Terminal"};
    lv_mem_monitor_t before, after;
    reset_ui("memory");
    for (int round = 0; round < 3; round++)
        for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) { activate(entries[i]); key(LV_KEY_ESC); }
    lv_mem_monitor(&before);
    /* A hundred rounds, which is twelve hundred screen changes. Twenty-five was
       not enough to be sure of what it measured: LVGL holds transient
       allocations of its own - tens of bytes, theme dependent - and sampling
       while one was outstanding read as a leak. They are gone by the end of a
       longer run, so this both settles them and gives four times as many
       chances to leak. What is being tested is accumulation, not an exact
       byte count. */
    for (int round = 0; round < 100; round++)
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
    const char *only;
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
    /* PIM_UI_ONLY=<name> runs a single scenario, which is how one failure gets
       looked at without waiting for the other thirty. */
    only = getenv("PIM_UI_ONLY");
#define RUN(call, name) do { if (only == NULL || strcmp(only, name) == 0) call; } while (0)
    RUN(test_home(), "home");
    test_startup(); test_calendar_navigation();
    RUN(test_date_picker(), "date-picker");
    RUN(test_time_entry(), "time-entry");
    RUN(test_appointment_flow(), "appointment");
    RUN(test_appointment_limits(), "appointment-limits");
    RUN(test_todos(), "todos");
    RUN(test_today(), "today");
    RUN(test_notes(), "notes");
    RUN(test_journal(), "journal");
    RUN(test_large_editor(), "large-editor");
    RUN(test_tools(), "tools");
    RUN(test_timer(), "timer");
    RUN(test_scores(), "scores");
    RUN(test_files(), "files");
    RUN(test_text_editor_entry(), "text-editor");
    RUN(test_wifi(), "wifi");
    RUN(test_wifi_networks(), "wifi networks");
    RUN(test_tailscale(), "tailscale");
    RUN(test_wifi_time(), "wifi time");
    RUN(test_lock(), "lock");
    RUN(test_terminal(), "terminal");
    RUN(test_games(), "games");
    if (strcmp(theme_name, "Amber") == 0) test_theme_switcher();
    RUN(test_navigation_leaks(), "memory");
    test_fuzz();
    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

/* Each theme runs the whole suite in a fresh process so no LVGL state leaks between runs. */
int main(void) {
    static const char *const theme_names[] = {"Amber", "Green", "Manila"};
    int status_total = 0;
    /* Each theme runs in a child that leaves through _exit, which does not flush
       stdio, so buffered output would be lost whenever this is redirected. */
    setvbuf(stdout, NULL, _IONBF, 0);
    fflush(stdout);
    for (size_t t = 0; t < 3; t++) {
        pid_t child = fork();
        int status = 0;
        if (child == 0) _exit(run_suite(theme_names[t]));
        if (child < 0 || waitpid(child, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) status_total = 1;
    }
    return status_total;
}
