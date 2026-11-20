#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pim/hosts.h"
#include "pim/sshkey.h"

static int failures;
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "%s:%d %s\n", __FILE__, __LINE__, #value); failures++; } } while (0)

#define KEY_DIR "test_ssh_dir"
#define HOSTS_FILE "test_hosts.cfg"

/* A real ed25519 key pair's public half, so the fingerprint below is the one
   ssh-keygen -lf prints for it. */
static const char ed25519_line[] =
    "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGb9ECWmEzf8xZ/MGMWyAkWyRcNnZaPtM3SbQ8Nn8PFC picocalc\n";

static void test_host_records(void) {
    pim_hosts_t list;
    pim_host_t host;
    const char *address = NULL;
    uint16_t port = 0;
    char line[160];

    pim_hosts_init(&list);
    CHECK(list.count == 0);

    pim_host_init(&host, PIM_HOST_TELNET);
    CHECK(host.port == 23 && host.kind == PIM_HOST_TELNET);
    pim_host_init(&host, PIM_HOST_SSH);
    CHECK(host.port == 22);

    /* an alias needs a name and somewhere to go */
    CHECK(!pim_host_valid(&host));
    snprintf(host.alias, sizeof(host.alias), "nas");
    CHECK(!pim_host_valid(&host));
    snprintf(host.address, sizeof(host.address), "nas.tail1a2b.ts.net");
    snprintf(host.user, sizeof(host.user), "user");
    CHECK(pim_host_valid(&host));

    /* with no relay the address is dialled directly */
    pim_host_dial(&host, &address, &port);
    CHECK(strcmp(address, "nas.tail1a2b.ts.net") == 0 && port == 22);

    /* a relay is what the device can actually reach, and needs its own port */
    snprintf(host.relay, sizeof(host.relay), "192.168.1.9");
    CHECK(!pim_host_valid(&host));
    host.relay_port = 2200;
    CHECK(pim_host_valid(&host));
    pim_host_dial(&host, &address, &port);
    CHECK(strcmp(address, "192.168.1.9") == 0 && port == 2200);

    pim_host_summary(&host, line, sizeof(line));
    CHECK(strcmp(line, "nas  ssh user@nas.tail1a2b.ts.net:22 via 192.168.1.9:2200") == 0);

    /* a tab or a control character would break the config file */
    snprintf(host.alias, sizeof(host.alias), "bad\tname");
    CHECK(!pim_host_valid(&host));
    snprintf(host.alias, sizeof(host.alias), "nas");
    CHECK(pim_host_valid(&host));

    CHECK(pim_hosts_put(&list, &host) && list.count == 1);
    /* the same alias replaces rather than duplicates, whatever its case */
    snprintf(host.alias, sizeof(host.alias), "NAS");
    host.port = 2022;
    CHECK(pim_hosts_put(&list, &host) && list.count == 1);
    CHECK(list.hosts[0].port == 2022);
    CHECK(pim_hosts_find(&list, "nas") != NULL && pim_hosts_find(&list, "other") == NULL);
}

static void test_host_list_limits(void) {
    pim_hosts_t list;
    pim_host_t host;
    unsigned i;
    pim_hosts_init(&list);
    for (i = 0; i < PIM_HOST_MAX + 3u; i++) {
        pim_host_init(&host, PIM_HOST_TELNET);
        snprintf(host.alias, sizeof(host.alias), "bbs%u", i);
        snprintf(host.address, sizeof(host.address), "bbs%u.example.org", i);
        if (i < PIM_HOST_MAX) CHECK(pim_hosts_put(&list, &host));
        else CHECK(!pim_hosts_put(&list, &host));   /* full, and it says so */
    }
    CHECK(list.count == PIM_HOST_MAX);
    CHECK(pim_hosts_remove(&list, 0) && list.count == PIM_HOST_MAX - 1);
    CHECK(strcmp(list.hosts[0].alias, "bbs1") == 0);
    CHECK(!pim_hosts_remove(&list, 99));
}

static void test_host_files(void) {
    pim_hosts_t list, loaded;
    pim_host_t host;
    FILE *file;

    remove(HOSTS_FILE);
    CHECK(!pim_hosts_load(HOSTS_FILE, &loaded));   /* nothing saved yet */
    CHECK(loaded.count == 0);

    pim_hosts_init(&list);
    pim_host_init(&host, PIM_HOST_SSH);
    snprintf(host.alias, sizeof(host.alias), "nas");
    snprintf(host.address, sizeof(host.address), "nas.tail1a2b.ts.net");
    snprintf(host.user, sizeof(host.user), "user");
    snprintf(host.key, sizeof(host.key), "picocalc");
    snprintf(host.relay, sizeof(host.relay), "192.168.1.9");
    host.relay_port = 2200;
    CHECK(pim_hosts_put(&list, &host));
    pim_host_init(&host, PIM_HOST_TELNET);
    snprintf(host.alias, sizeof(host.alias), "bbs");
    snprintf(host.address, sizeof(host.address), "bbs.example.org");
    host.port = 2323;
    CHECK(pim_hosts_put(&list, &host));
    CHECK(pim_hosts_save(HOSTS_FILE, &list));

    CHECK(pim_hosts_load(HOSTS_FILE, &loaded));
    CHECK(loaded.count == 2);
    CHECK(strcmp(loaded.hosts[0].alias, "nas") == 0);
    CHECK(loaded.hosts[0].kind == PIM_HOST_SSH);
    CHECK(strcmp(loaded.hosts[0].user, "user") == 0);
    CHECK(strcmp(loaded.hosts[0].key, "picocalc") == 0);
    CHECK(strcmp(loaded.hosts[0].relay, "192.168.1.9") == 0);
    CHECK(loaded.hosts[0].relay_port == 2200);
    CHECK(loaded.hosts[1].kind == PIM_HOST_TELNET && loaded.hosts[1].port == 2323);
    CHECK(loaded.hosts[1].relay[0] == '\0' && loaded.hosts[1].relay_port == 0);

    /* junk lines, short records and a bad port are skipped, not fatal */
    file = fopen(HOSTS_FILE, "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        fprintf(file, "# a comment\n");
        fprintf(file, "host=short\ttelnet\n");
        fprintf(file, "host=zero\ttelnet\tzero.example\t0\t\t\t\t0\n");
        fprintf(file, "host=huge\ttelnet\thuge.example\t99999\t\t\t\t0\n");
        fprintf(file, "host=good\ttelnet\tgood.example\t23\t\t\t\t0\n");
        fclose(file);
    }
    CHECK(pim_hosts_load(HOSTS_FILE, &loaded));
    CHECK(loaded.count == 1 && strcmp(loaded.hosts[0].alias, "good") == 0);
    remove(HOSTS_FILE);
}

static void test_digest_and_base64(void) {
    uint8_t digest[32];
    char text[64];
    uint8_t bytes[16];
    size_t n;

    /* the NIST "abc" vector */
    pim_sha256((const uint8_t *)"abc", 3, digest);
    CHECK(digest[0] == 0xba && digest[1] == 0x78 && digest[2] == 0x16 && digest[3] == 0xbf);
    CHECK(digest[30] == 0x15 && digest[31] == 0xad);

    /* the empty string */
    pim_sha256((const uint8_t *)"", 0, digest);
    CHECK(digest[0] == 0xe3 && digest[1] == 0xb0 && digest[31] == 0x55);

    /* a message that spills into a second padding block */
    {
        uint8_t long_input[120];
        memset(long_input, 'a', sizeof(long_input));
        pim_sha256(long_input, 56, digest);
        CHECK(digest[0] == 0xb3 && digest[1] == 0x54);
        pim_sha256(long_input, 64, digest);
        CHECK(digest[0] == 0xff && digest[1] == 0xe0);
    }

    n = pim_base64_encode((const uint8_t *)"abc", 3, text, sizeof(text), true);
    CHECK(n == 4 && strcmp(text, "YWJj") == 0);
    n = pim_base64_encode((const uint8_t *)"ab", 2, text, sizeof(text), true);
    CHECK(n == 4 && strcmp(text, "YWI=") == 0);
    n = pim_base64_encode((const uint8_t *)"ab", 2, text, sizeof(text), false);
    CHECK(n == 3 && strcmp(text, "YWI") == 0);
    CHECK(pim_base64_encode((const uint8_t *)"abc", 3, text, 4, true) == 0);  /* no room */

    n = pim_base64_decode("YWJj", bytes, sizeof(bytes));
    CHECK(n == 3 && memcmp(bytes, "abc", 3) == 0);
    n = pim_base64_decode("YWI=", bytes, sizeof(bytes));
    CHECK(n == 2 && memcmp(bytes, "ab", 2) == 0);
    CHECK(pim_base64_decode("not base64!", bytes, sizeof(bytes)) == 0);
    CHECK(pim_base64_decode("YWJjYWJjYWJj", bytes, 2) == 0);                 /* no room */
}

static void test_pubkey_parse(void) {
    pim_ssh_key_t key;

    CHECK(pim_ssh_pubkey_parse(ed25519_line, &key));
    CHECK(strcmp(key.type, "ssh-ed25519") == 0);
    CHECK(strcmp(key.comment, "picocalc") == 0);
    CHECK(key.bits == 256);
    CHECK(strncmp(key.fingerprint, "SHA256:", 7) == 0);
    CHECK(strlen(key.fingerprint) == 7 + 43);
    /* the fingerprint is over the blob, so it must not change between runs */
    {
        pim_ssh_key_t again;
        CHECK(pim_ssh_pubkey_parse(ed25519_line, &again));
        CHECK(strcmp(again.fingerprint, key.fingerprint) == 0);
    }

    /* a key with no comment is still a key */
    CHECK(pim_ssh_pubkey_parse("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGb9ECWmEzf8xZ/MGMWyAkWyRcNnZaPtM3SbQ8Nn8PFC", &key));
    CHECK(key.comment[0] == '\0');

    /* rubbish, unknown types, and a blob that disagrees with its label */
    CHECK(!pim_ssh_pubkey_parse("hello there", &key));
    CHECK(!pim_ssh_pubkey_parse("ssh-ed25519", &key));
    CHECK(!pim_ssh_pubkey_parse("ssh-magic AAAAC3NzaC1lZDI1NTE5AAAAIGb9ECWmEzf8xZ/MGMWyAkWyRcNnZaPtM3SbQ8Nn8PFC", &key));
    CHECK(!pim_ssh_pubkey_parse("ssh-rsa AAAAC3NzaC1lZDI1NTE5AAAAIGb9ECWmEzf8xZ/MGMWyAkWyRcNnZaPtM3SbQ8Nn8PFC", &key));
    CHECK(!pim_ssh_pubkey_parse("ssh-ed25519 not-base-64!!", &key));
}

static void test_private_keys(void) {
    bool encrypted = true;
    CHECK(pim_ssh_private_looks_valid("-----BEGIN OPENSSH PRIVATE KEY-----\nb3BlbnNzaC1rZXktdjEAAAAABG5vbmUAAAAEbm9uZQAAAAAAAAAB\n-----END OPENSSH PRIVATE KEY-----\n", &encrypted));
    CHECK(!encrypted);
    CHECK(pim_ssh_private_looks_valid("-----BEGIN OPENSSH PRIVATE KEY-----\nb3BlbnNzaC1rZXktdjEAAAAACmFlczI1Ni1jdHIAAAAGYmNyeXB0AAAAAAAAAAE=\n-----END OPENSSH PRIVATE KEY-----\n", &encrypted));
    CHECK(encrypted);
    CHECK(pim_ssh_private_looks_valid("-----BEGIN RSA PRIVATE KEY-----\nProc-Type: 4,ENCRYPTED\n-----END RSA PRIVATE KEY-----\n", &encrypted));
    CHECK(encrypted);
    CHECK(!pim_ssh_private_looks_valid(ed25519_line, NULL));
    CHECK(!pim_ssh_private_looks_valid("just a note\n", NULL));
}

static void test_key_names(void) {
    CHECK(pim_ssh_name_ok("picocalc"));
    CHECK(pim_ssh_name_ok("id_ed25519"));
    CHECK(pim_ssh_name_ok("key-2"));
    CHECK(!pim_ssh_name_ok(""));
    CHECK(!pim_ssh_name_ok("Upper"));
    CHECK(!pim_ssh_name_ok("with space"));
    CHECK(!pim_ssh_name_ok("../escape"));
    CHECK(!pim_ssh_name_ok("way_too_long_a_name_for_a_file_here"));
}

static void clean_key_dir(void) {
    static const char *const names[] = {
        KEY_DIR "/picocalc.pub", KEY_DIR "/picocalc", KEY_DIR "/work.pub", KEY_DIR "/work",
        KEY_DIR "/junk.pub", KEY_DIR "/notes.txt"
    };
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) remove(names[i]);
    rmdir(KEY_DIR);
}

static void test_key_store(void) {
    pim_ssh_keys_t keys;
    const pim_ssh_key_t *found;
    FILE *file;

    clean_key_dir();
    CHECK(pim_ssh_keys_scan(KEY_DIR, &keys) == 0);   /* no directory at all */
    CHECK(mkdir(KEY_DIR, 0777) == 0);
    CHECK(pim_ssh_keys_scan(KEY_DIR, &keys) == 0);

    /* a name that is not a file name, and text that is not a key, are refused */
    CHECK(!pim_ssh_key_import(KEY_DIR, "../escape", ed25519_line, false));
    CHECK(!pim_ssh_key_import(KEY_DIR, "junk", "hello there\n", false));
    CHECK(!pim_ssh_key_import(KEY_DIR, "junk", ed25519_line, true));  /* not a private key */

    CHECK(pim_ssh_key_import(KEY_DIR, "picocalc", ed25519_line, false));
    CHECK(pim_ssh_keys_scan(KEY_DIR, &keys) == 1);
    CHECK(strcmp(keys.keys[0].name, "picocalc") == 0);
    CHECK(strcmp(keys.keys[0].type, "ssh-ed25519") == 0);
    CHECK(!keys.keys[0].has_private);                /* public half only so far */

    CHECK(pim_ssh_key_import(KEY_DIR, "picocalc",
                             "-----BEGIN OPENSSH PRIVATE KEY-----\nb3BlbnNzaC1rZXktdjEAAAAABG5vbmUAAAAEbm9uZQAAAAAAAAAB\n-----END OPENSSH PRIVATE KEY-----\n",
                             true));
    CHECK(pim_ssh_keys_scan(KEY_DIR, &keys) == 1);
    CHECK(keys.keys[0].has_private);

    /* files that are not keys are ignored by the scan */
    file = fopen(KEY_DIR "/notes.txt", "wb");
    if (file != NULL) { fprintf(file, "shopping list\n"); fclose(file); }
    file = fopen(KEY_DIR "/junk.pub", "wb");
    if (file != NULL) { fprintf(file, "this is not a key\n"); fclose(file); }
    CHECK(pim_ssh_keys_scan(KEY_DIR, &keys) == 1);

    CHECK(pim_ssh_key_import(KEY_DIR, "work", ed25519_line, false));
    CHECK(pim_ssh_keys_scan(KEY_DIR, &keys) == 2);
    found = pim_ssh_keys_find(&keys, "work");
    CHECK(found != NULL && strcmp(found->type, "ssh-ed25519") == 0);
    CHECK(pim_ssh_keys_find(&keys, "missing") == NULL);

    CHECK(pim_ssh_key_delete(KEY_DIR, "picocalc"));
    CHECK(!pim_ssh_key_delete(KEY_DIR, "picocalc"));  /* already gone */
    CHECK(pim_ssh_keys_scan(KEY_DIR, &keys) == 1);
    clean_key_dir();
}

int main(void) {
    test_host_records();
    test_host_list_limits();
    test_host_files();
    test_digest_and_base64();
    test_pubkey_parse();
    test_private_keys();
    test_key_names();
    test_key_store();
    if (failures == 0) puts("hosts and ssh key tests passed");
    return failures == 0 ? 0 : 1;
}
