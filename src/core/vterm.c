#include "pim/vterm.h"

#include <string.h>

enum { S_GROUND, S_ESC, S_CSI, S_OSC, S_OSC_ESC, S_STRING, S_STRING_ESC, S_CHARSET, S_HASH };

#define ROWS PIM_VT_ROWS
#define COLS PIM_VT_COLS

/* ------------------------------------------------------------------ glyphs */

/* Unicode code points worth keeping; everything else folds to '?'. */
static uint8_t unicode_to_cp437(uint32_t cp) {
    static const uint16_t from[] = {
        0x00a0, 0x00a7, 0x00b0, 0x00b1, 0x00b2, 0x00b5, 0x00b7, 0x00bd, 0x00bc,
        0x2022, 0x2190, 0x2191, 0x2192, 0x2193, 0x2500, 0x2502, 0x250c, 0x2510,
        0x2514, 0x2518, 0x251c, 0x2524, 0x252c, 0x2534, 0x253c, 0x2550, 0x2551,
        0x2554, 0x2557, 0x255a, 0x255d, 0x2560, 0x2563, 0x2566, 0x2569, 0x256c,
        0x2580, 0x2584, 0x2588, 0x2591, 0x2592, 0x2593, 0x25a0, 0x25b2, 0x25bc,
        0x25cf, 0x2666
    };
    static const uint8_t to[] = {
        0x20, 0x15, 0xf8, 0xf1, 0xfd, 0xe6, 0xfa, 0xab, 0xac,
        0x07, 0x1b, 0x18, 0x1a, 0x19, 0xc4, 0xb3, 0xda, 0xbf,
        0xc0, 0xd9, 0xc3, 0xb4, 0xc2, 0xc1, 0xc5, 0xcd, 0xba,
        0xc9, 0xbb, 0xc8, 0xbc, 0xcc, 0xb9, 0xcb, 0xca, 0xce,
        0xdf, 0xdc, 0xdb, 0xb0, 0xb1, 0xb2, 0xfe, 0x1e, 0x1f,
        0x07, 0x04
    };
    size_t i;
    if (cp < 0x80) return (uint8_t)cp;
    for (i = 0; i < sizeof(from) / sizeof(from[0]); i++) {
        if (from[i] == cp) return to[i];
    }
    /* Latin-1 letters degrade to their unaccented ASCII base rather than '?'. */
    if (cp >= 0xc0 && cp <= 0xff) {
        static const char base[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
        return (uint8_t)base[cp - 0xc0];
    }
    return (uint8_t)'?';
}

/* DEC special graphics: ASCII in, line-drawing out (ESC ( 0). */
static uint8_t dec_graphic(uint8_t c) {
    switch (c) {
    case '`': return 0x04; case 'a': return 0xb1; case 'f': return 0xf8;
    case 'g': return 0xf1; case 'j': return 0xd9; case 'k': return 0xbf;
    case 'l': return 0xda; case 'm': return 0xc0; case 'n': return 0xc5;
    case 'q': return 0xc4; case 't': return 0xc3; case 'u': return 0xb4;
    case 'v': return 0xc1; case 'w': return 0xc2; case 'x': return 0xb3;
    case '0': return 0xdb; case '~': return 0xfa;
    default: return c;
    }
}

/* A zero means "no ASCII equivalent": the box-drawing, block and shade cells a
   renderer is expected to paint itself. */
char pim_vt_ascii(uint8_t ch) {
    static const char low[32] = {
        ' ', '@', '@', '*', 0,   '*', '*', 0,   'o', 'o', 'o', 'o',
        'o', 'o', 'o', '*', '>', '<', '^', '!', 'P', 'S', '_', '^',
        '^', 'v', '>', '<', '-', '-', 0,   0
    };
    static const char high[128] = {
        /* 0x80 */ 'C', 'u', 'e', 'a', 'a', 'a', 'a', 'c', 'e', 'e', 'e', 'i',
        /* 0x8c */ 'i', 'i', 'A', 'A', 'E', 'a', 'A', 'o', 'o', 'o', 'u', 'u',
        /* 0x98 */ 'y', 'O', 'U', 'c', 'L', 'Y', 'P', 'f', 'a', 'i', 'o', 'u',
        /* 0xa4 */ 'n', 'N', 'a', 'o', '?', '-', '-', '/', '/', '!', '<', '>',
        /* 0xb0 */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        /* 0xbc */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        /* 0xc8 */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        /* 0xd4 */ 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        /* 0xe0 */ 'a', 'B', 'G', 'p', 'S', 's', 'u', 't', 'F', 'O', 'O', 'd',
        /* 0xec */ '8', 'f', 'e', 'n', '=', '+', '>', '<', '|', '|', '/', '~',
        /* 0xf8 */ 'o', '.', '.', 'v', 'n', '2', 0,   ' '
    };
    if (ch < 0x20) return low[ch];
    if (ch < 0x7f) return (char)ch;
    if (ch == 0x7f) return 0;
    return high[ch - 0x80];
}

/* ------------------------------------------------------------------- dirty */

static void touch(pim_vt_t *vt, unsigned row, unsigned lo, unsigned hi) {
    if (row >= ROWS) return;
    if (vt->dirty_lo[row] > vt->dirty_hi[row]) {
        vt->dirty_lo[row] = (uint8_t)lo;
        vt->dirty_hi[row] = (uint8_t)hi;
        return;
    }
    if (lo < vt->dirty_lo[row]) vt->dirty_lo[row] = (uint8_t)lo;
    if (hi > vt->dirty_hi[row]) vt->dirty_hi[row] = (uint8_t)hi;
}

void pim_vt_mark_all_dirty(pim_vt_t *vt) {
    unsigned y;
    for (y = 0; y < ROWS; y++) { vt->dirty_lo[y] = 0; vt->dirty_hi[y] = COLS - 1; }
}

void pim_vt_clear_dirty(pim_vt_t *vt) {
    unsigned y;
    for (y = 0; y < ROWS; y++) { vt->dirty_lo[y] = 1; vt->dirty_hi[y] = 0; }
    vt->scrolled = 0;
}

bool pim_vt_row_dirty(const pim_vt_t *vt, unsigned row, unsigned *lo, unsigned *hi) {
    if (row >= ROWS || vt->dirty_lo[row] > vt->dirty_hi[row]) return false;
    if (lo != NULL) *lo = vt->dirty_lo[row];
    if (hi != NULL) *hi = vt->dirty_hi[row];
    return true;
}

/* ------------------------------------------------------------------ screen */

static pim_vt_cell_t *cell_at(pim_vt_t *vt, unsigned row, unsigned col) {
    return vt->alt_active ? &vt->alt[row][col] : &vt->screen[row][col];
}

static void blank_row(pim_vt_t *vt, unsigned row, uint8_t attr) {
    unsigned x;
    for (x = 0; x < COLS; x++) {
        pim_vt_cell_t *c = cell_at(vt, row, x);
        c->ch = ' ';
        /* Erased cells keep the background but never the inverse flag. */
        c->attr = (uint8_t)(attr & ~PIM_VT_INVERSE);
    }
    touch(vt, row, 0, COLS - 1);
}

static void push_history(pim_vt_t *vt, unsigned row) {
    unsigned slot;
    if (vt->alt_active) return;
    if (vt->hist_count == PIM_VT_SCROLLBACK) {
        slot = vt->hist_first;
        vt->hist_first = (uint16_t)((vt->hist_first + 1) % PIM_VT_SCROLLBACK);
    } else {
        slot = (unsigned)((vt->hist_first + vt->hist_count) % PIM_VT_SCROLLBACK);
        vt->hist_count++;
    }
    memcpy(vt->history[slot], vt->screen[row], sizeof(vt->history[slot]));
}

static void scroll_up(pim_vt_t *vt, unsigned count) {
    unsigned n, y;
    unsigned top = vt->top, bottom = vt->bottom;
    if (count == 0) return;
    if (count > (unsigned)(bottom - top + 1)) count = (unsigned)(bottom - top + 1);
    for (n = 0; n < count; n++) {
        /* Only a full-height region feeds the scrollback, as real terminals do. */
        if (top == 0 && bottom == ROWS - 1) push_history(vt, top);
        for (y = top; y < bottom; y++) {
            memcpy(cell_at(vt, y, 0), cell_at(vt, y + 1, 0), sizeof(pim_vt_cell_t) * COLS);
        }
        blank_row(vt, bottom, vt->attr);
        vt->scrolled++;
    }
    for (y = top; y <= bottom; y++) touch(vt, y, 0, COLS - 1);
}

static void scroll_down(pim_vt_t *vt, unsigned count) {
    unsigned n, y;
    unsigned top = vt->top, bottom = vt->bottom;
    if (count == 0) return;
    if (count > (unsigned)(bottom - top + 1)) count = (unsigned)(bottom - top + 1);
    for (n = 0; n < count; n++) {
        for (y = bottom; y > top; y--) {
            memcpy(cell_at(vt, y, 0), cell_at(vt, y - 1, 0), sizeof(pim_vt_cell_t) * COLS);
        }
        blank_row(vt, top, vt->attr);
    }
    for (y = top; y <= bottom; y++) touch(vt, y, 0, COLS - 1);
}

static void clear_screen(pim_vt_t *vt) {
    unsigned y;
    for (y = 0; y < ROWS; y++) blank_row(vt, y, vt->attr);
}

static void linefeed(pim_vt_t *vt) {
    if (vt->cy == vt->bottom) scroll_up(vt, 1);
    else if (vt->cy + 1 < ROWS) vt->cy++;
    vt->wrap_next = false;
}

/* ------------------------------------------------------------------- tabs */

static void tab_set(pim_vt_t *vt, unsigned col, bool on) {
    if (col >= COLS) return;
    if (on) vt->tabs[col / 8u] |= (uint8_t)(1u << (col % 8u));
    else vt->tabs[col / 8u] &= (uint8_t)~(1u << (col % 8u));
}

static bool tab_at(const pim_vt_t *vt, unsigned col) {
    if (col >= COLS) return false;
    return (vt->tabs[col / 8u] & (1u << (col % 8u))) != 0;
}

static void tabs_reset(pim_vt_t *vt) {
    unsigned x;
    memset(vt->tabs, 0, sizeof(vt->tabs));
    for (x = 8; x < COLS; x += 8) tab_set(vt, x, true);
}

static void tab_forward(pim_vt_t *vt, unsigned count) {
    while (count-- > 0) {
        unsigned x = vt->cx;
        do { x++; } while (x < COLS - 1u && !tab_at(vt, x));
        vt->cx = (uint8_t)(x < COLS ? x : COLS - 1);
    }
    vt->wrap_next = false;
}

static void tab_back(pim_vt_t *vt, unsigned count) {
    while (count-- > 0) {
        unsigned x = vt->cx;
        while (x > 0) { x--; if (tab_at(vt, x) || x == 0) break; }
        vt->cx = (uint8_t)x;
    }
    vt->wrap_next = false;
}

/* ------------------------------------------------------------------ output */

static void put_glyph(pim_vt_t *vt, uint8_t glyph) {
    pim_vt_cell_t *c;
    if (vt->wrap_next && vt->autowrap) {
        vt->cx = 0;
        linefeed(vt);
    }
    vt->wrap_next = false;
    if (vt->cx >= COLS) vt->cx = COLS - 1;
    if (vt->insert_mode) {
        unsigned x;
        for (x = COLS - 1; x > vt->cx; x--) *cell_at(vt, vt->cy, x) = *cell_at(vt, vt->cy, x - 1);
        touch(vt, vt->cy, vt->cx, COLS - 1);
    }
    c = cell_at(vt, vt->cy, vt->cx);
    c->ch = glyph;
    c->attr = vt->attr;
    touch(vt, vt->cy, vt->cx, vt->cx);
    if (vt->cx + 1 >= COLS) vt->wrap_next = true;
    else vt->cx++;
}

static void print_char(pim_vt_t *vt, uint8_t c) {
    put_glyph(vt, vt->charset[vt->gl & 3u] ? dec_graphic(c) : c);
}

/* Puts the bytes of an abandoned UTF-8 sequence on the screen as CP437. */
static void replay_cp437(pim_vt_t *vt) {
    unsigned i;
    for (i = 0; i < vt->utf8_have; i++) put_glyph(vt, vt->utf8_bytes[i]);
    vt->utf8_left = 0;
    vt->utf8_have = 0;
}

/* --------------------------------------------------------------- lifecycle */

void pim_vt_set_encoding(pim_vt_t *vt, pim_vt_encoding_t encoding) {
    /* Anything half-read belongs to the old encoding; put it on the screen
       before switching rather than reinterpreting it. */
    if (vt->utf8_have > 0) replay_cp437(vt);
    vt->encoding = (uint8_t)encoding;
}

void pim_vt_init(pim_vt_t *vt) {
    memset(vt, 0, sizeof(*vt));
    vt->attr = PIM_VT_ATTR_DEFAULT;
    vt->top = 0;
    vt->bottom = ROWS - 1;
    vt->autowrap = true;
    vt->cursor_visible = true;
    vt->saved_attr = PIM_VT_ATTR_DEFAULT;
    tabs_reset(vt);
    clear_screen(vt);
    /* alt screen starts blank too, so a switch to it shows nothing stale */
    vt->alt_active = true;
    clear_screen(vt);
    vt->alt_active = false;
    pim_vt_mark_all_dirty(vt);
}

static void save_cursor(pim_vt_t *vt) {
    vt->saved_cx = vt->cx;
    vt->saved_cy = vt->cy;
    vt->saved_attr = vt->attr;
    vt->saved_gl = vt->gl;
    vt->saved_origin = vt->origin_mode;
    memcpy(vt->saved_charset, vt->charset, sizeof(vt->charset));
}

static void restore_cursor(pim_vt_t *vt) {
    vt->cx = (uint8_t)(vt->saved_cx < COLS ? vt->saved_cx : COLS - 1);
    vt->cy = (uint8_t)(vt->saved_cy < ROWS ? vt->saved_cy : ROWS - 1);
    vt->attr = vt->saved_attr;
    vt->gl = (uint8_t)(vt->saved_gl & 3u);
    vt->origin_mode = vt->saved_origin;
    memcpy(vt->charset, vt->saved_charset, sizeof(vt->charset));
    vt->wrap_next = false;
}

/* -------------------------------------------------------------------- SGR */

/* Truecolour is folded onto the eight base colours by brightness, so a theme
   that only has light and dark still shows the contrast the host intended. */
static uint8_t fold_rgb(unsigned r, unsigned g, unsigned b, bool *bright) {
    unsigned peak = r > g ? r : g;
    if (b > peak) peak = b;
    *bright = peak > 170u;
    if (peak < 64u) return 0u;
    return (uint8_t)(((r > 96u) ? 1u : 0u) | ((g > 96u) ? 2u : 0u) | ((b > 96u) ? 4u : 0u));
}

static void apply_sgr(pim_vt_t *vt) {
    unsigned i;
    uint8_t fg = PIM_VT_FG(vt->attr);
    uint8_t bg = PIM_VT_BG(vt->attr);
    bool inverse = (vt->attr & PIM_VT_INVERSE) != 0;
    if (vt->param_count == 0) {
        vt->attr = PIM_VT_ATTR_DEFAULT;
        return;
    }
    for (i = 0; i < vt->param_count; i++) {
        unsigned p = vt->params[i];
        if (p == 0) { fg = PIM_VT_DEFAULT_FG; bg = PIM_VT_DEFAULT_BG; inverse = false; }
        else if (p == 1) fg |= 8u;                        /* bold -> bright */
        else if (p == 2 || p == 22) fg &= 7u;             /* dim / normal */
        else if (p == 7) inverse = true;
        else if (p == 27) inverse = false;
        else if (p >= 30 && p <= 37) fg = (uint8_t)((fg & 8u) | (p - 30u));
        else if (p == 39) fg = PIM_VT_DEFAULT_FG;
        else if (p >= 40 && p <= 47) bg = (uint8_t)(p - 40u);
        else if (p == 49) bg = PIM_VT_DEFAULT_BG;
        else if (p >= 90 && p <= 97) fg = (uint8_t)((p - 90u) | 8u);
        else if (p >= 100 && p <= 107) bg = (uint8_t)(p - 100u);
        else if ((p == 38 || p == 48) && i + 2 < vt->param_count && vt->params[i + 1] == 5) {
            /* 256-colour: fold onto the 8 base colours so themes stay coherent */
            unsigned idx = vt->params[i + 2];
            uint8_t folded = (uint8_t)(idx < 16 ? (idx & 7u) : ((idx - 16u) % 7u + 1u));
            if (p == 38) fg = (uint8_t)((fg & 8u) | (idx < 16 && idx >= 8 ? folded | 8u : folded));
            else bg = folded;
            i += 2;
        } else if ((p == 38 || p == 48) && i + 4 < vt->param_count && vt->params[i + 1] == 2) {
            bool bright = false;
            uint8_t folded = fold_rgb(vt->params[i + 2], vt->params[i + 3], vt->params[i + 4], &bright);
            if (p == 38) fg = (uint8_t)(folded | (bright ? 8u : 0u));
            else bg = folded;
            i += 4;
        }
    }
    vt->attr = PIM_VT_ATTR(fg, bg, inverse);
}

/* -------------------------------------------------------------------- CSI */

static unsigned param(const pim_vt_t *vt, unsigned index, unsigned fallback) {
    if (index >= vt->param_count) return fallback;
    return vt->params[index] == 0 ? fallback : vt->params[index];
}

static void set_mode(pim_vt_t *vt, bool on) {
    unsigned i;
    for (i = 0; i < vt->param_count; i++) {
        unsigned p = vt->params[i];
        if (!vt->private_csi) {
            if (p == 4) vt->insert_mode = on;       /* IRM */
            continue;
        }
        switch (p) {
        case 1: vt->app_cursor_keys = on; break;
        case 6:                                      /* DECOM */
            vt->origin_mode = on;
            vt->cx = 0;
            vt->cy = on ? vt->top : 0;
            vt->wrap_next = false;
            break;
        case 7: vt->autowrap = on; break;
        case 25: vt->cursor_visible = on; break;
        case 47: case 1047: case 1049:
            if (p == 1049 && on) save_cursor(vt);
            if (on != vt->alt_active) {
                vt->alt_active = on;
                if (on) { clear_screen(vt); vt->cx = 0; vt->cy = 0; vt->wrap_next = false; }
                pim_vt_mark_all_dirty(vt);
            }
            if (p == 1049 && !on) restore_cursor(vt);
            break;
        default: break;
        }
    }
}

static void erase_cells(pim_vt_t *vt, unsigned row, unsigned from, unsigned to) {
    unsigned x;
    if (from > to || row >= ROWS) return;
    if (to >= COLS) to = COLS - 1;
    for (x = from; x <= to; x++) {
        pim_vt_cell_t *c = cell_at(vt, row, x);
        c->ch = ' ';
        c->attr = (uint8_t)(vt->attr & ~PIM_VT_INVERSE);
    }
    touch(vt, row, from, to);
}

/* Rows given by the host are relative to the scrolling region in origin mode. */
static void goto_row(pim_vt_t *vt, unsigned row) {
    unsigned target;
    if (vt->origin_mode) {
        target = (unsigned)vt->top + row - 1u;
        if (target > vt->bottom) target = vt->bottom;
    } else {
        target = row - 1u;
        if (target >= ROWS) target = ROWS - 1;
    }
    vt->cy = (uint8_t)target;
    vt->wrap_next = false;
}

static void append_number(uint8_t *reply, size_t size, size_t *used, unsigned value) {
    char digits[6];
    int n = 0;
    if (value == 0) digits[n++] = '0';
    while (value > 0 && n < (int)sizeof(digits)) { digits[n++] = (char)('0' + value % 10u); value /= 10u; }
    while (n-- > 0) { if (*used < size) reply[(*used)++] = (uint8_t)digits[n]; }
}

static void csi_dispatch(pim_vt_t *vt, uint8_t final, uint8_t *reply, size_t size, size_t *used) {
    unsigned n = param(vt, 0, 1);
    unsigned y;
    switch (final) {
    case 'A': {
        unsigned floor_row = (vt->cy >= vt->top) ? vt->top : 0u;
        vt->cy = (uint8_t)(vt->cy > floor_row + n ? vt->cy - n : floor_row);
        vt->wrap_next = false;
        break;
    }
    case 'B': {
        unsigned ceiling = (vt->cy <= vt->bottom) ? vt->bottom : (unsigned)(ROWS - 1);
        vt->cy = (uint8_t)(vt->cy + n < ceiling ? vt->cy + n : ceiling);
        vt->wrap_next = false;
        break;
    }
    case 'C': vt->cx = (uint8_t)(vt->cx + n < COLS ? vt->cx + n : COLS - 1); vt->wrap_next = false; break;
    case 'D': vt->cx = (uint8_t)(vt->cx > n ? vt->cx - n : 0); vt->wrap_next = false; break;
    case 'E': vt->cx = 0; vt->cy = (uint8_t)(vt->cy + n < ROWS ? vt->cy + n : ROWS - 1); vt->wrap_next = false; break;
    case 'F': vt->cx = 0; vt->cy = (uint8_t)(vt->cy > n ? vt->cy - n : 0); vt->wrap_next = false; break;
    case 'G': case '`': vt->cx = (uint8_t)(n - 1 < COLS ? n - 1 : COLS - 1); vt->wrap_next = false; break;
    case 'd': goto_row(vt, n); break;
    case 'I': tab_forward(vt, n); break;
    case 'Z': tab_back(vt, n); break;
    case 'H': case 'f': {
        unsigned row = param(vt, 0, 1), col = param(vt, 1, 1);
        goto_row(vt, row);
        vt->cx = (uint8_t)(col - 1 < COLS ? col - 1 : COLS - 1);
        vt->wrap_next = false;
        break;
    }
    case 'g':
        if (param(vt, 0, 0) == 3) memset(vt->tabs, 0, sizeof(vt->tabs));
        else tab_set(vt, vt->cx, false);
        break;
    case 'J': {
        unsigned mode = (vt->param_count == 0) ? 0 : vt->params[0];
        if (mode == 0) {
            erase_cells(vt, vt->cy, vt->cx, COLS - 1);
            for (y = vt->cy + 1u; y < ROWS; y++) erase_cells(vt, y, 0, COLS - 1);
        } else if (mode == 1) {
            for (y = 0; y < vt->cy; y++) erase_cells(vt, y, 0, COLS - 1);
            erase_cells(vt, vt->cy, 0, vt->cx);
        } else {
            for (y = 0; y < ROWS; y++) erase_cells(vt, y, 0, COLS - 1);
            /* xterm's ED 3 also drops the scrollback */
            if (mode == 3) { vt->hist_first = 0; vt->hist_count = 0; }
        }
        break;
    }
    case 'K': {
        unsigned mode = (vt->param_count == 0) ? 0 : vt->params[0];
        if (mode == 0) erase_cells(vt, vt->cy, vt->cx, COLS - 1);
        else if (mode == 1) erase_cells(vt, vt->cy, 0, vt->cx);
        else erase_cells(vt, vt->cy, 0, COLS - 1);
        break;
    }
    case 'L': {                                   /* insert lines */
        unsigned save_top = vt->top;
        if (vt->cy >= vt->top && vt->cy <= vt->bottom) {
            vt->top = vt->cy; scroll_down(vt, n); vt->top = (uint8_t)save_top;
            vt->cx = 0; vt->wrap_next = false;
        }
        break;
    }
    case 'M': {                                   /* delete lines */
        unsigned save_top = vt->top;
        if (vt->cy >= vt->top && vt->cy <= vt->bottom) {
            vt->top = vt->cy; scroll_up(vt, n); vt->top = (uint8_t)save_top;
            vt->cx = 0; vt->wrap_next = false;
        }
        break;
    }
    case 'P': {                                   /* delete characters */
        unsigned x;
        if (n > (unsigned)(COLS - vt->cx)) n = (unsigned)(COLS - vt->cx);
        for (x = vt->cx; x + n < COLS; x++) *cell_at(vt, vt->cy, x) = *cell_at(vt, vt->cy, x + n);
        erase_cells(vt, vt->cy, COLS - n, COLS - 1);
        touch(vt, vt->cy, vt->cx, COLS - 1);
        break;
    }
    case '@': {                                   /* insert characters */
        unsigned x;
        if (n > (unsigned)(COLS - vt->cx)) n = (unsigned)(COLS - vt->cx);
        for (x = COLS - 1; x >= vt->cx + n; x--) *cell_at(vt, vt->cy, x) = *cell_at(vt, vt->cy, x - n);
        erase_cells(vt, vt->cy, vt->cx, vt->cx + n - 1);
        touch(vt, vt->cy, vt->cx, COLS - 1);
        break;
    }
    case 'X': erase_cells(vt, vt->cy, vt->cx, vt->cx + n - 1); break;
    case 'S': scroll_up(vt, n); break;
    case 'T': scroll_down(vt, n); break;
    case 'r': {
        unsigned top = param(vt, 0, 1), bottom = param(vt, 1, ROWS);
        if (top < bottom && bottom <= ROWS) {
            vt->top = (uint8_t)(top - 1);
            vt->bottom = (uint8_t)(bottom - 1);
            vt->cx = 0;
            vt->cy = vt->origin_mode ? vt->top : 0;
            vt->wrap_next = false;
        }
        break;
    }
    case 'h': set_mode(vt, true); break;
    case 'l': set_mode(vt, false); break;
    case 'm': apply_sgr(vt); break;
    case 's': save_cursor(vt); break;
    case 'u': restore_cursor(vt); break;
    case 'n': {
        unsigned what = param(vt, 0, 0);
        if (what == 6 && *used + 12 <= size) {
            /* cursor position report: ESC [ row ; col R, in origin-mode terms */
            unsigned row = (unsigned)vt->cy + 1u, col = (unsigned)vt->cx + 1u;
            if (vt->origin_mode) row = (unsigned)(vt->cy - vt->top) + 1u;
            reply[(*used)++] = 0x1b; reply[(*used)++] = '[';
            append_number(reply, size, used, row);
            reply[(*used)++] = ';';
            append_number(reply, size, used, col);
            reply[(*used)++] = 'R';
        } else if (what == 5 && *used + 4 <= size) {
            static const char ok[] = "\033[0n";       /* device is fine */
            memcpy(reply + *used, ok, sizeof(ok) - 1);
            *used += sizeof(ok) - 1;
        }
        break;
    }
    case 'c':
        if (!vt->private_csi && *used + 5 <= size) {   /* identify as a VT102 */
            static const char da[] = "\033[?6c";
            memcpy(reply + *used, da, sizeof(da) - 1);
            *used += sizeof(da) - 1;
        }
        break;
    default: break;
    }
}

/* ------------------------------------------------------------------- feed */

static void reset_params(pim_vt_t *vt) {
    memset(vt->params, 0, sizeof(vt->params));
    vt->param_count = 0;
    vt->param_seen = false;
    vt->private_csi = false;
}

/* C0 controls act wherever they appear, including in the middle of an escape
   sequence, which is what a real terminal does and what hosts rely on. */
static void execute_c0(pim_vt_t *vt, uint8_t c) {
    switch (c) {
    case '\n': case 0x0b: case 0x0c: linefeed(vt); break;
    case '\r': vt->cx = 0; vt->wrap_next = false; break;
    case '\b': if (vt->cx > 0) vt->cx--; vt->wrap_next = false; break;
    case '\t': tab_forward(vt, 1); break;
    case 0x0e: vt->gl = 1; break;                 /* SO: shift to G1 */
    case 0x0f: vt->gl = 0; break;                 /* SI: shift back to G0 */
    default: break;                               /* BEL and friends: the caller owns the speaker */
    }
}

static void decaln(pim_vt_t *vt) {
    unsigned y, x;
    for (y = 0; y < ROWS; y++) {
        for (x = 0; x < COLS; x++) {
            pim_vt_cell_t *c = cell_at(vt, y, x);
            c->ch = 'E';
            c->attr = PIM_VT_ATTR_DEFAULT;
        }
        touch(vt, y, 0, COLS - 1);
    }
    vt->cx = 0; vt->cy = 0; vt->wrap_next = false;
}

size_t pim_vt_feed(pim_vt_t *vt, const uint8_t *data, size_t length,
                   uint8_t *reply, size_t reply_size) {
    size_t used = 0, i;
    for (i = 0; i < length; i++) {
        uint8_t c = data[i];

        /* A UTF-8 tail byte is only valid mid-sequence and never a control. */
        if (vt->encoding == PIM_VT_UTF8 && vt->utf8_left > 0 && vt->state == S_GROUND) {
            if ((c & 0xc0u) == 0x80u) {
                vt->utf8_cp = (vt->utf8_cp << 6) | (uint32_t)(c & 0x3fu);
                if (vt->utf8_have < sizeof(vt->utf8_bytes)) vt->utf8_bytes[vt->utf8_have++] = c;
                if (--vt->utf8_left == 0) { put_glyph(vt, unicode_to_cp437(vt->utf8_cp)); vt->utf8_have = 0; }
                continue;
            }
            /* Not UTF-8 after all. A BBS sends its line drawing as raw CP437, so
               the bytes already taken are put on the screen as CP437 rather than
               thrown away, and this byte is then read afresh. */
            replay_cp437(vt);
        }

        /* A control other than ESC interrupts nothing: it runs and the sequence
           carries on. String states swallow their payload, so they are exempt. */
        if (c < 0x20 && c != 0x1b && vt->state != S_GROUND &&
            vt->state != S_OSC && vt->state != S_OSC_ESC &&
            vt->state != S_STRING && vt->state != S_STRING_ESC) {
            execute_c0(vt, c);
            continue;
        }

        switch (vt->state) {
        case S_ESC:
            switch (c) {
            case '[': vt->state = S_CSI; reset_params(vt); break;
            case ']': vt->state = S_OSC; break;
            case 'P': case '^': case '_': case 'X': vt->state = S_STRING; break;
            case '(': case ')': case '*': case '+':
                vt->charset_slot = (uint8_t)(c == '(' ? 0 : c == ')' ? 1 : c == '*' ? 2 : 3);
                vt->state = S_CHARSET;
                break;
            case '#': vt->state = S_HASH; break;
            case 'M':                                   /* reverse index */
                if (vt->cy == vt->top) scroll_down(vt, 1); else if (vt->cy > 0) vt->cy--;
                vt->wrap_next = false;
                vt->state = S_GROUND; break;
            case 'D': linefeed(vt); vt->state = S_GROUND; break;
            case 'E': vt->cx = 0; linefeed(vt); vt->state = S_GROUND; break;
            case 'H': tab_set(vt, vt->cx, true); vt->state = S_GROUND; break;
            case '7': save_cursor(vt); vt->state = S_GROUND; break;
            case '8': restore_cursor(vt); vt->state = S_GROUND; break;
            case '=': case '>': vt->state = S_GROUND; break;   /* keypad mode */
            case 'c': pim_vt_init(vt); vt->state = S_GROUND; break;
            default: vt->state = S_GROUND; break;
            }
            break;

        case S_CSI:
            if (c >= '0' && c <= '9') {
                unsigned slot = vt->param_count == 0 ? 0 : (unsigned)(vt->param_count - 1u);
                unsigned v = vt->params[slot] * 10u + (unsigned)(c - '0');
                vt->params[slot] = (uint16_t)(v > 9999u ? 9999u : v);
                if (vt->param_count == 0) vt->param_count = 1;
                vt->param_seen = true;
            } else if (c == ';' || c == ':') {
                if (vt->param_count == 0) vt->param_count = 1;
                if (vt->param_count < PIM_VT_MAX_PARAMS) vt->params[vt->param_count++] = 0;
                vt->param_seen = true;
            } else if (c >= 0x3c && c <= 0x3f) {
                /* private parameter prefix; only '?' changes how we read it */
                if (c == '?') vt->private_csi = true;
            } else if (c >= 0x20 && c <= 0x2f) {
                /* intermediate byte: ignored */
            } else if (c >= 0x40 && c <= 0x7e) {
                csi_dispatch(vt, c, reply, reply_size, &used);
                vt->state = S_GROUND;
            } else {
                vt->state = S_GROUND;
            }
            break;

        case S_OSC:                      /* ESC ] ... BEL | ESC \ */
            if (c == 0x07) vt->state = S_GROUND;
            else if (c == 0x1b) vt->state = S_OSC_ESC;
            break;
        case S_OSC_ESC:
            vt->state = (c == '\\') ? S_GROUND : S_OSC;
            break;
        case S_STRING:                   /* DCS / APC / PM / SOS */
            if (c == 0x1b) vt->state = S_STRING_ESC;
            break;
        case S_STRING_ESC:
            vt->state = (c == '\\') ? S_GROUND : S_STRING;
            break;
        case S_CHARSET:
            /* 0 and 2 are the line-drawing sets; B, A, and the rest are text. */
            vt->charset[vt->charset_slot & 3u] = (c == '0' || c == '2');
            vt->state = S_GROUND;
            break;
        case S_HASH:
            if (c == '8') decaln(vt);
            vt->state = S_GROUND;
            break;

        default:                          /* S_GROUND */
            if (c == 0x1b) { vt->state = S_ESC; }
            else if (c == 0x9b) { vt->state = S_CSI; reset_params(vt); }  /* 8-bit CSI */
            else if (c < 0x20) execute_c0(vt, c);
            else if (c < 0x80) print_char(vt, c);
            /* In CP437 every high byte is already a glyph. In UTF-8 a lead byte
               waits for its continuation, and anything that cannot begin a
               sequence is taken as CP437 as it stands. */
            else if (vt->encoding == PIM_VT_CP437) put_glyph(vt, c);
            else if ((c & 0xe0u) == 0xc0u) { vt->utf8_cp = c & 0x1fu; vt->utf8_left = 1; vt->utf8_bytes[0] = c; vt->utf8_have = 1; }
            else if ((c & 0xf0u) == 0xe0u) { vt->utf8_cp = c & 0x0fu; vt->utf8_left = 2; vt->utf8_bytes[0] = c; vt->utf8_have = 1; }
            else if ((c & 0xf8u) == 0xf0u) { vt->utf8_cp = c & 0x07u; vt->utf8_left = 3; vt->utf8_bytes[0] = c; vt->utf8_have = 1; }
            else put_glyph(vt, c);
            break;
        }
    }
    return used;
}

/* -------------------------------------------------------------------- keys */

static size_t emit(uint8_t *out, size_t size, const char *text) {
    size_t n = strlen(text);
    if (n > size) return 0;
    memcpy(out, text, n);
    return n;
}

size_t pim_vt_encode_key(const pim_vt_t *vt, int key, uint8_t *out, size_t out_size) {
    const char *seq = NULL;
    bool app = vt->app_cursor_keys;
    switch (key) {
    case PIM_VT_KEY_UP:     seq = app ? "\033OA" : "\033[A"; break;
    case PIM_VT_KEY_DOWN:   seq = app ? "\033OB" : "\033[B"; break;
    case PIM_VT_KEY_RIGHT:  seq = app ? "\033OC" : "\033[C"; break;
    case PIM_VT_KEY_LEFT:   seq = app ? "\033OD" : "\033[D"; break;
    case PIM_VT_KEY_HOME:   seq = "\033[H";  break;
    case PIM_VT_KEY_END:    seq = "\033[F";  break;
    case PIM_VT_KEY_PGUP:   seq = "\033[5~"; break;
    case PIM_VT_KEY_PGDN:   seq = "\033[6~"; break;
    case PIM_VT_KEY_INSERT: seq = "\033[2~"; break;
    case PIM_VT_KEY_DELETE: seq = "\033[3~"; break;
    case PIM_VT_KEY_F1:  seq = "\033OP";   break;
    case PIM_VT_KEY_F2:  seq = "\033OQ";   break;
    case PIM_VT_KEY_F3:  seq = "\033OR";   break;
    case PIM_VT_KEY_F4:  seq = "\033OS";   break;
    case PIM_VT_KEY_F5:  seq = "\033[15~"; break;
    case PIM_VT_KEY_F6:  seq = "\033[17~"; break;
    case PIM_VT_KEY_F7:  seq = "\033[18~"; break;
    case PIM_VT_KEY_F8:  seq = "\033[19~"; break;
    case PIM_VT_KEY_F9:  seq = "\033[20~"; break;
    case PIM_VT_KEY_F10: seq = "\033[21~"; break;
    default: break;
    }
    if (seq != NULL) return emit(out, out_size, seq);
    if (key < 0 || key > 0xff || out_size == 0) return 0;
    /* Enter sends CR; the host's termios turns it into a newline. */
    out[0] = (uint8_t)(key == '\n' ? '\r' : key);
    return 1;
}

/* ------------------------------------------------------------------ output */

unsigned pim_vt_max_scroll(const pim_vt_t *vt) {
    return vt->alt_active ? 0u : vt->hist_count;
}

const pim_vt_cell_t *pim_vt_view_row(const pim_vt_t *vt, unsigned scroll, unsigned row) {
    unsigned max = pim_vt_max_scroll(vt);
    unsigned virt;
    if (row >= ROWS) row = ROWS - 1;
    if (scroll > max) scroll = max;
    if (vt->alt_active) return vt->alt[row];
    /* History and the live screen form one buffer; the window slides back over it. */
    virt = (unsigned)vt->hist_count - scroll + row;
    if (virt < vt->hist_count) {
        unsigned index = (unsigned)((vt->hist_first + virt) % PIM_VT_SCROLLBACK);
        return vt->history[index];
    }
    {
        unsigned screen_row = virt - vt->hist_count;
        if (screen_row >= ROWS) screen_row = ROWS - 1;
        return vt->screen[screen_row];
    }
}

void pim_vt_text(const pim_vt_t *vt, char *out, size_t out_size) {
    size_t used = 0;
    unsigned y, x;
    if (out_size == 0) return;
    for (y = 0; y < ROWS; y++) {
        const pim_vt_cell_t *row = vt->alt_active ? vt->alt[y] : vt->screen[y];
        unsigned last = 0;
        bool any = false;
        for (x = 0; x < COLS; x++) {
            if (row[x].ch != ' ' && row[x].ch != 0) { last = x; any = true; }
        }
        if (any) {
            for (x = 0; x <= last && used + 1 < out_size; x++) {
                uint8_t ch = row[x].ch;
                out[used++] = (char)(ch == 0 ? ' ' : ch);
            }
        }
        if (y + 1 < ROWS && used + 1 < out_size) out[used++] = '\n';
    }
    out[used] = '\0';
}
