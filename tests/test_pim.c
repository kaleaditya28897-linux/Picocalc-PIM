#include "pim/games.h"
#include "pim/net.h"
#include "pim/pim.h"
#include "pim/sound.h"
#include "pim/storage.h"
#include "pim/telnet.h"
#include "pim/web.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "%s:%d %s\n", __FILE__, __LINE__, #value); failures++; } } while (0)

static size_t read_text(const char *path, char *buffer, size_t buffer_size) {
    FILE *file = fopen(path, "rb");
    size_t length = 0;
    if (file != NULL) {
        length = fread(buffer, 1, buffer_size - 1, file);
        fclose(file);
    }
    buffer[length] = '\0';
    return length;
}

static void test_calendar(void) {
    CHECK(pim_days_in_month(2024, 2) == 29);
    CHECK(pim_days_in_month(2023, 2) == 28);
    CHECK(pim_first_day_of_month(2024, 1) == 0);
    CHECK(pim_date_is_valid((pim_date_t){2024, 2, 29}));
    pim_date_t shifted = pim_date_shift((pim_date_t){2026, 3, 1}, -1);
    CHECK(shifted.year == 2026 && shifted.month == 2 && shifted.day == 28);
    shifted = pim_date_shift((pim_date_t){2026, 12, 31}, 1);
    CHECK(shifted.year == 2027 && shifted.month == 1 && shifted.day == 1);
    shifted = pim_date_shift((pim_date_t){2000, 1, 1}, -1);
    CHECK(shifted.year == 2000 && shifted.month == 1 && shifted.day == 1);
}
static void test_battery_decode(void) {
    pim_battery_t battery;
    const uint8_t discharging[2] = {0x0b, 73}, charging[2] = {0x0b, 0x80 | 100};
    const uint8_t empty[2] = {0x0b, 0}, wrong_reg[2] = {0x0c, 50}, too_high[2] = {0x0b, 101};
    CHECK(pim_battery_decode(discharging, &battery) && battery.percent == 73 && !battery.charging);
    CHECK(pim_battery_decode(charging, &battery) && battery.percent == 100 && battery.charging);
    CHECK(!pim_battery_decode(empty, &battery));
    CHECK(!pim_battery_decode(wrong_reg, &battery));
    CHECK(!pim_battery_decode(too_high, &battery));
    CHECK(!pim_battery_decode(NULL, &battery));
}

static void test_date_entry(void) {
    uint8_t digits = 0;
    uint16_t value = 0;
    pim_date_t moved = pim_date_add_months((pim_date_t){2026, 1, 31}, 1);
    CHECK(moved.year == 2026 && moved.month == 2 && moved.day == 28);
    moved = pim_date_add_months((pim_date_t){2026, 11, 15}, 3);
    CHECK(moved.year == 2027 && moved.month == 2 && moved.day == 15);
    moved = pim_date_add_months((pim_date_t){2026, 3, 31}, -12);
    CHECK(moved.year == 2025 && moved.month == 3 && moved.day == 31);
    moved = pim_date_add_months((pim_date_t){2000, 1, 5}, -1);
    CHECK(moved.year == 2000 && moved.month == 1 && moved.day == 5);
    moved = pim_date_add_months((pim_date_t){2099, 12, 5}, 1);
    CHECK(moved.year == 2099 && moved.month == 12 && moved.day == 5);
    moved = pim_date_add_months((pim_date_t){2024, 2, 29}, 12);
    CHECK(moved.year == 2025 && moved.month == 2 && moved.day == 28);
    value = pim_entry_digit(value, &digits, 2, 23); CHECK(value == 2 && digits == 1);
    value = pim_entry_digit(value, &digits, 3, 23); CHECK(value == 23 && digits == 2);
    value = pim_entry_digit(value, &digits, 5, 23); CHECK(value == 5 && digits == 1);
    digits = 0; value = pim_entry_digit(9, &digits, 7, 59); CHECK(value == 7 && digits == 1);
    value = pim_entry_digit(value, &digits, 5, 59); CHECK(value == 75 / 10 * 10 + 5 || value == 5);
    CHECK(value == 5);
    digits = 0; value = pim_entry_digit(0, &digits, 2, 2099); value = pim_entry_digit(value, &digits, 0, 2099);
    value = pim_entry_digit(value, &digits, 2, 2099); value = pim_entry_digit(value, &digits, 6, 2099);
    CHECK(value == 2026 && digits == 4);
    CHECK(pim_entry_digit(12, &digits, 11, 59) == 12);
}

static void test_models_and_storage(void) {
    pim_database_t source, loaded;
    pim_appointment_t appointment = {
        .date = {2026, 6, 20}, .minutes = 540, .duration = 90, .title = "Planning, \"weekly\"", .description = "Discuss \"C\""
    };
    pim_todo_t todo = {.priority = PIM_PRIORITY_HIGH, .title = "Ship"};
    pim_note_t note = {.title = "C ideas", .content = "Use interfaces", .modified_at = 1};
    pim_journal_entry_t journal = {.date = {2026, 6, 20}, .mood = PIM_MOOD_GOOD, .content = "PDA"};
    size_t matches[1];
    pim_database_init(&source);
    CHECK(pim_add_appointment(&source, &appointment));
    CHECK(pim_appointments_on(&source, (pim_date_t){2026, 6, 20}, matches, 1) == 1 && matches[0] == 0);
    CHECK(pim_appointments_on(&source, (pim_date_t){2026, 6, 21}, matches, 1) == 0);
    appointment.minutes = 23 * 60 + 30; appointment.duration = 60;
    CHECK(!pim_add_appointment(&source, &appointment));
    CHECK(pim_add_todo(&source, &todo));
    pim_toggle_todo(&source, 0);
    CHECK(source.todos[0].completed);
    CHECK(pim_add_note(&source, &note));
    CHECK(pim_search_notes(&source, "INTERFACES", matches, 1) == 1 && matches[0] == 0);
    CHECK(pim_add_or_update_journal(&source, &journal));
    CHECK(pim_storage_save("pim-test.dat", &source));
    CHECK(pim_storage_load("pim-test.dat", &loaded));
    CHECK(loaded.appointment_count == 1 && loaded.note_count == 1);
    FILE *snapshot = fopen("pim-test.dat", "r+b");
    CHECK(snapshot != NULL);
    if (snapshot != NULL) {
        fseek(snapshot, 0, SEEK_SET);
        fputc(0, snapshot);
        fclose(snapshot);
    }
    CHECK(!pim_storage_load("pim-test.dat", &loaded) && loaded.next_id == 1);
    CHECK(mkdir("pim-export", 0777) == 0);
    CHECK(mkdir("pim-export/appointments", 0777) == 0);
    CHECK(mkdir("pim-export/calendar", 0777) == 0);
    CHECK(mkdir("pim-export/journal", 0777) == 0);
    CHECK(mkdir("pim-export/notes", 0777) == 0);
    CHECK(mkdir("pim-export/todos", 0777) == 0);
    CHECK(pim_storage_export("pim-export", &source));
    char exported[512];
    CHECK(read_text("pim-export/appointments/appointments.csv", exported, sizeof(exported)) > 0);
    CHECK(strstr(exported, "date,time,duration_minutes,title,description\n") != NULL);
    CHECK(strstr(exported, "20-06-2026,09:00,90,\"Planning, \"\"weekly\"\"\",\"Discuss \"\"C\"\"\"") != NULL);
    CHECK(read_text("pim-export/todos/todos.csv", exported, sizeof(exported)) > 0);
    CHECK(strstr(exported, "done,High,\"Ship\"") != NULL);
    CHECK(read_text("pim-export/notes/note-00000003.md", exported, sizeof(exported)) > 0);
    CHECK(strstr(exported, "# C ideas\n\nUse interfaces\n") != NULL);
    CHECK(read_text("pim-export/journal/20-06-2026.md", exported, sizeof(exported)) > 0);
    CHECK(strstr(exported, "Mood: Good\n\nPDA\n") != NULL);
    CHECK(read_text("pim-export/calendar/calendar.md", exported, sizeof(exported)) > 0);
    CHECK(strstr(exported, "- 20-06-2026 09:00-10:30 - Planning") != NULL);
    remove("pim-test.dat");
    remove("pim-export/appointments/appointments.csv");
    remove("pim-export/calendar/calendar.md");
    remove("pim-export/journal/20-06-2026.md");
    remove("pim-export/notes/note-00000003.md");
    remove("pim-export/todos/todos.csv");
    rmdir("pim-export/appointments");
    rmdir("pim-export/calendar");
    rmdir("pim-export/journal");
    rmdir("pim-export/notes");
    rmdir("pim-export/todos");
    rmdir("pim-export");
}
static unsigned test_random(void) {
    static uint32_t state = 12345u;
    state = state * 1103515245u + 12345u;
    return state >> 16;
}
static void test_tetris(void) {
    pim_tetris_t tetris;
    uint8_t locked_cells = 0;
    pim_tetris_reset(&tetris, 1);
    CHECK(tetris.level == 1 && tetris.next < 7 && tetris.shape < 7);
    CHECK(pim_tetris_move(&tetris, -1, 0));
    pim_tetris_rotate(&tetris);
    pim_tetris_drop(&tetris);
    for (uint8_t row = 0; row < PIM_TETRIS_HEIGHT; row++)
        for (uint8_t column = 0; column < PIM_TETRIS_WIDTH; column++)
            if (tetris.cells[row][column]) locked_cells++;
    CHECK(tetris.score > 0 && locked_cells == 4);
    pim_tetris_reset(&tetris, 3);
    while (pim_tetris_move(&tetris, -1, 0)) {}
    pim_tetris_rotate(&tetris);
    for (uint8_t row = 0; row < 4; row++) for (uint8_t col = 0; col < 4; col++)
        if (pim_tetris_shape_cell(tetris.shape, tetris.rotation, row, col)) CHECK(tetris.x + col >= 0);
    memset(&tetris.cells[PIM_TETRIS_HEIGHT - 1][4], 1, 6);
    tetris.shape = 0; tetris.rotation = 0; tetris.x = 0; tetris.y = 0;
    pim_tetris_drop(&tetris);
    CHECK(tetris.lines == 1 && tetris.score >= 100);
    pim_tetris_reset(&tetris, 9);
    for (int i = 0; i < 20000 && !tetris.game_over; i++) {
        unsigned action = test_random() % 6;
        if (action == 0) pim_tetris_move(&tetris, -1, 0);
        else if (action == 1) pim_tetris_move(&tetris, 1, 0);
        else if (action == 2) pim_tetris_rotate(&tetris);
        else if (action == 3 && test_random() % 8 == 0) pim_tetris_drop(&tetris);
        else pim_tetris_move(&tetris, 0, 1);
        CHECK(tetris.shape < 7 && tetris.next < 7 && tetris.x >= -2 && tetris.x < PIM_TETRIS_WIDTH);
    }
    CHECK(tetris.game_over);
    CHECK(pim_tetris_interval_ms(&tetris) >= 120);
}
static bool dungeon_solvable(const pim_dungeon_t *game) {
    bool seen[PIM_DUNGEON_HEIGHT][PIM_DUNGEON_WIDTH] = {{false}};
    int stack[PIM_DUNGEON_HEIGHT * PIM_DUNGEON_WIDTH], top = 0;
    stack[top++] = PIM_DUNGEON_WIDTH + 1; seen[1][1] = true;
    while (top) {
        int at = stack[--top], x = at % PIM_DUNGEON_WIDTH, y = at / PIM_DUNGEON_WIDTH;
        const int steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        if (game->cells[y][x] == '>') return true;
        for (int i = 0; i < 4; i++) {
            int nx = x + steps[i][0], ny = y + steps[i][1];
            if (game->cells[ny][nx] != '#' && !seen[ny][nx]) { seen[ny][nx] = true; stack[top++] = ny * PIM_DUNGEON_WIDTH + nx; }
        }
    }
    return false;
}
static void test_dungeon(void) {
    pim_dungeon_t dungeon;
    for (uint32_t seed = 0; seed < 300; seed++) {
        pim_dungeon_reset(&dungeon, seed);
        CHECK(dungeon_solvable(&dungeon) && dungeon.enemies >= 3 && dungeon.health == 20);
        CHECK(dungeon.player_x == 1 && dungeon.player_y == 1);
    }
    pim_dungeon_reset(&dungeon, 10);
    memset(dungeon.enemy, 0, sizeof(dungeon.enemy));
    dungeon.enemies = 1;
    dungeon.cells[1][2] = '.';
    dungeon.enemy[0] = (pim_enemy_t){.x = 2, .y = 1, .hp = 1, .alive = true};
    pim_dungeon_move(&dungeon, PIM_RIGHT);
    CHECK(dungeon.enemies == 0 && dungeon.kills == 1 && dungeon.gold == 10 && dungeon.player_x == 1);
    dungeon.cells[1][2] = '$';
    pim_dungeon_move(&dungeon, PIM_RIGHT);
    CHECK(dungeon.player_x == 2 && dungeon.gold == 15 && dungeon.cells[1][2] == '.');
    dungeon.health = 5; dungeon.cells[1][3] = '!';
    pim_dungeon_move(&dungeon, PIM_RIGHT);
    CHECK(dungeon.health == 13);
    dungeon.cells[1][4] = '>';
    pim_dungeon_move(&dungeon, PIM_RIGHT);
    pim_dungeon_move(&dungeon, PIM_RIGHT);
    CHECK(dungeon.floor == 2 && dungeon.player_x == 1);
    dungeon.floor = PIM_DUNGEON_FLOORS; dungeon.cells[1][2] = '>';
    pim_dungeon_move(&dungeon, PIM_RIGHT);
    CHECK(dungeon.won);
    pim_dungeon_reset(&dungeon, 4);
    dungeon.health = 1;
    dungeon.enemy[0] = (pim_enemy_t){.x = 2, .y = 1, .hp = 250, .alive = true};
    dungeon.cells[1][2] = '.';
    for (int i = 0; i < 80 && !dungeon.game_over; i++) pim_dungeon_move(&dungeon, PIM_RIGHT);
    CHECK(dungeon.game_over);
    for (uint32_t seed = 0; seed < 40; seed++) {
        pim_dungeon_reset(&dungeon, seed);
        for (int i = 0; i < 600; i++) {
            pim_dungeon_move(&dungeon, (pim_direction_t)(test_random() % 4));
            CHECK(dungeon.player_x > 0 && dungeon.player_x < PIM_DUNGEON_WIDTH - 1);
            CHECK(dungeon.cells[dungeon.player_y][dungeon.player_x] != '#' && dungeon.health <= dungeon.max_health);
        }
    }
}
static void test_invaders(void) {
    pim_invaders_t invaders;
    pim_invaders_reset(&invaders, 5);
    pim_invaders_move(&invaders, -1);
    CHECK(invaders.player_x == PIM_INVADERS_WIDTH / 2 - 1);
    invaders.player_x = 0;
    pim_invaders_fire(&invaders);
    for (int tick = 0; tick < 20 && invaders.bullet_active; tick++) pim_invaders_tick(&invaders);
    CHECK(!invaders.bullet_active && invaders.score == 10 && invaders.remaining == 31 && !invaders.alive[3][0]);
    invaders.bomb_active = true; invaders.bomb_x = invaders.player_x; invaders.bomb_y = PIM_INVADERS_HEIGHT - 2; invaders.tick = 1;
    pim_invaders_tick(&invaders);
    CHECK(invaders.lives == 2 && !invaders.bomb_active);
    memset(invaders.alive, 0, sizeof(invaders.alive)); invaders.remaining = 0;
    pim_invaders_tick(&invaders);
    CHECK(invaders.wave == 2 && invaders.remaining == 32 && !invaders.game_over);
    invaders.offset_y = PIM_INVADERS_HEIGHT - 2 - (PIM_INVADERS_ROWS - 1);
    pim_invaders_tick(&invaders);
    CHECK(invaders.game_over);
    pim_invaders_reset(&invaders, 6);
    for (int i = 0; i < 20000 && !invaders.game_over; i++) {
        if (test_random() % 3 == 0) pim_invaders_move(&invaders, (int8_t)(test_random() % 2 ? 1 : -1));
        if (test_random() % 4 == 0) pim_invaders_fire(&invaders);
        pim_invaders_tick(&invaders);
        CHECK(invaders.player_x >= 0 && invaders.player_x < PIM_INVADERS_WIDTH && invaders.offset_x >= 0 &&
              invaders.offset_x <= PIM_INVADERS_MAX_OFFSET);
    }
}
static void test_solitaire(void) {
    pim_solitaire_t game;
    bool seen[52] = {false};
    pim_solitaire_new(&game, 7);
    CHECK(game.stock_count == 24 && game.waste_count == 0);
    for (int pile = 0; pile < 7; pile++) {
        CHECK(game.tableau_count[pile] == pile + 1 && game.face_up[pile] == 1);
        for (int i = 0; i < game.tableau_count[pile]; i++) seen[game.tableau[pile][i]] = true;
    }
    for (int i = 0; i < game.stock_count; i++) seen[game.stock[i]] = true;
    for (int i = 0; i < 52; i++) CHECK(seen[i]);
    CHECK(pim_solitaire_draw(&game) && game.waste_count == 1 && game.stock_count == 23);
    while (game.stock_count) pim_solitaire_draw(&game);
    CHECK(pim_solitaire_draw(&game) && game.stock_count == 24 && game.waste_count == 0);
    /* card = suit * 13 + rank; suit 0 black, 1 red */
    memset(&game, 0, sizeof(game));
    game.tableau[0][0] = 13 + 5; game.tableau_count[0] = 1; game.face_up[0] = 1;     /* 6 red */
    game.tableau[1][0] = 7; game.tableau[1][1] = 4; game.tableau_count[1] = 2; game.face_up[1] = 1; /* hidden 8, up 5 black */
    CHECK(pim_solitaire_move(&game, PIM_SOL_TABLEAU, 1, 1, PIM_SOL_TABLEAU, 0));
    CHECK(game.tableau_count[0] == 2 && game.tableau_count[1] == 1 && game.face_up[1] == 1 && game.face_up[0] == 2);
    CHECK(!pim_solitaire_move(&game, PIM_SOL_TABLEAU, 0, 2, PIM_SOL_TABLEAU, 1));
    CHECK(pim_solitaire_move(&game, PIM_SOL_TABLEAU, 0, 2, PIM_SOL_TABLEAU, 1) == false);
    game.tableau[2][0] = 12; game.tableau_count[2] = 1; game.face_up[2] = 1;
    CHECK(!pim_solitaire_move(&game, PIM_SOL_TABLEAU, 0, 1, PIM_SOL_TABLEAU, 5));
    game.tableau_count[1] = 0; game.face_up[1] = 0;
    CHECK(pim_solitaire_move(&game, PIM_SOL_TABLEAU, 2, 1, PIM_SOL_TABLEAU, 1));
    CHECK(game.tableau_count[2] == 0);
    memset(&game, 0, sizeof(game));
    game.waste[0] = 0; game.waste_count = 1;
    CHECK(pim_solitaire_to_foundation(&game, PIM_SOL_WASTE, 0) && game.foundation[0] == 1 && game.waste_count == 0);
    game.tableau[3][0] = 1; game.tableau_count[3] = 1; game.face_up[3] = 1;
    CHECK(pim_solitaire_to_foundation(&game, PIM_SOL_TABLEAU, 3) && game.foundation[0] == 2);
    game.tableau[3][0] = 14; game.tableau_count[3] = 1; game.face_up[3] = 1;
    CHECK(!pim_solitaire_to_foundation(&game, PIM_SOL_TABLEAU, 3));
    game.tableau[4][0] = 13 + 2; game.tableau_count[4] = 1; game.face_up[4] = 1;
    CHECK(pim_solitaire_move(&game, PIM_SOL_FOUNDATION, 0, 1, PIM_SOL_TABLEAU, 4) && game.foundation[0] == 1);
    for (int suit = 0; suit < 4; suit++) game.foundation[suit] = 12;
    game.tableau_count[0] = 0; game.waste_count = 0;
    for (int suit = 0; suit < 4; suit++) {
        game.tableau[suit][0] = (uint8_t)(suit * 13 + 12); game.tableau_count[suit] = 1; game.face_up[suit] = 1;
        CHECK(pim_solitaire_to_foundation(&game, PIM_SOL_TABLEAU, (uint8_t)suit));
    }
    CHECK(game.won);
    for (uint32_t seed = 0; seed < 200; seed++) {
        pim_solitaire_new(&game, seed);
        for (int i = 0; i < 400; i++) {
            pim_solitaire_zone_t from = (pim_solitaire_zone_t)(test_random() % 3), to = (pim_solitaire_zone_t)(test_random() % 3);
            unsigned action = test_random() % 3;
            if (action == 0) pim_solitaire_draw(&game);
            else if (action == 1) pim_solitaire_to_foundation(&game, from, (uint8_t)(test_random() % 7));
            else pim_solitaire_move(&game, from, (uint8_t)(test_random() % 7), (uint8_t)(1 + test_random() % 4), to, (uint8_t)(test_random() % 7));
            for (int pile = 0; pile < 7; pile++)
                CHECK(game.tableau_count[pile] <= PIM_SOLITAIRE_COLUMN_MAX && game.face_up[pile] <= game.tableau_count[pile] &&
                      (game.tableau_count[pile] == 0 || game.face_up[pile] > 0));
        }
    }
}
static void test_net(void) {
    uint8_t packet[PIM_NTP_PACKET_SIZE];
    uint32_t seconds = 0, ntp = 1767225600U + 2208988800U;
    pim_date_t date;
    uint16_t minutes;
    pim_wifi_config_t config, loaded;
    pim_database_t database, other;
    char text[20000];
    pim_ics_result_t result;
    pim_ntp_request(packet);
    CHECK(packet[0] == 0x23 && !pim_ntp_decode(packet, sizeof(packet), &seconds));
    packet[0] = 0x24; packet[1] = 2;
    packet[40] = (uint8_t)(ntp >> 24); packet[41] = (uint8_t)(ntp >> 16); packet[42] = (uint8_t)(ntp >> 8); packet[43] = (uint8_t)ntp;
    CHECK(pim_ntp_decode(packet, sizeof(packet), &seconds) && seconds == 1767225600U);
    CHECK(!pim_ntp_decode(packet, 47, &seconds));
    packet[1] = 0; CHECK(!pim_ntp_decode(packet, sizeof(packet), &seconds));
    CHECK(pim_time_from_unix(1767225600U, 0, &date, &minutes) && date.year == 2026 && date.month == 1 && date.day == 1 && minutes == 0);
    CHECK(pim_time_from_unix(1767225600U, 330, &date, &minutes) && minutes == 330);
    CHECK(pim_time_from_unix(1767225600U, -60, &date, &minutes) && date.year == 2025 && date.month == 12 && date.day == 31 && minutes == 1380);
    CHECK(pim_time_from_unix(1709164800U, 0, &date, &minutes) && date.year == 2024 && date.month == 2 && date.day == 29);
    CHECK(!pim_time_from_unix(1767225600U, 7, &date, &minutes) && !pim_time_from_unix(100, 0, &date, &minutes));

    pim_wifi_config_init(&config);
    CHECK(config.tz_minutes == 330 && config.count == 0);
    CHECK(pim_wifi_config_remember(&config, "Home Net", "pass one") && pim_wifi_config_remember(&config, "Work", "pw2"));
    CHECK(config.count == 2 && strcmp(config.networks[0].ssid, "Work") == 0);
    CHECK(pim_wifi_config_remember(&config, "Home Net", "newpass") && config.count == 2 && strcmp(config.networks[0].ssid, "Home Net") == 0);
    CHECK(strcmp(pim_wifi_config_find(&config, "Home Net")->password, "newpass") == 0 && pim_wifi_config_find(&config, "x") == NULL);
    CHECK(!pim_wifi_config_remember(&config, "bad\tname", "p") && !pim_wifi_config_remember(&config, "", "p") && !pim_wifi_config_remember(&config, "a", ""));
    pim_wifi_config_remember(&config, "C", "c"); pim_wifi_config_remember(&config, "D", "d"); pim_wifi_config_remember(&config, "E", "e");
    CHECK(config.count == PIM_WIFI_MAX_NETWORKS && pim_wifi_config_find(&config, "Work") == NULL);
    config.tz_minutes = -330;
    mkdir("test_net_dir", 0777);
    CHECK(pim_wifi_config_save("test_net_dir/wifi.cfg", &config) && pim_wifi_config_load("test_net_dir/wifi.cfg", &loaded));
    CHECK(loaded.count == config.count && loaded.tz_minutes == -330 && strcmp(loaded.networks[0].ssid, "E") == 0);
    CHECK(pim_wifi_config_forget(&loaded, 0) && loaded.count == 3 && !pim_wifi_config_forget(&loaded, 9));
    CHECK(!pim_wifi_config_load("test_net_dir/missing.cfg", &loaded) && loaded.count == 0);
    remove("test_net_dir/wifi.cfg"); rmdir("test_net_dir");

    pim_database_init(&database); pim_database_init(&other);
    pim_appointment_t appointment = {.date = {2026, 1, 15}, .minutes = 570, .duration = 90};
    snprintf(appointment.title, sizeof(appointment.title), "Review, plan; ship");
    snprintf(appointment.description, sizeof(appointment.description), "Line1\nLine2");
    CHECK(pim_add_appointment(&database, &appointment));
    pim_todo_t todo = {.priority = PIM_PRIORITY_HIGH, .completed = true};
    snprintf(todo.title, sizeof(todo.title), "Buy milk");
    CHECK(pim_add_todo(&database, &todo));
    CHECK(pim_ics_export(&database, text, sizeof(text)) > 0 && strstr(text, "DTSTART:20260115T093000") && strstr(text, "DTEND:20260115T110000"));
    CHECK(strstr(text, "SUMMARY:Review\\, plan\\; ship") && strstr(text, "STATUS:COMPLETED") && strstr(text, "PRIORITY:1"));
    { char tiny[50]; CHECK(pim_ics_export(&database, tiny, sizeof(tiny)) == 0); }
    result = pim_ics_import(&other, text);
    CHECK(result.events == 1 && result.todos == 1 && result.skipped == 0 && result.rejected == 0);
    CHECK(other.appointments[0].minutes == 570 && other.appointments[0].duration == 90 && strcmp(other.appointments[0].title, "Review, plan; ship") == 0);
    CHECK(strcmp(other.appointments[0].description, "Line1\nLine2") == 0 && other.todos[0].completed && other.todos[0].priority == PIM_PRIORITY_HIGH);
    result = pim_ics_import(&other, text);
    CHECK(result.events == 0 && result.todos == 0 && result.skipped == 2);
    result = pim_ics_import(&other,
        "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nDTSTART;TZID=Asia/Kolkata:20260201T140000\r\nDTEND:20260201T150000Z\r\n"
        "SUMMARY:Folded\r\n  title\r\nEND:VEVENT\r\nBEGIN:VEVENT\r\nDTSTART;VALUE=DATE:20260301\r\nSUMMARY:All day\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nDTSTART:20269999T000000\r\nSUMMARY:Bad\r\nEND:VEVENT\r\nBEGIN:VTODO\r\nSUMMARY:Low\r\nPRIORITY:9\r\nEND:VTODO\r\nEND:VCALENDAR\r\n");
    CHECK(result.events == 2 && result.todos == 1 && result.rejected == 1);
    CHECK(pim_appointments_on(&other, (pim_date_t){2026, 2, 1}, NULL, 0) == 1);
    CHECK(strcmp(other.appointments[1].title, "Folded title") == 0 && other.appointments[1].duration == 60);
    CHECK(pim_ics_import(&other, "").events == 0 && pim_ics_import(&other, "garbage\r\nBEGIN:VEVENT").rejected == 0);
}
static pim_web_t web_state;
static char web_out[20000];
static pim_web_response_t web_response;
static void web_request(const char *method, const char *target, const char *body, bool logged_in) {
    web_response = (pim_web_response_t){0};
    pim_web_handle(&web_state, method, target, logged_in ? "theme=x; pim=4321" : NULL, body, strlen(body), web_out, sizeof(web_out), &web_response);
}
static void test_web(void) {
    static pim_database_t database, reloaded;
    char path[300], exported[512];
    pim_database_init(&database);
    mkdir("test_web_dir", 0777);
    web_state = (pim_web_t){.database = &database, .root = "test_web_dir", .storage_path = "test_web_dir/pim.dat",
                            .today = {2026, 1, 15}, .minutes = 600};
    snprintf(web_state.pin, sizeof(web_state.pin), "4321");

    web_request("GET", "/", "", false);
    CHECK(web_response.status == 401 && strstr(web_out, "PIN"));
    web_request("GET", "/todos", "", false); CHECK(web_response.status == 401);
    web_request("POST", "/todos/add", "title=x", false); CHECK(web_response.status == 401 && database.todo_count == 0);
    web_request("GET", "/file/note.txt", "", false); CHECK(web_response.status == 401);
    web_request("POST", "/login", "pin=0000", false); CHECK(web_response.status == 401 && web_state.failures == 1);
    web_request("POST", "/login", "pin=4321", false); CHECK(web_response.status == 303 && web_response.set_cookie && web_state.failures == 0);
    for (int i = 0; i < PIM_WEB_MAX_FAILURES; i++) web_request("POST", "/login", "pin=1", false);
    web_request("POST", "/login", "pin=4321", false); CHECK(web_response.status == 401 && strstr(web_out, "Too many"));
    web_request("GET", "/", "", true); CHECK(web_response.status == 401); /* locked out even with the cookie */
    web_state.failures = 0;
    CHECK(pim_web_authorized(&web_state, "pim=4321") && !pim_web_authorized(&web_state, "xpim=4321") && !pim_web_authorized(&web_state, "pim=43210"));

    web_request("GET", "/", "", true); CHECK(web_response.status == 200 && strstr(web_out, "15-01-2026 10:00"));
    web_request("POST", "/agenda/add", "date=15-01-2026&time=09%3A30&duration=45&title=Stand+up+%3Cb%3E&description=Room+1", true);
    CHECK(web_response.status == 303 && strstr(web_response.location, "15-01-2026") && database.appointment_count == 1);
    CHECK(database.appointments[0].minutes == 570 && database.appointments[0].duration == 45 && strcmp(database.appointments[0].title, "Stand up <b>") == 0);
    web_request("GET", "/agenda", "", true);
    CHECK(strstr(web_out, "09:30-10:15") && strstr(web_out, "Stand up &lt;b&gt;") && !strstr(web_out, "Stand up <b>"));
    web_request("GET", "/agenda?date=16-01-2026", "", true); CHECK(strstr(web_out, "Nothing scheduled"));
    web_request("POST", "/agenda/add", "date=31-02-2026&time=09:30&duration=45&title=Bad", true); CHECK(web_response.status == 400);
    web_request("POST", "/agenda/add", "date=15-01-2026&time=25:00&duration=45&title=Bad", true); CHECK(web_response.status == 400);
    web_request("POST", "/agenda/add", "date=15-01-2026&time=23:30&duration=120&title=Late", true); CHECK(web_response.status == 400 && database.appointment_count == 1);
    CHECK(web_state.changed);
    snprintf(path, sizeof(path), "id=%lu&date=15-01-2026", (unsigned long)database.appointments[0].id);
    web_request("POST", "/agenda/delete", path, true); CHECK(web_response.status == 303 && database.appointment_count == 0);

    web_request("POST", "/todos/add", "title=Buy+milk&priority=2", true);
    CHECK(web_response.status == 303 && database.todo_count == 1 && database.todos[0].priority == PIM_PRIORITY_HIGH);
    web_request("POST", "/todos/add", "title=&priority=1", true); CHECK(web_response.status == 400);
    snprintf(path, sizeof(path), "id=%lu", (unsigned long)database.todos[0].id);
    web_request("POST", "/todos/toggle", path, true); CHECK(database.todos[0].completed);
    web_request("GET", "/todos", "", true); CHECK(strstr(web_out, "Buy milk") && strstr(web_out, "Undo"));
    web_request("POST", "/todos/delete", path, true); CHECK(database.todo_count == 0);
    web_request("POST", "/todos/delete", "id=999999", true); CHECK(web_response.status == 303);

    web_request("POST", "/note/save", "id=&title=Ideas&content=line+one%0D%0Aline+two", true);
    CHECK(web_response.status == 303 && database.note_count == 1 && strcmp(database.notes[0].content, "line one\nline two") == 0);
    snprintf(path, sizeof(path), "/note?id=%lu", (unsigned long)database.notes[0].id);
    web_request("GET", path, "", true); CHECK(strstr(web_out, "line two") && strstr(web_out, "Delete"));
    snprintf(path, sizeof(path), "id=%lu&title=Renamed&content=new", (unsigned long)database.notes[0].id);
    web_request("POST", "/note/save", path, true); CHECK(database.note_count == 1 && strcmp(database.notes[0].title, "Renamed") == 0);
    web_request("GET", "/notes", "", true); CHECK(strstr(web_out, "Renamed"));
    snprintf(path, sizeof(path), "id=%lu", (unsigned long)database.notes[0].id);
    web_request("POST", "/note/delete", path, true); CHECK(database.note_count == 0);
    web_request("POST", "/note/save", "id=77&title=Ghost&content=x", true); CHECK(web_response.status == 400);

    mkdir("test_web_dir/journal", 0777);
    web_request("POST", "/journal/save", "date=14-01-2026&content=Good+day%0D%0Asecond", true);
    CHECK(web_response.status == 303 && read_text("test_web_dir/journal/14-01-2026.md", exported, sizeof(exported)) > 0 && strstr(exported, "Good day"));
    web_request("GET", "/journal/edit?date=14-01-2026", "", true); CHECK(strstr(web_out, "Good day") && strstr(web_out, "Delete"));
    web_request("GET", "/journal", "", true); CHECK(strstr(web_out, "14-01-2026"));
    web_request("GET", "/", "", true); CHECK(strstr(web_out, "1 journal entries"));
    web_request("POST", "/journal/save", "date=99-99-2026&content=x", true); CHECK(web_response.status == 400);
    web_request("POST", "/journal/delete", "date=14-01-2026", true); CHECK(read_text("test_web_dir/journal/14-01-2026.md", exported, sizeof(exported)) == 0);
    web_request("POST", "/journal/save", "date=14-01-2026&content=", true); CHECK(web_response.status == 303 && read_text("test_web_dir/journal/14-01-2026.md", exported, sizeof(exported)) == 0);

    web_request("POST", "/save", "name=hello.c&text=%23include+%3Cstdio.h%3E%0D%0Aint+main%28void%29+%7B%7D", true);
    CHECK(web_response.status == 303);
    web_request("GET", "/edit?name=hello.c", "", true); CHECK(strstr(web_out, "#include &lt;stdio.h&gt;\nint main(void) {}"));
    web_request("GET", "/files", "", true); CHECK(strstr(web_out, "hello.c") && !strstr(web_out, "pim.dat"));
    web_request("GET", "/file/hello.c", "", true); CHECK(web_response.status == 200 && strncmp(web_out, "#include <stdio.h>", 18) == 0);
    web_request("GET", "/file/missing.c", "", true); CHECK(web_response.status == 404);
    web_request("GET", "/file/pim.dat", "", true); CHECK(web_response.status == 400);
    web_request("GET", "/file/..%2Fx", "", true); CHECK(web_response.status == 400);
    web_request("POST", "/save", "name=wifi.cfg&text=x", true); CHECK(web_response.status == 400);
    web_request("POST", "/save", "name=a%2Fb&text=x", true); CHECK(web_response.status == 400);
    web_request("GET", "/edit?name=theme.cfg", "", true); CHECK(web_response.status == 400);
    web_request("POST", "/upload/raw.txt", "raw body", true); CHECK(web_response.status == 201);
    web_request("GET", "/file/raw.txt", "", true); CHECK(web_response.length == 8 && strcmp(web_out, "raw body") == 0);
    {
        static char big[PIM_WEB_UPLOAD_MAX + 2];
        memset(big, 'a', sizeof(big) - 1);
        web_response = (pim_web_response_t){0};
        pim_web_handle(&web_state, "POST", "/upload/big.txt", "pim=4321", big, sizeof(big) - 1, web_out, sizeof(web_out), &web_response);
        CHECK(web_response.status == 413);
    }
    web_request("POST", "/files/delete", "name=hello.c", true); CHECK(web_response.status == 303 && access("test_web_dir/hello.c", 0) != 0);
    remove("test_web_dir/raw.txt");

    pim_todo_t todo = {.priority = PIM_PRIORITY_NORMAL};
    snprintf(todo.title, sizeof(todo.title), "Ship it");
    pim_add_todo(&database, &todo);
    web_request("GET", "/calendar.ics", "", true); CHECK(web_response.status == 200 && strstr(web_out, "SUMMARY:Ship it"));
    web_request("GET", "/import", "", true); CHECK(web_response.status == 200 && strstr(web_out, "Import"));
    web_request("POST", "/import", "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nDTSTART:20260320T100000\r\nSUMMARY:Imported\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n", true);
    CHECK(web_response.status == 200 && strstr(web_out, "1 events") && database.appointment_count == 1);
    web_request("POST", "/import", "ics=BEGIN%3AVTODO%0D%0ASUMMARY%3AFrom+form%0D%0AEND%3AVTODO%0D%0A", true);
    CHECK(strstr(web_out, "1 to-dos") && database.todo_count == 2);
    web_request("GET", "/nonexistent", "", true); CHECK(web_response.status == 404);
    web_request("DELETE", "/", "", true); CHECK(web_response.status == 400);

    CHECK(pim_storage_load("test_web_dir/pim.dat", &reloaded) && reloaded.todo_count == 2 && reloaded.appointment_count == 1);
    remove("test_web_dir/pim.dat"); remove("test_web_dir/pim.dat.tmp");
    {
        DIR *directory = opendir("test_web_dir");
        struct dirent *entry;
        while (directory != NULL && (entry = readdir(directory)) != NULL) {
            if (entry->d_name[0] == '.') continue;
            snprintf(path, sizeof(path), "test_web_dir/%s", entry->d_name);
            remove(path);
        }
        if (directory != NULL) closedir(directory);
    }
    rmdir("test_web_dir");
}
static void test_games(void) { test_tetris(); test_dungeon(); test_invaders(); test_solitaire(); }
static void test_telnet(void) {
    static pim_term_t term;
    uint8_t reply[32];
    char view[512];
    const uint8_t negotiate[] = {255, 251, 1, 255, 253, 24, 255, 253, 3, 255, 251, 3, 255, 250, 24, 1, 255, 240, 'H', 'i'};
    const uint8_t ansi[] = "\x1b[1;31mRED\x1b[0m\r\nA\x1b[3CB\r\n";
    size_t used;
    uint8_t key[2];
    pim_term_init(&term);
    used = pim_term_feed(&term, negotiate, sizeof(negotiate), reply, sizeof(reply));
    CHECK(used == 12);
    CHECK(reply[0] == 255 && reply[1] == 253 && reply[2] == 1);   /* WILL ECHO -> DO ECHO */
    CHECK(reply[3] == 255 && reply[4] == 252 && reply[5] == 24);  /* DO TTYPE -> WONT */
    CHECK(reply[6] == 255 && reply[7] == 251 && reply[8] == 3);   /* DO SGA -> WILL */
    CHECK(reply[9] == 255 && reply[10] == 253 && reply[11] == 3); /* WILL SGA -> DO */
    CHECK(term.remote_echo);
    pim_term_feed(&term, ansi, sizeof(ansi) - 1, reply, sizeof(reply));
    pim_term_view(&term, 4, 0, view, sizeof(view));
    CHECK(strstr(view, "HiRED") != NULL && strstr(view, "A   B") != NULL && strstr(view, "[") == NULL);
    pim_term_init(&term);
    for (int i = 0; i < 100; i++) {
        char line[16];
        snprintf(line, sizeof(line), "L%d\r\n", i);
        pim_term_feed(&term, (const uint8_t *)line, strlen(line), reply, sizeof(reply));
    }
    pim_term_view(&term, 3, 0, view, sizeof(view));
    CHECK(strcmp(view, "L98\nL99\n") == 0);
    pim_term_view(&term, 2, 2, view, sizeof(view));
    CHECK(strcmp(view, "L97\nL98") == 0);
    CHECK(pim_term_max_scroll(&term, 10) == term.count - 10U);
    pim_term_init(&term);
    {
        uint8_t wide[60];
        memset(wide, 'x', sizeof(wide));
        pim_term_feed(&term, wide, sizeof(wide), reply, sizeof(reply));
        pim_term_view(&term, 5, 0, view, sizeof(view));
        CHECK(strlen(view) == 61 && view[40] == '\n');
    }
    pim_term_feed(&term, (const uint8_t *)"abc\b\bZ\x1b[2J", 11, reply, sizeof(reply));
    pim_term_view(&term, 5, 0, view, sizeof(view));
    CHECK(view[0] == '\0');
    CHECK(pim_term_encode('\r', key) == 2 && key[1] == '\n');
    CHECK(pim_term_encode(255, key) == 2 && key[1] == 255);
    CHECK(pim_term_encode('a', key) == 1 && key[0] == 'a');
}

static void test_sound(void) {
    uint16_t hz, ms;
    for (int event = 0; event < PIM_SOUND_COUNT; event++)
        CHECK(pim_sound_tone((pim_sound_event_t)event, &hz, &ms) && hz >= 100 && hz <= 4000 && ms > 0 && ms <= 1000);
    CHECK(!pim_sound_tone(PIM_SOUND_COUNT, &hz, &ms));
}

int main(void) {
    test_calendar(); test_battery_decode(); test_net(); test_web(); test_telnet(); test_sound(); test_date_entry(); test_models_and_storage(); test_games();
    if (failures) { fprintf(stderr, "%d failures\n", failures); return 1; }
    puts("PIM core tests passed"); return 0;
}
