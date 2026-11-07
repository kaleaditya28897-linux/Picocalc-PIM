#include "pim/textfile.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef PIM_VFS_DIRENT
#include <sys/dirent.h>
extern DIR *opendir(const char *path);
extern struct dirent *readdir(DIR *directory);
extern int closedir(DIR *directory);
#else
#include <dirent.h>
#endif

bool pim_text_size(const char *path, uint32_t *size) {
    long end;
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;
    if (fseek(file, 0, SEEK_END) != 0 || (end = ftell(file)) < 0) { fclose(file); return false; }
    fclose(file);
    *size = (uint32_t)end;
    return true;
}

static bool continuation(char c) { return ((unsigned char)c & 0xc0) == 0x80; }

bool pim_text_page(const char *path, uint32_t offset, char *buffer, size_t capacity, size_t *length, bool *more) {
    FILE *file;
    size_t got;
    int next;
    if (capacity < 2) return false;
    file = fopen(path, "rb");
    if (file == NULL) return false;
    if (fseek(file, (long)offset, SEEK_SET) != 0) { fclose(file); return false; }
    got = fread(buffer, 1, capacity - 1, file);
    *more = false;
    next = got == capacity - 1 ? fgetc(file) : EOF;
    if (next != EOF) {
        size_t cut = got;
        *more = true;
        while (cut > 0 && buffer[cut - 1] != '\n') cut--;
        if (cut == 0) {
            cut = got;
            while (cut > 1 && continuation(cut < got ? buffer[cut] : (char)next)) cut--;
        }
        got = cut;
    }
    fclose(file);
    for (size_t i = 0; i < got; i++) if (buffer[i] == '\0') buffer[i] = ' ';
    buffer[got] = '\0';
    *length = got;
    return true;
}

uint32_t pim_text_previous(const char *path, uint32_t offset) {
    char chunk[PIM_PAGE_BYTES];
    FILE *file;
    uint32_t start;
    size_t got, skip = 0;
    if (offset == 0) return 0;
    start = offset > PIM_PAGE_BYTES - 1 ? offset - (PIM_PAGE_BYTES - 1) : 0;
    file = fopen(path, "rb");
    if (file == NULL) return 0;
    if (fseek(file, (long)start, SEEK_SET) != 0) { fclose(file); return 0; }
    got = fread(chunk, 1, offset - start, file);
    fclose(file);
    if (start > 0) {
        for (size_t i = 0; i < got; i++) if (chunk[i] == '\n') { skip = i + 1; break; }
        if (skip == 0) while (skip < got && continuation(chunk[skip])) skip++;
    }
    return start + (uint32_t)skip;
}

static int last_error;
int pim_text_last_errno(void) { return last_error; }

bool pim_text_replace(const char *path, uint32_t offset, uint32_t old_length, const char *data, size_t length) {
    static char copy[512];
    char temporary[160];
    FILE *source, *target;
    uint32_t remaining;
    bool ok = true;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary)) return false;
    last_error = 0;
    source = fopen(path, "rb");
    target = fopen(temporary, "wb");
    if (target == NULL) { last_error = errno; if (source != NULL) fclose(source); return false; }
    for (remaining = offset; ok && remaining > 0 && source != NULL;) {
        size_t want = remaining < sizeof(copy) ? remaining : sizeof(copy);
        size_t got = fread(copy, 1, want, source);
        if (got == 0) break;
        ok = fwrite(copy, 1, got, target) == got;
        remaining -= (uint32_t)got;
    }
    if (remaining > 0 || (source == NULL && offset > 0)) ok = false;
    if (ok && length > 0) ok = fwrite(data, 1, length, target) == length;
    if (ok && source != NULL && fseek(source, (long)offset + (long)old_length, SEEK_SET) == 0) {
        size_t got;
        while (ok && (got = fread(copy, 1, sizeof(copy), source)) > 0) ok = fwrite(copy, 1, got, target) == got;
    }
    if (source != NULL) fclose(source);
    if (fclose(target) != 0) ok = false;
    if (!ok) { last_error = errno != 0 ? errno : EIO; remove(temporary); return false; }
    if (rename(temporary, path) != 0) {
        /* Some FAT layers refuse to rename over an existing file; the temp copy is complete, so replace it explicitly. */
        if (remove(path) != 0 || rename(temporary, path) != 0) { last_error = errno != 0 ? errno : EIO; remove(temporary); return false; }
    }
    return true;
}

bool pim_journal_path(const char *root, pim_date_t date, char *out, size_t size) {
    if (!pim_date_is_valid(date)) return false;
    return snprintf(out, size, "%s/journal/%02u-%02u-%04u.md", root, date.day, date.month, date.year) < (int)size;
}

bool pim_journal_exists(const char *root, pim_date_t date) {
    char path[160];
    uint32_t size;
    return pim_journal_path(root, date, path, sizeof(path)) && pim_text_size(path, &size);
}

bool pim_journal_delete(const char *root, pim_date_t date) {
    char path[160];
    return pim_journal_path(root, date, path, sizeof(path)) && remove(path) == 0;
}

static bool parse_name(const char *name, pim_date_t *date) {
    unsigned day, month, year;
    char tail;
    if (strlen(name) != 13 || strcmp(name + 10, ".md") != 0 || name[2] != '-' || name[5] != '-') return false;
    if (sscanf(name, "%2u-%2u-%4u%c", &day, &month, &year, &tail) != 4) return false;
    *date = (pim_date_t){(uint16_t)year, (uint8_t)month, (uint8_t)day};
    return pim_date_is_valid(*date);
}

size_t pim_journal_list(const char *root, pim_date_t *dates, size_t max, size_t *total) {
    char path[160];
    DIR *directory;
    struct dirent *entry;
    size_t count = 0, found = 0;
    if (total != NULL) *total = 0;
    if (snprintf(path, sizeof(path), "%s/journal", root) >= (int)sizeof(path)) return 0;
    directory = opendir(path);
    if (directory == NULL) return 0;
    while ((entry = readdir(directory)) != NULL) {
        pim_date_t date;
        size_t at;
        if (!parse_name(entry->d_name, &date)) continue;
        found++;
        for (at = 0; at < count && pim_date_compare(dates[at], date) > 0; at++) {}
        if (at >= max) continue;
        if (count < max) count++;
        memmove(&dates[at + 1], &dates[at], (count - at - 1) * sizeof(dates[0]));
        dates[at] = date;
    }
    closedir(directory);
    if (total != NULL) *total = found;
    return count;
}

bool pim_journal_migrate(const char *root, pim_database_t *database) {
    bool changed = database->journal_count > 0;
    for (size_t i = 0; i < database->journal_count; i++) {
        char path[160], body[480];
        const pim_journal_entry_t *entry = &database->journal[i];
        if (!pim_journal_path(root, entry->date, path, sizeof(path))) continue;
        if (pim_journal_exists(root, entry->date)) {
            /* An export from an earlier version already holds this entry. */
            continue;
        }
        int length = snprintf(body, sizeof(body), "Mood: %s\n\n%s\n", pim_mood_name(entry->mood), entry->content);
        if (length > 0 && !pim_text_replace(path, 0, 0, body, (size_t)length)) return false;
    }
    if (changed) { database->journal_count = 0; memset(database->journal, 0, sizeof(database->journal)); }
    return changed;
}

void pim_journal_preview(const char *root, pim_date_t date, char *out, size_t size) {
    char path[160], line[96];
    FILE *file;
    out[0] = '\0';
    if (!pim_journal_path(root, date, path, sizeof(path)) || (file = fopen(path, "rb")) == NULL) return;
    while (fgets(line, sizeof(line), file) != NULL) {
        char *start = line;
        size_t length;
        while (*start == '#' || *start == ' ' || *start == '\t') start++;
        length = strlen(start);
        while (length > 0 && (start[length - 1] == '\n' || start[length - 1] == '\r')) start[--length] = '\0';
        if (length == 0) continue;
        snprintf(out, size, "%s", start);
        break;
    }
    fclose(file);
}
