#include "pim/games.h"

#include <stddef.h>
#include <stdio.h>
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

static uint32_t random_next(uint32_t *state) {
    *state = *state * 1664525u + 1013904223u;
    return *state >> 16;
}

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

static void lock_piece(pim_tetris_t *game) {
    for (uint8_t row = 0; row < 4; row++) for (uint8_t col = 0; col < 4; col++) {
        if (shape_cell(game->shape, game->rotation, row, col)) {
            int8_t y = game->y + (int8_t)row;
            if (y >= 0) game->cells[y][game->x + (int8_t)col] = game->shape + 1;
        }
    }
    clear_lines(game);
    game->shape = game->next;
    game->next = (uint8_t)(random_next(&game->rng) % 7);
    game->rotation = 0;
    game->x = 3;
    game->y = -1;
    if (!valid(game, game->x, game->y, game->rotation)) game->game_over = true;
}

bool pim_tetris_shape_cell(uint8_t shape, uint8_t rotation, uint8_t row, uint8_t column) {
    return shape < 7 && row < 4 && column < 4 && shape_cell(shape, rotation, row, column);
}

void pim_tetris_reset(pim_tetris_t *game, uint32_t seed) {
    memset(game, 0, sizeof(*game));
    game->rng = seed * 2654435761u + 12345u;
    game->shape = (uint8_t)(random_next(&game->rng) % 7);
    game->next = (uint8_t)(random_next(&game->rng) % 7);
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
    if (dy > 0) lock_piece(game);
    return false;
}

void pim_tetris_rotate(pim_tetris_t *game) {
    static const int8_t kicks[] = {0, -1, 1, -2, 2};
    uint8_t rotation = (uint8_t)((game->rotation + 1) % 4);
    if (game->game_over) return;
    for (size_t i = 0; i < sizeof(kicks); i++) {
        if (valid(game, (int8_t)(game->x + kicks[i]), game->y, rotation)) {
            game->x = (int8_t)(game->x + kicks[i]);
            game->rotation = rotation;
            return;
        }
    }
}

void pim_tetris_drop(pim_tetris_t *game) {
    while (!game->game_over && pim_tetris_move(game, 0, 1)) game->score += 2;
}

uint16_t pim_tetris_interval_ms(const pim_tetris_t *game) {
    int interval = 700 - (game->level - 1) * 60;
    return (uint16_t)(interval < 120 ? 120 : interval);
}

/* ---- Dungeon ---- */

static int8_t sign(int8_t value) { return (int8_t)((value > 0) - (value < 0)); }
static int8_t absolute(int8_t value) { return value < 0 ? (int8_t)-value : value; }

static bool dungeon_reachable(const pim_dungeon_t *game) {
    int8_t queue[PIM_DUNGEON_WIDTH * PIM_DUNGEON_HEIGHT][2];
    bool seen[PIM_DUNGEON_HEIGHT][PIM_DUNGEON_WIDTH] = {{false}};
    size_t head = 0, tail = 0;
    static const int8_t steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    queue[tail][0] = 1; queue[tail++][1] = 1; seen[1][1] = true;
    while (head < tail) {
        int8_t x = queue[head][0], y = queue[head][1];
        head++;
        if (game->cells[y][x] == '>') return true;
        for (size_t i = 0; i < 4; i++) {
            int8_t nx = (int8_t)(x + steps[i][0]), ny = (int8_t)(y + steps[i][1]);
            if (game->cells[ny][nx] != '#' && !seen[ny][nx]) { seen[ny][nx] = true; queue[tail][0] = nx; queue[tail++][1] = ny; }
        }
    }
    return false;
}

static bool dungeon_enemy_at(const pim_dungeon_t *game, int8_t x, int8_t y) {
    for (uint8_t i = 0; i < PIM_DUNGEON_MAX_ENEMIES; i++)
        if (game->enemy[i].alive && game->enemy[i].x == x && game->enemy[i].y == y) return true;
    return false;
}

static void dungeon_place(pim_dungeon_t *game, char item, uint8_t count) {
    for (uint8_t placed = 0, tries = 0; placed < count && tries < 200; tries++) {
        int8_t x = (int8_t)(1 + random_next(&game->rng) % (PIM_DUNGEON_WIDTH - 2));
        int8_t y = (int8_t)(1 + random_next(&game->rng) % (PIM_DUNGEON_HEIGHT - 2));
        if (game->cells[y][x] == '.' && !dungeon_enemy_at(game, x, y) && !(x == 1 && y == 1)) {
            game->cells[y][x] = item;
            placed++;
        }
    }
}

static void dungeon_generate(pim_dungeon_t *game) {
    uint8_t wanted = (uint8_t)(2 + game->floor);
    bool reachable = false;
    if (wanted > PIM_DUNGEON_MAX_ENEMIES) wanted = PIM_DUNGEON_MAX_ENEMIES;
    for (uint8_t attempt = 0; attempt < 40 && !reachable; attempt++) {
        for (int8_t y = 0; y < PIM_DUNGEON_HEIGHT; y++) for (int8_t x = 0; x < PIM_DUNGEON_WIDTH; x++)
            game->cells[y][x] = (x == 0 || y == 0 || x == PIM_DUNGEON_WIDTH - 1 || y == PIM_DUNGEON_HEIGHT - 1) ? '#' : '.';
        for (uint8_t wall = 0; wall < 30; wall++)
            game->cells[1 + random_next(&game->rng) % (PIM_DUNGEON_HEIGHT - 2)][1 + random_next(&game->rng) % (PIM_DUNGEON_WIDTH - 2)] = '#';
        game->cells[1][1] = '.';
        game->cells[PIM_DUNGEON_HEIGHT - 2][PIM_DUNGEON_WIDTH - 2] = '>';
        reachable = dungeon_reachable(game);
    }
    if (!reachable) {
        for (int8_t y = 1; y < PIM_DUNGEON_HEIGHT - 1; y++) for (int8_t x = 1; x < PIM_DUNGEON_WIDTH - 1; x++) game->cells[y][x] = '.';
        game->cells[PIM_DUNGEON_HEIGHT - 2][PIM_DUNGEON_WIDTH - 2] = '>';
    }
    memset(game->enemy, 0, sizeof(game->enemy));
    game->enemies = 0;
    for (uint8_t tries = 0; game->enemies < wanted && tries < 250; tries++) {
        int8_t x = (int8_t)(1 + random_next(&game->rng) % (PIM_DUNGEON_WIDTH - 2));
        int8_t y = (int8_t)(1 + random_next(&game->rng) % (PIM_DUNGEON_HEIGHT - 2));
        if (game->cells[y][x] == '.' && x + y >= 7 && !dungeon_enemy_at(game, x, y)) {
            pim_enemy_t *enemy = &game->enemy[game->enemies++];
            enemy->x = x; enemy->y = y; enemy->hp = (uint8_t)(2 + game->floor / 2); enemy->alive = true;
        }
    }
    dungeon_place(game, '$', 4);
    dungeon_place(game, '!', 2);
    game->player_x = 1;
    game->player_y = 1;
}

void pim_dungeon_reset(pim_dungeon_t *game, uint32_t seed) {
    memset(game, 0, sizeof(*game));
    game->rng = seed * 2654435761u + 777u;
    game->health = game->max_health = 20;
    game->floor = 1;
    dungeon_generate(game);
    snprintf(game->message, sizeof(game->message), "Find the stairs >");
}

static void dungeon_enemies_act(pim_dungeon_t *game) {
    for (uint8_t i = 0; i < PIM_DUNGEON_MAX_ENEMIES && !game->game_over; i++) {
        pim_enemy_t *enemy = &game->enemy[i];
        int8_t dx, dy, distance;
        if (!enemy->alive) continue;
        dx = (int8_t)(game->player_x - enemy->x);
        dy = (int8_t)(game->player_y - enemy->y);
        distance = (int8_t)(absolute(dx) + absolute(dy));
        if (distance == 1) {
            if (random_next(&game->rng) % 2 == 0) {
                uint8_t damage = (uint8_t)(1 + game->floor / 3 + random_next(&game->rng) % 2);
                game->health = damage >= game->health ? 0 : (uint8_t)(game->health - damage);
                snprintf(game->message, sizeof(game->message), "Ouch! -%u HP", damage);
                if (game->health == 0) { game->game_over = true; snprintf(game->message, sizeof(game->message), "You died"); }
            } else snprintf(game->message, sizeof(game->message), "It missed you");
        } else if (distance <= 7) {
            int8_t options[2][2];
            if (absolute(dx) >= absolute(dy)) {
                options[0][0] = sign(dx); options[0][1] = 0; options[1][0] = 0; options[1][1] = sign(dy);
            } else {
                options[0][0] = 0; options[0][1] = sign(dy); options[1][0] = sign(dx); options[1][1] = 0;
            }
            for (uint8_t option = 0; option < 2; option++) {
                int8_t x = (int8_t)(enemy->x + options[option][0]), y = (int8_t)(enemy->y + options[option][1]);
                if ((options[option][0] == 0 && options[option][1] == 0) || game->cells[y][x] == '#' ||
                    dungeon_enemy_at(game, x, y) || (x == game->player_x && y == game->player_y)) continue;
                enemy->x = x; enemy->y = y;
                break;
            }
        }
    }
}

void pim_dungeon_move(pim_dungeon_t *game, pim_direction_t direction) {
    int8_t x = game->player_x, y = game->player_y;
    char cell;
    if (game->game_over || game->won) return;
    if (direction == PIM_UP) y--; else if (direction == PIM_DOWN) y++;
    else if (direction == PIM_LEFT) x--; else x++;
    cell = game->cells[y][x];
    if (cell == '#') return;
    for (uint8_t i = 0; i < PIM_DUNGEON_MAX_ENEMIES; i++) {
        pim_enemy_t *enemy = &game->enemy[i];
        uint8_t damage;
        if (!enemy->alive || enemy->x != x || enemy->y != y) continue;
        damage = (uint8_t)(1 + random_next(&game->rng) % 3);
        if (damage >= enemy->hp) {
            enemy->alive = false; game->enemies--; game->kills++;
            game->gold = game->gold > 245 ? 255 : (uint8_t)(game->gold + 10);
            snprintf(game->message, sizeof(game->message), "Slain! +10 gold");
        } else {
            enemy->hp = (uint8_t)(enemy->hp - damage);
            snprintf(game->message, sizeof(game->message), "You hit for %u", damage);
        }
        dungeon_enemies_act(game);
        return;
    }
    game->player_x = x; game->player_y = y;
    game->message[0] = '\0';
    if (cell == '$') {
        game->cells[y][x] = '.'; game->gold = game->gold > 250 ? 255 : (uint8_t)(game->gold + 5);
        snprintf(game->message, sizeof(game->message), "+5 gold");
    } else if (cell == '!') {
        game->cells[y][x] = '.';
        game->health = (uint8_t)(game->health + 8 > game->max_health ? game->max_health : game->health + 8);
        snprintf(game->message, sizeof(game->message), "Potion! +HP");
    } else if (cell == '>') {
        if (game->floor >= PIM_DUNGEON_FLOORS) { game->won = true; snprintf(game->message, sizeof(game->message), "You escaped!"); return; }
        game->floor++;
        game->health = (uint8_t)(game->health + 4 > game->max_health ? game->max_health : game->health + 4);
        dungeon_generate(game);
        snprintf(game->message, sizeof(game->message), "Floor %u", game->floor);
        return;
    }
    dungeon_enemies_act(game);
}

/* ---- Space invaders ---- */

static void invaders_new_wave(pim_invaders_t *game) {
    for (uint8_t row = 0; row < PIM_INVADERS_ROWS; row++) for (uint8_t col = 0; col < PIM_INVADERS_COLUMNS; col++)
        game->alive[row][col] = true;
    game->remaining = PIM_INVADERS_ROWS * PIM_INVADERS_COLUMNS;
    game->offset_x = 0; game->offset_y = 0; game->direction = 1;
    game->bullet_active = false; game->bomb_active = false;
}

void pim_invaders_reset(pim_invaders_t *game, uint32_t seed) {
    memset(game, 0, sizeof(*game));
    game->rng = seed * 2654435761u + 99u;
    game->player_x = PIM_INVADERS_WIDTH / 2;
    game->lives = 3;
    game->wave = 1;
    invaders_new_wave(game);
}

void pim_invaders_move(pim_invaders_t *game, int8_t direction) {
    int8_t x = (int8_t)(game->player_x + direction);
    if (!game->game_over && x >= 0 && x < PIM_INVADERS_WIDTH) game->player_x = x;
}

void pim_invaders_fire(pim_invaders_t *game) {
    if (!game->bullet_active && !game->game_over) {
        game->bullet_active = true; game->bullet_x = game->player_x; game->bullet_y = PIM_INVADERS_HEIGHT - 2;
    }
}

uint16_t pim_invaders_interval_ms(const pim_invaders_t *game) { (void)game; return 90; }

static bool invaders_bullet_hit(pim_invaders_t *game) {
    int8_t dx = (int8_t)(game->bullet_x - game->offset_x), dy = (int8_t)(game->bullet_y - game->offset_y);
    if (!game->bullet_active || dx < 0 || dx % 2 != 0 || dx / 2 >= PIM_INVADERS_COLUMNS ||
        dy < 0 || dy >= PIM_INVADERS_ROWS || !game->alive[dy][dx / 2]) return false;
    game->alive[dy][dx / 2] = false;
    game->bullet_active = false;
    game->remaining--;
    game->score = (uint16_t)(game->score + 10 + (PIM_INVADERS_ROWS - 1 - dy) * 5);
    return true;
}

static void invaders_drop_bomb(pim_invaders_t *game) {
    uint8_t start = (uint8_t)(random_next(&game->rng) % PIM_INVADERS_COLUMNS);
    for (uint8_t i = 0; i < PIM_INVADERS_COLUMNS; i++) {
        uint8_t col = (uint8_t)((start + i) % PIM_INVADERS_COLUMNS);
        for (int8_t row = PIM_INVADERS_ROWS - 1; row >= 0; row--) {
            if (!game->alive[row][col]) continue;
            game->bomb_active = true;
            game->bomb_x = (int8_t)(game->offset_x + 2 * col);
            game->bomb_y = (int8_t)(game->offset_y + row + 1);
            return;
        }
    }
}

void pim_invaders_tick(pim_invaders_t *game) {
    int interval;
    if (game->game_over) return;
    game->tick++;
    if (game->bullet_active) {
        game->bullet_y--;
        if (game->bullet_y < 0) game->bullet_active = false;
        else invaders_bullet_hit(game);
    }
    if (game->bomb_active && game->tick % 2 == 0) {
        game->bomb_y++;
        if (game->bomb_y >= PIM_INVADERS_HEIGHT) game->bomb_active = false;
        else if (game->bomb_y == PIM_INVADERS_HEIGHT - 1 && game->bomb_x == game->player_x) {
            game->bomb_active = false;
            if (--game->lives == 0) game->game_over = true;
        }
    } else if (!game->bomb_active && game->tick % (game->wave > 8 ? 6 : 14 - game->wave) == 0) {
        invaders_drop_bomb(game);
    }
    interval = 2 + game->remaining / 3 - (game->wave - 1);
    if (interval < 2) interval = 2;
    if (game->tick % interval == 0) {
        int8_t next = (int8_t)(game->offset_x + game->direction);
        if (next < 0 || next > PIM_INVADERS_MAX_OFFSET) {
            game->direction = (int8_t)-game->direction;
            game->offset_y++;
        } else game->offset_x = next;
        invaders_bullet_hit(game);
    }
    for (int8_t row = PIM_INVADERS_ROWS - 1; row >= 0; row--) {
        bool any = false;
        for (uint8_t col = 0; col < PIM_INVADERS_COLUMNS; col++) any = any || game->alive[row][col];
        if (any) {
            if (game->offset_y + row >= PIM_INVADERS_HEIGHT - 2) game->game_over = true;
            break;
        }
    }
    if (game->remaining == 0 && !game->game_over) { game->wave++; invaders_new_wave(game); }
}

/* ---- Solitaire ---- */

bool pim_solitaire_is_red(uint8_t card) { return card / 13 == 1 || card / 13 == 2; }

void pim_solitaire_new(pim_solitaire_t *game, uint32_t seed) {
    uint8_t deck[52];
    uint32_t state = seed * 2654435761u + 31u;
    uint8_t next = 0;
    memset(game, 0, sizeof(*game));
    for (uint8_t i = 0; i < 52; i++) deck[i] = i;
    for (uint8_t i = 51; i > 0; i--) {
        uint8_t j = (uint8_t)(random_next(&state) % (i + 1u)), swap = deck[i];
        deck[i] = deck[j]; deck[j] = swap;
    }
    for (uint8_t pile = 0; pile < PIM_SOLITAIRE_PILES; pile++) {
        for (uint8_t i = 0; i <= pile; i++) game->tableau[pile][i] = deck[next++];
        game->tableau_count[pile] = (uint8_t)(pile + 1);
        game->face_up[pile] = 1;
    }
    while (next < 52) game->stock[game->stock_count++] = deck[next++];
}

bool pim_solitaire_draw(pim_solitaire_t *game) {
    if (game->stock_count > 0) {
        game->waste[game->waste_count++] = game->stock[--game->stock_count];
    } else if (game->waste_count > 0) {
        while (game->waste_count > 0) game->stock[game->stock_count++] = game->waste[--game->waste_count];
    } else return false;
    game->moves++;
    return true;
}

bool pim_solitaire_move(pim_solitaire_t *game, pim_solitaire_zone_t from, uint8_t from_index, uint8_t count,
                        pim_solitaire_zone_t to, uint8_t to_index) {
    uint8_t cards[PIM_SOLITAIRE_COLUMN_MAX], total = 0;
    if (count == 0 || to == PIM_SOL_WASTE) return false;
    if (from == PIM_SOL_WASTE) {
        if (count != 1 || game->waste_count == 0) return false;
        cards[0] = game->waste[game->waste_count - 1];
    } else if (from == PIM_SOL_FOUNDATION) {
        if (count != 1 || to == PIM_SOL_FOUNDATION || from_index >= 4 || game->foundation[from_index] == 0) return false;
        cards[0] = (uint8_t)(from_index * 13 + game->foundation[from_index] - 1);
    } else {
        if (from_index >= PIM_SOLITAIRE_PILES || count > game->face_up[from_index]) return false;
        for (uint8_t i = 0; i < count; i++) cards[i] = game->tableau[from_index][game->tableau_count[from_index] - count + i];
    }
    if (to == PIM_SOL_FOUNDATION) {
        if (count != 1 || game->foundation[cards[0] / 13] != cards[0] % 13) return false;
    } else {
        if (to_index >= PIM_SOLITAIRE_PILES || (from == PIM_SOL_TABLEAU && from_index == to_index)) return false;
        total = game->tableau_count[to_index];
        if (total == 0) {
            if (cards[0] % 13 != 12) return false;
        } else {
            uint8_t top = game->tableau[to_index][total - 1];
            if (pim_solitaire_is_red(top) == pim_solitaire_is_red(cards[0]) || top % 13 != cards[0] % 13 + 1) return false;
        }
        if (total + count > PIM_SOLITAIRE_COLUMN_MAX) return false;
    }
    if (from == PIM_SOL_WASTE) game->waste_count--;
    else if (from == PIM_SOL_FOUNDATION) game->foundation[from_index]--;
    else {
        game->tableau_count[from_index] = (uint8_t)(game->tableau_count[from_index] - count);
        game->face_up[from_index] = (uint8_t)(game->face_up[from_index] - count);
        if (game->face_up[from_index] == 0 && game->tableau_count[from_index] > 0) game->face_up[from_index] = 1;
    }
    if (to == PIM_SOL_FOUNDATION) game->foundation[cards[0] / 13]++;
    else {
        memcpy(&game->tableau[to_index][total], cards, count);
        game->tableau_count[to_index] = (uint8_t)(total + count);
        game->face_up[to_index] = (uint8_t)(game->face_up[to_index] + count);
    }
    game->moves++;
    game->won = game->foundation[0] + game->foundation[1] + game->foundation[2] + game->foundation[3] == 52;
    return true;
}

bool pim_solitaire_to_foundation(pim_solitaire_t *game, pim_solitaire_zone_t from, uint8_t from_index) {
    uint8_t card;
    if (from == PIM_SOL_WASTE && game->waste_count > 0) card = game->waste[game->waste_count - 1];
    else if (from == PIM_SOL_TABLEAU && from_index < PIM_SOLITAIRE_PILES && game->tableau_count[from_index] > 0)
        card = game->tableau[from_index][game->tableau_count[from_index] - 1];
    else return false;
    return pim_solitaire_move(game, from, from_index, 1, PIM_SOL_FOUNDATION, (uint8_t)(card / 13));
}
