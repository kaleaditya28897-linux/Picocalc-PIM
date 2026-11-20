#ifndef PIM_VTERM_H
#define PIM_VTERM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Screen-oriented VT100/ECMA-48 terminal emulator, free of hardware and transport.
   Telnet feeds it bytes through the IAC filter in pim/telnet.h; the renderer reads
   cells back out. The geometry matches the PicoCalc screen: 320 x 320 pixels with
   the 8x8 unscii font is 40 columns, and 38 rows leave the last 16 pixels for a
   status line. A pim_vt_t is far too large for the 4 KB stack: keep one in static
   storage. */

#ifndef PIM_VT_COLS
#define PIM_VT_COLS 40
#endif
#ifndef PIM_VT_ROWS
#define PIM_VT_ROWS 38
#endif
#ifndef PIM_VT_SCROLLBACK
/* Each line costs COLS * 2 bytes, so this is the one knob that trades RAM for
   how far back you can look. 48 lines is a little over one screenful. */
#define PIM_VT_SCROLLBACK 48
#endif

#define PIM_VT_MAX_PARAMS 16

/* Attribute byte: fg 0-15 (bold folds into the bright half), bg 0-7, inverse.
   Underline is parsed but not stored; an 8x8 cell has no room to show it. */
#define PIM_VT_FG(a)     ((uint8_t)((a) & 0x0fu))
#define PIM_VT_BG(a)     ((uint8_t)(((a) >> 4) & 0x07u))
#define PIM_VT_INVERSE   0x80u
#define PIM_VT_DEFAULT_FG 7u
#define PIM_VT_DEFAULT_BG 0u
#define PIM_VT_ATTR(fg, bg, inv) \
    ((uint8_t)(((fg) & 0x0fu) | (((bg) & 0x07u) << 4) | ((inv) ? PIM_VT_INVERSE : 0u)))
#define PIM_VT_ATTR_DEFAULT PIM_VT_ATTR(PIM_VT_DEFAULT_FG, PIM_VT_DEFAULT_BG, 0)

/* How bytes above 127 are read. CP437 is what a BBS sends: every byte is a
   glyph, so nothing is ever lost or guessed at. UTF-8 is what a modern host
   sends; an incomplete or malformed sequence there falls back to CP437 rather
   than being dropped. The two cannot be told apart by inspection - 0xcd 0xbb is
   both a valid UTF-8 character and a pair of CP437 box-drawing glyphs - so this
   is a setting and not a heuristic. */
typedef enum { PIM_VT_CP437 = 0, PIM_VT_UTF8 = 1 } pim_vt_encoding_t;

/* ch is a CP437 code point: ASCII below 128, box drawing and accents above. */
typedef struct {
    uint8_t ch;
    uint8_t attr;
} pim_vt_cell_t;

/* Keys above 0xff are the ones that need escape sequences rather than a byte. */
enum {
    PIM_VT_KEY_UP = 0x100, PIM_VT_KEY_DOWN, PIM_VT_KEY_RIGHT, PIM_VT_KEY_LEFT,
    PIM_VT_KEY_HOME, PIM_VT_KEY_END, PIM_VT_KEY_PGUP, PIM_VT_KEY_PGDN,
    PIM_VT_KEY_INSERT, PIM_VT_KEY_DELETE,
    PIM_VT_KEY_F1, PIM_VT_KEY_F2, PIM_VT_KEY_F3, PIM_VT_KEY_F4, PIM_VT_KEY_F5,
    PIM_VT_KEY_F6, PIM_VT_KEY_F7, PIM_VT_KEY_F8, PIM_VT_KEY_F9, PIM_VT_KEY_F10
};

#define PIM_VT_TAB_BYTES ((PIM_VT_COLS + 7) / 8)

typedef struct {
    pim_vt_cell_t screen[PIM_VT_ROWS][PIM_VT_COLS];
    pim_vt_cell_t alt[PIM_VT_ROWS][PIM_VT_COLS];
    pim_vt_cell_t history[PIM_VT_SCROLLBACK][PIM_VT_COLS];
    uint16_t hist_first, hist_count;

    uint8_t cx, cy;                 /* cursor, 0-based */
    uint8_t attr;
    uint8_t saved_cx, saved_cy, saved_attr;
    uint8_t top, bottom;            /* scrolling region, inclusive row indices */

    bool wrap_next;                 /* cursor is parked past the last column */
    bool autowrap;                  /* DECAWM */
    bool cursor_visible;            /* DECTCEM */
    bool app_cursor_keys;           /* DECCKM */
    bool insert_mode;               /* IRM */
    bool origin_mode;               /* DECOM: rows are relative to the region */
    bool alt_active;
    uint8_t encoding;               /* pim_vt_encoding_t */

    /* Character sets. charset[n] true means n holds DEC special graphics;
       gl selects which of G0/G1 the printable range draws from (SI/SO). */
    bool charset[4];
    uint8_t gl;
    uint8_t charset_slot;           /* slot the pending ESC ( ) * + designates */
    bool saved_charset[4];
    uint8_t saved_gl;
    bool saved_origin;

    uint8_t tabs[PIM_VT_TAB_BYTES];

    /* parser */
    uint8_t state;
    uint16_t params[PIM_VT_MAX_PARAMS];
    uint8_t param_count;
    bool param_seen;
    bool private_csi;
    uint32_t utf8_cp;
    uint8_t utf8_left;
    uint8_t utf8_bytes[4];          /* kept so a sequence that never completes can be replayed as CP437 */
    uint8_t utf8_have;

    /* per-row dirty span for the renderer; lo > hi means the row is clean */
    uint8_t dirty_lo[PIM_VT_ROWS], dirty_hi[PIM_VT_ROWS];
    uint32_t scrolled;              /* rows scrolled up since the last clear */
} pim_vt_t;

void pim_vt_init(pim_vt_t *vt);
/* Defaults to CP437, which is right for a BBS. Set UTF-8 for a host that speaks
   it, such as an SSH session to a Linux box. */
void pim_vt_set_encoding(pim_vt_t *vt, pim_vt_encoding_t encoding);

/* Consumes bytes from the host. Any reply the session owes (cursor position
   reports, device attributes) is appended to `reply`; returns reply length. */
size_t pim_vt_feed(pim_vt_t *vt, const uint8_t *data, size_t length,
                   uint8_t *reply, size_t reply_size);

/* Encodes a key press for the host. `key` is a byte, or one of the PIM_VT_KEY_*
   values. Returns the number of bytes written. */
size_t pim_vt_encode_key(const pim_vt_t *vt, int key, uint8_t *out, size_t out_size);

/* Rendering. `scroll` is how many rows back into history to look, 0 being live. */
unsigned pim_vt_max_scroll(const pim_vt_t *vt);
const pim_vt_cell_t *pim_vt_view_row(const pim_vt_t *vt, unsigned scroll, unsigned row);

/* Dirty tracking. Returns false when the row needs no redraw. Scrollback
   browsing and scrolls mark everything dirty, so callers can simply redraw. */
bool pim_vt_row_dirty(const pim_vt_t *vt, unsigned row, unsigned *lo, unsigned *hi);
void pim_vt_clear_dirty(pim_vt_t *vt);
void pim_vt_mark_all_dirty(pim_vt_t *vt);

/* Copies the visible screen as text, rows separated by \n, for tests and search. */
void pim_vt_text(const pim_vt_t *vt, char *out, size_t out_size);

/* An ASCII stand-in for a CP437 cell, for renderers whose font stops at 0x7f
   and for text extraction. Returns 0 when the glyph has no sensible ASCII form
   (the box-drawing and block range), which a renderer should draw itself. */
char pim_vt_ascii(uint8_t ch);

#endif
