#include "pim/pim.h"

#include <ctype.h>
#include <string.h>

static void remove_at(void *items, size_t item_size, size_t count, size_t index) {
    if (index + 1 < count) {
        memmove((char *)items + item_size * index, (char *)items + item_size * (index + 1),
                item_size * (count - index - 1));
    }
}

static uint32_t make_id(pim_database_t *database) {
    uint32_t id = database->next_id++;
    if (database->next_id == 0) database->next_id = 1;
    return id;
}

static bool contains_casefold(const char *text, const char *term) {
    if (*term == '\0') return true;
    for (; *text; text++) {
        size_t index = 0;
        while (term[index] && text[index] &&
               tolower((unsigned char)text[index]) == tolower((unsigned char)term[index])) {
            index++;
        }
        if (!term[index]) return true;
    }
    return false;
}

void pim_database_init(pim_database_t *database) {
    memset(database, 0, sizeof(*database));
    database->next_id = 1;
}

static void sort_appointments(pim_database_t *database) {
    for (size_t i = 1; i < database->appointment_count; i++) {
        pim_appointment_t value = database->appointments[i];
        size_t j = i;
        while (j > 0) {
            pim_appointment_t *previous = &database->appointments[j - 1];
            int date = pim_date_compare(previous->date, value.date);
            if (date < 0 || (date == 0 && previous->minutes <= value.minutes)) break;
            database->appointments[j] = *previous;
            j--;
        }
        database->appointments[j] = value;
    }
}

static void sort_todos(pim_database_t *database) {
    for (size_t i = 1; i < database->todo_count; i++) {
        pim_todo_t value = database->todos[i];
        size_t j = i;
        while (j > 0) {
            pim_todo_t *previous = &database->todos[j - 1];
            if (previous->completed < value.completed ||
                (previous->completed == value.completed &&
                 (previous->priority > value.priority ||
                  (previous->priority == value.priority &&
                   previous->created_at <= value.created_at)))) break;
            database->todos[j] = *previous;
            j--;
        }
        database->todos[j] = value;
    }
}

static void sort_notes(pim_database_t *database) {
    for (size_t i = 1; i < database->note_count; i++) {
        pim_note_t value = database->notes[i];
        size_t j = i;
        while (j > 0 && database->notes[j - 1].modified_at < value.modified_at) {
            database->notes[j] = database->notes[j - 1];
            j--;
        }
        database->notes[j] = value;
    }
}

static void sort_journal(pim_database_t *database) {
    for (size_t i = 1; i < database->journal_count; i++) {
        pim_journal_entry_t value = database->journal[i];
        size_t j = i;
        while (j > 0 && pim_date_compare(database->journal[j - 1].date, value.date) < 0) {
            database->journal[j] = database->journal[j - 1];
            j--;
        }
        database->journal[j] = value;
    }
}

void pim_sort_all(pim_database_t *database) {
    sort_appointments(database);
    sort_todos(database);
    sort_notes(database);
    sort_journal(database);
}

bool pim_add_appointment(pim_database_t *database, const pim_appointment_t *appointment) {
    if (database->appointment_count == PIM_MAX_APPOINTMENTS || !pim_date_is_valid(appointment->date) ||
        appointment->minutes >= 24 * 60 || appointment->minutes + appointment->duration > 24 * 60 ||
        appointment->title[0] == '\0') return false;
    database->appointments[database->appointment_count] = *appointment;
    database->appointments[database->appointment_count++].id = make_id(database);
    sort_appointments(database);
    return true;
}

bool pim_delete_appointment(pim_database_t *database, size_t index) {
    if (index >= database->appointment_count) return false;
    remove_at(database->appointments, sizeof(database->appointments[0]),
              database->appointment_count--, index);
    return true;
}

size_t pim_appointments_on(const pim_database_t *database, pim_date_t date, size_t *indices,
                           size_t max_indices) {
    size_t count = 0;
    for (size_t i = 0; i < database->appointment_count; i++) {
        if (pim_date_compare(database->appointments[i].date, date) == 0) {
            if (count < max_indices) indices[count] = i;
            count++;
        }
    }
    return count;
}

bool pim_add_todo(pim_database_t *database, const pim_todo_t *todo) {
    if (database->todo_count == PIM_MAX_TODOS || todo->priority > PIM_PRIORITY_HIGH ||
        todo->title[0] == '\0') return false;
    database->todos[database->todo_count] = *todo;
    database->todos[database->todo_count++].id = make_id(database);
    sort_todos(database);
    return true;
}

bool pim_delete_todo(pim_database_t *database, size_t index) {
    if (index >= database->todo_count) return false;
    remove_at(database->todos, sizeof(database->todos[0]), database->todo_count--, index);
    return true;
}

void pim_toggle_todo(pim_database_t *database, size_t index) {
    if (index < database->todo_count) {
        database->todos[index].completed = !database->todos[index].completed;
        sort_todos(database);
    }
}

bool pim_add_note(pim_database_t *database, const pim_note_t *note) {
    if (database->note_count == PIM_MAX_NOTES || note->title[0] == '\0') return false;
    database->notes[database->note_count] = *note;
    database->notes[database->note_count++].id = make_id(database);
    sort_notes(database);
    return true;
}

bool pim_update_note(pim_database_t *database, size_t index, const pim_note_t *note) {
    uint32_t id;
    if (index >= database->note_count || note->title[0] == '\0') return false;
    id = database->notes[index].id;
    database->notes[index] = *note;
    database->notes[index].id = id;
    sort_notes(database);
    return true;
}

bool pim_delete_note(pim_database_t *database, size_t index) {
    if (index >= database->note_count) return false;
    remove_at(database->notes, sizeof(database->notes[0]), database->note_count--, index);
    return true;
}

size_t pim_search_notes(const pim_database_t *database, const char *term, size_t *matches,
                        size_t max_matches) {
    size_t count = 0;
    for (size_t i = 0; i < database->note_count; i++) {
        if (contains_casefold(database->notes[i].title, term) ||
            contains_casefold(database->notes[i].content, term)) {
            if (count < max_matches) matches[count] = i;
            count++;
        }
    }
    return count;
}

bool pim_add_or_update_journal(pim_database_t *database, const pim_journal_entry_t *entry) {
    if (!pim_date_is_valid(entry->date) || entry->mood > PIM_MOOD_TERRIBLE ||
        entry->content[0] == '\0') return false;
    for (size_t i = 0; i < database->journal_count; i++) {
        if (pim_date_compare(database->journal[i].date, entry->date) == 0) {
            uint32_t id = database->journal[i].id;
            database->journal[i] = *entry;
            database->journal[i].id = id;
            sort_journal(database);
            return true;
        }
    }
    if (database->journal_count == PIM_MAX_JOURNAL_ENTRIES) return false;
    database->journal[database->journal_count] = *entry;
    database->journal[database->journal_count++].id = make_id(database);
    sort_journal(database);
    return true;
}

bool pim_delete_journal(pim_database_t *database, size_t index) {
    if (index >= database->journal_count) return false;
    remove_at(database->journal, sizeof(database->journal[0]), database->journal_count--, index);
    return true;
}

const char *pim_priority_name(pim_priority_t priority) {
    static const char *const names[] = {"Low", "Normal", "High"};
    return priority <= PIM_PRIORITY_HIGH ? names[priority] : "Unknown";
}

const char *pim_mood_name(pim_mood_t mood) {
    static const char *const names[] = {"Great", "Good", "Okay", "Bad", "Terrible"};
    return mood <= PIM_MOOD_TERRIBLE ? names[mood] : "Unknown";
}

bool pim_battery_decode(const uint8_t reply[2], pim_battery_t *battery) {
    uint8_t percent;
    if (reply == NULL || battery == NULL || reply[0] != 0x0b) return false;
    percent = reply[1] & 0x7f;
    if (percent == 0 || percent > 100) return false;
    battery->percent = percent;
    battery->charging = (reply[1] & 0x80) != 0;
    return true;
}
