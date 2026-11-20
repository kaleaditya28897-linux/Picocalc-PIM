#include "pim/telnet.h"

#include <string.h>

enum { S_DATA, S_IAC, S_VERB, S_SUB, S_SUB_IAC };
enum { IAC = 255, DONT = 254, DO = 253, WONT = 252, WILL = 251, SB = 250, SE = 240 };
enum { OPT_BINARY = 0, OPT_ECHO = 1, OPT_SGA = 3, OPT_TTYPE = 24, OPT_NAWS = 31 };
enum { TTYPE_IS = 0, TTYPE_SEND = 1 };

/* What we claim to be, in the order the host may ask for it. A BBS that sees
   "ansi" sends colour; "vt100" is the fallback for hosts that want a real DEC. */
static const char *const terminal_types[] = { "ansi", "vt100" };
#define TYPE_COUNT (sizeof(terminal_types) / sizeof(terminal_types[0]))

void pim_iac_init(pim_iac_t *iac, unsigned cols, unsigned rows) {
    memset(iac, 0, sizeof(*iac));
    iac->cols = (uint8_t)(cols > 255 ? 255 : cols);
    iac->rows = (uint8_t)(rows > 255 ? 255 : rows);
}

static void put_byte(uint8_t *out, size_t size, size_t *used, uint8_t value) {
    if (*used < size) out[(*used)++] = value;
}

static void command(uint8_t *out, size_t size, size_t *used, uint8_t verb, uint8_t option) {
    if (*used + 3 > size) return;
    out[(*used)++] = IAC;
    out[(*used)++] = verb;
    out[(*used)++] = option;
}

size_t pim_iac_hello(const pim_iac_t *iac, uint8_t *out, size_t out_size) {
    size_t used = 0;
    (void)iac;
    /* Ask the host to stop waiting for a go-ahead, and offer what we can tell it
       about ourselves. The window size itself waits for DO NAWS. */
    command(out, out_size, &used, DO, OPT_SGA);
    command(out, out_size, &used, WILL, OPT_SGA);
    command(out, out_size, &used, WILL, OPT_TTYPE);
    command(out, out_size, &used, WILL, OPT_NAWS);
    return used;
}

static void send_naws(const pim_iac_t *iac, uint8_t *out, size_t size, size_t *used) {
    if (*used + 9 > size) return;
    out[(*used)++] = IAC; out[(*used)++] = SB; out[(*used)++] = OPT_NAWS;
    out[(*used)++] = 0; out[(*used)++] = iac->cols;
    out[(*used)++] = 0; out[(*used)++] = iac->rows;
    out[(*used)++] = IAC; out[(*used)++] = SE;
}

static void send_type(pim_iac_t *iac, uint8_t *out, size_t size, size_t *used) {
    const char *name = terminal_types[iac->type_index < TYPE_COUNT ? iac->type_index : TYPE_COUNT - 1];
    size_t length = strlen(name);
    if (*used + length + 6 > size) return;
    out[(*used)++] = IAC; out[(*used)++] = SB; out[(*used)++] = OPT_TTYPE;
    out[(*used)++] = TTYPE_IS;
    memcpy(out + *used, name, length);
    *used += length;
    out[(*used)++] = IAC; out[(*used)++] = SE;
    /* Each further SEND walks the list and then sticks on the last entry. */
    if (iac->type_index + 1u < TYPE_COUNT) iac->type_index++;
}

static void negotiate(pim_iac_t *iac, uint8_t verb, uint8_t option,
                      uint8_t *reply, size_t size, size_t *used) {
    switch (verb) {
    case WILL:
        if (option == OPT_ECHO) {
            if (!iac->remote_echo) command(reply, size, used, DO, option);
            iac->remote_echo = true;
        } else if (option == OPT_SGA) {
            if (!iac->sga_agreed) command(reply, size, used, DO, option);
            iac->sga_agreed = true;
        } else if (option == OPT_BINARY) {
            if (!iac->binary_in) command(reply, size, used, DO, option);
            iac->binary_in = true;
        } else {
            command(reply, size, used, DONT, option);
        }
        break;
    case WONT:
        if (option == OPT_ECHO && iac->remote_echo) {
            iac->remote_echo = false;
            command(reply, size, used, DONT, option);
        } else if (option == OPT_BINARY && iac->binary_in) {
            iac->binary_in = false;
            command(reply, size, used, DONT, option);
        } else if (option == OPT_SGA && iac->sga_agreed) {
            iac->sga_agreed = false;
            command(reply, size, used, DONT, option);
        }
        break;
    case DO:
        if (option == OPT_SGA) {
            command(reply, size, used, WILL, option);
        } else if (option == OPT_TTYPE) {
            if (!iac->type_agreed) command(reply, size, used, WILL, option);
            iac->type_agreed = true;
        } else if (option == OPT_NAWS) {
            if (!iac->naws_agreed) command(reply, size, used, WILL, option);
            iac->naws_agreed = true;
            send_naws(iac, reply, size, used);
        } else if (option == OPT_BINARY) {
            if (!iac->binary_out) command(reply, size, used, WILL, option);
            iac->binary_out = true;
        } else {
            command(reply, size, used, WONT, option);
        }
        break;
    case DONT:
        /* Only answer options we had actually taken on, so this cannot loop. */
        if (option == OPT_NAWS && iac->naws_agreed) {
            iac->naws_agreed = false;
            command(reply, size, used, WONT, option);
        } else if (option == OPT_TTYPE && iac->type_agreed) {
            iac->type_agreed = false;
            command(reply, size, used, WONT, option);
        } else if (option == OPT_BINARY && iac->binary_out) {
            iac->binary_out = false;
            command(reply, size, used, WONT, option);
        }
        break;
    default: break;
    }
}

static void subnegotiation(pim_iac_t *iac, uint8_t *reply, size_t size, size_t *used) {
    if (iac->sub_length < 1) return;
    if (iac->sub[0] == OPT_TTYPE && iac->sub_length >= 2 && iac->sub[1] == TTYPE_SEND) {
        send_type(iac, reply, size, used);
    }
    /* Anything else (NAWS echoes, charset, speed) needs no answer from us. */
}

size_t pim_iac_feed(pim_iac_t *iac, const uint8_t *in, size_t length,
                    uint8_t *data, size_t data_size,
                    uint8_t *reply, size_t reply_size, size_t *reply_used) {
    size_t out = 0, used = 0, i;
    for (i = 0; i < length; i++) {
        uint8_t c = in[i];
        switch (iac->state) {
        case S_IAC:
            if (c == IAC) { put_byte(data, data_size, &out, IAC); iac->state = S_DATA; }
            else if (c >= WILL && c <= DONT) { iac->verb = c; iac->state = S_VERB; }
            else if (c == SB) { iac->state = S_SUB; iac->sub_length = 0; }
            else iac->state = S_DATA;      /* NOP, GA, AYT and the rest: nothing to do */
            break;
        case S_VERB:
            negotiate(iac, iac->verb, c, reply, reply_size, &used);
            iac->state = S_DATA;
            break;
        case S_SUB:
            if (c == IAC) iac->state = S_SUB_IAC;
            else if (iac->sub_length < PIM_IAC_SUB_MAX) iac->sub[iac->sub_length++] = c;
            break;
        case S_SUB_IAC:
            if (c == SE) {
                subnegotiation(iac, reply, reply_size, &used);
                iac->state = S_DATA;
            } else if (c == IAC) {
                /* a doubled 0xff inside a subnegotiation is one data byte */
                if (iac->sub_length < PIM_IAC_SUB_MAX) iac->sub[iac->sub_length++] = IAC;
                iac->state = S_SUB;
            } else {
                iac->state = S_SUB;
            }
            break;
        default:
            if (c == IAC) iac->state = S_IAC;
            else put_byte(data, data_size, &out, c);
            break;
        }
    }
    if (reply_used != NULL) *reply_used = used;
    return out;
}

size_t pim_iac_encode(const pim_iac_t *iac, const uint8_t *in, size_t length,
                      uint8_t *out, size_t out_size) {
    size_t used = 0, i;
    for (i = 0; i < length; i++) {
        uint8_t c = in[i];
        bool twice = c == IAC || (c == '\r' && !iac->binary_out && (i + 1 >= length || in[i + 1] != '\n'));
        if (used + (twice ? 2u : 1u) > out_size) return used;
        out[used++] = c;
        if (c == IAC) out[used++] = IAC;
        else if (twice) out[used++] = '\n';
    }
    return used;
}
