#include "pim/sshcrypto.h"

#include <stdio.h>
#include <string.h>

#include "tweetnacl.h"

/* ------------------------------------------------------------------ ChaCha20 */

#define ROTATE(v, n) (((v) << (n)) | ((v) >> (32 - (n))))
#define QUARTER(a, b, c, d)                                  \
    do {                                                     \
        a += b; d ^= a; d = ROTATE(d, 16);                   \
        c += d; b ^= c; b = ROTATE(b, 12);                   \
        a += b; d ^= a; d = ROTATE(d, 8);                    \
        c += d; b ^= c; b = ROTATE(b, 7);                    \
    } while (0)

static uint32_t load32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void store32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* The 20 rounds themselves, over a prepared state. Both nonce conventions share
   this, differing only in how they fill words 12 to 15. */
static void chacha20_core(const uint32_t state[16], uint8_t out[64]) {
    uint32_t x[16];
    unsigned i;
    memcpy(x, state, sizeof(x));
    for (i = 0; i < 10; i++) {
        QUARTER(x[0], x[4], x[8], x[12]);
        QUARTER(x[1], x[5], x[9], x[13]);
        QUARTER(x[2], x[6], x[10], x[14]);
        QUARTER(x[3], x[7], x[11], x[15]);
        QUARTER(x[0], x[5], x[10], x[15]);
        QUARTER(x[1], x[6], x[11], x[12]);
        QUARTER(x[2], x[7], x[8], x[13]);
        QUARTER(x[3], x[4], x[9], x[14]);
    }
    for (i = 0; i < 16; i++) store32(out + 4 * i, x[i] + state[i]);
}

static void chacha20_state(uint32_t state[16], const uint8_t key[32]) {
    static const char sigma[] = "expand 32-byte k";
    state[0] = load32((const uint8_t *)sigma);
    state[1] = load32((const uint8_t *)sigma + 4);
    state[2] = load32((const uint8_t *)sigma + 8);
    state[3] = load32((const uint8_t *)sigma + 12);
    for (unsigned i = 0; i < 8; i++) state[4 + i] = load32(key + 4 * i);
}

void pim_chacha20_block(const uint8_t key[32], uint64_t counter,
                        const uint8_t nonce[8], uint8_t out[64]) {
    uint32_t state[16];
    chacha20_state(state, key);
    state[12] = (uint32_t)counter;
    state[13] = (uint32_t)(counter >> 32);
    state[14] = load32(nonce);
    state[15] = load32(nonce + 4);
    chacha20_core(state, out);
}

void pim_chacha20_rfc_block(const uint8_t key[32], uint32_t counter,
                            const uint8_t nonce[12], uint8_t out[64]) {
    uint32_t state[16];
    chacha20_state(state, key);
    state[12] = counter;
    state[13] = load32(nonce);
    state[14] = load32(nonce + 4);
    state[15] = load32(nonce + 8);
    chacha20_core(state, out);
}

void pim_chacha20_xor(const uint8_t key[32], uint64_t counter, const uint8_t nonce[8],
                      const uint8_t *in, uint8_t *out, size_t length) {
    uint8_t stream[64];
    size_t done = 0;
    while (done < length) {
        size_t chunk = length - done < 64 ? length - done : 64;
        pim_chacha20_block(key, counter, nonce, stream);
        for (size_t i = 0; i < chunk; i++) out[done + i] = in[done + i] ^ stream[i];
        done += chunk;
        counter++;
    }
}

/* ------------------------------------------------------------------ Poly1305 */

/* The 130-bit accumulator is held as five 26-bit limbs, which keeps every
   intermediate inside 64 bits without needing 128-bit arithmetic. */
void pim_poly1305(const uint8_t key[32], const uint8_t *message, size_t length,
                  uint8_t tag[16]) {
    uint32_t r0, r1, r2, r3, r4;
    uint32_t s1, s2, s3, s4;
    uint32_t h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;
    uint64_t d0, d1, d2, d3, d4;
    uint32_t c;

    /* r is clamped as the specification requires */
    r0 = (load32(key + 0)) & 0x3ffffff;
    r1 = (load32(key + 3) >> 2) & 0x3ffff03;
    r2 = (load32(key + 6) >> 4) & 0x3ffc0ff;
    r3 = (load32(key + 9) >> 6) & 0x3f03fff;
    r4 = (load32(key + 12) >> 8) & 0x00fffff;
    s1 = r1 * 5; s2 = r2 * 5; s3 = r3 * 5; s4 = r4 * 5;

    while (length > 0) {
        uint8_t block[17];
        size_t chunk = length < 16 ? length : 16;
        memset(block, 0, sizeof(block));
        memcpy(block, message, chunk);
        block[chunk] = 1;              /* the high bit of a full block, or the pad of a short one */
        message += chunk;
        length -= chunk;

        h0 += (load32(block + 0)) & 0x3ffffff;
        h1 += (load32(block + 3) >> 2) & 0x3ffffff;
        h2 += (load32(block + 6) >> 4) & 0x3ffffff;
        h3 += (load32(block + 9) >> 6) & 0x3ffffff;
        /* The limbs straddle byte boundaries: the top one takes bytes 13 to 15
           from the word starting at byte 12, and block[16] carries the bit that
           is set past the end of the message. */
        h4 += ((uint32_t)block[16] << 24) | (load32(block + 12) >> 8);

        d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4 + (uint64_t)h2 * s3 + (uint64_t)h3 * s2 + (uint64_t)h4 * s1;
        d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * s4 + (uint64_t)h3 * s3 + (uint64_t)h4 * s2;
        d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0 + (uint64_t)h3 * s4 + (uint64_t)h4 * s3;
        d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1 + (uint64_t)h3 * r0 + (uint64_t)h4 * s4;
        d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2 + (uint64_t)h3 * r1 + (uint64_t)h4 * r0;

        c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x3ffffff;
        d1 += c; c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x3ffffff;
        d2 += c; c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x3ffffff;
        d3 += c; c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x3ffffff;
        d4 += c; c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x3ffffff;
        h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff;
        h1 += c;
    }

    /* carry propagation, then the final reduction modulo 2^130 - 5 */
    c = h1 >> 26; h1 &= 0x3ffffff;
    h2 += c; c = h2 >> 26; h2 &= 0x3ffffff;
    h3 += c; c = h3 >> 26; h3 &= 0x3ffffff;
    h4 += c; c = h4 >> 26; h4 &= 0x3ffffff;
    h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff;
    h1 += c;

    {
        uint32_t g0, g1, g2, g3, g4, mask;
        g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
        g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
        g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
        g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
        g4 = h4 + c - (1u << 26);
        mask = (g4 >> 31) - 1;         /* all ones when g is the smaller one */
        g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
        mask = ~mask;
        h0 = (h0 & mask) | g0;
        h1 = (h1 & mask) | g1;
        h2 = (h2 & mask) | g2;
        h3 = (h3 & mask) | g3;
        h4 = (h4 & mask) | g4;

        /* add s, then serialise the low 128 bits */
        {
            uint64_t f;
            uint32_t t0 = h0 | (h1 << 26);
            uint32_t t1 = (h1 >> 6) | (h2 << 20);
            uint32_t t2 = (h2 >> 12) | (h3 << 14);
            uint32_t t3 = (h3 >> 18) | (h4 << 8);
            f = (uint64_t)t0 + load32(key + 16); t0 = (uint32_t)f;
            f = (uint64_t)t1 + load32(key + 20) + (f >> 32); t1 = (uint32_t)f;
            f = (uint64_t)t2 + load32(key + 24) + (f >> 32); t2 = (uint32_t)f;
            f = (uint64_t)t3 + load32(key + 28) + (f >> 32); t3 = (uint32_t)f;
            store32(tag + 0, t0);
            store32(tag + 4, t1);
            store32(tag + 8, t2);
            store32(tag + 12, t3);
        }
    }
}

bool pim_poly1305_verify(const uint8_t key[32], const uint8_t *message, size_t length,
                         const uint8_t tag[16]) {
    uint8_t expected[16];
    uint8_t difference = 0;
    pim_poly1305(key, message, length, expected);
    for (unsigned i = 0; i < 16; i++) difference |= (uint8_t)(expected[i] ^ tag[i]);
    return difference == 0;
}

/* ------------------------------------------------- curve25519 and ed25519 */

void pim_x25519_base(uint8_t out[32], const uint8_t scalar[32]) {
    crypto_scalarmult_base(out, scalar);
}

void pim_x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]) {
    crypto_scalarmult(out, scalar, point);
}

/* tweetnacl only does the combined form, where the signature and the message
   travel together, so a detached signature is checked by putting them back
   together. SSH only ever signs an exchange hash or a short request, so the
   scratch buffer stays small. */
#define SIGNED_MAX (PIM_SSH_SIG_BYTES + 256)

bool pim_ed25519_verify(const uint8_t signature[64], const uint8_t *message, size_t length,
                        const uint8_t public_key[32]) {
    uint8_t signed_message[SIGNED_MAX], recovered[SIGNED_MAX];
    unsigned long long recovered_length = 0;
    if (length > SIGNED_MAX - PIM_SSH_SIG_BYTES) return false;
    memcpy(signed_message, signature, PIM_SSH_SIG_BYTES);
    memcpy(signed_message + PIM_SSH_SIG_BYTES, message, length);
    if (crypto_sign_open(recovered, &recovered_length, signed_message,
                         (unsigned long long)(PIM_SSH_SIG_BYTES + length), public_key) != 0) {
        return false;
    }
    return recovered_length == length && memcmp(recovered, message, length) == 0;
}

void pim_ed25519_sign(uint8_t signature[64], const uint8_t *message, size_t length,
                      const uint8_t secret[64]) {
    uint8_t signed_message[SIGNED_MAX];
    unsigned long long signed_length = 0;
    if (length > SIGNED_MAX - PIM_SSH_SIG_BYTES) { memset(signature, 0, PIM_SSH_SIG_BYTES); return; }
    crypto_sign(signed_message, &signed_length, message, (unsigned long long)length, secret);
    memcpy(signature, signed_message, PIM_SSH_SIG_BYTES);
}

bool pim_ed25519_key_consistent(const uint8_t secret[64]) {
    /* Signing needs only the seed, verifying only the public half, so a round
       trip proves the two match without reaching inside the curve code. */
    static const uint8_t probe[] = "picocalc key check";
    uint8_t signature[PIM_SSH_SIG_BYTES];
    pim_ed25519_sign(signature, probe, sizeof(probe) - 1, secret);
    return pim_ed25519_verify(signature, probe, sizeof(probe) - 1, secret + 32);
}

/* ------------------------------------------------------------------ random */

/* Overridden by the firmware with the RP2350's hardware source; this default
   is what the host tests use. */
__attribute__((weak)) bool pim_ssh_random(void *buffer, size_t length) {
    FILE *source = fopen("/dev/urandom", "rb");
    size_t got;
    if (source == NULL) return false;
    got = fread(buffer, 1, length, source);
    fclose(source);
    return got == length;
}

/* tweetnacl wants this for key generation, which is never called here: keys
   come off the SD card and exchange scalars come from pim_ssh_random. Defined
   so the link succeeds, and loud if it is ever reached. */
void randombytes(unsigned char *buffer, unsigned long long length) {
    if (!pim_ssh_random(buffer, (size_t)length)) memset(buffer, 0, (size_t)length);
}
