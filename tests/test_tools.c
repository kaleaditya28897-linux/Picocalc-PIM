#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "pim/games.h"
#include "pim/sdgames.h"
#include "pim/textfile.h"
#include "pim/tools.h"

static int failures;
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "%s:%d %s\n", __FILE__, __LINE__, #value); failures++; } } while (0)

#define ROOT "tools_root"

static void fresh_root(void) {
    pim_remove_tree(ROOT);
    mkdir(ROOT, 0777);
    mkdir(ROOT "/journal", 0777);
}

static void write_file(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    if (file != NULL) { fputs(text, file); fclose(file); }
}

static size_t read_all(const char *path, char *buffer, size_t size) {
    FILE *file = fopen(path, "rb");
    size_t length = 0;
    if (file != NULL) { length = fread(buffer, 1, size - 1, file); fclose(file); }
    buffer[length] = '\0';
    return length;
}

static bool calc(const char *expression, double *value) {
    char error[40];
    return pim_calc_eval(expression, 7, true, value, error, sizeof(error));
}

static void test_calc(void) {
    double v;
    char error[40], text[40];
    CHECK(calc("2+3*4", &v) && v == 14);
    CHECK(calc("(2+3)*4", &v) && v == 20);
    CHECK(calc("2^3^2", &v) && v == 512);
    CHECK(calc("-2^2", &v) && fabs(v + 4) < 1e-9);
    CHECK(calc("10%4", &v) && v == 2);
    CHECK(calc("sin(30)", &v) && fabs(v - 0.5) < 1e-9);
    CHECK(calc("sqrt(16)+ans", &v) && v == 11);
    CHECK(calc("pi", &v) && fabs(v - 3.14159265358979) < 1e-9);
    CHECK(calc("log(1000)", &v) && fabs(v - 3) < 1e-9);
    CHECK(calc("  2 * ( 3 )  ", &v) && v == 6);
    CHECK(!calc("1/0", &v));
    CHECK(!calc("", &v));
    CHECK(!calc("2+", &v));
    CHECK(!calc("(2", &v));
    CHECK(!calc("2)", &v));
    CHECK(!calc("foo(2)", &v));
    CHECK(!calc("sqrt(-1)", &v));
    CHECK(!calc("ln(0)", &v));
    CHECK(!calc("1 2", &v));
    CHECK(!calc("10%0", &v));
    CHECK(!pim_calc_eval("(((((((((((((((((((((((((((((1)))))))))))))))))))))))))))))", 0, false, &v, error, sizeof(error)));
    CHECK(error[0] != '\0');
    CHECK(pim_calc_eval("sin(pi/2)", 0, false, &v, error, sizeof(error)) && fabs(v - 1) < 1e-9);
    pim_calc_format(0.1 + 0.2, text, sizeof(text)); CHECK(strcmp(text, "0.3") == 0);
    pim_calc_format(1234567, text, sizeof(text)); CHECK(strcmp(text, "1234567") == 0);
    pim_calc_format(-2.5, text, sizeof(text)); CHECK(strcmp(text, "-2.5") == 0);
    pim_calc_format(1e30, text, sizeof(text)); CHECK(strlen(text) < 20);
}

static void test_clock_and_recur(void) {
    char text[24];
    pim_recur_t item = {{2024, 1, 31}, 540, 'M', "Pay rent"}, list[PIM_RECUR_MAX];
    pim_clock_format(61234, true, text, sizeof(text)); CHECK(strcmp(text, "01:01.2") == 0);
    pim_clock_format(3723000, false, text, sizeof(text)); CHECK(strcmp(text, "1:02:03") == 0);
    CHECK(pim_day_number((pim_date_t){2000, 1, 2}) - pim_day_number((pim_date_t){2000, 1, 1}) == 1);
    CHECK(pim_day_number((pim_date_t){2025, 1, 1}) - pim_day_number((pim_date_t){2024, 1, 1}) == 366);
    CHECK(pim_recur_occurs(&item, (pim_date_t){2024, 1, 31}));
    CHECK(!pim_recur_occurs(&item, (pim_date_t){2024, 1, 30}));
    CHECK(pim_recur_occurs(&item, (pim_date_t){2024, 2, 29}));
    CHECK(!pim_recur_occurs(&item, (pim_date_t){2024, 2, 28}));
    CHECK(pim_recur_occurs(&item, (pim_date_t){2024, 4, 30}));
    CHECK(pim_recur_occurs(&item, (pim_date_t){2024, 5, 31}));
    item.freq = 'D';
    CHECK(pim_recur_occurs(&item, (pim_date_t){2030, 6, 1}));
    CHECK(!pim_recur_occurs(&item, (pim_date_t){2024, 1, 30}));
    item.freq = 'W';
    CHECK(pim_recur_occurs(&item, (pim_date_t){2024, 2, 7}));
    CHECK(!pim_recur_occurs(&item, (pim_date_t){2024, 2, 8}));
    item.freq = 'Y'; item.start = (pim_date_t){2024, 2, 29};
    CHECK(pim_recur_occurs(&item, (pim_date_t){2028, 2, 29}));
    CHECK(pim_recur_occurs(&item, (pim_date_t){2025, 2, 28}));
    CHECK(!pim_recur_occurs(&item, (pim_date_t){2025, 3, 1}));
    item.freq = 'X';
    CHECK(!pim_recur_occurs(&item, (pim_date_t){2024, 2, 29}));

    fresh_root();
    CHECK(pim_recur_load(ROOT, list, PIM_RECUR_MAX) == 0);
    item = (pim_recur_t){{2024, 3, 1}, 600, 'W', "Standup"};
    list[0] = item;
    list[1] = (pim_recur_t){{2024, 3, 2}, 5, 'D', "Pills with spaces  "};
    CHECK(pim_recur_save(ROOT, list, 2));
    memset(list, 0, sizeof(list));
    CHECK(pim_recur_load(ROOT, list, PIM_RECUR_MAX) == 2);
    CHECK(strcmp(list[0].title, "Standup") == 0 && list[0].minutes == 600 && list[0].freq == 'W' && list[0].start.day == 1);
    CHECK(list[1].freq == 'D' && list[1].minutes == 5);
    write_file(ROOT "/recurring.cfg", "garbage\n2024-13-45 99:99 D bad\n2024-03-01 10:00 Q bad\n2024-03-01 10:00 D ok\n");
    CHECK(pim_recur_load(ROOT, list, PIM_RECUR_MAX) == 1 && strcmp(list[0].title, "ok") == 0);
}

static void test_textfile(void) {
    char path[] = ROOT "/big.txt", page[PIM_PAGE_BYTES], *big;
    size_t length, total = 30000, pages = 0;
    uint32_t size, offset = 0;
    bool more = false;
    fresh_root();
    big = malloc(total + 1);
    for (size_t i = 0; i < total; i++) big[i] = (i % 50 == 49) ? '\n' : (char)('a' + i % 26);
    big[total] = '\0';
    write_file(path, big);
    CHECK(pim_text_size(path, &size) && size == total);
    do {
        CHECK(pim_text_page(path, offset, page, sizeof(page), &length, &more));
        CHECK(length > 0 && length < PIM_PAGE_BYTES && strlen(page) == length);
        CHECK(memcmp(page, big + offset, length) == 0);
        CHECK(!more || page[length - 1] == '\n');
        offset += (uint32_t)length; pages++;
    } while (more && pages < 100);
    CHECK(offset == total && pages >= 8);
    /* walk backwards and match forwards boundaries */
    while (offset > 0) {
        uint32_t before = pim_text_previous(path, offset);
        CHECK(before < offset);
        CHECK(offset - before < PIM_PAGE_BYTES && (before == 0 || (pim_text_page(path, before - 1, page, sizeof(page), &length, &more) && page[0] != '\0')));
        offset = before;
    }
    /* edit in the middle of a later page */
    CHECK(pim_text_replace(path, 10000, 5, "HELLO WORLD", 11));
    CHECK(pim_text_size(path, &size) && size == total + 6);
    CHECK(pim_text_page(path, 10000, page, sizeof(page), &length, &more) && memcmp(page, "HELLO WORLD", 11) == 0);
    CHECK(pim_text_page(path, 9990, page, sizeof(page), &length, &more) && memcmp(page, big + 9990, 10) == 0);
    /* head, delete, append */
    CHECK(pim_text_replace(path, 0, 0, "TOP\n", 4));
    CHECK(pim_text_page(path, 0, page, sizeof(page), &length, &more) && memcmp(page, "TOP\n", 4) == 0);
    CHECK(pim_text_replace(path, 0, 4, "", 0));
    CHECK(pim_text_size(path, &size) && size == total + 6);
    CHECK(pim_text_replace(path, size, 0, "END", 3));
    CHECK(pim_text_size(path, &size) && size == total + 9);
    free(big);
    /* new file, empty file, NUL and UTF-8 handling */
    CHECK(pim_text_replace(ROOT "/new.txt", 0, 0, "hi", 2));
    CHECK(pim_text_page(ROOT "/new.txt", 0, page, sizeof(page), &length, &more) && length == 2 && !more && strcmp(page, "hi") == 0);
    write_file(ROOT "/empty.txt", "");
    CHECK(pim_text_page(ROOT "/empty.txt", 0, page, sizeof(page), &length, &more) && length == 0 && !more);
    CHECK(!pim_text_page(ROOT "/missing.txt", 0, page, sizeof(page), &length, &more));
    {
        FILE *file = fopen(ROOT "/nul.txt", "wb");
        fwrite("a\0b", 1, 3, file); fclose(file);
        CHECK(pim_text_page(ROOT "/nul.txt", 0, page, sizeof(page), &length, &more) && length == 3 && strcmp(page, "a b") == 0);
    }
    {
        char utf[5000];
        memset(utf, 'x', sizeof(utf)); utf[4094] = (char)0xc3; utf[4095] = (char)0xa9; utf[4999] = '\0';
        write_file(ROOT "/utf.txt", utf);
        CHECK(pim_text_page(ROOT "/utf.txt", 0, page, sizeof(page), &length, &more) && more && (unsigned char)page[length - 1] != 0xc3);
    }
    CHECK(!pim_text_replace(ROOT "/nodir/x.txt", 0, 0, "a", 1));
    CHECK(!pim_text_replace(path, size + 100, 0, "a", 1));
}

static void test_journal(void) {
    pim_date_t dates[10], d1 = {2024, 5, 1}, d2 = {2025, 1, 15}, d3 = {2024, 12, 31};
    pim_database_t *db = calloc(1, sizeof(*db));
    pim_journal_entry_t entry = {0};
    char path[96], preview[40];
    size_t total = 0, count;
    fresh_root();
    CHECK(pim_journal_path(ROOT, d1, path, sizeof(path)) && strcmp(path, ROOT "/journal/01-05-2024.md") == 0);
    CHECK(!pim_journal_exists(ROOT, d1));
    pim_text_replace(path, 0, 0, "# Hello\nsecond line", 19);
    CHECK(pim_journal_exists(ROOT, d1));
    pim_journal_path(ROOT, d2, path, sizeof(path)); pim_text_replace(path, 0, 0, "newer", 5);
    pim_journal_path(ROOT, d3, path, sizeof(path)); pim_text_replace(path, 0, 0, "mid", 3);
    write_file(ROOT "/journal/notes.md", "ignored");
    count = pim_journal_list(ROOT, dates, 10, &total);
    CHECK(count == 3 && total == 3);
    CHECK(dates[0].year == 2025 && dates[1].year == 2024 && dates[1].month == 12 && dates[2].month == 5);
    count = pim_journal_list(ROOT, dates, 2, &total);
    CHECK(count == 2 && total == 3);
    pim_journal_preview(ROOT, d1, preview, sizeof(preview)); CHECK(strcmp(preview, "Hello") == 0);
    CHECK(pim_journal_delete(ROOT, d2) && !pim_journal_exists(ROOT, d2));
    CHECK(!pim_journal_delete(ROOT, d2));

    pim_database_init(db);
    entry.date = (pim_date_t){2023, 2, 3}; entry.mood = PIM_MOOD_GOOD; snprintf(entry.content, sizeof(entry.content), "old text");
    CHECK(pim_add_or_update_journal(db, &entry));
    entry.date = d1; snprintf(entry.content, sizeof(entry.content), "conflict");
    CHECK(pim_add_or_update_journal(db, &entry));
    CHECK(pim_journal_migrate(ROOT, db) && db->journal_count == 0);
    pim_journal_path(ROOT, (pim_date_t){2023, 2, 3}, path, sizeof(path));
    {
        char text[200]; read_all(path, text, sizeof(text));
        CHECK(strstr(text, "Mood: ") != NULL && strstr(text, "old text") != NULL);
    }
    pim_journal_path(ROOT, d1, path, sizeof(path));
    {
        char text[200]; read_all(path, text, sizeof(text));
        CHECK(strstr(text, "conflict") == NULL && strstr(text, "second line") != NULL);
    }
    CHECK(!pim_journal_migrate(ROOT, db));
    free(db);
}

static void test_search(void) {
    pim_database_t *db = calloc(1, sizeof(*db));
    pim_hit_t hits[16];
    pim_appointment_t appointment = {.date = {2024, 1, 2}, .minutes = 60};
    pim_todo_t todo = {0};
    pim_note_t note = {0};
    size_t count;
    fresh_root();
    mkdir(ROOT "/notes", 0777); mkdir(ROOT "/code", 0777);
    pim_database_init(db);
    snprintf(appointment.title, sizeof(appointment.title), "Dentist visit"); pim_add_appointment(db, &appointment);
    snprintf(todo.title, sizeof(todo.title), "Buy DENTAL floss"); pim_add_todo(db, &todo);
    snprintf(note.title, sizeof(note.title), "Shopping"); snprintf(note.content, sizeof(note.content), "remember the dentist"); pim_add_note(db, &note);
    write_file(ROOT "/journal/01-01-2024.md", "went to the Dentist today\nDentist again");
    write_file(ROOT "/ideas.txt", "no match here");
    write_file(ROOT "/code/a.c", "// dentist.c\n");
    write_file(ROOT "/pim.dat", "dentist");
    write_file(ROOT "/wifi.cfg", "dentist");
    count = pim_search(ROOT, db, "dentist", hits, 16);
    CHECK(count == 4);
    {
        int kinds[5] = {0};
        for (size_t i = 0; i < count; i++) kinds[hits[i].kind]++;
        CHECK(kinds[PIM_HIT_APPOINTMENT] == 1 && kinds[PIM_HIT_NOTE] == 1 && kinds[PIM_HIT_JOURNAL] == 1 && kinds[PIM_HIT_FILE] == 1);
    }
    CHECK(pim_search(ROOT, db, "dent", hits, 2) == 2);
    CHECK(pim_search(ROOT, db, "zzzz", hits, 16) == 0);
    CHECK(pim_search(ROOT, db, "", hits, 16) == 0);
    free(db);
}

static bool exists(const char *path) { struct stat s; return stat(path, &s) == 0; }

static void test_backup(void) {
    char names[8][PIM_BACKUP_NAME], text[64];
    fresh_root();
    write_file(ROOT "/pim.dat", "database-v1");
    write_file(ROOT "/sound.cfg", "1");
    mkdir(ROOT "/notes", 0777); write_file(ROOT "/notes/export.txt", "derived");
    mkdir(ROOT "/games", 0777); mkdir(ROOT "/games/dungeon", 0777); write_file(ROOT "/games/dungeon/scores.txt", "10\n");
    write_file(ROOT "/journal/01-01-2024.md", "entry");
    CHECK(pim_backup_create(ROOT, "2024-01-01_0900"));
    CHECK(exists(ROOT "/backup/2024-01-01_0900/pim.dat") && exists(ROOT "/backup/2024-01-01_0900/journal/01-01-2024.md"));
    CHECK(exists(ROOT "/backup/2024-01-01_0900/games/dungeon/scores.txt"));
    CHECK(!exists(ROOT "/backup/2024-01-01_0900/notes") && !exists(ROOT "/backup/2024-01-01_0900/backup"));
    CHECK(!pim_backup_create(ROOT, "../evil") && !pim_backup_create(ROOT, "") && !pim_backup_create(ROOT, "a/b"));
    write_file(ROOT "/pim.dat", "database-v2");
    remove(ROOT "/journal/01-01-2024.md");
    CHECK(pim_backup_restore(ROOT, "2024-01-01_0900"));
    read_all(ROOT "/pim.dat", text, sizeof(text)); CHECK(strcmp(text, "database-v1") == 0);
    CHECK(exists(ROOT "/journal/01-01-2024.md"));
    CHECK(!pim_backup_restore(ROOT, "1999-01-01_0000"));
    for (int i = 2; i <= 8; i++) { char name[32]; snprintf(name, sizeof(name), "2024-01-0%d_0900", i); CHECK(pim_backup_create(ROOT, name)); }
    CHECK(pim_backup_list(ROOT, names, 8) == PIM_BACKUP_KEEP);
    CHECK(strcmp(names[0], "2024-01-08_0900") == 0 && strcmp(names[PIM_BACKUP_KEEP - 1], "2024-01-04_0900") == 0);
    CHECK(!exists(ROOT "/backup/2024-01-01_0900"));
    CHECK(pim_remove_tree(ROOT "/backup") && !exists(ROOT "/backup"));
}

static void test_games(void) {
    char text[300];
    uint32_t scores[PIM_SCORE_SLOTS];
    long value = 0;
    fresh_root();
    pim_games_set_root(NULL);
    CHECK(pim_scores_add("tetris", 100) == 0 && !pim_game_floor_text(1, text, sizeof(text)));
    pim_games_set_root(ROOT);
    CHECK(pim_games_prepare(ROOT) && pim_games_prepare(ROOT));
    CHECK(exists(ROOT "/games/dungeon/floor5.txt") && exists(ROOT "/games/README.txt"));
    CHECK(pim_game_config("dungeon", "floors", &value) && value == 5);
    CHECK(pim_game_config("dungeon", "health", &value) && value == 20);
    CHECK(!pim_game_config("dungeon", "nothing", &value));
    for (unsigned floor = 1; floor <= 5; floor++) CHECK(pim_game_floor_text(floor, text, sizeof(text)) && strlen(text) == 12 * 17);
    write_file(ROOT "/games/dungeon/floor1.txt", "short\n");
    CHECK(!pim_game_floor_text(1, text, sizeof(text)));
    CHECK(pim_scores_load("tetris", scores, PIM_SCORE_SLOTS) == 0);
    CHECK(pim_scores_add("tetris", 0) == 0);
    CHECK(pim_scores_add("tetris", 50) == 1);
    CHECK(pim_scores_add("tetris", 80) == 1);
    CHECK(pim_scores_add("tetris", 60) == 2);
    CHECK(pim_scores_add("tetris", 10) == 4 && pim_scores_add("tetris", 20) == 4 && pim_scores_add("tetris", 5) == 0);
    CHECK(pim_scores_load("tetris", scores, PIM_SCORE_SLOTS) == 5 && scores[0] == 80 && scores[4] == 10);
    write_file(ROOT "/games/tetris/scores.txt", "5\n99\njunk\n");
    CHECK(pim_scores_load("tetris", scores, PIM_SCORE_SLOTS) == 2 && scores[0] == 99);
    remove(ROOT "/games/dungeon/floor1.txt");
    /* dungeon floors come from the files */
    CHECK(pim_games_prepare(ROOT));
    {
        pim_dungeon_t d;
        int enemies;
        for (unsigned floor = 1; floor <= 5; floor++) { char t[300]; CHECK(pim_game_floor_text(floor, t, sizeof(t))); }
        write_file(ROOT "/games/dungeon/game.cfg", "floors=2\nhealth=33\n");
        pim_dungeon_reset(&d, 5);
        CHECK(d.health == 33 && d.floors == 2 && d.player_x == 1 && d.player_y == 1 && d.cells[10][14] == '>');
        enemies = d.enemies;
        CHECK(enemies >= 3);
        for (int y = 0; y < PIM_DUNGEON_HEIGHT; y++) for (int x = 0; x < PIM_DUNGEON_WIDTH; x++) CHECK(d.cells[y][x] != 'M' && d.cells[y][x] != '@');
        d.player_x = 13; d.player_y = 10;
        pim_dungeon_move(&d, PIM_RIGHT);
        CHECK(d.floor == 2 && !d.won);
        d.cells[10][14] = '>'; d.player_x = 13; d.player_y = 10; d.enemies = 0; memset(d.enemy, 0, sizeof(d.enemy));
        pim_dungeon_move(&d, PIM_RIGHT);
        CHECK(d.won);
        /* invalid files fall back to random floors */
        write_file(ROOT "/games/dungeon/game.cfg", "floors=99\nhealth=1\n");
        write_file(ROOT "/games/dungeon/floor1.txt", "################\n#@#............#\n#.#............#\n#.#............#\n#.#............#\n#.#............#\n#.#............#\n#.#............#\n#.#............#\n#.#...........>#\n#.#............#\n################\n");
        pim_dungeon_reset(&d, 1);
        CHECK(d.health == 20 && d.floors == 5);
        CHECK(d.player_x == 1 && d.player_y == 1);
    }
    pim_games_set_root(NULL);
}


int main(void) {
    test_calc();
    test_clock_and_recur();
    test_textfile();
    test_journal();
    test_search();
    test_backup();
    test_games();
    pim_remove_tree(ROOT);
    if (failures == 0) puts("tools tests passed");
    return failures == 0 ? 0 : 1;
}
