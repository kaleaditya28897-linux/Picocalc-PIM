#include "pim/games.h"

#include <string.h>

static const uint8_t shapes[7][4][4] = {
    {{0, 0, 0, 0}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {{0, 1, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {{0, 1, 0, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {{0, 1, 1, 0}, {1, 1, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {{1, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {{1, 0, 0, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {{0, 0, 1, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
};

static uint8_t shape_cell(uint8_t shape, uint8_t rotation, uint8_t row, uint8_t column) {
    while (rotation-- % 4) {
        uint8_t old_row = row;
        row = 3 - column;
        column = old_row;
    }
    return shapes[shape][row][column];
}

static bool valid(const pim_tetris_t *game, int8_t x, int8_t y, uint8_t rotation) {
    for (uint8_t row = 0; row < 4; row++) for (uint8_t column = 0; column < 4; column++) {
        int8_t grid_x, grid_y;
        if (!shape_cell(game->shape, rotation, row, column)) continue;
        grid_x = x + (int8_t)column;
        grid_y = y + (int8_t)row;
        if (grid_x < 0 || grid_x >= PIM_TETRIS_WIDTH || grid_y >= PIM_TETRIS_HEIGHT ||
            (grid_y >= 0 && game->cells[grid_y][grid_x])) return false;
    }
    return true;
}

static void clear_lines(pim_tetris_t *game) {
    uint8_t count = 0;
    for (uint8_t row = 0; row < PIM_TETRIS_HEIGHT; row++) {
        bool full = true;
        for (uint8_t col = 0; col < PIM_TETRIS_WIDTH; col++)
            if (!game->cells[row][col]) { full = false; break; }
        if (full) {
            memmove(&game->cells[1], &game->cells[0], row * sizeof(game->cells[0]));
            memset(&game->cells[0], 0, sizeof(game->cells[0]));
            count++;
        }
    }
    if (count) {
        static const uint16_t points[] = {0, 100, 300, 500, 800};
        game->score += points[count] * game->level;
        game->lines += count;
        game->level = (uint8_t)(1 + game->lines / 10);
    }
}

static void lock_piece(pim_tetris_t *game, uint32_t random_value) {
    for (uint8_t row = 0; row < 4; row++) for (uint8_t col = 0; col < 4; col++) {
        if (shape_cell(game->shape, game->rotation, row, col)) {
            int8_t y = game->y + (int8_t)row;
            if (y >= 0) game->cells[y][game->x + (int8_t)col] = game->shape + 1;
        }
    }
    clear_lines(game);
    game->shape = (uint8_t)(random_value % 7);
    game->rotation = 0;
    game->x = 3;
    game->y = -1;
    if (!valid(game, game->x, game->y, game->rotation)) game->game_over = true;
}

void pim_tetris_reset(pim_tetris_t *game, uint32_t random_value) {
    memset(game, 0, sizeof(*game));
    game->shape = (uint8_t)(random_value % 7);
    game->x = 3;
    game->y = -1;
    game->level = 1;
}

bool pim_tetris_active_cell(const pim_tetris_t *game, uint8_t x, uint8_t y) {
    if (game->game_over) return false;
    for (uint8_t row = 0; row < 4; row++) for (uint8_t column = 0; column < 4; column++) {
        if (shape_cell(game->shape, game->rotation, row, column) &&
            game->x + (int8_t)column == (int8_t)x && game->y + (int8_t)row == (int8_t)y) return true;
    }
    return false;
}

bool pim_tetris_move(pim_tetris_t *game, int8_t dx, int8_t dy) {
    if (game->game_over) return false;
    if (valid(game, game->x + dx, game->y + dy, game->rotation)) {
        game->x += dx; game->y += dy; return true;
    }
    if (dy > 0) lock_piece(game, game->score + game->lines + game->shape + 1);
    return false;
}

void pim_tetris_rotate(pim_tetris_t *game) {
    uint8_t rotation = (uint8_t)((game->rotation + 1) % 4);
    if (!game->game_over && valid(game, game->x, game->y, rotation)) game->rotation = rotation;
}

void pim_tetris_drop(pim_tetris_t *game) {
    while (pim_tetris_move(game, 0, 1)) game->score += 2;
}

void pim_dungeon_reset(pim_dungeon_t *game, uint32_t random_value) {
    memset(game, 0, sizeof(*game));
    for (int8_t y = 0; y < PIM_DUNGEON_HEIGHT; y++) for (int8_t x = 0; x < PIM_DUNGEON_WIDTH; x++)
        game->cells[y][x] = x == 0 || y == 0 || x == PIM_DUNGEON_WIDTH - 1 ||
                            y == PIM_DUNGEON_HEIGHT - 1 ? '#' : '.';
    for (uint8_t wall = 0; wall < 18; wall++) {
        int8_t x = (int8_t)(1 + ((random_value >> (wall % 16)) + wall * 3) % (PIM_DUNGEON_WIDTH - 2));
        int8_t y = (int8_t)(1 + ((random_value >> ((wall + 5) % 16)) + wall * 5) % (PIM_DUNGEON_HEIGHT - 2));
        game->cells[y][x] = '#';
    }
    game->player_x = 1; game->player_y = 1;
    game->exit_x = PIM_DUNGEON_WIDTH - 2; game->exit_y = PIM_DUNGEON_HEIGHT - 2;
    game->cells[game->player_y][game->player_x] = '.';
    game->cells[game->exit_y][game->exit_x] = '>';
    for (uint8_t enemy = 0; enemy < 6; enemy++) {
        int8_t x = (int8_t)(2 + (random_value + enemy * 7) % (PIM_DUNGEON_WIDTH - 4));
        int8_t y = (int8_t)(2 + ((random_value >> 8) + enemy * 3) % (PIM_DUNGEON_HEIGHT - 4));
        if (game->cells[y][x] == '.') { game->cells[y][x] = 'g'; game->enemies++; }
    }
    game->health = 10; game->floor = 1;
}

void pim_dungeon_move(pim_dungeon_t *game, pim_direction_t direction, uint32_t random_value) {
    int8_t x = game->player_x, y = game->player_y;
    char cell;
    if (game->game_over || game->won) return;
    if (direction == PIM_UP) y--; else if (direction == PIM_DOWN) y++;
    else if (direction == PIM_LEFT) x--; else x++;
    cell = game->cells[y][x];
    if (cell == '#') return;
    if (cell == 'g') {
        if (random_value % 3 == 0 && game->health > 0) game->health--;
        if (random_value % 2 == 0) { game->cells[y][x] = '.'; game->gold += 2; game->enemies--; }
        if (game->health == 0) game->game_over = true;
        return;
    }
    game->player_x = x; game->player_y = y;
    if (cell == '>') game->won = true;
}

void pim_invaders_reset(pim_invaders_t *game) {
    memset(game, 0, sizeof(*game));
    for (uint8_t y = 0; y < 3; y++) for (uint8_t x = 2; x < PIM_INVADERS_WIDTH - 2; x++)
        game->alive[y][x] = true;
    game->player_x = PIM_INVADERS_WIDTH / 2;
    game->direction = 1;
}

void pim_invaders_move(pim_invaders_t *game, int8_t direction) {
    if (!game->game_over && !game->won && game->player_x + direction >= 0 &&
        game->player_x + direction < PIM_INVADERS_WIDTH) game->player_x += direction;
}

void pim_invaders_fire(pim_invaders_t *game) {
    if (!game->bullet_active && !game->game_over && !game->won) {
        game->bullet_active = true; game->bullet_x = game->player_x; game->bullet_y = PIM_INVADERS_HEIGHT - 2;
    }
}

void pim_invaders_tick(pim_invaders_t *game) {
    uint8_t remaining = 0;
    if (game->game_over || game->won) return;
    game->tick++;
    if (game->bullet_active) {
        game->bullet_y--;
        for (uint8_t row = 0; row < PIM_INVADERS_HEIGHT; row++) {
            int8_t x = game->bullet_x - game->invader_offset_x;
            int8_t y = game->bullet_y - game->invader_offset_y;
            if (x >= 0 && x < PIM_INVADERS_WIDTH && y == (int8_t)row && game->alive[row][x]) {
                game->alive[row][x] = false; game->bullet_active = false; game->score += 10; break;
            }
        }
        if (game->bullet_y < 0) game->bullet_active = false;
    }
    if (game->tick % 4 == 0) {
        game->invader_offset_x += game->direction;
        if (game->invader_offset_x < -2 || game->invader_offset_x > 2) {
            game->direction = -game->direction; game->invader_offset_x += game->direction;
            game->invader_offset_y++;
        }
    }
    for (uint8_t y = 0; y < PIM_INVADERS_HEIGHT; y++) for (uint8_t x = 0; x < PIM_INVADERS_WIDTH; x++)
        if (game->alive[y][x]) remaining++;
    game->won = remaining == 0;
    game->game_over = game->invader_offset_y >= PIM_INVADERS_HEIGHT - 2;
}
