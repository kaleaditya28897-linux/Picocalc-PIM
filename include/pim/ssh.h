#ifndef PIM_SSH_H
#define PIM_SSH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* An SSH-2 client, free of hardware and transport, in the same shape as the
   terminal and the telnet filter: bytes from the socket go in, bytes for the
   socket and bytes for the screen come out. The caller owns the socket.

   One algorithm of each kind, all of them mandatory-to-implement in practice
   for any OpenSSH since 6.5:

     kex       curve25519-sha256 (and @libssh.org, the same thing)
     host key  ssh-ed25519
     cipher    chacha20-poly1305@openssh.com
     mac       implicit in the cipher
     compress  none

   Refusing to negotiate anything else keeps the state machine small enough to
   reason about, and means there is no weak option to be talked into.

   A pim_ssh_t is several kilobytes: keep one in static storage. */

#define PIM_SSH_PACKET_MAX 4096     /* biggest packet we will send or accept */
#define PIM_SSH_KEXINIT_MAX 2048    /* OpenSSH's own list runs to about 1 KB */
#define PIM_SSH_HOSTKEY_MAX 512
#define PIM_SSH_VERSION_MAX 256
#define PIM_SSH_FINGERPRINT_MAX 50  /* "SHA256:" plus 43 base64 characters */
#define PIM_SSH_USER_MAX 32
#define PIM_SSH_ERROR_MAX 96

typedef enum {
    PIM_SSH_CLOSED = 0,
    PIM_SSH_VERSION_SENT,       /* waiting for the server's identification */
    PIM_SSH_KEX,                /* exchanging algorithms and keys */
    PIM_SSH_HOSTKEY_UNKNOWN,    /* stopped: the caller must accept the host key */
    PIM_SSH_AUTH,               /* authenticating */
    PIM_SSH_OPENING,            /* opening the session channel and asking for a pty */
    PIM_SSH_READY,              /* a shell is running; data flows */
    PIM_SSH_FAILED
} pim_ssh_state_t;

typedef struct {
    char user[PIM_SSH_USER_MAX + 1];
    /* The 64-byte OpenSSH secret: 32-byte seed then public key. Zero when only
       a password is on offer. */
    uint8_t secret[64];
    bool have_secret;
    char password[64];
    bool have_password;
    uint16_t cols, rows;
    /* The host key this host is already known by, as a fingerprint. Empty means
       nothing is pinned, and the session stops at PIM_SSH_HOSTKEY_UNKNOWN so the
       caller can show it and ask. */
    char known_fingerprint[PIM_SSH_FINGERPRINT_MAX + 1];
} pim_ssh_config_t;

typedef struct {
    uint8_t state;
    char error[PIM_SSH_ERROR_MAX + 1];

    pim_ssh_config_t config;

    /* identification strings, without their CR LF, as the hash needs them */
    char client_version[PIM_SSH_VERSION_MAX + 1];
    char server_version[PIM_SSH_VERSION_MAX + 1];
    size_t server_version_length;
    bool server_version_done;

    /* both KEXINIT payloads, kept for the exchange hash */
    uint8_t client_kexinit[512];
    uint16_t client_kexinit_length;
    uint8_t server_kexinit[PIM_SSH_KEXINIT_MAX];
    uint16_t server_kexinit_length;

    uint8_t host_key[PIM_SSH_HOSTKEY_MAX];      /* the whole K_S blob */
    uint16_t host_key_length;
    uint8_t host_key_ed25519[32];               /* the key inside it */
    char fingerprint[PIM_SSH_FINGERPRINT_MAX + 1];

    uint8_t ephemeral_secret[32], ephemeral_public[32];
    uint8_t session_id[32];
    uint8_t exchange_hash[32];
    bool have_session_id;

    uint8_t key_c2s[64], key_s2c[64];
    bool encrypt_out, decrypt_in;
    uint32_t sequence_out, sequence_in;

    /* incoming bytes, assembled until a whole packet is present */
    uint8_t in[PIM_SSH_PACKET_MAX + 64];
    size_t in_length;

    uint32_t channel;               /* our channel number, always 0 */
    uint32_t remote_channel;
    uint32_t remote_window;         /* how much the server will accept */
    uint32_t local_window;          /* how much we have told the server we accept */
    bool channel_open;
    bool pty_requested;
    bool shell_requested;
    bool auth_sent;
    bool auth_password_tried;
    /* Room to build the exchange hash and the signed authentication request.
       These were locals, and four kilobytes of them on the same stack as LVGL,
       FatFS and the curve25519 and ed25519 maths ran that stack off the end
       while a host key was being accepted. The session already lives on the
       heap, so they live here instead - the same memory, somewhere it does not
       have to share with everything else the screen is doing. */
    uint8_t hash_scratch[2048];
    uint8_t auth_scratch[1024];
    uint8_t sign_scratch[1024];
} pim_ssh_t;

/* Starts a session. Writes the client identification string into `out`, which
   the caller sends as soon as the socket is up. */
size_t pim_ssh_start(pim_ssh_t *ssh, const pim_ssh_config_t *config,
                     uint8_t *out, size_t out_size);

/* Consumes bytes from the server. Bytes for the socket go to `out`, with the
   count in *out_length; bytes for the terminal go to `screen`, and the return
   value is how many. Both buffers should be at least PIM_SSH_PACKET_MAX. */
size_t pim_ssh_feed(pim_ssh_t *ssh, const uint8_t *data, size_t length,
                    uint8_t *out, size_t out_size, size_t *out_length,
                    uint8_t *screen, size_t screen_size);

/* Sends typed bytes to the shell. Returns the number of bytes for the socket. */
size_t pim_ssh_send(pim_ssh_t *ssh, const uint8_t *data, size_t length,
                    uint8_t *out, size_t out_size);

/* Tells the server the window changed, so full-screen programs redraw. */
size_t pim_ssh_resize(pim_ssh_t *ssh, unsigned cols, unsigned rows,
                      uint8_t *out, size_t out_size);

/* Closes the channel politely. */
size_t pim_ssh_close(pim_ssh_t *ssh, uint8_t *out, size_t out_size);

/* Accepts the host key the session stopped on, and carries on. The caller is
   expected to have shown pim_ssh_fingerprint() to the user first, and to store
   it so the next connection does not ask again. */
size_t pim_ssh_accept_host_key(pim_ssh_t *ssh, uint8_t *out, size_t out_size,
                               size_t *out_length);

pim_ssh_state_t pim_ssh_state(const pim_ssh_t *ssh);
const char *pim_ssh_error(const pim_ssh_t *ssh);
/* "SHA256:..." for the server's host key, the same string OpenSSH prints. */
const char *pim_ssh_fingerprint(const pim_ssh_t *ssh);
const char *pim_ssh_server_version(const pim_ssh_t *ssh);

/* Reads an OpenSSH private key file into the 64-byte secret. Only unencrypted
   ed25519 keys are handled; anything else says so rather than guessing. */
bool pim_ssh_parse_private_key(const char *text, uint8_t secret[64], const char **why);

#endif
