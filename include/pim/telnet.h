#ifndef PIM_TELNET_H
#define PIM_TELNET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Telnet protocol filter (RFC 854 and friends), hardware-free.

   This layer only separates protocol from payload: the bytes it hands back are
   fed to the terminal emulator in pim/vterm.h, which owns the screen. Options
   worth answering are ECHO, SGA, BINARY, TERMINAL-TYPE and NAWS, so the host
   learns the real 40x38 window instead of assuming 80x24. */

#define PIM_IAC_SUB_MAX 40

typedef struct {
    uint8_t state, verb;
    uint8_t sub[PIM_IAC_SUB_MAX];
    uint8_t sub_length;
    uint8_t cols, rows;
    uint8_t type_index;        /* which terminal type the next SEND answers */
    bool remote_echo;          /* the host echoes what is typed, so we must not */
    bool binary_in, binary_out;
    bool naws_agreed, type_agreed, sga_agreed;
} pim_iac_t;

void pim_iac_init(pim_iac_t *iac, unsigned cols, unsigned rows);

/* The opening offer, sent as soon as the socket is up. */
size_t pim_iac_hello(const pim_iac_t *iac, uint8_t *out, size_t out_size);

/* Splits a chunk from the host into screen bytes and protocol answers. Screen
   bytes go to `data` (never more than `length` of them) and the return value is
   how many; protocol answers go to `reply` and *reply_used, which may be NULL. */
size_t pim_iac_feed(pim_iac_t *iac, const uint8_t *in, size_t length,
                    uint8_t *data, size_t data_size,
                    uint8_t *reply, size_t reply_size, size_t *reply_used);

/* Prepares typed bytes for the wire: 0xff is doubled so it cannot read as a
   command, and outside binary mode a lone CR becomes the CR LF that NVT asks
   for. Returns the number of bytes written; a run that will not fit is cut at
   the last whole character rather than split. */
size_t pim_iac_encode(const pim_iac_t *iac, const uint8_t *in, size_t length,
                      uint8_t *out, size_t out_size);

#endif
