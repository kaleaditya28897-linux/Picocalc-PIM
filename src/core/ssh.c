#include "pim/ssh.h"

#include <stdio.h>
#include <string.h>

#include "pim/sshcrypto.h"
#include "pim/sshkey.h"

/* Message numbers from RFC 4253, 4252 and 4254. */
enum {
    MSG_DISCONNECT = 1, MSG_IGNORE = 2, MSG_UNIMPLEMENTED = 3, MSG_DEBUG = 4,
    MSG_SERVICE_REQUEST = 5, MSG_SERVICE_ACCEPT = 6, MSG_EXT_INFO = 7,
    MSG_KEXINIT = 20, MSG_NEWKEYS = 21,
    MSG_KEX_ECDH_INIT = 30, MSG_KEX_ECDH_REPLY = 31,
    MSG_USERAUTH_REQUEST = 50, MSG_USERAUTH_FAILURE = 51, MSG_USERAUTH_SUCCESS = 52,
    MSG_USERAUTH_BANNER = 53, MSG_USERAUTH_PK_OK = 60,
    MSG_GLOBAL_REQUEST = 80, MSG_REQUEST_SUCCESS = 81, MSG_REQUEST_FAILURE = 82,
    MSG_CHANNEL_OPEN = 90, MSG_CHANNEL_OPEN_CONFIRMATION = 91, MSG_CHANNEL_OPEN_FAILURE = 92,
    MSG_CHANNEL_WINDOW_ADJUST = 93, MSG_CHANNEL_DATA = 94, MSG_CHANNEL_EXTENDED_DATA = 95,
    MSG_CHANNEL_EOF = 96, MSG_CHANNEL_CLOSE = 97, MSG_CHANNEL_REQUEST = 98,
    MSG_CHANNEL_SUCCESS = 99, MSG_CHANNEL_FAILURE = 100
};

#define CLIENT_VERSION "SSH-2.0-PicoDeck_1.0"
#define KEX_NAME "curve25519-sha256"
#define KEX_NAME_ALT "curve25519-sha256@libssh.org"
#define HOSTKEY_NAME "ssh-ed25519"
#define CIPHER_NAME "chacha20-poly1305@openssh.com"
#define LOCAL_WINDOW 32768u
#define LOCAL_PACKET_MAX 2048u

/* ------------------------------------------------------------------ writing */

/* A bounded writer over the caller's buffer: every put is checked, and one
   overflow poisons the whole thing so a truncated packet can never be sent. */
typedef struct { uint8_t *out; size_t size, used; bool overflow; } writer_t;

static void put_raw(writer_t *w, const void *data, size_t length) {
    if (w->overflow || w->used + length > w->size) { w->overflow = true; return; }
    memcpy(w->out + w->used, data, length);
    w->used += length;
}
static void put_byte(writer_t *w, uint8_t value) { put_raw(w, &value, 1); }
static void put_u32(writer_t *w, uint32_t value) {
    uint8_t b[4] = {(uint8_t)(value >> 24), (uint8_t)(value >> 16), (uint8_t)(value >> 8), (uint8_t)value};
    put_raw(w, b, 4);
}
static void put_string(writer_t *w, const void *data, size_t length) {
    put_u32(w, (uint32_t)length);
    put_raw(w, data, length);
}
static void put_text(writer_t *w, const char *text) { put_string(w, text, strlen(text)); }
/* An mpint is signed, so a leading zero goes in front of a high bit. */
static void put_mpint(writer_t *w, const uint8_t *value, size_t length) {
    size_t lead = 0;
    while (lead < length && value[lead] == 0) lead++;      /* no needless zeroes */
    if (lead == length) { put_u32(w, 0); return; }
    if (value[lead] & 0x80u) {
        put_u32(w, (uint32_t)(length - lead + 1));
        put_byte(w, 0);
    } else {
        put_u32(w, (uint32_t)(length - lead));
    }
    put_raw(w, value + lead, length - lead);
}

/* ------------------------------------------------------------------ reading */

typedef struct { const uint8_t *in; size_t size, at; bool bad; } reader_t;

static uint8_t get_byte(reader_t *r) {
    if (r->bad || r->at + 1 > r->size) { r->bad = true; return 0; }
    return r->in[r->at++];
}
static uint32_t get_u32(reader_t *r) {
    uint32_t value;
    if (r->bad || r->at + 4 > r->size) { r->bad = true; return 0; }
    value = ((uint32_t)r->in[r->at] << 24) | ((uint32_t)r->in[r->at + 1] << 16) |
            ((uint32_t)r->in[r->at + 2] << 8) | (uint32_t)r->in[r->at + 3];
    r->at += 4;
    return value;
}
/* Returns a pointer into the packet; nothing is copied. */
static const uint8_t *get_string(reader_t *r, size_t *length) {
    uint32_t n = get_u32(r);
    const uint8_t *at;
    if (r->bad || n > r->size - r->at) { r->bad = true; *length = 0; return NULL; }
    at = r->in + r->at;
    r->at += n;
    *length = n;
    return at;
}

static bool name_list_has(const uint8_t *list, size_t length, const char *wanted) {
    size_t start = 0, i;
    size_t want = strlen(wanted);
    for (i = 0; i <= length; i++) {
        if (i == length || list[i] == ',') {
            if (i - start == want && memcmp(list + start, wanted, want) == 0) return true;
            start = i + 1;
        }
    }
    return false;
}

static void fail(pim_ssh_t *ssh, const char *why) {
    if (ssh->state != PIM_SSH_FAILED) {
        snprintf(ssh->error, sizeof(ssh->error), "%s", why);
        ssh->state = PIM_SSH_FAILED;
    }
}

/* ------------------------------------------------------------- packet output */

/* Padding is computed over the padding byte and the payload only: with an AEAD
   cipher the length field is encrypted separately and is not part of the block
   alignment. */
static size_t padding_for(size_t payload, bool encrypted) {
    size_t base = encrypted ? payload + 1 : payload + 1 + 4;
    size_t pad = 8 - (base % 8);
    if (pad < 4) pad += 8;
    return pad;
}

static void send_packet(pim_ssh_t *ssh, const uint8_t *payload, size_t length, writer_t *out) {
    uint8_t frame[PIM_SSH_PACKET_MAX + 64];
    uint8_t nonce[8] = {0};
    size_t pad = padding_for(length, ssh->encrypt_out);
    size_t packet_length = 1 + length + pad;
    size_t total;

    if (4 + packet_length + PIM_SSH_TAG_BYTES > sizeof(frame)) { fail(ssh, "packet too large to send"); return; }
    frame[0] = (uint8_t)(packet_length >> 24);
    frame[1] = (uint8_t)(packet_length >> 16);
    frame[2] = (uint8_t)(packet_length >> 8);
    frame[3] = (uint8_t)packet_length;
    frame[4] = (uint8_t)pad;
    memcpy(frame + 5, payload, length);
    /* Padding must be unpredictable once the session is encrypted. */
    if (ssh->encrypt_out) {
        if (!pim_ssh_random(frame + 5 + length, pad)) { fail(ssh, "no entropy for padding"); return; }
    } else {
        memset(frame + 5 + length, 0, pad);
    }
    total = 4 + packet_length;

    if (!ssh->encrypt_out) {
        put_raw(out, frame, total);
        ssh->sequence_out++;
        return;
    }
    {
        /* chacha20-poly1305@openssh.com: the length goes under the second key,
           the body under the first starting at block one, and the tag covers
           both halves of the ciphertext. The nonce is the sequence number. */
        const uint8_t *main_key = ssh->key_c2s;
        const uint8_t *header_key = ssh->key_c2s + 32;
        uint8_t poly_block[64], tag[PIM_SSH_TAG_BYTES];
        nonce[4] = (uint8_t)(ssh->sequence_out >> 24);
        nonce[5] = (uint8_t)(ssh->sequence_out >> 16);
        nonce[6] = (uint8_t)(ssh->sequence_out >> 8);
        nonce[7] = (uint8_t)ssh->sequence_out;
        pim_chacha20_block(main_key, 0, nonce, poly_block);
        pim_chacha20_xor(header_key, 0, nonce, frame, frame, 4);
        pim_chacha20_xor(main_key, 1, nonce, frame + 4, frame + 4, packet_length);
        pim_poly1305(poly_block, frame, total, tag);
        put_raw(out, frame, total);
        put_raw(out, tag, sizeof(tag));
        ssh->sequence_out++;
    }
}

/* ------------------------------------------------------------ key derivation */

/* RFC 4253 section 7.2: HASH(K || H || letter || session_id), extended by
   hashing in what has been produced so far. */
static void derive_key(const pim_ssh_t *ssh, const uint8_t *shared, size_t shared_length,
                       char letter, uint8_t *out, size_t out_size) {
    uint8_t buffer[64 + 32 + 1 + 32 + 64];
    uint8_t digest[32];
    size_t done = 0;
    while (done < out_size) {
        writer_t w = {buffer, sizeof(buffer), 0, false};
        put_mpint(&w, shared, shared_length);
        put_raw(&w, ssh->exchange_hash, 32);
        if (done == 0) {
            put_byte(&w, (uint8_t)letter);
            put_raw(&w, ssh->session_id, 32);
        } else {
            put_raw(&w, out, done);
        }
        pim_sha256(buffer, w.used, digest);
        {
            size_t chunk = out_size - done < 32 ? out_size - done : 32;
            memcpy(out + done, digest, chunk);
            done += chunk;
        }
    }
}

/* ------------------------------------------------------------------- KEXINIT */

static void send_kexinit(pim_ssh_t *ssh, writer_t *out) {
    writer_t w = {ssh->client_kexinit, sizeof(ssh->client_kexinit), 0, false};
    uint8_t cookie[16];
    if (!pim_ssh_random(cookie, sizeof(cookie))) { fail(ssh, "no entropy for the kex cookie"); return; }
    put_byte(&w, MSG_KEXINIT);
    put_raw(&w, cookie, sizeof(cookie));
    put_text(&w, KEX_NAME "," KEX_NAME_ALT);
    put_text(&w, HOSTKEY_NAME);
    put_text(&w, CIPHER_NAME);          /* client to server */
    put_text(&w, CIPHER_NAME);          /* server to client */
    put_text(&w, "");                   /* mac: implicit in the cipher */
    put_text(&w, "");
    put_text(&w, "none");               /* compression */
    put_text(&w, "none");
    put_text(&w, "");                   /* languages */
    put_text(&w, "");
    put_byte(&w, 0);                    /* no guessed kex packet follows */
    put_u32(&w, 0);                     /* reserved */
    if (w.overflow) { fail(ssh, "kexinit did not fit"); return; }
    ssh->client_kexinit_length = (uint16_t)w.used;
    send_packet(ssh, ssh->client_kexinit, w.used, out);
}

static void send_ecdh_init(pim_ssh_t *ssh, writer_t *out) {
    uint8_t payload[1 + 4 + 32];
    writer_t w = {payload, sizeof(payload), 0, false};
    if (!pim_ssh_random(ssh->ephemeral_secret, 32)) { fail(ssh, "no entropy for the key exchange"); return; }
    pim_x25519_base(ssh->ephemeral_public, ssh->ephemeral_secret);
    put_byte(&w, MSG_KEX_ECDH_INIT);
    put_string(&w, ssh->ephemeral_public, 32);
    send_packet(ssh, payload, w.used, out);
}

/* Pulls the ed25519 key out of a host key blob and fingerprints the blob the
   way OpenSSH does, so what is shown matches what ssh-keygen -lf prints. */
static bool take_host_key(pim_ssh_t *ssh, const uint8_t *blob, size_t length) {
    reader_t r = {blob, length, 0, false};
    size_t name_length = 0, key_length = 0;
    const uint8_t *name = get_string(&r, &name_length);
    const uint8_t *key = get_string(&r, &key_length);
    uint8_t digest[32];
    if (r.bad || name == NULL || key == NULL) return false;
    if (name_length != strlen(HOSTKEY_NAME) || memcmp(name, HOSTKEY_NAME, name_length) != 0) return false;
    if (key_length != 32) return false;
    if (length > sizeof(ssh->host_key)) return false;
    memcpy(ssh->host_key, blob, length);
    ssh->host_key_length = (uint16_t)length;
    memcpy(ssh->host_key_ed25519, key, 32);
    pim_sha256(blob, length, digest);
    memcpy(ssh->fingerprint, "SHA256:", 7);
    return pim_base64_encode(digest, sizeof(digest), ssh->fingerprint + 7,
                             sizeof(ssh->fingerprint) - 7, false) > 0;
}

static void send_newkeys_and_service(pim_ssh_t *ssh, writer_t *out);

static void handle_ecdh_reply(pim_ssh_t *ssh, reader_t *r, writer_t *out) {
    size_t blob_length = 0, server_public_length = 0, signature_length = 0;
    const uint8_t *blob = get_string(r, &blob_length);
    const uint8_t *server_public = get_string(r, &server_public_length);
    const uint8_t *signature = get_string(r, &signature_length);
    uint8_t shared[32];
    uint8_t *hash_input = ssh->hash_scratch;
    writer_t h = {hash_input, sizeof(ssh->hash_scratch), 0, false};

    if (r->bad || blob == NULL || server_public == NULL || signature == NULL) { fail(ssh, "malformed key exchange reply"); return; }
    if (server_public_length != 32) { fail(ssh, "server sent a bad curve25519 point"); return; }
    if (!take_host_key(ssh, blob, blob_length)) { fail(ssh, "host key is not ed25519"); return; }

    pim_x25519(shared, ssh->ephemeral_secret, server_public);
    {   /* an all-zero shared secret means a degenerate point: refuse it */
        uint8_t zero[32] = {0};
        if (memcmp(shared, zero, 32) == 0) { fail(ssh, "server sent a degenerate point"); return; }
    }

    put_text(&h, ssh->client_version);
    put_text(&h, ssh->server_version);
    put_string(&h, ssh->client_kexinit, ssh->client_kexinit_length);
    put_string(&h, ssh->server_kexinit, ssh->server_kexinit_length);
    put_string(&h, ssh->host_key, ssh->host_key_length);
    put_string(&h, ssh->ephemeral_public, 32);
    put_string(&h, server_public, 32);
    put_mpint(&h, shared, 32);
    if (h.overflow) { fail(ssh, "exchange hash did not fit"); return; }
    pim_sha256(hash_input, h.used, ssh->exchange_hash);
    if (!ssh->have_session_id) {
        memcpy(ssh->session_id, ssh->exchange_hash, 32);
        ssh->have_session_id = true;
    }

    /* The signature blob is a name and the 64 raw bytes. */
    {
        reader_t sr = {signature, signature_length, 0, false};
        size_t sig_name_length = 0, sig_bytes_length = 0;
        const uint8_t *sig_name = get_string(&sr, &sig_name_length);
        const uint8_t *sig_bytes = get_string(&sr, &sig_bytes_length);
        if (sr.bad || sig_name == NULL || sig_bytes == NULL ||
            sig_name_length != strlen(HOSTKEY_NAME) ||
            memcmp(sig_name, HOSTKEY_NAME, sig_name_length) != 0 ||
            sig_bytes_length != PIM_SSH_SIG_BYTES) {
            fail(ssh, "malformed host key signature");
            return;
        }
        if (!pim_ed25519_verify(sig_bytes, ssh->exchange_hash, 32, ssh->host_key_ed25519)) {
            fail(ssh, "host key signature does not verify");
            return;
        }
    }

    /* Derive both directions now; they take effect at NEWKEYS. */
    derive_key(ssh, shared, 32, 'C', ssh->key_c2s, sizeof(ssh->key_c2s));
    derive_key(ssh, shared, 32, 'D', ssh->key_s2c, sizeof(ssh->key_s2c));

    /* A host nobody has vouched for stops here, with the fingerprint ready to
       show. A pinned one that does not match is an error, not a question. */
    if (ssh->config.known_fingerprint[0] != '\0') {
        if (strcmp(ssh->config.known_fingerprint, ssh->fingerprint) != 0) {
            fail(ssh, "host key changed since last time");
            return;
        }
        send_newkeys_and_service(ssh, out);
    } else {
        ssh->state = PIM_SSH_HOSTKEY_UNKNOWN;
    }
}

/* --------------------------------------------------------------- user auth */

static void send_userauth_request(pim_ssh_t *ssh, bool with_signature, writer_t *out) {
    uint8_t *payload = ssh->auth_scratch;
    writer_t w = {payload, sizeof(ssh->auth_scratch), 0, false};
    uint8_t key_blob[4 + 11 + 4 + 32];
    writer_t kb = {key_blob, sizeof(key_blob), 0, false};

    if (ssh->config.have_secret) {
        put_text(&kb, HOSTKEY_NAME);
        put_string(&kb, ssh->config.secret + 32, 32);      /* the public half */
    }

    put_byte(&w, MSG_USERAUTH_REQUEST);
    put_text(&w, ssh->config.user);
    put_text(&w, "ssh-connection");
    if (ssh->config.have_secret) {
        put_text(&w, "publickey");
        put_byte(&w, with_signature ? 1 : 0);
        put_text(&w, HOSTKEY_NAME);
        put_string(&w, key_blob, kb.used);
        if (with_signature) {
            /* The signature covers the session id followed by this very
               request, as RFC 4252 section 7 sets out. */
            uint8_t *signed_data = ssh->sign_scratch;
            writer_t sd = {signed_data, sizeof(ssh->sign_scratch), 0, false};
            uint8_t signature[PIM_SSH_SIG_BYTES];
            uint8_t signature_blob[4 + 11 + 4 + PIM_SSH_SIG_BYTES];
            writer_t sb = {signature_blob, sizeof(signature_blob), 0, false};
            put_string(&sd, ssh->session_id, 32);
            put_raw(&sd, payload, w.used);
            if (sd.overflow) { fail(ssh, "signature input did not fit"); return; }
            pim_ed25519_sign(signature, signed_data, sd.used, ssh->config.secret);
            put_text(&sb, HOSTKEY_NAME);
            put_string(&sb, signature, sizeof(signature));
            put_string(&w, signature_blob, sb.used);
        }
    } else if (ssh->config.have_password) {
        put_text(&w, "password");
        put_byte(&w, 0);
        put_text(&w, ssh->config.password);
    } else {
        put_text(&w, "none");
    }
    if (w.overflow) { fail(ssh, "auth request did not fit"); return; }
    send_packet(ssh, payload, w.used, out);
}

static void send_newkeys_and_service(pim_ssh_t *ssh, writer_t *out) {
    uint8_t payload[64];
    writer_t w = {payload, sizeof(payload), 0, false};
    put_byte(&w, MSG_NEWKEYS);
    send_packet(ssh, payload, w.used, out);
    ssh->encrypt_out = true;            /* everything after this is encrypted */

    w.used = 0;
    put_byte(&w, MSG_SERVICE_REQUEST);
    put_text(&w, "ssh-userauth");
    send_packet(ssh, payload, w.used, out);
    ssh->state = PIM_SSH_AUTH;
}

/* ----------------------------------------------------------------- channel */

static void send_channel_open(pim_ssh_t *ssh, writer_t *out) {
    uint8_t payload[64];
    writer_t w = {payload, sizeof(payload), 0, false};
    put_byte(&w, MSG_CHANNEL_OPEN);
    put_text(&w, "session");
    put_u32(&w, ssh->channel);
    put_u32(&w, LOCAL_WINDOW);
    put_u32(&w, LOCAL_PACKET_MAX);
    ssh->local_window = LOCAL_WINDOW;
    send_packet(ssh, payload, w.used, out);
    ssh->state = PIM_SSH_OPENING;
}

static void send_pty_and_shell(pim_ssh_t *ssh, writer_t *out) {
    uint8_t payload[256];
    writer_t w = {payload, sizeof(payload), 0, false};
    /* No terminal modes: the defaults suit a dumb 40-column screen, and an
       empty string is what "use yours" looks like on the wire. */
    put_byte(&w, MSG_CHANNEL_REQUEST);
    put_u32(&w, ssh->remote_channel);
    put_text(&w, "pty-req");
    put_byte(&w, 1);
    put_text(&w, "ansi");
    put_u32(&w, ssh->config.cols);
    put_u32(&w, ssh->config.rows);
    put_u32(&w, 0);
    put_u32(&w, 0);
    put_text(&w, "");
    send_packet(ssh, payload, w.used, out);
    ssh->pty_requested = true;

    w.used = 0;
    put_byte(&w, MSG_CHANNEL_REQUEST);
    put_u32(&w, ssh->remote_channel);
    put_text(&w, "shell");
    put_byte(&w, 1);
    send_packet(ssh, payload, w.used, out);
    ssh->shell_requested = true;
}

/* Tops the window back up as data is consumed, so the server never stalls. */
static void adjust_window(pim_ssh_t *ssh, uint32_t consumed, writer_t *out) {
    uint8_t payload[16];
    writer_t w = {payload, sizeof(payload), 0, false};
    if (consumed == 0) return;
    ssh->local_window = ssh->local_window > consumed ? ssh->local_window - consumed : 0;
    if (ssh->local_window > LOCAL_WINDOW / 2) return;    /* not worth a packet yet */
    put_byte(&w, MSG_CHANNEL_WINDOW_ADJUST);
    put_u32(&w, ssh->remote_channel);
    put_u32(&w, LOCAL_WINDOW - ssh->local_window);
    send_packet(ssh, payload, w.used, out);
    ssh->local_window = LOCAL_WINDOW;
}

/* ---------------------------------------------------------------- dispatch */

static size_t handle_payload(pim_ssh_t *ssh, const uint8_t *payload, size_t length,
                             writer_t *out, uint8_t *screen, size_t screen_size) {
    reader_t r = {payload, length, 0, false};
    uint8_t type = get_byte(&r);
    size_t screen_used = 0;

    switch (type) {
    case MSG_IGNORE: case MSG_DEBUG: case MSG_UNIMPLEMENTED: case MSG_EXT_INFO:
    case MSG_USERAUTH_BANNER: case MSG_GLOBAL_REQUEST:
        break;                          /* nothing here changes what we do next */
    case MSG_DISCONNECT: {
        size_t text_length = 0;
        const uint8_t *text;
        (void)get_u32(&r);              /* reason code */
        text = get_string(&r, &text_length);
        if (text != NULL && text_length > 0) {
            char message[PIM_SSH_ERROR_MAX + 1];
            size_t copy = text_length < sizeof(message) - 1 ? text_length : sizeof(message) - 1;
            memcpy(message, text, copy);
            message[copy] = '\0';
            fail(ssh, message);
        } else {
            fail(ssh, "server disconnected");
        }
        break;
    }
    case MSG_KEXINIT: {
        /* Keep the whole payload: the exchange hash covers it verbatim. */
        if (length > sizeof(ssh->server_kexinit)) { fail(ssh, "server kexinit too large"); break; }
        memcpy(ssh->server_kexinit, payload, length);
        ssh->server_kexinit_length = (uint16_t)length;
        {
            size_t n = 0;
            const uint8_t *kex, *hostkey, *cipher_out, *cipher_in;
            size_t kex_length = 0, hostkey_length = 0, out_length = 0, in_length = 0;
            r.at = 1 + 16;              /* past the type byte and the cookie */
            kex = get_string(&r, &kex_length);
            hostkey = get_string(&r, &hostkey_length);
            cipher_out = get_string(&r, &out_length);
            cipher_in = get_string(&r, &in_length);
            (void)n;
            if (r.bad) { fail(ssh, "malformed server kexinit"); break; }
            if (!name_list_has(kex, kex_length, KEX_NAME) &&
                !name_list_has(kex, kex_length, KEX_NAME_ALT)) {
                fail(ssh, "server will not do curve25519-sha256");
                break;
            }
            if (!name_list_has(hostkey, hostkey_length, HOSTKEY_NAME)) {
                fail(ssh, "server has no ed25519 host key");
                break;
            }
            if (!name_list_has(cipher_out, out_length, CIPHER_NAME) ||
                !name_list_has(cipher_in, in_length, CIPHER_NAME)) {
                fail(ssh, "server will not do chacha20-poly1305");
                break;
            }
        }
        send_ecdh_init(ssh, out);
        break;
    }
    case MSG_KEX_ECDH_REPLY:
        handle_ecdh_reply(ssh, &r, out);
        break;
    case MSG_NEWKEYS:
        ssh->decrypt_in = true;         /* the next packet from the server is encrypted */
        break;
    case MSG_SERVICE_ACCEPT:
        if (!ssh->auth_sent) {
            /* Offer the key without a signature first: a server that will not
               take it says so cheaply, before anything is signed. */
            send_userauth_request(ssh, false, out);
            ssh->auth_sent = true;
        }
        break;
    case MSG_USERAUTH_PK_OK:
        send_userauth_request(ssh, true, out);
        break;
    case MSG_USERAUTH_SUCCESS:
        send_channel_open(ssh, out);
        break;
    case MSG_USERAUTH_FAILURE:
        /* Fall back to a password once, if one was given. */
        if (ssh->config.have_password && !ssh->auth_password_tried) {
            ssh->auth_password_tried = true;
            ssh->config.have_secret = false;
            send_userauth_request(ssh, false, out);
        } else {
            fail(ssh, ssh->config.have_secret ? "server rejected the key" : "authentication failed");
        }
        break;
    case MSG_CHANNEL_OPEN_CONFIRMATION:
        (void)get_u32(&r);              /* our channel */
        ssh->remote_channel = get_u32(&r);
        ssh->remote_window = get_u32(&r);
        (void)get_u32(&r);              /* their maximum packet */
        if (r.bad) { fail(ssh, "malformed channel confirmation"); break; }
        ssh->channel_open = true;
        send_pty_and_shell(ssh, out);
        break;
    case MSG_CHANNEL_OPEN_FAILURE:
        fail(ssh, "server refused the session channel");
        break;
    case MSG_CHANNEL_SUCCESS:
        /* The shell is the second request; once it is granted, data can flow. */
        if (ssh->shell_requested && ssh->state == PIM_SSH_OPENING) ssh->state = PIM_SSH_READY;
        break;
    case MSG_CHANNEL_FAILURE:
        if (ssh->shell_requested) fail(ssh, "server would not start a shell");
        break;
    case MSG_CHANNEL_WINDOW_ADJUST:
        (void)get_u32(&r);
        ssh->remote_window += get_u32(&r);
        break;
    case MSG_CHANNEL_DATA: case MSG_CHANNEL_EXTENDED_DATA: {
        size_t data_length = 0;
        const uint8_t *data;
        (void)get_u32(&r);              /* our channel */
        if (type == MSG_CHANNEL_EXTENDED_DATA) (void)get_u32(&r);   /* stderr goes to the screen too */
        data = get_string(&r, &data_length);
        if (data == NULL) { fail(ssh, "malformed channel data"); break; }
        screen_used = data_length < screen_size ? data_length : screen_size;
        memcpy(screen, data, screen_used);
        adjust_window(ssh, (uint32_t)data_length, out);
        break;
    }
    case MSG_CHANNEL_EOF:
        break;
    case MSG_CHANNEL_CLOSE:
        ssh->channel_open = false;
        ssh->state = PIM_SSH_CLOSED;
        break;
    default:
        break;                          /* anything else is not ours to mind */
    }
    return screen_used;
}

/* ------------------------------------------------------------ packet input */

/* Pulls one packet out of the buffer. Returns 0 when more bytes are needed. */
static size_t take_packet(pim_ssh_t *ssh, uint8_t *payload, size_t payload_size, size_t *payload_length) {
    if (!ssh->decrypt_in) {
        uint32_t packet_length;
        size_t padding;
        if (ssh->in_length < 4) return 0;
        packet_length = ((uint32_t)ssh->in[0] << 24) | ((uint32_t)ssh->in[1] << 16) |
                        ((uint32_t)ssh->in[2] << 8) | (uint32_t)ssh->in[3];
        if (packet_length < 8 || packet_length > PIM_SSH_PACKET_MAX) { fail(ssh, "bad packet length"); return 0; }
        if (ssh->in_length < 4 + packet_length) return 0;
        padding = ssh->in[4];
        if (padding + 1 > packet_length) { fail(ssh, "bad padding"); return 0; }
        *payload_length = packet_length - padding - 1;
        if (*payload_length > payload_size) { fail(ssh, "packet larger than the buffer"); return 0; }
        memcpy(payload, ssh->in + 5, *payload_length);
        ssh->sequence_in++;
        return 4 + packet_length;
    }
    {
        const uint8_t *main_key = ssh->key_s2c;
        const uint8_t *header_key = ssh->key_s2c + 32;
        uint8_t nonce[8] = {0};
        uint8_t length_bytes[4], poly_block[64];
        uint32_t packet_length;
        size_t total, padding;
        if (ssh->in_length < 4) return 0;
        nonce[4] = (uint8_t)(ssh->sequence_in >> 24);
        nonce[5] = (uint8_t)(ssh->sequence_in >> 16);
        nonce[6] = (uint8_t)(ssh->sequence_in >> 8);
        nonce[7] = (uint8_t)ssh->sequence_in;
        pim_chacha20_xor(header_key, 0, nonce, ssh->in, length_bytes, 4);
        packet_length = ((uint32_t)length_bytes[0] << 24) | ((uint32_t)length_bytes[1] << 16) |
                        ((uint32_t)length_bytes[2] << 8) | (uint32_t)length_bytes[3];
        if (packet_length < 8 || packet_length > PIM_SSH_PACKET_MAX) { fail(ssh, "bad encrypted packet length"); return 0; }
        total = 4 + packet_length + PIM_SSH_TAG_BYTES;
        if (ssh->in_length < total) return 0;
        pim_chacha20_block(main_key, 0, nonce, poly_block);
        if (!pim_poly1305_verify(poly_block, ssh->in, 4 + packet_length,
                                 ssh->in + 4 + packet_length)) {
            fail(ssh, "message authentication failed");
            return 0;
        }
        {
            uint8_t body[PIM_SSH_PACKET_MAX];
            pim_chacha20_xor(main_key, 1, nonce, ssh->in + 4, body, packet_length);
            padding = body[0];
            if (padding + 1 > packet_length) { fail(ssh, "bad padding"); return 0; }
            *payload_length = packet_length - padding - 1;
            if (*payload_length > payload_size) { fail(ssh, "packet larger than the buffer"); return 0; }
            memcpy(payload, body + 1, *payload_length);
        }
        ssh->sequence_in++;
        return total;
    }
}

/* ------------------------------------------------------------------- public */

size_t pim_ssh_start(pim_ssh_t *ssh, const pim_ssh_config_t *config,
                     uint8_t *out, size_t out_size) {
    size_t length;
    memset(ssh, 0, sizeof(*ssh));
    ssh->config = *config;
    if (ssh->config.cols == 0) ssh->config.cols = 80;
    if (ssh->config.rows == 0) ssh->config.rows = 24;
    snprintf(ssh->client_version, sizeof(ssh->client_version), "%s", CLIENT_VERSION);
    length = strlen(ssh->client_version);
    if (length + 2 > out_size) { fail(ssh, "no room for the identification string"); return 0; }
    memcpy(out, ssh->client_version, length);
    out[length] = '\r';
    out[length + 1] = '\n';
    ssh->state = PIM_SSH_VERSION_SENT;
    return length + 2;
}

size_t pim_ssh_feed(pim_ssh_t *ssh, const uint8_t *data, size_t length,
                    uint8_t *out, size_t out_size, size_t *out_length,
                    uint8_t *screen, size_t screen_size) {
    writer_t w = {out, out_size, 0, false};
    size_t screen_used = 0;

    if (out_length != NULL) *out_length = 0;
    if (ssh->state == PIM_SSH_FAILED) return 0;

    /* The server's identification line comes before any packet, and may arrive
       in pieces. Anything before it that is not a version line is a banner, and
       is discarded as the specification allows. */
    while (length > 0 && !ssh->server_version_done) {
        uint8_t c = *data++;
        length--;
        if (c == '\n') {
            ssh->server_version[ssh->server_version_length] = '\0';
            if (ssh->server_version_length > 0 &&
                ssh->server_version[ssh->server_version_length - 1] == '\r') {
                ssh->server_version[--ssh->server_version_length] = '\0';
            }
            if (strncmp(ssh->server_version, "SSH-2.0-", 8) == 0 ||
                strncmp(ssh->server_version, "SSH-1.99-", 9) == 0) {
                ssh->server_version_done = true;
                ssh->state = PIM_SSH_KEX;
                send_kexinit(ssh, &w);
            } else {
                ssh->server_version_length = 0;     /* a banner line: ignore it */
            }
        } else if (ssh->server_version_length < PIM_SSH_VERSION_MAX) {
            ssh->server_version[ssh->server_version_length++] = (char)c;
        } else {
            fail(ssh, "server identification line too long");
            return 0;
        }
    }

    if (length > 0) {
        if (ssh->in_length + length > sizeof(ssh->in)) { fail(ssh, "input overran the packet buffer"); return 0; }
        memcpy(ssh->in + ssh->in_length, data, length);
        ssh->in_length += length;
    }

    for (;;) {
        uint8_t payload[PIM_SSH_PACKET_MAX];
        size_t payload_length = 0;
        size_t consumed;
        if (ssh->state == PIM_SSH_FAILED || ssh->state == PIM_SSH_HOSTKEY_UNKNOWN) break;
        consumed = take_packet(ssh, payload, sizeof(payload), &payload_length);
        if (consumed == 0) break;
        memmove(ssh->in, ssh->in + consumed, ssh->in_length - consumed);
        ssh->in_length -= consumed;
        screen_used += handle_payload(ssh, payload, payload_length, &w,
                                      screen + screen_used, screen_size - screen_used);
    }

    if (w.overflow) fail(ssh, "reply did not fit in the buffer");
    if (out_length != NULL) *out_length = w.used;
    return screen_used;
}

size_t pim_ssh_accept_host_key(pim_ssh_t *ssh, uint8_t *out, size_t out_size,
                               size_t *out_length) {
    writer_t w = {out, out_size, 0, false};
    if (out_length != NULL) *out_length = 0;
    if (ssh->state != PIM_SSH_HOSTKEY_UNKNOWN) return 0;
    snprintf(ssh->config.known_fingerprint, sizeof(ssh->config.known_fingerprint), "%s", ssh->fingerprint);
    send_newkeys_and_service(ssh, &w);
    if (w.overflow) fail(ssh, "reply did not fit in the buffer");
    if (out_length != NULL) *out_length = w.used;
    return w.used;
}

size_t pim_ssh_send(pim_ssh_t *ssh, const uint8_t *data, size_t length,
                    uint8_t *out, size_t out_size) {
    writer_t w = {out, out_size, 0, false};
    uint8_t payload[PIM_SSH_PACKET_MAX];
    writer_t p = {payload, sizeof(payload), 0, false};
    if (ssh->state != PIM_SSH_READY || length == 0) return 0;
    if (length > ssh->remote_window) length = ssh->remote_window;
    if (length == 0) return 0;          /* the server is not accepting more yet */
    if (length > LOCAL_PACKET_MAX) length = LOCAL_PACKET_MAX;
    put_byte(&p, MSG_CHANNEL_DATA);
    put_u32(&p, ssh->remote_channel);
    put_string(&p, data, length);
    if (p.overflow) return 0;
    send_packet(ssh, payload, p.used, &w);
    ssh->remote_window -= (uint32_t)length;
    return w.used;
}

size_t pim_ssh_resize(pim_ssh_t *ssh, unsigned cols, unsigned rows,
                      uint8_t *out, size_t out_size) {
    writer_t w = {out, out_size, 0, false};
    uint8_t payload[64];
    writer_t p = {payload, sizeof(payload), 0, false};
    if (ssh->state != PIM_SSH_READY) return 0;
    ssh->config.cols = (uint16_t)cols;
    ssh->config.rows = (uint16_t)rows;
    put_byte(&p, MSG_CHANNEL_REQUEST);
    put_u32(&p, ssh->remote_channel);
    put_text(&p, "window-change");
    put_byte(&p, 0);                    /* no reply wanted */
    put_u32(&p, cols);
    put_u32(&p, rows);
    put_u32(&p, 0);
    put_u32(&p, 0);
    send_packet(ssh, payload, p.used, &w);
    return w.used;
}

size_t pim_ssh_close(pim_ssh_t *ssh, uint8_t *out, size_t out_size) {
    writer_t w = {out, out_size, 0, false};
    uint8_t payload[32];
    writer_t p = {payload, sizeof(payload), 0, false};
    if (ssh->state == PIM_SSH_FAILED || ssh->state == PIM_SSH_CLOSED) return 0;
    if (ssh->channel_open) {
        put_byte(&p, MSG_CHANNEL_CLOSE);
        put_u32(&p, ssh->remote_channel);
        send_packet(ssh, payload, p.used, &w);
    }
    ssh->state = PIM_SSH_CLOSED;
    return w.used;
}

pim_ssh_state_t pim_ssh_state(const pim_ssh_t *ssh) { return (pim_ssh_state_t)ssh->state; }
const char *pim_ssh_error(const pim_ssh_t *ssh) { return ssh->error; }
const char *pim_ssh_fingerprint(const pim_ssh_t *ssh) { return ssh->fingerprint; }
const char *pim_ssh_server_version(const pim_ssh_t *ssh) { return ssh->server_version; }

/* ------------------------------------------------------- private key files */

/* The OpenSSH format, which is its own container rather than PEM inside:

     "openssh-key-v1\0"
     string cipher, string kdf, string kdf options
     uint32 key count
     string public key blob
     string the private section, which holds two check words, the key type,
            the public key, the 64-byte secret, and a comment

   Only an unencrypted ed25519 key is accepted. Anything else is reported
   rather than half-parsed: a wrong key is a confusing failure later. */
bool pim_ssh_parse_private_key(const char *text, uint8_t secret[64], const char **why) {
    static const char opening[] = "-----BEGIN OPENSSH PRIVATE KEY-----";
    static uint8_t blob[2048];
    static char base64[3072];
    const char *start, *end;
    size_t base64_length = 0, blob_length;
    const char *reason = "not an OpenSSH private key";

    if (why != NULL) *why = reason;
    if (text == NULL || strncmp(text, opening, sizeof(opening) - 1) != 0) return false;
    start = strchr(text, '\n');
    if (start == NULL) return false;
    start++;
    end = strstr(start, "-----END");
    if (end == NULL) return false;
    for (const char *at = start; at < end; at++) {
        if (*at == '\n' || *at == '\r' || *at == ' ') continue;
        if (base64_length + 1 >= sizeof(base64)) { reason = "key file too large"; goto fail; }
        base64[base64_length++] = *at;
    }
    base64[base64_length] = '\0';
    blob_length = pim_base64_decode(base64, blob, sizeof(blob));
    if (blob_length < 16) { reason = "key body is not base64"; goto fail; }
    if (memcmp(blob, "openssh-key-v1", 15) != 0) { reason = "unknown key container"; goto fail; }

    {
        reader_t r = {blob, blob_length, 15, false};
        size_t cipher_length = 0, kdf_length = 0, options_length = 0, public_length = 0, section_length = 0;
        const uint8_t *cipher = get_string(&r, &cipher_length);
        const uint8_t *kdf = get_string(&r, &kdf_length);
        const uint8_t *section;
        (void)get_string(&r, &options_length);
        (void)kdf;
        if (r.bad || cipher == NULL) { reason = "malformed key header"; goto fail; }
        if (cipher_length != 4 || memcmp(cipher, "none", 4) != 0) {
            reason = "key has a passphrase; decrypt it first";
            goto fail;
        }
        if (get_u32(&r) != 1) { reason = "key file holds more than one key"; goto fail; }
        (void)get_string(&r, &public_length);
        section = get_string(&r, &section_length);
        if (r.bad || section == NULL) { reason = "malformed key body"; goto fail; }
        {
            reader_t s = {section, section_length, 0, false};
            size_t type_length = 0, public_half = 0, private_half = 0;
            const uint8_t *type, *public_key, *private_key;
            uint32_t check_one = get_u32(&s), check_two = get_u32(&s);
            if (check_one != check_two) { reason = "key check words differ"; goto fail; }
            type = get_string(&s, &type_length);
            public_key = get_string(&s, &public_half);
            private_key = get_string(&s, &private_half);
            if (s.bad || type == NULL || public_key == NULL || private_key == NULL) {
                reason = "malformed key body";
                goto fail;
            }
            if (type_length != strlen(HOSTKEY_NAME) || memcmp(type, HOSTKEY_NAME, type_length) != 0) {
                reason = "only ed25519 keys are supported";
                goto fail;
            }
            if (public_half != 32 || private_half != 64) { reason = "ed25519 key is the wrong size"; goto fail; }
            if (memcmp(private_key + 32, public_key, 32) != 0) { reason = "key halves do not match"; goto fail; }
            memcpy(secret, private_key, 64);
            if (!pim_ed25519_key_consistent(secret)) { reason = "key does not verify against itself"; goto fail; }
            if (why != NULL) *why = NULL;
            return true;
        }
    }
fail:
    if (why != NULL) *why = reason;
    return false;
}
