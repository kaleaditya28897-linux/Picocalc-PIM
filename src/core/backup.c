#include "pim/tools.h"

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

#define PATH_MAX_LENGTH 128
#define COPY_DEPTH 4

typedef struct {
    char source[PATH_MAX_LENGTH], target[PATH_MAX_LENGTH];
    char copy[512];
    bool top_level;
} copier_t;

static bool is_directory(const char *path) {
    struct stat status;
    return stat(path, &status) == 0 && S_ISDIR(status.st_mode);
}

static bool make_directory(const char *path) {
    return mkdir(path, 0777) == 0 || is_directory(path);
}

static bool copy_file(copier_t *work) {
    FILE *in = fopen(work->source, "rb"), *out;
    bool ok = true;
    size_t got;
    if (in == NULL) return false;
    out = fopen(work->target, "wb");
    if (out == NULL) { fclose(in); return false; }
    while (ok && (got = fread(work->copy, 1, sizeof(work->copy), in)) > 0) ok = fwrite(work->copy, 1, got, out) == got;
    fclose(in);
    return fclose(out) == 0 && ok;
}

/* Regenerated from pim.dat on every start, so they are not worth copying. */
static bool skipped(const char *name, bool top_level) {
    static const char *const names[] = {"appointments", "calendar", "notes", "todos", "backup"};
    size_t length = strlen(name);
    if (name[0] == '.') return true;
    if (length > 4 && strcmp(name + length - 4, ".tmp") == 0) return true;
    if (top_level)
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) if (strcmp(name, names[i]) == 0) return true;
    return false;
}

static bool copy_tree(copier_t *work, int depth) {
    DIR *directory;
    struct dirent *entry;
    size_t source_length = strlen(work->source), target_length = strlen(work->target);
    bool ok = true, top = work->top_level;
    if (depth > COPY_DEPTH || !make_directory(work->target)) return false;
    directory = opendir(work->source);
    if (directory == NULL) return false;
    work->top_level = false;
    while (ok && (entry = readdir(directory)) != NULL) {
        size_t name_length = strlen(entry->d_name);
        if (skipped(entry->d_name, top)) continue;
        if (source_length + name_length + 2 > PATH_MAX_LENGTH || target_length + name_length + 2 > PATH_MAX_LENGTH) continue;
        snprintf(work->source + source_length, PATH_MAX_LENGTH - source_length, "/%.*s", (int)name_length, entry->d_name);
        snprintf(work->target + target_length, PATH_MAX_LENGTH - target_length, "/%.*s", (int)name_length, entry->d_name);
        ok = is_directory(work->source) ? copy_tree(work, depth + 1) : copy_file(work);
        work->source[source_length] = '\0';
        work->target[target_length] = '\0';
    }
    closedir(directory);
    work->top_level = top;
    return ok;
}

bool pim_remove_tree(const char *path) {
    char child[PATH_MAX_LENGTH];
    DIR *directory;
    struct dirent *entry;
    bool ok = true;
    if (!is_directory(path)) return remove(path) == 0;
    directory = opendir(path);
    if (directory == NULL) return false;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) >= (int)sizeof(child)) { ok = false; continue; }
        if (!pim_remove_tree(child)) ok = false;
    }
    closedir(directory);
    return remove(path) == 0 && ok;
}

static bool valid_name(const char *name) {
    size_t length = strlen(name);
    if (length == 0 || length >= PIM_BACKUP_NAME) return false;
    for (size_t i = 0; i < length; i++)
        if (!((name[i] >= '0' && name[i] <= '9') || name[i] == '-' || name[i] == '_')) return false;
    return true;
}

size_t pim_backup_list(const char *root, char names[][PIM_BACKUP_NAME], size_t max) {
    char path[PATH_MAX_LENGTH];
    DIR *directory;
    struct dirent *entry;
    size_t count = 0;
    if (snprintf(path, sizeof(path), "%s/backup", root) >= (int)sizeof(path) || (directory = opendir(path)) == NULL) return 0;
    while ((entry = readdir(directory)) != NULL) {
        size_t at;
        if (!valid_name(entry->d_name)) continue;
        for (at = 0; at < count && strcmp(names[at], entry->d_name) > 0; at++) {}
        if (at >= max) continue;
        if (count < max) count++;
        memmove(names[at + 1], names[at], (count - at - 1) * PIM_BACKUP_NAME);
        snprintf(names[at], PIM_BACKUP_NAME, "%.*s", PIM_BACKUP_NAME - 1, entry->d_name);
    }
    closedir(directory);
    return count;
}

bool pim_backup_create(const char *root, const char *name) {
    copier_t *work;
    char names[PIM_BACKUP_KEEP + 8][PIM_BACKUP_NAME];
    bool ok;
    size_t count;
    if (!valid_name(name)) return false;
    work = calloc(1, sizeof(*work));
    if (work == NULL) return false;
    if (snprintf(work->target, sizeof(work->target), "%s/backup", root) >= (int)sizeof(work->target) || !make_directory(work->target) ||
        snprintf(work->target, sizeof(work->target), "%s/backup/%s", root, name) >= (int)sizeof(work->target)) { free(work); return false; }
    snprintf(work->source, sizeof(work->source), "%s", root);
    work->top_level = true;
    ok = copy_tree(work, 0);
    free(work);
    if (!ok) { char partial[PATH_MAX_LENGTH]; snprintf(partial, sizeof(partial), "%s/backup/%s", root, name); pim_remove_tree(partial); return false; }
    count = pim_backup_list(root, names, sizeof(names) / sizeof(names[0]));
    for (size_t i = PIM_BACKUP_KEEP; i < count; i++) {
        char old[PATH_MAX_LENGTH];
        if (snprintf(old, sizeof(old), "%s/backup/%s", root, names[i]) < (int)sizeof(old)) pim_remove_tree(old);
    }
    return true;
}

bool pim_backup_restore(const char *root, const char *name) {
    copier_t *work;
    bool ok;
    if (!valid_name(name)) return false;
    work = calloc(1, sizeof(*work));
    if (work == NULL) return false;
    if (snprintf(work->source, sizeof(work->source), "%s/backup/%s", root, name) >= (int)sizeof(work->source) || !is_directory(work->source)) { free(work); return false; }
    snprintf(work->target, sizeof(work->target), "%s", root);
    ok = copy_tree(work, 0);
    free(work);
    return ok;
}
