#ifndef PIM_SSHCRYPTO_H
#define PIM_SSHCRYPTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The crypto an SSH-2 client needs, and nothing else.

   The algorithm suite is deliberately one row wide:

     key exchange   curve25519-sha256
     host key       ssh-ed25519
     cipher and MAC chacha20-poly1305@openssh.com

   Every OpenSSH since 6.5 offers all three, so there is no negotiation to get
   wrong and no second code path to test. That reduces the whole requirement to
   SHA-256 (pim_sha256, already here for key fingerprints), ChaCha20 and
   Poly1305 (below, which are short and have exhaustive RFC 8439 vectors), and
   X25519 and Ed25519 (third_party/tweetnacl, because curve arithmetic is not
   something to write by hand).

   ChaCha20 here is the original 64-bit-nonce form, not RFC 8439's 96-bit one:
   that is what OpenSSH's construction uses, and the difference is only in how
   the last 16 bytes of the state are split. */

#define PIM_SSH_KEY_BYTES 32
#define PIM_SSH_TAG_BYTES 16
#define PIM_SSH_SIG_BYTES 64

/* ---- ChaCha20 ---- */

/* One 64-byte key stream block. `counter` and `nonce` occupy the last four
   state words, counter first, both little endian. */
void pim_chacha20_block(const uint8_t key[32], uint64_t counter,
                        const uint8_t nonce[8], uint8_t out[64]);
/* XORs `length` bytes, starting at the given block counter. In and out may alias. */
void pim_chacha20_xor(const uint8_t key[32], uint64_t counter, const uint8_t nonce[8],
                      const uint8_t *in, uint8_t *out, size_t length);
/* The RFC 8439 layout (32-bit counter, 96-bit nonce), for the test vectors and
   for anything that later wants the standard form. */
void pim_chacha20_rfc_block(const uint8_t key[32], uint32_t counter,
                            const uint8_t nonce[12], uint8_t out[64]);

/* ---- Poly1305 ---- */

void pim_poly1305(const uint8_t key[32], const uint8_t *message, size_t length,
                  uint8_t tag[16]);
/* Constant-time comparison, so a wrong tag leaks nothing by how long it took. */
bool pim_poly1305_verify(const uint8_t key[32], const uint8_t *message, size_t length,
                         const uint8_t tag[16]);

/* ---- curve25519 and ed25519, over tweetnacl ---- */

void pim_x25519_base(uint8_t out[32], const uint8_t scalar[32]);
void pim_x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]);
/* True when `signature` is a valid Ed25519 signature over `message`. */
bool pim_ed25519_verify(const uint8_t signature[64], const uint8_t *message, size_t length,
                        const uint8_t public_key[32]);
/* `secret` is the 64-byte OpenSSH form: the 32-byte seed followed by the
   public key. */
void pim_ed25519_sign(uint8_t signature[64], const uint8_t *message, size_t length,
                      const uint8_t secret[64]);
/* True when the two halves of a 64-byte secret belong together: it signs a
   fixed message and checks the signature against the public half. An OpenSSH
   key file carries both, so this catches a truncated or mismatched file before
   the server rejects the authentication for reasons it will not explain. */
bool pim_ed25519_key_consistent(const uint8_t secret[64]);

/* ---- randomness ---- */

/* Ephemeral key exchange scalars come from here. The host build reads
   /dev/urandom; the firmware overrides this with the RP2350's hardware source.
   Returns false when no entropy could be had, which must abort a handshake
   rather than fall back to something weaker. */
bool pim_ssh_random(void *buffer, size_t length);

#endif
