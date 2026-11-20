#include <stdio.h>
#include <string.h>

#include "pim/vterm.h"

static int failures;
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "%s:%d %s\n", __FILE__, __LINE__, #value); failures++; } } while (0)

/* Far too big for the stack; the firmware keeps one of these in .bss too. */
static pim_vt_t vt;

static void feed(const char *text) {
    uint8_t reply[32];
    pim_vt_feed(&vt, (const uint8_t *)text, strlen(text), reply, sizeof(reply));
}

static size_t feed_reply(const char *text, uint8_t *reply, size_t size) {
    return pim_vt_feed(&vt, (const uint8_t *)text, strlen(text), reply, size);
}

/* Reads a visible row as a NUL-terminated string with trailing blanks removed. */
static const char *row_text(unsigned y) {
    static char buffer[PIM_VT_COLS + 1];
    const pim_vt_cell_t *row = pim_vt_view_row(&vt, 0, y);
    unsigned x, last = 0;
    bool any = false;
    for (x = 0; x < PIM_VT_COLS; x++) {
        buffer[x] = (char)(row[x].ch == 0 ? ' ' : row[x].ch);
        if (row[x].ch != ' ' && row[x].ch != 0) { last = x; any = true; }
    }
    buffer[any ? last + 1 : 0] = '\0';
    return buffer;
}

static void test_basic_text(void) {
    pim_vt_init(&vt);
    feed("hello");
    CHECK(strcmp(row_text(0), "hello") == 0);
    CHECK(vt.cx == 5 && vt.cy == 0);

    feed("\r\nworld");
    CHECK(strcmp(row_text(1), "world") == 0);
    CHECK(vt.cx == 5 && vt.cy == 1);

    /* backspace erases by overwriting, as a shell does */
    feed("\b \b");
    CHECK(strcmp(row_text(1), "worl") == 0);

    /* tab snaps to the next multiple of eight */
    pim_vt_init(&vt);
    feed("ab\tc");
    CHECK(strcmp(row_text(0), "ab      c") == 0);
}

static void test_wrap(void) {
    char line[PIM_VT_COLS + 8];
    unsigned i;
    pim_vt_init(&vt);
    for (i = 0; i < PIM_VT_COLS; i++) line[i] = 'x';
    line[PIM_VT_COLS] = 'Y';
    line[PIM_VT_COLS + 1] = '\0';
    feed(line);
    /* the last column is filled before the wrap happens, never skipped */
    CHECK(row_text(0)[PIM_VT_COLS - 1] == 'x');
    CHECK(strcmp(row_text(1), "Y") == 0);
    CHECK(vt.cy == 1);

    /* with autowrap off the final column is overwritten in place */
    pim_vt_init(&vt);
    feed("\033[?7l");
    feed(line);
    CHECK(row_text(0)[PIM_VT_COLS - 1] == 'Y');
    CHECK(vt.cy == 0);
}

static void test_cursor_and_erase(void) {
    pim_vt_init(&vt);
    feed("\033[3;5Hmark");
    CHECK(strcmp(row_text(2), "    mark") == 0);
    CHECK(vt.cy == 2 && vt.cx == 8);

    /* CUP is 1-based and clamps rather than wrapping */
    feed("\033[999;999H");
    CHECK(vt.cy == PIM_VT_ROWS - 1 && vt.cx == PIM_VT_COLS - 1);

    /* erase to end of line */
    feed("\033[3;7H\033[K");
    CHECK(strcmp(row_text(2), "    ma") == 0);

    /* erase whole display */
    feed("\033[2J");
    CHECK(strcmp(row_text(2), "") == 0);

    /* erase to start of line keeps the tail */
    pim_vt_init(&vt);
    feed("abcdef\033[1;4H\033[1K");
    CHECK(strcmp(row_text(0), "    ef") == 0);
}

static void test_sgr(void) {
    const pim_vt_cell_t *row;
    pim_vt_init(&vt);
    feed("\033[31mR\033[1mB\033[0mN");
    row = pim_vt_view_row(&vt, 0, 0);
    CHECK(PIM_VT_FG(row[0].attr) == 1);                 /* red */
    CHECK(PIM_VT_FG(row[1].attr) == (1u | 8u));         /* bold red -> bright */
    CHECK(row[2].attr == PIM_VT_ATTR_DEFAULT);

    feed("\033[44;37mX");
    row = pim_vt_view_row(&vt, 0, 0);
    CHECK(PIM_VT_BG(row[3].attr) == 4 && PIM_VT_FG(row[3].attr) == 7);

    /* inverse on and off */
    feed("\033[7mI\033[27mJ");
    row = pim_vt_view_row(&vt, 0, 0);
    CHECK((row[4].attr & PIM_VT_INVERSE) != 0);
    CHECK((row[5].attr & PIM_VT_INVERSE) == 0);

    /* a 256-colour request folds onto the base palette instead of being dropped */
    feed("\033[38;5;9mZ");
    row = pim_vt_view_row(&vt, 0, 0);
    CHECK(PIM_VT_FG(row[6].attr) != PIM_VT_DEFAULT_FG);
}

static void test_scroll_and_history(void) {
    char buffer[16], newest[16];
    unsigned i;
    pim_vt_init(&vt);
    for (i = 0; i < PIM_VT_ROWS + 5u; i++) {
        snprintf(buffer, sizeof(buffer), "L%u\r\n", i);
        feed(buffer);
    }
    /* the newest lines are on screen; the last printed line sits one row up */
    snprintf(newest, sizeof(newest), "L%u", PIM_VT_ROWS + 4u);
    CHECK(strcmp(row_text(PIM_VT_ROWS - 2), newest) == 0);
    CHECK(pim_vt_max_scroll(&vt) >= 5);

    /* scrolling all the way back reaches the oldest line still in history */
    {
        const pim_vt_cell_t *row = pim_vt_view_row(&vt, pim_vt_max_scroll(&vt), 0);
        CHECK(row[0].ch == 'L' && row[1].ch == '0');
    }
    /* one row further down the same view is the line after it */
    {
        const pim_vt_cell_t *row = pim_vt_view_row(&vt, pim_vt_max_scroll(&vt), 1);
        CHECK(row[0].ch == 'L' && row[1].ch == '1');
    }
    /* scroll 0 is still the live screen */
    CHECK(strcmp(row_text(PIM_VT_ROWS - 2), newest) == 0);
}

static void test_scroll_region(void) {
    pim_vt_init(&vt);
    feed("\033[1;3r");                 /* region is rows 1..3 */
    CHECK(vt.top == 0 && vt.bottom == 2);
    feed("\033[1;1Ha\r\nb\r\nc");
    CHECK(strcmp(row_text(0), "a") == 0);
    CHECK(strcmp(row_text(2), "c") == 0);
    feed("\r\n");                      /* scrolls only inside the region */
    CHECK(strcmp(row_text(0), "b") == 0);
    CHECK(strcmp(row_text(1), "c") == 0);
    CHECK(strcmp(row_text(2), "") == 0);
}

static void test_insert_delete(void) {
    pim_vt_init(&vt);
    feed("abcdef\033[1;3H\033[2P");     /* delete two chars at column 3 */
    CHECK(strcmp(row_text(0), "abef") == 0);

    pim_vt_init(&vt);
    feed("abcdef\033[1;3H\033[2@");     /* insert two blanks */
    CHECK(strcmp(row_text(0), "ab  cdef") == 0);

    pim_vt_init(&vt);
    feed("one\r\ntwo\r\nthree\033[1;1H\033[M");  /* delete the first line */
    CHECK(strcmp(row_text(0), "two") == 0);
    CHECK(strcmp(row_text(1), "three") == 0);

    pim_vt_init(&vt);
    feed("one\r\ntwo\033[1;1H\033[L");   /* insert a line */
    CHECK(strcmp(row_text(0), "") == 0);
    CHECK(strcmp(row_text(1), "one") == 0);
}

static void test_alt_screen(void) {
    pim_vt_init(&vt);
    feed("shell output");
    feed("\033[?1049h");                 /* vim starts */
    CHECK(vt.alt_active);
    CHECK(strcmp(row_text(0), "") == 0);
    feed("editor");
    CHECK(strcmp(row_text(0), "editor") == 0);
    CHECK(pim_vt_max_scroll(&vt) == 0);  /* no scrollback on the alt screen */
    feed("\033[?1049l");                 /* vim exits */
    CHECK(!vt.alt_active);
    CHECK(strcmp(row_text(0), "shell output") == 0);
}

static void test_utf8_and_graphics(void) {
    const pim_vt_cell_t *row;
    pim_vt_init(&vt);
    pim_vt_set_encoding(&vt, PIM_VT_UTF8);
    feed("\xe2\x94\x80\xe2\x94\x82");    /* U+2500 U+2502 box drawing */
    row = pim_vt_view_row(&vt, 0, 0);
    CHECK(row[0].ch == 0xc4 && row[1].ch == 0xb3);

    feed("\xc3\xa9");                    /* e-acute degrades to 'e' */
    row = pim_vt_view_row(&vt, 0, 0);
    CHECK(row[2].ch == 'e');

    /* A sequence that never completes is not thrown away: its bytes are CP437,
       which is how a BBS sends its line drawing, and the character after it
       still lands in the right place. */
    pim_vt_init(&vt);
    pim_vt_set_encoding(&vt, PIM_VT_UTF8);
    feed("\xe2\x94");
    feed("A");
    row = pim_vt_view_row(&vt, 0, 0);
    CHECK(row[0].ch == 0xe2 && row[1].ch == 0x94 && row[2].ch == 'A');
    CHECK(vt.cx == 3);

    /* and a sequence split across two reads still decodes as one character */
    pim_vt_init(&vt);
    pim_vt_set_encoding(&vt, PIM_VT_UTF8);
    feed("\xe2\x94");
    feed("\x80");
    row = pim_vt_view_row(&vt, 0, 0);
    CHECK(row[0].ch == 0xc4 && vt.cx == 1);

    /* The default is CP437, where every high byte is a glyph of its own. */
    pim_vt_init(&vt);
    feed("\xc9\xcd\xbb");
    row = pim_vt_view_row(&vt, 0, 0);
    CHECK(row[0].ch == 0xc9 && row[1].ch == 0xcd && row[2].ch == 0xbb);
    CHECK(vt.cx == 3);

    /* DEC special graphics maps ASCII onto line drawing */
    pim_vt_init(&vt);
    feed("\033(0qx\033(B");
    row = pim_vt_view_row(&vt, 0, 0);
    CHECK(row[0].ch == 0xc4 && row[1].ch == 0xb3);
    feed("q");
    row = pim_vt_view_row(&vt, 0, 0);
    CHECK(row[2].ch == 'q');             /* charset reset back to ASCII */
}

/* The common idiom is ESC ) 0 once, then SO/SI around each run of line drawing. */
static void test_g1_charset(void) {
    const pim_vt_cell_t *row;
    pim_vt_init(&vt);
    feed("\033)0");                      /* G1 is line drawing, G0 still ASCII */
    feed("q");
    CHECK(row_text(0)[0] == 'q');        /* nothing is shifted in yet */
    feed("\016q");                       /* SO: shift to G1 */
    row = pim_vt_view_row(&vt, 0, 0);
    CHECK(row[1].ch == 0xc4);
    feed("\017q");                       /* SI: back to G0 */
    row = pim_vt_view_row(&vt, 0, 0);
    CHECK(row[2].ch == 'q');

    /* a saved cursor carries the charset state with it */
    pim_vt_init(&vt);
    feed("\033(0\0337\033(B\0338x");
    row = pim_vt_view_row(&vt, 0, 0);
    CHECK(row[0].ch == 0xb3);
}

static void test_tabs(void) {
    pim_vt_init(&vt);
    feed("\033[1;1H\tA");                /* default stops every eight columns */
    CHECK(strcmp(row_text(0), "        A") == 0);

    /* HTS sets a stop, TBC 3 clears them all */
    pim_vt_init(&vt);
    feed("\033[1;4H\033H\033[1;1H\tB");
    CHECK(strcmp(row_text(0), "   B") == 0);
    feed("\033[3g\033[1;1H\tC");         /* no stops left: tab parks at the end */
    CHECK(row_text(0)[PIM_VT_COLS - 1] == 'C');

    /* CHT and CBT walk whole stops */
    pim_vt_init(&vt);
    feed("\033[1;1H\033[2IX");
    CHECK(vt.cx == 17);
    feed("\033[1;20H\033[1ZY");
    CHECK(vt.cx == 17);
}

static void test_insert_mode(void) {
    pim_vt_init(&vt);
    feed("abc\033[1;1H\033[4hXY");       /* IRM pushes the line right */
    CHECK(strcmp(row_text(0), "XYabc") == 0);
    feed("\033[4l\033[1;1HZ");           /* back to overwrite */
    CHECK(strcmp(row_text(0), "ZYabc") == 0);
}

static void test_origin_mode(void) {
    pim_vt_init(&vt);
    feed("\033[5;10r\033[?6h");          /* region rows 5..10, origin mode on */
    CHECK(vt.cy == 4 && vt.cx == 0);
    feed("\033[2;1Hhere");               /* row 2 of the region is screen row 6 */
    CHECK(strcmp(row_text(5), "here") == 0);
    feed("\033[99;1H");                  /* clamped to the bottom of the region */
    CHECK(vt.cy == 9);
    feed("\033[?6l\033[2;1Hthere");      /* absolute again */
    CHECK(strcmp(row_text(1), "there") == 0);
}

static void test_margins_clamp_cursor(void) {
    pim_vt_init(&vt);
    feed("\033[5;10r\033[7;1H\033[20A"); /* CUU stops at the top margin */
    CHECK(vt.cy == 4);
    feed("\033[20B");                    /* CUD stops at the bottom margin */
    CHECK(vt.cy == 9);
}

static void test_controls_inside_sequences(void) {
    pim_vt_init(&vt);
    /* a CR arriving mid-sequence acts at once and the sequence still completes */
    feed("abc\033[\r1;1Hz");
    CHECK(strcmp(row_text(0), "zbc") == 0);
}

static void test_eight_bit_csi_and_decaln(void) {
    pim_vt_init(&vt);
    feed("\x9b""2J\x9b""1;1Hok");
    CHECK(strcmp(row_text(0), "ok") == 0);

    pim_vt_init(&vt);
    feed("\033#8");                      /* DECALN fills the screen with E */
    CHECK(row_text(0)[0] == 'E');
    CHECK(row_text(PIM_VT_ROWS - 1)[PIM_VT_COLS - 1] == 'E');
    CHECK(vt.cx == 0 && vt.cy == 0);
}

static void test_scrollback_reset(void) {
    unsigned i;
    pim_vt_init(&vt);
    for (i = 0; i < PIM_VT_ROWS + 10u; i++) feed("x\r\n");
    CHECK(pim_vt_max_scroll(&vt) > 0);
    feed("\033[3J");                     /* ED 3 drops the scrollback too */
    CHECK(pim_vt_max_scroll(&vt) == 0);
}

static void test_ascii_fallback(void) {
    CHECK(pim_vt_ascii('A') == 'A');
    CHECK(pim_vt_ascii(' ') == ' ');
    CHECK(pim_vt_ascii(0) == ' ');
    CHECK(pim_vt_ascii(0xc4) == 0);      /* box drawing: the renderer paints it */
    CHECK(pim_vt_ascii(0xdb) == 0);
    CHECK(pim_vt_ascii(0x82) == 'e');    /* e-acute */
    CHECK(pim_vt_ascii(0xf1) == '+');    /* plus-minus */
}

static void test_strings_are_swallowed(void) {
    pim_vt_init(&vt);
    feed("\033]0;window title\007visible");
    CHECK(strcmp(row_text(0), "visible") == 0);

    pim_vt_init(&vt);
    feed("\033]0;title\033\\ok");
    CHECK(strcmp(row_text(0), "ok") == 0);

    pim_vt_init(&vt);
    feed("\033Psome dcs\033\\after");
    CHECK(strcmp(row_text(0), "after") == 0);
}

static void test_replies(void) {
    uint8_t reply[32];
    size_t n;
    pim_vt_init(&vt);
    feed("\033[5;9H");
    n = feed_reply("\033[6n", reply, sizeof(reply));
    reply[n] = '\0';
    CHECK(n > 0 && strcmp((char *)reply, "\033[5;9R") == 0);

    n = feed_reply("\033[c", reply, sizeof(reply));
    CHECK(n == 5 && memcmp(reply, "\033[?6c", 5) == 0);
}

static void test_keys(void) {
    uint8_t out[8];
    size_t n;
    pim_vt_init(&vt);
    n = pim_vt_encode_key(&vt, PIM_VT_KEY_UP, out, sizeof(out));
    CHECK(n == 3 && memcmp(out, "\033[A", 3) == 0);

    feed("\033[?1h");                    /* application cursor keys */
    n = pim_vt_encode_key(&vt, PIM_VT_KEY_UP, out, sizeof(out));
    CHECK(n == 3 && memcmp(out, "\033OA", 3) == 0);

    n = pim_vt_encode_key(&vt, 'a', out, sizeof(out));
    CHECK(n == 1 && out[0] == 'a');

    n = pim_vt_encode_key(&vt, '\n', out, sizeof(out));
    CHECK(n == 1 && out[0] == '\r');

    n = pim_vt_encode_key(&vt, PIM_VT_KEY_F5, out, sizeof(out));
    CHECK(n == 5 && memcmp(out, "\033[15~", 5) == 0);
}

static void test_dirty(void) {
    unsigned lo, hi;
    pim_vt_init(&vt);
    pim_vt_clear_dirty(&vt);
    CHECK(!pim_vt_row_dirty(&vt, 0, &lo, &hi));

    feed("\033[2;4Hhi");
    CHECK(!pim_vt_row_dirty(&vt, 0, &lo, &hi));
    CHECK(pim_vt_row_dirty(&vt, 1, &lo, &hi));
    CHECK(lo == 3 && hi == 4);

    /* a scroll is reported so the renderer can use the panel's hardware scroll */
    pim_vt_clear_dirty(&vt);
    feed("\033[99;1H\r\n");
    CHECK(vt.scrolled == 1);
}

static void test_text_dump(void) {
    char buffer[PIM_VT_ROWS * (PIM_VT_COLS + 1) + 1];
    pim_vt_init(&vt);
    feed("one\r\ntwo");
    pim_vt_text(&vt, buffer, sizeof(buffer));
    CHECK(strncmp(buffer, "one\ntwo\n", 8) == 0);
}

/* However the host's bytes are split across reads, the screen must come out the
   same. This is the one invariant that catches a parser holding state it should
   not, and it is the realistic case: TCP chops wherever it likes. */
static void test_chunking_is_invisible(void) {
    static const char stream[] =
        "\033[2J\033[H\033[1;36m\xe2\x95\x94\xe2\x95\x90\xe2\x95\x97\033[0m\r\n"
        "\033[7m Main menu \033[27m\r\n"
        "\033[32m(1)\033[0m Files  \033[32m(2)\033[0m Mail\r\n"
        "\033[5;10HPick: \033[s\033[1A\033[u"
        "\033[?25l\033[4h\033[4l\033[?25h"
        "\033]0;a title\007\033(0qqx\033(B"
        "\033[10;1H\033[1;5r\033[2Sline\033[r";
    char reference[PIM_VT_ROWS * (PIM_VT_COLS + 1) + 1];
    unsigned attrs[PIM_VT_ROWS][PIM_VT_COLS];
    unsigned chunk, y, x;

    pim_vt_init(&vt);
    feed(stream);
    pim_vt_text(&vt, reference, sizeof(reference));
    for (y = 0; y < PIM_VT_ROWS; y++) {
        const pim_vt_cell_t *row = pim_vt_view_row(&vt, 0, y);
        for (x = 0; x < PIM_VT_COLS; x++) attrs[y][x] = row[x].attr;
    }

    /* every chunk size from one byte upwards */
    for (chunk = 1; chunk <= 7; chunk++) {
        char again[sizeof(reference)];
        size_t offset, length = strlen(stream);
        uint8_t reply[32];
        pim_vt_init(&vt);
        for (offset = 0; offset < length; offset += chunk) {
            size_t count = length - offset < chunk ? length - offset : chunk;
            pim_vt_feed(&vt, (const uint8_t *)stream + offset, count, reply, sizeof(reply));
        }
        pim_vt_text(&vt, again, sizeof(again));
        CHECK(strcmp(again, reference) == 0);
        if (strcmp(again, reference) != 0) { printf("  chunk %u differs\n", chunk); break; }
        for (y = 0; y < PIM_VT_ROWS; y++) {
            const pim_vt_cell_t *row = pim_vt_view_row(&vt, 0, y);
            for (x = 0; x < PIM_VT_COLS; x++) {
                if (row[x].attr != attrs[y][x]) { CHECK(row[x].attr == attrs[y][x]); return; }
            }
        }
    }
}

/* What a BBS actually sends: positioning, colour, and CP437 art as raw high
   bytes rather than UTF-8. */
static void test_bbs_screen(void) {
    const pim_vt_cell_t *row;
    pim_vt_init(&vt);
    feed("\033[2J\033[1;1H");
    feed("\xc9\xcd\xcd\xbb");                 /* double-line box corners */
    row = pim_vt_view_row(&vt, 0, 0);
    CHECK(row[0].ch == 0xc9 && row[1].ch == 0xcd && row[3].ch == 0xbb);
    feed("\033[2;1H\033[44;1;37m WELCOME \033[0m");
    row = pim_vt_view_row(&vt, 0, 1);
    CHECK(PIM_VT_BG(row[1].attr) == 4 && PIM_VT_FG(row[1].attr) == (7u | 8u));
    CHECK(row[9].attr == PIM_VT_ATTR_DEFAULT);  /* the reset took effect */
    /* a reverse-video status bar, as menus and editors draw */
    feed("\033[3;1H\033[7mstatus\033[0m");
    row = pim_vt_view_row(&vt, 0, 2);
    CHECK((row[0].attr & PIM_VT_INVERSE) != 0);
    CHECK(strcmp(row_text(2), "status") == 0);
}

/* A full-screen editor: alternate screen, a scrolling region, lines inserted
   and deleted, then a clean exit back to the shell's screen. */
static void test_fullscreen_app(void) {
    pim_vt_init(&vt);
    feed("$ ls\r\nfile.txt\r\n$ vi file.txt\r\n");
    CHECK(strcmp(row_text(1), "file.txt") == 0);

    feed("\033[?1049h\033[2J\033[H");          /* enter the alternate screen */
    CHECK(vt.alt_active && strcmp(row_text(0), "") == 0);
    feed("\033[1;5rhello\r\nworld");           /* region, then two lines */
    CHECK(strcmp(row_text(0), "hello") == 0);
    CHECK(strcmp(row_text(1), "world") == 0);
    feed("\033[1;1H\033[L");                    /* open a line above */
    CHECK(strcmp(row_text(0), "") == 0 && strcmp(row_text(1), "hello") == 0);
    feed("\033[1;1H\033[M");                    /* and take it back out */
    CHECK(strcmp(row_text(0), "hello") == 0);
    feed("\033[5;1H\033[7m-- INSERT --\033[0m");
    CHECK(strcmp(row_text(4), "-- INSERT --") == 0);

    feed("\033[r\033[?1049l");                  /* quit: region reset, screen back */
    CHECK(!vt.alt_active);
    CHECK(strcmp(row_text(1), "file.txt") == 0);  /* the shell's screen survived */
    CHECK(vt.top == 0 && vt.bottom == PIM_VT_ROWS - 1);
}

/* Random and semi-structured garbage must never escape the buffers. A terminal
   parses whatever the far end sends, so this is the realistic hostile case. */
static void test_fuzz(void) {
    static const char seeds[][8] = {
        "\033[", "\033]", "\033(", "\033P", "\033#", "\033[?", "\033[9999",
        "\033[;;;;", "\033[r", "\033[m", "\xe2\x94", "\xff", "\033[1049h"
    };
    uint32_t state = 12345u;
    unsigned i;
    pim_vt_init(&vt);
    for (i = 0; i < 200000u; i++) {
        uint8_t byte;
        uint8_t reply[32];
        state = state * 1103515245u + 12345u;
        if ((state >> 28) == 0) {
            const char *seed = seeds[(state >> 8) % (sizeof(seeds) / sizeof(seeds[0]))];
            pim_vt_feed(&vt, (const uint8_t *)seed, strlen(seed), reply, sizeof(reply));
            continue;
        }
        byte = (uint8_t)((state >> 16) & 0xffu);
        pim_vt_feed(&vt, &byte, 1, reply, sizeof(reply));
        /* invariants that must hold after every single byte */
        if (vt.cx >= PIM_VT_COLS || vt.cy >= PIM_VT_ROWS) {
            fprintf(stderr, "cursor escaped at %u: cx=%u cy=%u\n", i, vt.cx, vt.cy);
            failures++;
            return;
        }
        if (vt.top > vt.bottom || vt.bottom >= PIM_VT_ROWS) {
            fprintf(stderr, "bad scroll region at %u: %u..%u\n", i, vt.top, vt.bottom);
            failures++;
            return;
        }
        if (vt.hist_count > PIM_VT_SCROLLBACK || vt.param_count > PIM_VT_MAX_PARAMS) {
            fprintf(stderr, "buffer overrun at %u\n", i);
            failures++;
            return;
        }
    }
    /* every reachable view must stay in bounds */
    {
        unsigned s, y;
        for (s = 0; s <= pim_vt_max_scroll(&vt); s++) {
            for (y = 0; y < PIM_VT_ROWS; y++) {
                const pim_vt_cell_t *row = pim_vt_view_row(&vt, s, y);
                CHECK(row != NULL);
            }
        }
    }
}

int main(void) {
    test_basic_text();
    test_wrap();
    test_cursor_and_erase();
    test_sgr();
    test_scroll_and_history();
    test_scroll_region();
    test_insert_delete();
    test_alt_screen();
    test_utf8_and_graphics();
    test_g1_charset();
    test_tabs();
    test_insert_mode();
    test_origin_mode();
    test_margins_clamp_cursor();
    test_controls_inside_sequences();
    test_eight_bit_csi_and_decaln();
    test_scrollback_reset();
    test_ascii_fallback();
    test_strings_are_swallowed();
    test_replies();
    test_keys();
    test_dirty();
    test_text_dump();
    test_chunking_is_invisible();
    test_bbs_screen();
    test_fullscreen_app();
    test_fuzz();
    if (failures == 0) puts("vterm tests passed");
    return failures == 0 ? 0 : 1;
}
