#include "pim/tools.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "pim/textfile.h"

#ifdef PIM_VFS_DIRENT
#include <sys/dirent.h>
extern DIR *opendir(const char *path);
extern struct dirent *readdir(DIR *directory);
extern int closedir(DIR *directory);
#else
#include <dirent.h>
#endif

#define SCAN_BUDGET (512UL * 1024UL)

typedef struct {
    const char *query;
    size_t length;
    pim_hit_t *hits;
    size_t max, count;
    unsigned long budget;
} search_t;

static const char *find_text(const char *text, const char *query, size_t length) {
    for (; *text; text++) {
        size_t i = 0;
        while (i < length && text[i] && tolower((unsigned char)text[i]) == tolower((unsigned char)query[i])) i++;
        if (i == length) return text;
    }
    return NULL;
}

static void make_snippet(char *out, size_t size, const char *line, const char *match) {
    const char *start = match - line > 12 ? match - 12 : line;
    size_t length;
    while (*start == ' ' || *start == '\t') start++;
    snprintf(out, size, "%.*s", (int)(size - 1), start);
    length = strlen(out);
    while (length > 0 && (out[length - 1] == '\n' || out[length - 1] == '\r')) out[--length] = '\0';
    for (size_t i = 0; out[i]; i++) if ((unsigned char)out[i] < 0x20) out[i] = ' ';
}

static pim_hit_t *add_hit(search_t *search, pim_hit_kind_t kind) {
    pim_hit_t *hit;
    if (search->count >= search->max) return NULL;
    hit = &search->hits[search->count++];
    memset(hit, 0, sizeof(*hit));
    hit->kind = kind;
    return hit;
}

/* Returns true when the file holds the query; fills the first matching line. */
static bool scan_file(search_t *search, const char *path, char *snippet, size_t snippet_size) {
    char line[160];
    FILE *file = fopen(path, "rb");
    bool found = false;
    if (file == NULL) return false;
    while (search->budget > 0 && fgets(line, sizeof(line), file) != NULL) {
        const char *match;
        size_t length = strlen(line);
        search->budget = search->budget > length ? search->budget - length : 0;
        match = find_text(line, search->query, search->length);
        if (match != NULL) { make_snippet(snippet, snippet_size, line, match); found = true; break; }
    }
    fclose(file);
    return found;
}

static bool text_name(const char *name) {
    static const char *const reserved[] = {"pim.dat", "wifi.cfg", "theme.cfg", "today.cfg", "sound.cfg", "recurring.cfg"};
    const char *dot = strrchr(name, '.');
    size_t length = strlen(name);
    for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++) if (strcmp(name, reserved[i]) == 0) return false;
    if (length > 4 && strcmp(name + length - 4, ".tmp") == 0) return false;
    if (dot == NULL) return true;
    return strcasecmp(dot, ".txt") == 0 || strcasecmp(dot, ".c") == 0 || strcasecmp(dot, ".h") == 0 ||
           strcasecmp(dot, ".md") == 0 || strcasecmp(dot, ".csv") == 0 || strcasecmp(dot, ".ics") == 0;
}

static void scan_directory(search_t *search, const char *directory_path, bool journal) {
    DIR *directory = opendir(directory_path);
    struct dirent *entry;
    if (directory == NULL) return;
    while (search->budget > 0 && search->count < search->max && (entry = readdir(directory)) != NULL) {
        char path[160], snippet[44];
        struct stat status;
        pim_hit_t *hit;
        if (entry->d_name[0] == '.' || !text_name(entry->d_name)) continue;
        if (snprintf(path, sizeof(path), "%s/%s", directory_path, entry->d_name) >= (int)sizeof(path)) continue;
        if (stat(path, &status) != 0 || S_ISDIR(status.st_mode)) continue;
        if (!scan_file(search, path, snippet, sizeof(snippet))) continue;
        hit = add_hit(search, journal ? PIM_HIT_JOURNAL : PIM_HIT_FILE);
        if (hit == NULL) break;
        snprintf(hit->path, sizeof(hit->path), "%.*s", (int)sizeof(hit->path) - 1, path);
        snprintf(hit->snippet, sizeof(hit->snippet), "%s", snippet);
        if (journal) {
            unsigned day, month, year;
            if (sscanf(entry->d_name, "%2u-%2u-%4u", &day, &month, &year) == 3) hit->date = (pim_date_t){(uint16_t)year, (uint8_t)month, (uint8_t)day};
            snprintf(hit->title, sizeof(hit->title), "Journal %.10s", entry->d_name);
        } else snprintf(hit->title, sizeof(hit->title), "%.39s", entry->d_name);
    }
    closedir(directory);
}

static void database_hit(search_t *search, pim_hit_kind_t kind, size_t index, pim_date_t date, const char *title, const char *body) {
    const char *match = find_text(title, search->query, search->length);
    pim_hit_t *hit;
    if (match == NULL && body != NULL) match = find_text(body, search->query, search->length);
    if (match == NULL || (hit = add_hit(search, kind)) == NULL) return;
    hit->index = index;
    hit->date = date;
    snprintf(hit->title, sizeof(hit->title), "%s", title);
    snprintf(hit->snippet, sizeof(hit->snippet), "%s", body != NULL && body[0] != '\0' ? body : title);
}

size_t pim_search(const char *root, const pim_database_t *database, const char *query, pim_hit_t *hits, size_t max) {
    search_t search = {query, strlen(query), hits, max, 0, SCAN_BUDGET};
    char path[128];
    pim_date_t none = {0, 0, 0};
    if (search.length == 0 || max == 0) return 0;
    for (size_t i = 0; i < database->appointment_count; i++)
        database_hit(&search, PIM_HIT_APPOINTMENT, i, database->appointments[i].date, database->appointments[i].title, database->appointments[i].description);
    for (size_t i = 0; i < database->todo_count; i++) database_hit(&search, PIM_HIT_TODO, i, none, database->todos[i].title, NULL);
    for (size_t i = 0; i < database->note_count; i++) database_hit(&search, PIM_HIT_NOTE, i, none, database->notes[i].title, database->notes[i].content);
    snprintf(path, sizeof(path), "%s/journal", root);
    scan_directory(&search, path, true);
    scan_directory(&search, root, false);
    snprintf(path, sizeof(path), "%s/code", root);
    scan_directory(&search, path, false);
    return search.count;
}
