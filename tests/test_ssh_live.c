/* Drives the SSH client against a real OpenSSH server over a real socket.

   This is the test that decides whether any of it works: the exchange hash, the
   key derivation, the chacha20-poly1305 framing and the nonce convention are
   either exactly right or the server hangs up, and no amount of reading the
   specification settles that as well as an actual handshake does.

   It needs a server to talk to, so it skips rather than fails when there is
   none. tools/ssh-live-test.sh starts a throwaway sshd and sets these:

     PIM_SSH_TEST_HOST         the address to dial, default 127.0.0.1
     PIM_SSH_TEST_PORT         the port it listens on
     PIM_SSH_TEST_USER         the user to log in as
     PIM_SSH_TEST_KEY          an unencrypted ed25519 private key file
     PIM_SSH_TEST_FINGERPRINT  what ssh-keygen says the host key is  */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "pim/ssh.h"

static int failures;
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "%s:%d %s\n", __FILE__, __LINE__, #value); failures++; } } while (0)

static pim_ssh_t ssh;

static char *read_file(const char *path) {
    static char text[8192];
    FILE *file = fopen(path, "rb");
    size_t got;
    if (file == NULL) return NULL;
    got = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[got] = '\0';
    return got > 0 ? text : NULL;
}

static int connect_to(const char *host, int port) {
    struct sockaddr_in address;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    if (fd < 0) return -1;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    if (host == NULL || inet_pton(AF_INET, host, &address.sin_addr) != 1) {
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) { close(fd); return -1; }
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    {   /* so a mistake stalls the test rather than hanging it */
        struct timeval timeout = {5, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    }
    return fd;
}

static bool write_all(int fd, const uint8_t *data, size_t length) {
    while (length > 0) {
        ssize_t sent = send(fd, data, length, 0);
        if (sent <= 0) return false;
        data += sent;
        length -= (size_t)sent;
    }
    return true;
}

/* Pumps the session until it reaches `wanted`, or fails, or runs out of
   patience. Anything the shell prints is collected into `screen`. */
static bool pump_until(int fd, pim_ssh_state_t wanted, char *screen, size_t screen_size,
                       size_t *screen_length) {
    for (int round = 0; round < 200; round++) {
        uint8_t chunk[4096], out[PIM_SSH_PACKET_MAX], text[PIM_SSH_PACKET_MAX];
        size_t out_length = 0, text_length;
        ssize_t got;
        if (pim_ssh_state(&ssh) == wanted) return true;
        if (pim_ssh_state(&ssh) == PIM_SSH_FAILED) return false;
        got = recv(fd, chunk, sizeof(chunk), 0);
        if (got <= 0) return pim_ssh_state(&ssh) == wanted;
        text_length = pim_ssh_feed(&ssh, chunk, (size_t)got, out, sizeof(out), &out_length,
                                   text, sizeof(text));
        if (out_length > 0 && !write_all(fd, out, out_length)) return false;
        if (text_length > 0 && screen != NULL) {
            size_t room = screen_size - 1 - *screen_length;
            size_t copy = text_length < room ? text_length : room;
            memcpy(screen + *screen_length, text, copy);
            *screen_length += copy;
            screen[*screen_length] = '\0';
        }
    }
    return pim_ssh_state(&ssh) == wanted;
}

int main(void) {
    const char *port_text = getenv("PIM_SSH_TEST_PORT");
    const char *user = getenv("PIM_SSH_TEST_USER");
    const char *key_path = getenv("PIM_SSH_TEST_KEY");
    const char *expected_fingerprint = getenv("PIM_SSH_TEST_FINGERPRINT");
    pim_ssh_config_t config;
    char screen[8192] = {0};
    size_t screen_length = 0;
    uint8_t out[PIM_SSH_PACKET_MAX];
    size_t out_length;
    int fd;

    if (port_text == NULL || user == NULL || key_path == NULL) {
        puts("ssh live test skipped (no server configured; see tools/ssh-live-test.sh)");
        return 0;
    }

    memset(&config, 0, sizeof(config));
    snprintf(config.user, sizeof(config.user), "%s", user);
    config.cols = 40;
    config.rows = 38;
    {
        const char *why = NULL;
        char *text = read_file(key_path);
        CHECK(text != NULL);
        if (text == NULL) return 1;
        CHECK(pim_ssh_parse_private_key(text, config.secret, &why));
        if (why != NULL) { fprintf(stderr, "key: %s\n", why); return 1; }
        config.have_secret = true;
    }

    fd = connect_to(getenv("PIM_SSH_TEST_HOST"), atoi(port_text));
    if (fd < 0) {
        puts("ssh live test skipped (nothing listening)");
        return 0;
    }

    /* ---- identification and key exchange ---- */
    out_length = pim_ssh_start(&ssh, &config, out, sizeof(out));
    CHECK(out_length > 0 && write_all(fd, out, out_length));
    CHECK(pump_until(fd, PIM_SSH_HOSTKEY_UNKNOWN, NULL, 0, NULL));
    if (pim_ssh_state(&ssh) != PIM_SSH_HOSTKEY_UNKNOWN) {
        fprintf(stderr, "key exchange failed: %s (server %s)\n",
                pim_ssh_error(&ssh), pim_ssh_server_version(&ssh));
        close(fd);
        return 1;
    }
    printf("  server      %s\n", pim_ssh_server_version(&ssh));
    printf("  host key    %s\n", pim_ssh_fingerprint(&ssh));
    /* The fingerprint must match what ssh-keygen makes of the same key: that is
       an independent check on the host key parsing and on SHA-256. */
    if (expected_fingerprint != NULL) {
        CHECK(strcmp(pim_ssh_fingerprint(&ssh), expected_fingerprint) == 0);
    }

    /* ---- everything past here is encrypted ---- */
    pim_ssh_accept_host_key(&ssh, out, sizeof(out), &out_length);
    CHECK(out_length > 0 && write_all(fd, out, out_length));

    /* Reaching a running shell means the whole transport is right: the service
       request, the signed authentication, the channel and the pty all travelled
       under chacha20-poly1305 and came back verified. */
    CHECK(pump_until(fd, PIM_SSH_READY, screen, sizeof(screen), &screen_length));
    if (pim_ssh_state(&ssh) != PIM_SSH_READY) {
        fprintf(stderr, "did not reach a shell: %s\n", pim_ssh_error(&ssh));
        close(fd);
        return 1;
    }
    puts("  authenticated with the key and opened a shell");

    /* ---- a command, and its output ---- */
    out_length = pim_ssh_send(&ssh, (const uint8_t *)"echo PICODECK_OK\n", 17, out, sizeof(out));
    CHECK(out_length > 0 && write_all(fd, out, out_length));
    for (int round = 0; round < 60 && strstr(screen, "PICODECK_OK") == NULL; round++) {
        pump_until(fd, PIM_SSH_CLOSED, screen, sizeof(screen), &screen_length);
    }
    CHECK(strstr(screen, "PICODECK_OK") != NULL);
    if (strstr(screen, "PICODECK_OK") == NULL) fprintf(stderr, "shell said: %.200s\n", screen);
    else puts("  ran a command and read its output back");

    /* a window change is accepted without upsetting the session */
    out_length = pim_ssh_resize(&ssh, 40, 20, out, sizeof(out));
    CHECK(out_length > 0 && write_all(fd, out, out_length));

    out_length = pim_ssh_close(&ssh, out, sizeof(out));
    if (out_length > 0) write_all(fd, out, out_length);
    close(fd);

    if (failures == 0) puts("ssh live test passed");
    return failures == 0 ? 0 : 1;
}
