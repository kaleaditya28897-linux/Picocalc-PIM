#ifndef PIM_GAMES_H
#define PIM_GAMES_H

#include <stdbool.h>
#include <stdint.h>

#define PIM_TETRIS_WIDTH 10
#define PIM_TETRIS_HEIGHT 20
#define PIM_DUNGEON_WIDTH 16
#define PIM_DUNGEON_HEIGHT 12
#define PIM_DUNGEON_MAX_ENEMIES 8
#define PIM_DUNGEON_FLOORS 8
#define PIM_INVADERS_WIDTH 20
#define PIM_INVADERS_HEIGHT 16
#define PIM_INVADERS_COLUMNS 8
#define PIM_INVADERS_ROWS 4
#define PIM_INVADERS_MAX_OFFSET (PIM_INVADERS_WIDTH - 2 * PIM_INVADERS_COLUMNS + 1)

typedef enum { PIM_UP, PIM_DOWN, PIM_LEFT, PIM_RIGHT } pim_direction_t;

typedef struct {
    uint8_t cells[PIM_TETRIS_HEIGHT][PIM_TETRIS_WIDTH];
    uint8_t shape, next, rotation, level;
    int8_t x, y;
    uint16_t score, lines;
    uint32_t rng;
    bool game_over;
} pim_tetris_t;

typedef struct {
    int8_t x, y;
    uint8_t hp;
    bool alive;
    uint8_t kind; /* 0 monster (M), 1 orc (O), 2 boss (B) */
} pim_enemy_t;

typedef struct {
    char cells[PIM_DUNGEON_HEIGHT][PIM_DUNGEON_WIDTH]; /* '#' wall, '.' floor, '>' stairs, '$' gold, '!' potion, '/' sword, '[' armor */
    pim_enemy_t enemy[PIM_DUNGEON_MAX_ENEMIES];
    int8_t player_x, player_y;
    uint8_t health, max_health, gold, floor, floors, enemies, kills, attack, defense;
    uint32_t rng;
    char message[32];
    bool game_over, won;
} pim_dungeon_t;

typedef struct {
    bool alive[PIM_INVADERS_ROWS][PIM_INVADERS_COLUMNS];
    int8_t player_x, bullet_x, bullet_y, bomb_x, bomb_y, offset_x, offset_y, direction;
    uint8_t lives, wave;
    uint16_t score, tick, remaining;
    uint32_t rng;
    bool bullet_active, bomb_active, game_over;
} pim_invaders_t;

#define PIM_SOLITAIRE_PILES 7
#define PIM_SOLITAIRE_COLUMN_MAX 20
typedef enum { PIM_SOL_WASTE, PIM_SOL_FOUNDATION, PIM_SOL_TABLEAU } pim_solitaire_zone_t;

/* A card is suit * 13 + rank; rank 0 is the ace. Suits 1 and 2 are red. */
typedef struct {
    uint8_t stock[24], waste[24], tableau[PIM_SOLITAIRE_PILES][PIM_SOLITAIRE_COLUMN_MAX];
    uint8_t stock_count, waste_count, tableau_count[PIM_SOLITAIRE_PILES], face_up[PIM_SOLITAIRE_PILES];
    uint8_t foundation[4];
    uint16_t moves;
    bool won;
} pim_solitaire_t;

void pim_tetris_reset(pim_tetris_t *game, uint32_t seed);
bool pim_tetris_shape_cell(uint8_t shape, uint8_t rotation, uint8_t row, uint8_t column);
bool pim_tetris_active_cell(const pim_tetris_t *game, uint8_t x, uint8_t y);
bool pim_tetris_move(pim_tetris_t *game, int8_t dx, int8_t dy);
void pim_tetris_rotate(pim_tetris_t *game);
void pim_tetris_drop(pim_tetris_t *game);
uint16_t pim_tetris_interval_ms(const pim_tetris_t *game);

void pim_dungeon_reset(pim_dungeon_t *game, uint32_t seed);
void pim_dungeon_move(pim_dungeon_t *game, pim_direction_t direction);

void pim_invaders_reset(pim_invaders_t *game, uint32_t seed);
void pim_invaders_move(pim_invaders_t *game, int8_t direction);
void pim_invaders_fire(pim_invaders_t *game);
void pim_invaders_tick(pim_invaders_t *game);
uint16_t pim_invaders_interval_ms(const pim_invaders_t *game);

void pim_solitaire_new(pim_solitaire_t *game, uint32_t seed);
bool pim_solitaire_draw(pim_solitaire_t *game);
bool pim_solitaire_move(pim_solitaire_t *game, pim_solitaire_zone_t from, uint8_t from_index, uint8_t count,
                        pim_solitaire_zone_t to, uint8_t to_index);
bool pim_solitaire_to_foundation(pim_solitaire_t *game, pim_solitaire_zone_t from, uint8_t from_index);
bool pim_solitaire_is_red(uint8_t card);

/* ---- Snake ---- */
#define PIM_SNAKE_WIDTH 20
#define PIM_SNAKE_HEIGHT 15
typedef struct {
    int8_t x[PIM_SNAKE_WIDTH * PIM_SNAKE_HEIGHT], y[PIM_SNAKE_WIDTH * PIM_SNAKE_HEIGHT]; /* [0] is the head */
    uint16_t length, score;
    int8_t food_x, food_y;
    pim_direction_t direction, pending;
    uint32_t rng;
    bool game_over, won;
} pim_snake_t;
void pim_snake_reset(pim_snake_t *game, uint32_t seed);
/* Ignored when it would reverse the snake onto itself. */
void pim_snake_turn(pim_snake_t *game, pim_direction_t direction);
void pim_snake_tick(pim_snake_t *game);
uint16_t pim_snake_interval_ms(const pim_snake_t *game);

/* ---- 2048 ---- */
typedef struct {
    uint8_t cells[4][4]; /* exponent: 0 empty, 1 = 2, 2 = 4, ... 11 = 2048 */
    uint32_t score, rng;
    bool game_over, won;
} pim_2048_t;
void pim_2048_reset(pim_2048_t *game, uint32_t seed);
/* Slides and merges; returns false (and spawns nothing) when no tile moved. */
bool pim_2048_move(pim_2048_t *game, pim_direction_t direction);

/* ---- Minesweeper ---- */
#define PIM_MINES_WIDTH 16
#define PIM_MINES_HEIGHT 12
#define PIM_MINES_COUNT 24
#define PIM_MINE_REVEALED 1
#define PIM_MINE_FLAGGED 2
#define PIM_MINE_MINE 4
typedef struct {
    uint8_t flags[PIM_MINES_HEIGHT][PIM_MINES_WIDTH];
    int8_t cursor_x, cursor_y;
    uint16_t revealed;
    uint32_t rng;
    bool placed, game_over, won;
} pim_mines_t;
void pim_mines_reset(pim_mines_t *game, uint32_t seed);
void pim_mines_move_cursor(pim_mines_t *game, int8_t dx, int8_t dy);
/* The first reveal places the mines so it is never a mine; revealing a zero floods outward. */
void pim_mines_reveal(pim_mines_t *game, int8_t x, int8_t y);
void pim_mines_flag(pim_mines_t *game, int8_t x, int8_t y);
uint8_t pim_mines_around(const pim_mines_t *game, int8_t x, int8_t y);
uint8_t pim_mines_flags_left(const pim_mines_t *game);

#endif
