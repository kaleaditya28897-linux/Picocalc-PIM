#include "pim/sshkey.h"

#include <stdio.h>
#include <string.h>

#include <sys/stat.h>

#include "pim/sshcrypto.h"
#include "tweetnacl.h"

#ifdef PIM_VFS_DIRENT
#include <sys/dirent.h>
extern DIR *opendir(const char *path);
extern struct dirent *readdir(DIR *directory);
extern int closedir(DIR *directory);
#else
#include <dirent.h>
#endif

/* ------------------------------------------------------------------ SHA-256 */

typedef struct { uint32_t state[8]; } sha256_t;

static uint32_t rotate_right(uint32_t value, unsigned count) {
    return (value >> count) | (value << (32u - count));
}

static void sha256_block(sha256_t *sha, const uint8_t *block) {
    static const uint32_t k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
        0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
        0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
        0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
        0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
        0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };
    uint32_t w[64], a, b, c, d, e, f, g, h;
    unsigned i;
    for (i = 0; i < 16; i++) {
        w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 |
               (uint32_t)block[i * 4 + 2] << 8 | (uint32_t)block[i * 4 + 3];
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = rotate_right(w[i - 15], 7) ^ rotate_right(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotate_right(w[i - 2], 17) ^ rotate_right(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = sha->state[0]; b = sha->state[1]; c = sha->state[2]; d = sha->state[3];
    e = sha->state[4]; f = sha->state[5]; g = sha->state[6]; h = sha->state[7];
    for (i = 0; i < 64; i++) {
        uint32_t s1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        uint32_t choose = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + choose + k[i] + w[i];
        uint32_t s0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + majority;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    sha->state[0] += a; sha->state[1] += b; sha->state[2] += c; sha->state[3] += d;
    sha->state[4] += e; sha->state[5] += f; sha->state[6] += g; sha->state[7] += h;
}

void pim_sha256(const uint8_t *data, size_t length, uint8_t digest[32]) {
    static const uint32_t initial[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    sha256_t sha;
    uint8_t tail[64];
    uint64_t bits = (uint64_t)length * 8u;
    size_t i, used;
    memcpy(sha.state, initial, sizeof(initial));
    while (length >= 64) { sha256_block(&sha, data); data += 64; length -= 64; }
    memset(tail, 0, sizeof(tail));
    memcpy(tail, data, length);
    tail[length] = 0x80;
    used = length + 1;
    if (used > 56) {
        sha256_block(&sha, tail);
        memset(tail, 0, sizeof(tail));
    }
    for (i = 0; i < 8; i++) tail[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
    sha256_block(&sha, tail);
    for (i = 0; i < 8; i++) {
        digest[i * 4] = (uint8_t)(sha.state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(sha.state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(sha.state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)sha.state[i];
    }
}

/* ------------------------------------------------------------------- base64 */

static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t pim_base64_encode(const uint8_t *data, size_t length, char *out, size_t out_size, bool pad) {
    size_t groups = (length + 2) / 3;
    size_t needed = pad ? groups * 4 : (length * 4 + 2) / 3;
    size_t i = 0, used = 0;
    if (out_size < needed + 1) return 0;
    while (i < length) {
        unsigned chunk = (unsigned)data[i] << 16;
        size_t have = 1;
        if (i + 1 < length) { chunk |= (unsigned)data[i + 1] << 8; have = 2; }
        if (i + 2 < length) { chunk |= (unsigned)data[i + 2]; have = 3; }
        out[used++] = alphabet[(chunk >> 18) & 63u];
        out[used++] = alphabet[(chunk >> 12) & 63u];
        if (have > 1) out[used++] = alphabet[(chunk >> 6) & 63u];
        else if (pad) out[used++] = '=';
        if (have > 2) out[used++] = alphabet[chunk & 63u];
        else if (pad) out[used++] = '=';
        i += 3;
    }
    out[used] = '\0';
    return used;
}

static int value_of(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

size_t pim_base64_decode(const char *text, uint8_t *out, size_t out_size) {
    unsigned chunk = 0, bits = 0;
    size_t used = 0;
    for (; *text != '\0'; text++) {
        int value;
        if (*text == '=' || *text == '\n' || *text == '\r') break;
        if (*text == ' ' || *text == '\t') continue;
        value = value_of(*text);
        if (value < 0) return 0;
        chunk = (chunk << 6) | (unsigned)value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (used >= out_size) return 0;
            out[used++] = (uint8_t)(chunk >> bits);
        }
    }
    return used;
}

/* ---------------------------------------------------------------- key files */

/* The blob is a run of length-prefixed strings; the first names the key type. */
static bool blob_string(const uint8_t *blob, size_t length, size_t *offset,
                        const uint8_t **value, size_t *value_length) {
    size_t size;
    if (*offset + 4 > length) return false;
    size = (size_t)blob[*offset] << 24 | (size_t)blob[*offset + 1] << 16 |
           (size_t)blob[*offset + 2] << 8 | (size_t)blob[*offset + 3];
    if (size > length - *offset - 4) return false;
    *offset += 4;
    *value = blob + *offset;
    *value_length = size;
    *offset += size;
    return true;
}

static unsigned key_bits(const char *type, const uint8_t *blob, size_t length) {
    size_t offset = 0, field_length = 0;
    const uint8_t *field = NULL;
    if (!blob_string(blob, length, &offset, &field, &field_length)) return 0;
    if (strcmp(type, "ssh-rsa") == 0) {
        /* exponent then modulus; the modulus length, minus its sign byte, is the size */
        if (!blob_string(blob, length, &offset, &field, &field_length)) return 0;
        if (!blob_string(blob, length, &offset, &field, &field_length)) return 0;
        while (field_length > 0 && field[0] == 0) { field++; field_length--; }
        return (unsigned)(field_length * 8u);
    }
    if (strcmp(type, "ssh-ed25519") == 0) return 256;
    if (strncmp(type, "ecdsa-sha2-nistp", 16) == 0) {
        unsigned bits = 0;
        const char *digits = type + 16;
        while (*digits >= '0' && *digits <= '9') bits = bits * 10u + (unsigned)(*digits++ - '0');
        return bits;
    }
    if (strcmp(type, "ssh-dss") == 0) return 1024;
    return 0;
}

static bool known_type(const char *type) {
    return strcmp(type, "ssh-ed25519") == 0 || strcmp(type, "ssh-rsa") == 0 ||
           strcmp(type, "ssh-dss") == 0 || strncmp(type, "ecdsa-sha2-nistp", 16) == 0 ||
           strcmp(type, "sk-ssh-ed25519@openssh.com") == 0;
}

static void copy_field(char *out, size_t size, const char *text, size_t length) {
    if (length >= size) length = size - 1;
    memcpy(out, text, length);
    out[length] = '\0';
}

/* The scratch buffers are static: a key blob and its base64 are a few kilobytes
   together, and the firmware runs this on a 4 KB stack. Nothing here is
   reentrant, which is fine because only the UI calls it. */
bool pim_ssh_pubkey_parse(const char *line, pim_ssh_key_t *key) {
    static uint8_t blob[PIM_SSH_BLOB_MAX];
    static char base64[PIM_SSH_BLOB_MAX * 2];
    uint8_t digest[32];
    const char *type_end, *body, *body_end, *comment;
    size_t blob_length, offset = 0, named_length = 0;
    const uint8_t *named = NULL;

    memset(key->type, 0, sizeof(key->type));
    memset(key->comment, 0, sizeof(key->comment));
    memset(key->fingerprint, 0, sizeof(key->fingerprint));
    key->bits = 0;

    while (*line == ' ' || *line == '\t') line++;
    type_end = strchr(line, ' ');
    if (type_end == NULL || (size_t)(type_end - line) > PIM_SSH_TYPE_MAX) return false;
    copy_field(key->type, sizeof(key->type), line, (size_t)(type_end - line));
    if (!known_type(key->type)) return false;

    body = type_end;
    while (*body == ' ') body++;
    body_end = body;
    while (*body_end != '\0' && *body_end != ' ' && *body_end != '\r' && *body_end != '\n') body_end++;
    if (body_end == body || (size_t)(body_end - body) >= sizeof(base64)) return false;
    copy_field(base64, sizeof(base64), body, (size_t)(body_end - body));

    blob_length = pim_base64_decode(base64, blob, sizeof(blob));
    if (blob_length == 0) return false;
    /* The type inside the blob has to match the one spelled out in front of it. */
    if (!blob_string(blob, blob_length, &offset, &named, &named_length)) return false;
    if (named_length != strlen(key->type) || memcmp(named, key->type, named_length) != 0) return false;

    comment = body_end;
    while (*comment == ' ') comment++;
    {
        size_t length = strlen(comment);
        while (length > 0 && (comment[length - 1] == '\n' || comment[length - 1] == '\r')) length--;
        copy_field(key->comment, sizeof(key->comment), comment, length);
    }

    key->bits = key_bits(key->type, blob, blob_length);
    pim_sha256(blob, blob_length, digest);
    memcpy(key->fingerprint, "SHA256:", 7);
    if (pim_base64_encode(digest, sizeof(digest), key->fingerprint + 7,
                          sizeof(key->fingerprint) - 7, false) == 0) return false;
    return true;
}

bool pim_ssh_private_looks_valid(const char *text, bool *encrypted) {
    static const char openssh[] = "-----BEGIN OPENSSH PRIVATE KEY-----";
    static const char pem[] = "-----BEGIN ";
    bool is_openssh = strncmp(text, openssh, sizeof(openssh) - 1) == 0;
    bool is_pem = !is_openssh && strncmp(text, pem, sizeof(pem) - 1) == 0 &&
                  strstr(text, "PRIVATE KEY-----") != NULL;
    if (encrypted != NULL) {
        /* An OpenSSH key names its cipher in the clear; "none" means no passphrase.
           PEM keys say so in a header instead. */
        *encrypted = false;
        if (is_openssh) {
            static uint8_t blob[PIM_SSH_BLOB_MAX];
            const char *body = strchr(text, '\n');
            size_t length;
            if (body != NULL) {
                length = pim_base64_decode(body + 1, blob, sizeof(blob));
                if (length > 20) {
                    size_t offset = 15;  /* "openssh-key-v1\0" */
                    const uint8_t *cipher = NULL;
                    size_t cipher_length = 0;
                    if (blob_string(blob, length, &offset, &cipher, &cipher_length))
                        *encrypted = !(cipher_length == 4 && memcmp(cipher, "none", 4) == 0);
                }
            }
        } else if (is_pem) {
            *encrypted = strstr(text, "ENCRYPTED") != NULL;
        }
    }
    return is_openssh || is_pem;
}

bool pim_ssh_name_ok(const char *name) {
    size_t i, length = strlen(name);
    if (length == 0 || length > PIM_SSH_NAME_MAX) return false;
    for (i = 0; i < length; i++) {
        char c = name[i];
        bool plain = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!plain) return false;
    }
    return true;
}

static bool read_text(const char *path, char *out, size_t out_size) {
    FILE *file = fopen(path, "rb");
    size_t read;
    if (file == NULL) return false;
    read = fread(out, 1, out_size - 1, file);
    fclose(file);
    out[read] = '\0';
    return read > 0;
}

static void key_path(char *out, size_t size, const char *directory, const char *name, bool pub) {
    snprintf(out, size, "%s/%s%s", directory, name, pub ? ".pub" : "");
}

size_t pim_ssh_keys_scan(const char *directory, pim_ssh_keys_t *keys) {
    DIR *handle;
    struct dirent *entry;
    memset(keys, 0, sizeof(*keys));
    handle = opendir(directory);
    if (handle == NULL) return 0;
    while ((entry = readdir(handle)) != NULL && keys->count < PIM_SSH_KEY_MAX) {
        static char line[PIM_SSH_BLOB_MAX * 2 + 80];
        char path[160];
        pim_ssh_key_t *key = &keys->keys[keys->count];
        size_t length = strlen(entry->d_name);
        if (length < 5 || strcmp(entry->d_name + length - 4, ".pub") != 0) continue;
        if (length - 4 > PIM_SSH_NAME_MAX) continue;
        memset(key, 0, sizeof(*key));
        memcpy(key->name, entry->d_name, length - 4);
        key->name[length - 4] = '\0';
        key_path(path, sizeof(path), directory, key->name, true);
        if (!read_text(path, line, sizeof(line))) continue;
        if (!pim_ssh_pubkey_parse(line, key)) continue;
        key_path(path, sizeof(path), directory, key->name, false);
        {
            FILE *file = fopen(path, "rb");
            key->has_private = file != NULL;
            if (file != NULL) fclose(file);
        }
        keys->count++;
    }
    closedir(handle);
    return keys->count;
}

const pim_ssh_key_t *pim_ssh_keys_find(const pim_ssh_keys_t *keys, const char *name) {
    size_t i;
    for (i = 0; i < keys->count; i++) {
        if (strcmp(keys->keys[i].name, name) == 0) return &keys->keys[i];
    }
    return NULL;
}

bool pim_ssh_key_import(const char *directory, const char *name, const char *text, bool is_private) {
    char path[160];
    FILE *file;
    size_t length;
    if (!pim_ssh_name_ok(name)) return false;
    if (is_private) {
        if (!pim_ssh_private_looks_valid(text, NULL)) return false;
    } else {
        pim_ssh_key_t key;
        if (!pim_ssh_pubkey_parse(text, &key)) return false;
    }
    key_path(path, sizeof(path), directory, name, !is_private);
    file = fopen(path, "wb");
    if (file == NULL) return false;
    length = strlen(text);
    if (fwrite(text, 1, length, file) != length) { fclose(file); remove(path); return false; }
    /* Key files are newline terminated, which is what every other tool expects. */
    if (length > 0 && text[length - 1] != '\n') fputc('\n', file);
    if (fclose(file) != 0) { remove(path); return false; }
    return true;
}

bool pim_ssh_key_delete(const char *directory, const char *name) {
    char path[160];
    bool gone = false;
    if (!pim_ssh_name_ok(name)) return false;
    key_path(path, sizeof(path), directory, name, true);
    if (remove(path) == 0) gone = true;
    key_path(path, sizeof(path), directory, name, false);
    if (remove(path) == 0) gone = true;
    return gone;
}


/* ---------------------------------------------------------- known hosts */

#define KNOWN_HOST_LINE 160

static bool known_host_name_ok(const char *host) {
    size_t length = host != NULL ? strlen(host) : 0;
    if (length == 0 || length > 64) return false;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)host[i];
        if (c <= 0x20 || c > 0x7e) return false;   /* no spaces: the file is space separated */
    }
    return true;
}

bool pim_ssh_known_host_find(const char *path, const char *host,
                             char *fingerprint, size_t size) {
    char line[KNOWN_HOST_LINE];
    FILE *file;
    bool found = false;
    if (!known_host_name_ok(host) || fingerprint == NULL || size == 0) return false;
    file = fopen(path, "rb");
    if (file == NULL) return false;
    while (!found && fgets(line, sizeof(line), file) != NULL) {
        char *space = strchr(line, ' ');
        size_t length;
        if (space == NULL) continue;
        *space = '\0';
        if (strcmp(line, host) != 0) continue;
        length = strlen(space + 1);
        while (length > 0 && (space[length] == '\n' || space[length] == '\r')) space[length--] = '\0';
        snprintf(fingerprint, size, "%s", space + 1);
        found = fingerprint[0] != '\0';
    }
    fclose(file);
    return found;
}

/* Rewritten through a temporary file, so a card pulled mid-write leaves the
   previous list rather than half of one. */
static bool known_host_rewrite(const char *path, const char *host, const char *fingerprint) {
    char temporary[160];
    char line[KNOWN_HOST_LINE];
    FILE *source, *target;
    bool ok = true;
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    target = fopen(temporary, "wb");
    if (target == NULL) return false;
    source = fopen(path, "rb");
    if (source != NULL) {
        while (fgets(line, sizeof(line), source) != NULL) {
            char keep[KNOWN_HOST_LINE];
            char *space;
            snprintf(keep, sizeof(keep), "%s", line);
            space = strchr(line, ' ');
            if (space != NULL) {
                *space = '\0';
                if (strcmp(line, host) == 0) continue;      /* replaced or removed */
            }
            if (fputs(keep, target) < 0) { ok = false; break; }
        }
        fclose(source);
    }
    if (ok && fingerprint != NULL) ok = fprintf(target, "%s %s\n", host, fingerprint) > 0;
    if (fclose(target) != 0 || !ok) { remove(temporary); return false; }
    remove(path);
    return rename(temporary, path) == 0;
}

bool pim_ssh_known_host_remember(const char *path, const char *host, const char *fingerprint) {
    if (!known_host_name_ok(host) || fingerprint == NULL) return false;
    if (strlen(fingerprint) > PIM_SSH_KNOWN_FINGERPRINT_MAX || strchr(fingerprint, ' ') != NULL) return false;
    return known_host_rewrite(path, host, fingerprint);
}

bool pim_ssh_known_host_forget(const char *path, const char *host) {
    if (!known_host_name_ok(host)) return false;
    return known_host_rewrite(path, host, NULL);
}


/* ------------------------------------------------------- making a new key */

/* Just enough of the SSH wire format to write a key file. */
typedef struct { uint8_t *out; size_t size, used; bool overflow; } blob_t;

static void blob_put(blob_t *b, const void *data, size_t length) {
    if (b->overflow || b->used + length > b->size) { b->overflow = true; return; }
    memcpy(b->out + b->used, data, length);
    b->used += length;
}
static void blob_put_u32(blob_t *b, uint32_t value) {
    uint8_t bytes[4] = {(uint8_t)(value >> 24), (uint8_t)(value >> 16), (uint8_t)(value >> 8), (uint8_t)value};
    blob_put(b, bytes, 4);
}
static void blob_put_string(blob_t *b, const void *data, size_t length) {
    blob_put_u32(b, (uint32_t)length);
    blob_put(b, data, length);
}
static void blob_put_text(blob_t *b, const char *text) { blob_put_string(b, text, strlen(text)); }

bool pim_ssh_key_create(const char *directory, const char *name, const char *comment) {
    static uint8_t container[1024];
    static char base64[2048];
    uint8_t public_key[32], secret[64];
    uint8_t public_blob[64];
    char path[160];
    FILE *file;
    uint32_t check;

    if (!pim_ssh_name_ok(name)) return false;
    if (comment == NULL) comment = "picocalc";
    /* Never overwrite an existing key: losing one silently is worse than
       refusing to make another. */
    key_path(path, sizeof(path), directory, name, false);
    file = fopen(path, "rb");
    if (file != NULL) { fclose(file); return false; }

    /* tweetnacl draws its own seed through randombytes, which is wired to the
       platform's entropy, and hands back seed||public as the 64-byte secret. */
    if (crypto_sign_keypair(public_key, secret) != 0) return false;
    if (!pim_ed25519_key_consistent(secret)) return false;
    if (!pim_ssh_random(&check, sizeof(check))) return false;

    {
        blob_t pb = {public_blob, sizeof(public_blob), 0, false};
        blob_put_text(&pb, "ssh-ed25519");
        blob_put_string(&pb, public_key, 32);
        if (pb.overflow) return false;

        {
            blob_t cb = {container, sizeof(container), 0, false};
            uint8_t section[512];
            blob_t sb = {section, sizeof(section), 0, false};
            size_t pad = 0;
            blob_put_u32(&sb, check);
            blob_put_u32(&sb, check);           /* both check words match on an unencrypted key */
            blob_put_text(&sb, "ssh-ed25519");
            blob_put_string(&sb, public_key, 32);
            blob_put_string(&sb, secret, 64);
            blob_put_text(&sb, comment);
            pad = 1;
            while ((sb.used + 0) % 8 != 0) { uint8_t byte = (uint8_t)pad++; blob_put(&sb, &byte, 1); }
            if (sb.overflow) return false;

            blob_put(&cb, "openssh-key-v1\0", 15);
            blob_put_text(&cb, "none");         /* cipher */
            blob_put_text(&cb, "none");         /* kdf */
            blob_put_text(&cb, "");             /* kdf options */
            blob_put_u32(&cb, 1);               /* one key */
            blob_put_string(&cb, public_blob, pb.used);
            blob_put_string(&cb, section, sb.used);
            if (cb.overflow) return false;
            if (pim_base64_encode(container, cb.used, base64, sizeof(base64), true) == 0) return false;
        }

        /* the private half, wrapped the way OpenSSH writes it */
        file = fopen(path, "wb");
        if (file == NULL) return false;
        fprintf(file, "-----BEGIN OPENSSH PRIVATE KEY-----\n");
        for (size_t at = 0; base64[at] != '\0'; at += 70) {
            fprintf(file, "%.70s\n", base64 + at);
        }
        fprintf(file, "-----END OPENSSH PRIVATE KEY-----\n");
        if (fclose(file) != 0) { remove(path); return false; }
        /* A private key must not be readable by anyone else. FAT on the card has
           no notion of this, but the same code runs on a host during testing,
           and ssh tools rightly refuse a key that is left open. */
#ifndef PIM_VFS_DIRENT
        if (chmod(path, 0600) != 0) { remove(path); return false; }
#endif

        /* and the public half, as one line */
        if (pim_base64_encode(public_blob, pb.used, base64, sizeof(base64), true) == 0) {
            remove(path);
            return false;
        }
        key_path(path, sizeof(path), directory, name, true);
        file = fopen(path, "wb");
        if (file == NULL) return false;
        fprintf(file, "ssh-ed25519 %s %s\n", base64, comment);
        if (fclose(file) != 0) { remove(path); return false; }
#ifndef PIM_VFS_DIRENT
        chmod(path, 0644);              /* the public half is meant to be shared */
#endif
    }
    return true;
}
