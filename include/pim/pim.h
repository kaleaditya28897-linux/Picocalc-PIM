#ifndef PIM_PIM_H
#define PIM_PIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PIM_MAX_APPOINTMENTS 48
#define PIM_MAX_TODOS 96
#define PIM_MAX_NOTES 40
#define PIM_MAX_JOURNAL_ENTRIES 40

typedef struct { uint16_t year; uint8_t month; uint8_t day; } pim_date_t;
typedef enum { PIM_PRIORITY_LOW, PIM_PRIORITY_NORMAL, PIM_PRIORITY_HIGH } pim_priority_t;
typedef enum { PIM_MOOD_GREAT, PIM_MOOD_GOOD, PIM_MOOD_OKAY, PIM_MOOD_BAD,
               PIM_MOOD_TERRIBLE } pim_mood_t;

typedef struct {
    uint32_t id;
    pim_date_t date;
    uint16_t minutes;
    char title[48];
    char description[120];
    uint16_t duration; /* minutes; 0 means unspecified. Placed last to keep the stored layout. */
} pim_appointment_t;

typedef struct {
    uint32_t id;
    pim_priority_t priority;
    bool completed;
    uint8_t due_year, due_month, due_day; /* year - 2000; all zero means no due date. Uses former padding. */
    uint32_t created_at;
    char title[64];
} pim_todo_t;

typedef struct {
    uint32_t id;
    uint32_t created_at;
    uint32_t modified_at;
    char title[48];
    char content[384];
} pim_note_t;

typedef struct {
    uint32_t id;
    pim_date_t date;
    pim_mood_t mood;
    uint32_t modified_at;
    char content[384];
} pim_journal_entry_t;

typedef struct {
    uint32_t next_id;
    size_t appointment_count, todo_count, note_count, journal_count;
    pim_appointment_t appointments[PIM_MAX_APPOINTMENTS];
    pim_todo_t todos[PIM_MAX_TODOS];
    pim_note_t notes[PIM_MAX_NOTES];
    pim_journal_entry_t journal[PIM_MAX_JOURNAL_ENTRIES];
} pim_database_t;

void pim_database_init(pim_database_t *database);
uint8_t pim_days_in_month(uint16_t year, uint8_t month);
bool pim_date_is_valid(pim_date_t date);
int pim_date_compare(pim_date_t left, pim_date_t right);
uint8_t pim_first_day_of_month(uint16_t year, uint8_t month);
pim_date_t pim_date_shift(pim_date_t date, int days);
/* Moves by whole months (12 per year), keeping the day where possible; the result stays in 2000-2099. */
pim_date_t pim_date_add_months(pim_date_t date, int months);
/* Typed-number entry: appends a digit while the value fits max_value, else restarts at that digit.
 * Set *digits to 0 whenever the user moves to another field. */
uint16_t pim_entry_digit(uint16_t current, uint8_t *digits, unsigned digit, uint16_t max_value);

bool pim_add_appointment(pim_database_t *database, const pim_appointment_t *appointment);
bool pim_delete_appointment(pim_database_t *database, size_t index);
size_t pim_appointments_on(const pim_database_t *database, pim_date_t date, size_t *indices,
                           size_t max_indices);
bool pim_add_todo(pim_database_t *database, const pim_todo_t *todo);
bool pim_delete_todo(pim_database_t *database, size_t index);
void pim_toggle_todo(pim_database_t *database, size_t index);
bool pim_todo_due(const pim_todo_t *todo, pim_date_t *date);
bool pim_todo_set_due(pim_todo_t *todo, pim_date_t date);
void pim_todo_clear_due(pim_todo_t *todo);
/* Open to-dos that are due on or before `date` (overdue included), in list order. */
size_t pim_todos_due_by(const pim_database_t *database, pim_date_t date, size_t *indices, size_t max_indices);
bool pim_add_note(pim_database_t *database, const pim_note_t *note);
bool pim_update_note(pim_database_t *database, size_t index, const pim_note_t *note);
bool pim_delete_note(pim_database_t *database, size_t index);
size_t pim_search_notes(const pim_database_t *database, const char *term,
                        size_t *matches, size_t max_matches);
bool pim_add_or_update_journal(pim_database_t *database, const pim_journal_entry_t *entry);
bool pim_delete_journal(pim_database_t *database, size_t index);
void pim_sort_all(pim_database_t *database);
const char *pim_priority_name(pim_priority_t priority);
const char *pim_mood_name(pim_mood_t mood);

typedef struct {
    uint8_t percent;
    bool charging;
} pim_battery_t;
/* Decodes the keyboard MCU's battery register reply (register id, value); bit 7 of the value is the charging flag. */
bool pim_battery_decode(const uint8_t reply[2], pim_battery_t *battery);

#endif
