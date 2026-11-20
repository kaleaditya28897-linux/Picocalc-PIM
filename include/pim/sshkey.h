#ifndef PIM_SSHKEY_H
#define PIM_SSHKEY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* SSH identities kept on the SD card in /PIM/ssh: "<name>.pub" holds the public
   key in the one-line OpenSSH format and "<name>" the matching private key.

   This module stores, checks and fingerprints keys. It does not speak the SSH
   protocol: that transport is a separate piece of work and nothing here opens a
   socket. Keys live unencrypted on a removable card, so a key kept here should
   be one made for this device and nothing else. */

#define PIM_SSH_KEY_MAX 8
#define PIM_SSH_NAME_MAX 24
#define PIM_SSH_TYPE_MAX 24
#define PIM_SSH_COMMENT_MAX 40
#define PIM_SSH_BLOB_MAX 640
/* "SHA256:" plus 43 unpadded base64 characters */
#define PIM_SSH_FINGERPRINT_MAX 50

typedef struct {
    char name[PIM_SSH_NAME_MAX + 1];
    char type[PIM_SSH_TYPE_MAX + 1];            /* "ssh-ed25519", "ssh-rsa", ... */
    char comment[PIM_SSH_COMMENT_MAX + 1];
    char fingerprint[PIM_SSH_FINGERPRINT_MAX + 1];
    unsigned bits;
    bool has_private;
} pim_ssh_key_t;

typedef struct {
    size_t count;
    pim_ssh_key_t keys[PIM_SSH_KEY_MAX];
} pim_ssh_keys_t;

/* ---- primitives, shared with the SSH transport when it lands ---- */

void pim_sha256(const uint8_t *data, size_t length, uint8_t digest[32]);
/* Returns the text length written, or 0 when it would not fit. */
size_t pim_base64_encode(const uint8_t *data, size_t length, char *out, size_t out_size, bool pad);
/* Returns the byte count, or 0 for malformed input. */
size_t pim_base64_decode(const char *text, uint8_t *out, size_t out_size);

/* ---- keys ---- */

/* Parses one "<type> <base64> [comment]" line and fills in type, comment, bits
   and the SHA256 fingerprint OpenSSH prints. False when the line is not a key
   or its blob does not agree with the named type. */
bool pim_ssh_pubkey_parse(const char *line, pim_ssh_key_t *key);

/* Recognises an OpenSSH or PEM private key; *encrypted, when given, says
   whether a passphrase would be needed to use it. */
bool pim_ssh_private_looks_valid(const char *text, bool *encrypted);

/* A key name becomes a file name, so it stays short, lower case and plain. */
bool pim_ssh_name_ok(const char *name);

/* Lists the keys in `directory`, newest listing order unspecified. */
size_t pim_ssh_keys_scan(const char *directory, pim_ssh_keys_t *keys);
const pim_ssh_key_t *pim_ssh_keys_find(const pim_ssh_keys_t *keys, const char *name);

/* Writes a key file, refusing text that does not parse as the kind named. */
bool pim_ssh_key_import(const char *directory, const char *name, const char *text, bool is_private);
/* Makes a new ed25519 key pair on the device and writes both halves into
   `directory` in the formats OpenSSH reads: "<name>" for the private key and
   "<name>.pub" for the public one. The entropy is the platform's, so on the
   firmware this is the hardware source. False when the name is unusable, a key
   of that name is already there, or the card refuses the write. */
bool pim_ssh_key_create(const char *directory, const char *name, const char *comment);

/* Removes both halves of a key; true when at least one file went away. */
bool pim_ssh_key_delete(const char *directory, const char *name);

/* ---- known hosts ----

   One line per host: the name as it was dialled, then the fingerprint. A server
   whose key has changed is refused rather than queried, because that is the one
   case where asking the user teaches them to click through a real attack. */
#define PIM_SSH_KNOWN_FINGERPRINT_MAX 50

/* Looks up a host. False when it has not been seen before. */
bool pim_ssh_known_host_find(const char *path, const char *host,
                             char *fingerprint, size_t size);
/* Records or replaces a host's fingerprint. */
bool pim_ssh_known_host_remember(const char *path, const char *host, const char *fingerprint);
/* Forgets a host, for when a server really was rebuilt. */
bool pim_ssh_known_host_forget(const char *path, const char *host);

#endif
