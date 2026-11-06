#include "pim/telnet.h"

#include <string.h>

enum { S_TEXT, S_IAC, S_VERB, S_SUB, S_SUB_IAC, S_ESC, S_CSI };
enum { IAC = 255, DONT = 254, DO = 253, WONT = 252, WILL = 251, SB = 250, SE = 240 };
enum { OPT_ECHO = 1, OPT_SGA = 3 };

void pim_term_init(pim_term_t *term) {
    memset(term, 0, sizeof(*term));
    term->count = 1;
}

static char *current(pim_term_t *term) { return term->lines[(term->first + term->count - 1) % PIM_TERM_LINES]; }

static void newline(pim_term_t *term) {
    if (term->count == PIM_TERM_LINES) term->first = (uint16_t)((term->first + 1) % PIM_TERM_LINES);
    else term->count++;
    memset(current(term), 0, PIM_TERM_COLS + 1);
    term->column = 0;
}

static void put(pim_term_t *term, char c) {
    if (term->column >= PIM_TERM_COLS) newline(term);
    current(term)[term->column++] = c;
}

static void add_reply(uint8_t *reply, size_t size, size_t *used, uint8_t verb, uint8_t option) {
    if (*used + 3 > size) return;
    reply[(*used)++] = IAC;
    reply[(*used)++] = verb;
    reply[(*used)++] = option;
}

static void negotiate(pim_term_t *term, uint8_t verb, uint8_t option, uint8_t *reply, size_t size, size_t *used) {
    if (verb == WILL) {
        if (option == OPT_ECHO) {
            if (!term->remote_echo) add_reply(reply, size, used, DO, option);
            term->remote_echo = true;
        } else if (option == OPT_SGA) {
            add_reply(reply, size, used, DO, option);
        } else {
            add_reply(reply, size, used, DONT, option);
        }
    } else if (verb == WONT) {
        if (option == OPT_ECHO) term->remote_echo = false;
    } else if (verb == DO) {
        add_reply(reply, size, used, option == OPT_SGA ? WILL : WONT, option);
    }
}

size_t pim_term_feed(pim_term_t *term, const uint8_t *data, size_t length, uint8_t *reply, size_t reply_size) {
    size_t used = 0;
    for (size_t i = 0; i < length; i++) {
        uint8_t c = data[i];
        switch (term->state) {
        case S_IAC:
            if (c == IAC) { put(term, '#'); term->state = S_TEXT; }
            else if (c >= WILL && c <= DONT) { term->verb = c; term->state = S_VERB; }
            else if (c == SB) term->state = S_SUB;
            else term->state = S_TEXT;
            break;
        case S_VERB:
            negotiate(term, term->verb, c, reply, reply_size, &used);
            term->state = S_TEXT;
            break;
        case S_SUB:
            if (c == IAC) term->state = S_SUB_IAC;
            break;
        case S_SUB_IAC:
            term->state = c == SE ? S_TEXT : S_SUB;
            break;
        case S_ESC:
            if (c == '[') { term->state = S_CSI; term->csi_value = 0; term->csi_has_value = false; }
            else term->state = S_TEXT;
            break;
        case S_CSI:
            if (c >= '0' && c <= '9') {
                unsigned v = (unsigned)term->csi_value * 10U + (unsigned)(c - '0');
                term->csi_value = v > 255 ? 255 : (uint8_t)v;
                term->csi_has_value = true;
            } else if (c >= 0x40 && c <= 0x7e) {
                if (c == 'C') {
                    unsigned n = term->csi_has_value ? term->csi_value : 1U;
                    while (n-- > 0 && term->column < PIM_TERM_COLS) put(term, ' ');
                } else if (c == 'J' && term->csi_has_value && term->csi_value == 2) {
                    { bool echo = term->remote_echo; pim_term_init(term); term->remote_echo = echo; }
                }
                term->state = S_TEXT;
            }
            break;
        default:
            if (c == IAC) term->state = S_IAC;
            else if (c == 0x1b) term->state = S_ESC;
            else if (c == '\n') newline(term);
            else if (c == '\r') term->column = 0;
            else if (c == '\b') { if (term->column > 0) { term->column--; current(term)[term->column] = '\0'; } }
            else if (c == '\t') { do put(term, ' '); while (term->column % 8 != 0 && term->column < PIM_TERM_COLS); }
            else if (c >= 32 && c < 127) put(term, (char)c);
            else if (c >= 128) put(term, '#');
            break;
        }
    }
    return used;
}

unsigned pim_term_max_scroll(const pim_term_t *term, unsigned rows) {
    return term->count > rows ? term->count - rows : 0;
}

void pim_term_view(const pim_term_t *term, unsigned rows, unsigned scroll, char *out, size_t out_size) {
    unsigned max = pim_term_max_scroll(term, rows), last, start;
    size_t used = 0;
    if (out_size == 0) return;
    if (scroll > max) scroll = max;
    last = term->count - scroll;
    start = last > rows ? last - rows : 0;
    for (unsigned i = start; i < last; i++) {
        const char *line = term->lines[(term->first + i) % PIM_TERM_LINES];
        size_t length = strlen(line);
        if (used + length + 2 > out_size) break;
        memcpy(out + used, line, length);
        used += length;
        if (i + 1 < last) out[used++] = '\n';
    }
    out[used] = '\0';
}

size_t pim_term_encode(uint8_t key, uint8_t out[2]) {
    if (key == '\n' || key == '\r') { out[0] = '\r'; out[1] = '\n'; return 2; }
    if (key == 0xff) { out[0] = 0xff; out[1] = 0xff; return 2; }
    out[0] = key;
    return 1;
}
