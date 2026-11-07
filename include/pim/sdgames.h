#ifndef PIM_SDGAMES_H
#define PIM_SDGAMES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PIM_SCORE_SLOTS 5

/* Games keep their data under <root>/games/<game>/ and read it only on demand.
 * Pass NULL to disable file access (the host tests do this for deterministic games). */
void pim_games_set_root(const char *root);
/* Creates the games folders and writes any missing default files (floors, settings, README). */
bool pim_games_prepare(const char *root);
/* Reads `key=value` from <game>/game.cfg. */
bool pim_game_config(const char *game, const char *key, long *value);
/* Best scores, highest first; returns how many are stored. Solitaire stores wins as score = 10000 - moves. */
size_t pim_scores_load(const char *game, uint32_t *scores, size_t max);
/* Records a score; returns its 1-based rank, or 0 when it did not make the top list or storage is unavailable. */
unsigned pim_scores_add(const char *game, uint32_t score);
/* Reads one dungeon floor file into 12 rows of 16 characters, if it exists and is valid. */
bool pim_game_floor_text(unsigned floor, char *text, size_t size);

#endif
