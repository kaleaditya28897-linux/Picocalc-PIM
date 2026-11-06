#ifndef PIM_TELNET_H
#define PIM_TELNET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PIM_TERM_COLS 40
#define PIM_TERM_LINES 96

/* Telnet protocol filter plus a scrolling 40-column text terminal. Hardware-free. */
typedef struct {
    char lines[PIM_TERM_LINES][PIM_TERM_COLS + 1];
    uint16_t first, count; /* ring of lines; the last one is being written */
    uint8_t column, state, verb, csi_value;
    bool remote_echo;
    bool csi_has_value;
} pim_term_t;

void pim_term_init(pim_term_t *term);
/* Processes bytes from the server. Protocol answers (IAC replies) are written to `reply`;
   returns how many reply bytes were produced. Colours, cursor moves and options are dropped. */
size_t pim_term_feed(pim_term_t *term, const uint8_t *data, size_t length, uint8_t *reply, size_t reply_size);
/* Copies up to `rows` lines ending `scroll` lines above the newest into `out` separated by \n. */
void pim_term_view(const pim_term_t *term, unsigned rows, unsigned scroll, char *out, size_t out_size);
unsigned pim_term_max_scroll(const pim_term_t *term, unsigned rows);
/* Encodes a typed character for the server: CR becomes CR LF and 0xFF is doubled. */
size_t pim_term_encode(uint8_t key, uint8_t out[2]);

#endif
