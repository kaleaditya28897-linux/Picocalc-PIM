#include "pim/sdgames.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "pim/samples.h"

static char games_root[96];

void pim_games_set_root(const char *root) {
    if (root == NULL) games_root[0] = '\0';
    else snprintf(games_root, sizeof(games_root), "%s", root);
}

static bool game_path(const char *game, const char *file, char *out, size_t size) {
    if (games_root[0] == '\0') return false;
    return snprintf(out, size, "%s/games/%s/%s", games_root, game, file) < (int)size;
}

static bool make_directory(const char *path) {
    return mkdir(path, 0777) == 0 || errno == EEXIST;
}

bool pim_games_prepare(const char *root) {
    char path[160];
    bool ok = true;
    static const char *const games[] = {"tetris", "dungeon", "invaders", "solitaire"};
    snprintf(path, sizeof(path), "%s/games", root);
    ok = make_directory(path);
    for (size_t i = 0; ok && i < sizeof(games) / sizeof(games[0]); i++) {
        snprintf(path, sizeof(path), "%s/games/%s", root, games[i]);
        ok = make_directory(path);
    }
    for (unsigned i = 0; ok && i < pim_sample_count; i++) {
        FILE *file;
        if (strncmp(pim_samples[i].name, "games/", 6) != 0) continue;
        if (snprintf(path, sizeof(path), "%s/%s", root, pim_samples[i].name) >= (int)sizeof(path)) continue;
        file = fopen(path, "rb");
        if (file != NULL) { fclose(file); continue; }
        file = fopen(path, "wb");
        if (file == NULL) { ok = false; break; }
        ok = fputs(pim_samples[i].text, file) >= 0;
        if (fclose(file) != 0) ok = false;
    }
    return ok;
}

bool pim_game_config(const char *game, const char *key, long *value) {
    char path[160], line[64];
    FILE *file;
    size_t length = strlen(key);
    bool found = false;
    if (!game_path(game, "game.cfg", path, sizeof(path)) || (file = fopen(path, "rb")) == NULL) return false;
    while (!found && fgets(line, sizeof(line), file) != NULL) {
        char *end;
        long parsed;
        if (strncmp(line, key, length) != 0 || line[length] != '=') continue;
        parsed = strtol(line + length + 1, &end, 10);
        if (end != line + length + 1) { *value = parsed; found = true; }
    }
    fclose(file);
    return found;
}

size_t pim_scores_load(const char *game, uint32_t *scores, size_t max) {
    char path[160];
    FILE *file;
    unsigned long value;
    size_t count = 0;
    if (!game_path(game, "scores.txt", path, sizeof(path)) || (file = fopen(path, "rb")) == NULL) return 0;
    while (count < max && fscanf(file, "%lu", &value) == 1) scores[count++] = (uint32_t)value;
    fclose(file);
    /* The file is hand-editable, so keep the order guaranteed. */
    for (size_t i = 1; i < count; i++)
        for (size_t j = i; j > 0 && scores[j] > scores[j - 1]; j--) { uint32_t swap = scores[j]; scores[j] = scores[j - 1]; scores[j - 1] = swap; }
    return count;
}

unsigned pim_scores_add(const char *game, uint32_t score) {
    char path[160];
    uint32_t list[PIM_SCORE_SLOTS + 1];
    size_t count, at;
    FILE *file;
    if (score == 0 || !game_path(game, "scores.txt", path, sizeof(path))) return 0;
    count = pim_scores_load(game, list, PIM_SCORE_SLOTS);
    for (at = 0; at < count && list[at] >= score; at++) {}
    if (at >= PIM_SCORE_SLOTS) return 0;
    if (count < PIM_SCORE_SLOTS) count++;
    memmove(&list[at + 1], &list[at], (count - at - 1) * sizeof(list[0]));
    list[at] = score;
    file = fopen(path, "wb");
    if (file == NULL) return 0;
    for (size_t i = 0; i < count; i++) fprintf(file, "%lu\n", (unsigned long)list[i]);
    return fclose(file) == 0 ? (unsigned)at + 1 : 0;
}

bool pim_game_floor_text(unsigned floor, char *text, size_t size) {
    char path[160], name[24], line[160];
    FILE *file;
    size_t rows = 0, used = 0;
    snprintf(name, sizeof(name), "floor%u.txt", floor);
    if (size < 12 * 17 + 1 || !game_path("dungeon", name, path, sizeof(path)) || (file = fopen(path, "rb")) == NULL) return false;
    while (rows < 12 && fgets(line, sizeof(line), file) != NULL) {
        size_t length = strcspn(line, "\r\n");
        if (line[0] == ';' || length == 0) continue;
        if (length != 16) { fclose(file); return false; }
        memcpy(text + used, line, 16);
        text[used + 16] = '\n';
        used += 17;
        rows++;
    }
    fclose(file);
    text[used] = '\0';
    return rows == 12;
}
