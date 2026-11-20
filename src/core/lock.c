#include "pim/lock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pim/sshcrypto.h"
#include "pim/sshkey.h"

#define SALT_BYTES 16
#define HASH_BYTES 32

bool pim_lock_pin_ok(const char *pin) {
    size_t i;
    if (pin == NULL) return false;
    for (i = 0; i < PIM_LOCK_DIGITS; i++) {
        if (pin[i] < '0' || pin[i] > '9') return false;
    }
    return pin[PIM_LOCK_DIGITS] == '\0';
}

/* salt and PIN hashed together, then the digest re-hashed `rounds` times. The
   rounds are what make an offline sweep of a million PINs slow. */
static void derive(const uint8_t salt[SALT_BYTES], const char *pin, uint32_t rounds,
                   uint8_t out[HASH_BYTES]) {
    uint8_t buffer[SALT_BYTES + PIM_LOCK_DIGITS];
    uint32_t i;
    memcpy(buffer, salt, SALT_BYTES);
    memcpy(buffer + SALT_BYTES, pin, PIM_LOCK_DIGITS);
    pim_sha256(buffer, sizeof(buffer), out);
    for (i = 1; i < rounds; i++) pim_sha256(out, HASH_BYTES, out);
}

static void to_hex(const uint8_t *bytes, size_t length, char *out) {
    static const char digits[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < length; i++) {
        out[2 * i] = digits[bytes[i] >> 4];
        out[2 * i + 1] = digits[bytes[i] & 0x0fu];
    }
    out[2 * length] = '\0';
}

static bool from_hex(const char *text, uint8_t *out, size_t length) {
    size_t i;
    for (i = 0; i < length; i++) {
        unsigned value = 0;
        if (sscanf(text + 2 * i, "%2x", &value) != 1) return false;
        out[i] = (uint8_t)value;
    }
    return true;
}

/* "pin=<rounds>:<salt hex>:<hash hex>" */
static bool read_record(const char *path, uint32_t *rounds, uint8_t salt[SALT_BYTES],
                        uint8_t hash[HASH_BYTES]) {
    char line[160];
    FILE *file = fopen(path, "rb");
    char *first, *second;
    bool ok = false;
    if (file == NULL) return false;
    if (fgets(line, sizeof(line), file) != NULL && strncmp(line, "pin=", 4) == 0) {
        first = strchr(line + 4, ':');
        second = first != NULL ? strchr(first + 1, ':') : NULL;
        if (second != NULL) {
            unsigned long value;
            *first = '\0';
            *second = '\0';
            value = strtoul(line + 4, NULL, 10);
            if (value > 0 && value <= 1000000u &&
                strlen(first + 1) >= 2 * SALT_BYTES && strlen(second + 1) >= 2 * HASH_BYTES &&
                from_hex(first + 1, salt, SALT_BYTES) && from_hex(second + 1, hash, HASH_BYTES)) {
                *rounds = (uint32_t)value;
                ok = true;
            }
        }
    }
    fclose(file);
    return ok;
}

bool pim_lock_is_set(const char *path) {
    uint32_t rounds;
    uint8_t salt[SALT_BYTES], hash[HASH_BYTES];
    return read_record(path, &rounds, salt, hash);
}

bool pim_lock_set(const char *path, const char *pin) {
    uint8_t salt[SALT_BYTES], hash[HASH_BYTES];
    char salt_hex[2 * SALT_BYTES + 1], hash_hex[2 * HASH_BYTES + 1];
    char temporary[160];
    FILE *file;
    if (!pim_lock_pin_ok(pin)) return false;
    /* A fresh salt each time, so setting the same PIN twice does not produce
       the same file and nothing can be compared between devices. */
    if (!pim_ssh_random(salt, sizeof(salt))) return false;
    derive(salt, pin, PIM_LOCK_ROUNDS, hash);
    to_hex(salt, sizeof(salt), salt_hex);
    to_hex(hash, sizeof(hash), hash_hex);
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    file = fopen(temporary, "wb");
    if (file == NULL) return false;
    {
        /* Both always run: written as one condition, a failed write skipped
           fclose and left the file open - and FatFS has only a few handles. */
        bool written = fprintf(file, "pin=%u:%s:%s\n", PIM_LOCK_ROUNDS, salt_hex, hash_hex) > 0;
        bool closed = fclose(file) == 0;
        if (!written || !closed) {
            remove(temporary);
            return false;
        }
    }
    remove(path);
    return rename(temporary, path) == 0;
}

bool pim_lock_check(const char *path, const char *pin) {
    uint32_t rounds = 0;
    uint8_t salt[SALT_BYTES], stored[HASH_BYTES], computed[HASH_BYTES];
    uint8_t difference = 0;
    size_t i;
    if (!pim_lock_pin_ok(pin)) return false;
    if (!read_record(path, &rounds, salt, stored)) return false;
    /* The round count comes from the file, so a PIN set by an older build still
       works if the number ever changes. */
    derive(salt, pin, rounds, computed);
    for (i = 0; i < HASH_BYTES; i++) difference |= (uint8_t)(stored[i] ^ computed[i]);
    return difference == 0;
}

bool pim_lock_clear(const char *path) {
    return remove(path) == 0;
}
