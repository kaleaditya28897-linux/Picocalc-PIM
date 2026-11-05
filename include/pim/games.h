#ifndef PIM_GAMES_H
#define PIM_GAMES_H

#include <stdbool.h>
#include <stdint.h>

#define PIM_TETRIS_WIDTH 10
#define PIM_TETRIS_HEIGHT 20
#define PIM_DUNGEON_WIDTH 16
#define PIM_DUNGEON_HEIGHT 12
#define PIM_INVADERS_WIDTH 12
#define PIM_INVADERS_HEIGHT 8

typedef enum { PIM_UP, PIM_DOWN, PIM_LEFT, PIM_RIGHT } pim_direction_t;
typedef struct {
    uint8_t cells[PIM_TETRIS_HEIGHT][PIM_TETRIS_WIDTH];
    uint8_t shape, rotation, level;
    int8_t x, y;
    uint16_t score, lines;
    bool game_over;
} pim_tetris_t;

typedef struct {
    char cells[PIM_DUNGEON_HEIGHT][PIM_DUNGEON_WIDTH];
    int8_t player_x, player_y, exit_x, exit_y;
    uint8_t health, gold, floor, enemies;
    bool game_over, won;
} pim_dungeon_t;

typedef struct {
    bool alive[PIM_INVADERS_HEIGHT][PIM_INVADERS_WIDTH];
    int8_t player_x, bullet_x, bullet_y, invader_offset_x, invader_offset_y;
    int8_t direction;
    uint16_t score, tick;
    bool bullet_active, game_over, won;
} pim_invaders_t;

void pim_tetris_reset(pim_tetris_t *game, uint32_t random_value);
bool pim_tetris_active_cell(const pim_tetris_t *game, uint8_t x, uint8_t y);
bool pim_tetris_move(pim_tetris_t *game, int8_t dx, int8_t dy);
void pim_tetris_rotate(pim_tetris_t *game);
void pim_tetris_drop(pim_tetris_t *game);
void pim_dungeon_reset(pim_dungeon_t *game, uint32_t random_value);
void pim_dungeon_move(pim_dungeon_t *game, pim_direction_t direction, uint32_t random_value);
void pim_invaders_reset(pim_invaders_t *game);
void pim_invaders_move(pim_invaders_t *game, int8_t direction);
void pim_invaders_fire(pim_invaders_t *game);
void pim_invaders_tick(pim_invaders_t *game);

#endif
