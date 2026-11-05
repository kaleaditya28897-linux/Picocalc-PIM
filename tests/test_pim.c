#include "pim/games.h"
#include "pim/pim.h"
#include "pim/storage.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "%s:%d %s\n", __FILE__, __LINE__, #value); failures++; } } while (0)

static void test_calendar(void) {
    CHECK(pim_days_in_month(2024, 2) == 29);
    CHECK(pim_days_in_month(2023, 2) == 28);
    CHECK(pim_first_day_of_month(2024, 1) == 0);
    CHECK(pim_date_is_valid((pim_date_t){2024, 2, 29}));
}
static void test_models_and_storage(void) {
    pim_database_t source, loaded;
    pim_appointment_t appointment = {.date = {2026, 6, 20}, .minutes = 540, .title = "Planning"};
    pim_todo_t todo = {.priority = PIM_PRIORITY_HIGH, .title = "Ship"};
    pim_note_t note = {.title = "C ideas", .content = "Use interfaces", .modified_at = 1};
    pim_journal_entry_t journal = {.date = {2026, 6, 20}, .mood = PIM_MOOD_GOOD, .content = "PDA"};
    size_t matches[1];
    pim_database_init(&source);
    CHECK(pim_add_appointment(&source, &appointment));
    CHECK(pim_add_todo(&source, &todo));
    pim_toggle_todo(&source, 0);
    CHECK(source.todos[0].completed);
    CHECK(pim_add_note(&source, &note));
    CHECK(pim_search_notes(&source, "INTERFACES", matches, 1) == 1 && matches[0] == 0);
    CHECK(pim_add_or_update_journal(&source, &journal));
    CHECK(pim_storage_save("pim-test.dat", &source));
    CHECK(pim_storage_load("pim-test.dat", &loaded));
    CHECK(loaded.appointment_count == 1 && loaded.note_count == 1);
    remove("pim-test.dat");
}
static void test_games(void) {
    pim_tetris_t tetris; pim_dungeon_t dungeon; pim_invaders_t invaders;
    pim_tetris_reset(&tetris, 1);
    CHECK(pim_tetris_active_cell(&tetris, 4, 0));
    CHECK(tetris.level == 1 && pim_tetris_move(&tetris, 0, 1));
    pim_tetris_rotate(&tetris); pim_tetris_drop(&tetris);
    pim_dungeon_reset(&dungeon, 10);
    CHECK(dungeon.health == 10 && dungeon.floor == 1);
    pim_invaders_reset(&invaders);
    pim_invaders_fire(&invaders); pim_invaders_tick(&invaders);
    CHECK(invaders.bullet_active || invaders.score > 0);
}
int main(void) {
    test_calendar(); test_models_and_storage(); test_games();
    if (failures) { fprintf(stderr, "%d failures\n", failures); return 1; }
    puts("PIM core tests passed"); return 0;
}
