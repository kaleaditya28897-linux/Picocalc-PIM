#include "pim/storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIM_STORAGE_MAGIC 0x50494D31U

typedef struct {
    uint32_t magic, size, crc;
} header_t;

static uint32_t checksum(const void *data, size_t length) {
    const unsigned char *bytes = data;
    uint32_t value = 0xffffffffU;
    for (size_t i = 0; i < length; i++) {
        value ^= bytes[i];
        for (unsigned int bit = 0; bit < 8; bit++)
            value = (value >> 1) ^ (0xedb88320U & (-(int32_t)(value & 1U)));
    }
    return ~value;
}

bool pim_storage_load(const char *path, pim_database_t *database) {
    header_t header;
    pim_database_t *loaded = malloc(sizeof(*loaded));
    FILE *file = fopen(path, "rb");
    if (loaded == NULL || file == NULL) {
        if (file != NULL) fclose(file);
        free(loaded);
        pim_database_init(database);
        return false;
    }
    if (fread(&header, sizeof(header), 1, file) != 1 || header.magic != PIM_STORAGE_MAGIC ||
        header.size != sizeof(*loaded) || fread(loaded, sizeof(*loaded), 1, file) != 1 ||
        fgetc(file) != EOF || header.crc != checksum(loaded, sizeof(*loaded))) {
        fclose(file);
        free(loaded);
        pim_database_init(database);
        return false;
    }
    fclose(file);
    *database = *loaded;
    free(loaded);
    return true;
}

bool pim_storage_save(const char *path, const pim_database_t *database) {
    char temporary[128];
    header_t header = {PIM_STORAGE_MAGIC, sizeof(*database), checksum(database, sizeof(*database))};
    int result = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (result < 0 || (size_t)result >= sizeof(temporary)) return false;
    FILE *file = fopen(temporary, "wb");
    if (file == NULL) return false;
    bool written = fwrite(&header, sizeof(header), 1, file) == 1 &&
                   fwrite(database, sizeof(*database), 1, file) == 1;
    if (fclose(file) != 0 || !written) {
        remove(temporary);
        return false;
    }
    if (rename(temporary, path) != 0) { remove(temporary); return false; }
    return true;
}

static bool csv_field(FILE *file, const char *value) {
    if (fputc('"', file) == EOF) return false;
    for (; *value; value++) {
        if (*value == '"' && fputc('"', file) == EOF) return false;
        if (fputc(*value, file) == EOF) return false;
    }
    return fputc('"', file) != EOF;
}

static bool write_appointments(const char *root, const pim_database_t *database) {
    char path[128];
    FILE *file;
    if (snprintf(path, sizeof(path), "%s/appointments/appointments.csv", root) >= (int)sizeof(path)) return false;
    file = fopen(path, "wb");
    if (file == NULL || fputs("date,time,duration_minutes,title,description\n", file) == EOF) goto failed;
    for (size_t i = 0; i < database->appointment_count; i++) {
        const pim_appointment_t *value = &database->appointments[i];
        if (fprintf(file, "%02u-%02u-%04u,%02u:%02u,%u,", value->date.day, value->date.month,
                    value->date.year, value->minutes / 60, value->minutes % 60, value->duration) < 0 ||
            !csv_field(file, value->title) || fputc(',', file) == EOF ||
            !csv_field(file, value->description) || fputc('\n', file) == EOF) goto failed;
    }
    return fclose(file) == 0;
failed:
    if (file != NULL) fclose(file);
    return false;
}

static bool write_todos(const char *root, const pim_database_t *database) {
    char path[128];
    FILE *file;
    if (snprintf(path, sizeof(path), "%s/todos/todos.csv", root) >= (int)sizeof(path)) return false;
    file = fopen(path, "wb");
    if (file == NULL || fputs("id,status,priority,title\n", file) == EOF) goto failed;
    for (size_t i = 0; i < database->todo_count; i++) {
        const pim_todo_t *value = &database->todos[i];
        if (fprintf(file, "%lu,%s,%s,", (unsigned long)value->id,
                    value->completed ? "done" : "open", pim_priority_name(value->priority)) < 0 ||
            !csv_field(file, value->title) || fputc('\n', file) == EOF) goto failed;
    }
    return fclose(file) == 0;
failed:
    if (file != NULL) fclose(file);
    return false;
}

static bool write_notes(const char *root, const pim_database_t *database) {
    for (size_t i = 0; i < database->note_count; i++) {
        char path[128];
        const pim_note_t *value = &database->notes[i];
        FILE *file;
        if (snprintf(path, sizeof(path), "%s/notes/note-%08lu.md", root,
                     (unsigned long)value->id) >= (int)sizeof(path)) return false;
        file = fopen(path, "wb");
        if (file == NULL) return false;
        bool written = fprintf(file, "# %s\n\n%s\n", value->title, value->content) >= 0;
        if (fclose(file) != 0 || !written) return false;
    }
    return true;
}

static bool write_journal(const char *root, const pim_database_t *database) {
    for (size_t i = 0; i < database->journal_count; i++) {
        char path[128];
        const pim_journal_entry_t *value = &database->journal[i];
        FILE *file;
        if (snprintf(path, sizeof(path), "%s/journal/%02u-%02u-%04u.md", root, value->date.day,
                     value->date.month, value->date.year) >= (int)sizeof(path)) return false;
        file = fopen(path, "wb");
        if (file == NULL) return false;
        bool written = fprintf(file, "# %02u-%02u-%04u\n\nMood: %s\n\n%s\n", value->date.day,
                               value->date.month, value->date.year, pim_mood_name(value->mood),
                               value->content) >= 0;
        if (fclose(file) != 0 || !written) return false;
    }
    return true;
}

static bool write_calendar(const char *root, const pim_database_t *database) {
    char path[128];
    FILE *file;
    if (snprintf(path, sizeof(path), "%s/calendar/calendar.md", root) >= (int)sizeof(path)) return false;
    file = fopen(path, "wb");
    if (file == NULL || fputs("# Calendar\n\n", file) == EOF) goto failed;
    for (size_t i = 0; i < database->appointment_count; i++) {
        const pim_appointment_t *value = &database->appointments[i];
        unsigned int end = value->minutes + value->duration;
        if (value->duration > 0
                ? fprintf(file, "- %02u-%02u-%04u %02u:%02u-%02u:%02u - %s\n", value->date.day,
                          value->date.month, value->date.year, value->minutes / 60, value->minutes % 60,
                          end / 60, end % 60, value->title) < 0
                : fprintf(file, "- %02u-%02u-%04u %02u:%02u - %s\n", value->date.day, value->date.month,
                          value->date.year, value->minutes / 60, value->minutes % 60, value->title) < 0) goto failed;
    }
    return fclose(file) == 0;
failed:
    if (file != NULL) fclose(file);
    return false;
}

bool pim_storage_export(const char *root, const pim_database_t *database) {
    return root != NULL && database != NULL && write_appointments(root, database) &&
           write_todos(root, database) && write_notes(root, database) &&
           write_journal(root, database) && write_calendar(root, database);
}
