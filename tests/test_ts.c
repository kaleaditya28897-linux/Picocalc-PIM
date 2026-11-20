/* The Tailscale settings file, and what counts as an auth key.

   The client itself needs a radio and a tailnet, so it is not tested here; this
   covers the part that decides whether the device has something usable to
   register with, and the part that keeps a secret off the screen. */

#include "pim/tailscale.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;

#define CHECK(expr) do { \
    checks++; \
    if (!(expr)) { printf("FAIL line %d: %s\n", __LINE__, #expr); failures++; } \
} while (0)

/* Not a real key: the shape is all that is being tested. */
#define SAMPLE_KEY "tskey-auth-kExampleOnly1CNTRL-abcdefghijklmnopqrstuvwxyz01"

static const char *temp_path(void) {
    static char path[256];
    const char *dir = getenv("TMPDIR");
    snprintf(path, sizeof(path), "%s/pim_ts_test.cfg", dir != NULL ? dir : "/tmp");
    return path;
}

static void write_file(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) { printf("cannot write %s\n", path); exit(1); }
    fputs(text, file);
    fclose(file);
}

static void test_authkey_shape(void) {
    char summary[64];

    CHECK(pim_ts_authkey_ok(SAMPLE_KEY));
    CHECK(!pim_ts_authkey_ok(NULL));
    CHECK(!pim_ts_authkey_ok(""));
    CHECK(!pim_ts_authkey_ok("tskey-auth-short"));            /* too short to be one */
    CHECK(!pim_ts_authkey_ok("nottskey-auth-kAAAAAAAAAAAAAAAAAAAAAAAA"));
    /* A key pasted with a trailing space or a quote is a common slip, and one
       the control plane would reject far away from the cause. */
    CHECK(!pim_ts_authkey_ok(SAMPLE_KEY " "));
    CHECK(!pim_ts_authkey_ok("\"" SAMPLE_KEY "\""));
    CHECK(!pim_ts_authkey_ok("tskey-auth-k with spaces in the middle 00000"));

    /* The summary identifies a key without putting the secret on screen. */
    pim_ts_authkey_summary(SAMPLE_KEY, summary, sizeof(summary));
    CHECK(strstr(summary, "tskey-auth-kExam") != NULL);
    CHECK(strstr(summary, "abcdefghijklmnop") == NULL);   /* the secret part stays off */
    CHECK(strstr(summary, "characters") != NULL);
    pim_ts_authkey_summary("", summary, sizeof(summary));
    CHECK(strcmp(summary, "no key on the card") == 0);
}

static void test_defaults(void) {
    pim_ts_config_t config;
    pim_ts_config_init(&config);
    CHECK(strcmp(config.name, "picocalc") == 0);
    CHECK(config.authkey[0] == '\0');
    CHECK(config.derp_region == 0);
}

static void test_load(void) {
    pim_ts_config_t config;
    const char *path = temp_path();

    /* The ordinary case. */
    write_file(path,
        "# a comment\n"
        "\n"
        "authkey=" SAMPLE_KEY "\n"
        "name=picodeck\n"
        "derp=6\n");
    CHECK(pim_ts_config_load(path, &config));
    CHECK(strcmp(config.authkey, SAMPLE_KEY) == 0);
    CHECK(strcmp(config.name, "picodeck") == 0);
    CHECK(config.derp_region == 6);

    /* Comments after a value, and trailing spaces, as the README shows them:
       the key must survive them, not be dropped as malformed. */
    write_file(path,
        "authkey=" SAMPLE_KEY "   # optional\n"
        "name=picocalc            # the name on the tailnet\n"
        "derp=6 \t\n");
    CHECK(pim_ts_config_load(path, &config));
    CHECK(strcmp(config.authkey, SAMPLE_KEY) == 0);
    CHECK(strcmp(config.name, "picocalc") == 0);
    CHECK(config.derp_region == 6);

    /* Carriage returns: the file is likely written on another machine. */
    write_file(path, "authkey=" SAMPLE_KEY "\r\nname=crlf\r\nderp=9\r\n");
    CHECK(pim_ts_config_load(path, &config));
    CHECK(strcmp(config.name, "crlf") == 0);
    CHECK(config.derp_region == 9);
    CHECK(strcmp(config.authkey, SAMPLE_KEY) == 0);

    /* No key: the file parses, but there is nothing to register with, so this
       is not a usable configuration and the caller is told so. */
    write_file(path, "name=keyless\nderp=6\n");
    CHECK(!pim_ts_config_load(path, &config));
    CHECK(strcmp(config.name, "keyless") == 0);   /* the rest is still read */

    /* A mangled key is dropped rather than carried forward to fail later. */
    write_file(path, "authkey=tskey-oops\nname=bad\n");
    CHECK(!pim_ts_config_load(path, &config));
    CHECK(config.authkey[0] == '\0');

    /* A missing file leaves the defaults in place. */
    remove(path);
    CHECK(!pim_ts_config_load(path, &config));
    CHECK(strcmp(config.name, "picocalc") == 0);

    /* Junk does not get mistaken for settings, and nothing overruns. */
    write_file(path, "authkey\nname\n=\nderp=notanumber\nderp=99999999999\n");
    CHECK(!pim_ts_config_load(path, &config));
    CHECK(config.derp_region == 0);

    /* A card from an older build carries two settings that were dropped
       because they never took effect. They are passed over, not refused. */
    write_file(path, "authkey=" SAMPLE_KEY "\nname=older\n"
                     "control=https://headscale.example.org\nderp=6\nverifyderp=1\n");
    CHECK(pim_ts_config_load(path, &config));
    CHECK(strcmp(config.name, "older") == 0);
    CHECK(config.derp_region == 6);

    /* A name longer than the field is cut, not written past. */
    write_file(path, "authkey=" SAMPLE_KEY "\nname=0123456789012345678901234567890123456789\n");
    CHECK(pim_ts_config_load(path, &config));
    CHECK(strlen(config.name) == PIM_TS_NAME_MAX);
    remove(path);
}

static void test_round_trip(void) {
    pim_ts_config_t saved, loaded;
    const char *path = temp_path();

    pim_ts_config_init(&saved);
    snprintf(saved.authkey, sizeof(saved.authkey), "%s", SAMPLE_KEY);
    snprintf(saved.name, sizeof(saved.name), "%s", "picodeck");
    saved.derp_region = 6;

    CHECK(pim_ts_config_save(path, &saved));
    CHECK(pim_ts_config_load(path, &loaded));
    CHECK(strcmp(loaded.authkey, saved.authkey) == 0);
    CHECK(strcmp(loaded.name, saved.name) == 0);
    CHECK(loaded.derp_region == saved.derp_region);
    remove(path);
}

static void test_tailnet_addresses(void) {
    /* 100.64.0.0/10: the second octet runs 64 to 127. */
    CHECK(pim_ts_is_tailnet_address("100.64.0.0"));
    CHECK(pim_ts_is_tailnet_address("100.127.255.255"));
    CHECK(pim_ts_is_tailnet_address("100.100.100.100"));
    CHECK(pim_ts_is_tailnet_address("100.96.0.1"));
    /* Just outside it, both ways. */
    CHECK(!pim_ts_is_tailnet_address("100.63.255.255"));
    CHECK(!pim_ts_is_tailnet_address("100.128.0.0"));
    CHECK(!pim_ts_is_tailnet_address("99.64.0.1"));
    CHECK(!pim_ts_is_tailnet_address("101.64.0.1"));
    /* Ordinary local addresses, which is what a relay is. */
    CHECK(!pim_ts_is_tailnet_address("192.168.1.9"));
    CHECK(!pim_ts_is_tailnet_address("127.0.0.1"));
    CHECK(!pim_ts_is_tailnet_address("10.0.0.1"));
    /* Not a dotted quad at all: a name needs the tailnet's DNS, not this. */
    CHECK(!pim_ts_is_tailnet_address("myserver"));
    CHECK(!pim_ts_is_tailnet_address("nas.tail1a2b.ts.net"));
    CHECK(!pim_ts_is_tailnet_address("fd7a:115c:a1e0::1"));
    CHECK(!pim_ts_is_tailnet_address(""));
    CHECK(!pim_ts_is_tailnet_address(NULL));
    /* Trailing rubbish is not an address. */
    CHECK(!pim_ts_is_tailnet_address("100.64.0.1x"));
    CHECK(!pim_ts_is_tailnet_address("100.64.0.1.5"));
    CHECK(!pim_ts_is_tailnet_address("100.999.0.1"));
}

static void test_client_absent(void) {
    /* A host build has no client, and the interface says so plainly rather
       than pretending to connect. */
    pim_ts_config_t config;
    pim_ts_config_init(&config);
    CHECK(!pim_ts_available());
    CHECK(!pim_ts_start(&config));
    CHECK(pim_ts_state() == PIM_TS_OFF);
    CHECK(pim_ts_address()[0] == '\0');
    CHECK(pim_ts_message()[0] != '\0');
    pim_ts_poll();
    pim_ts_stop();
    CHECK(pim_ts_state() == PIM_TS_OFF);
}

int main(void) {
    test_authkey_shape();
    test_defaults();
    test_load();
    test_round_trip();
    test_tailnet_addresses();
    test_client_absent();
    printf("%d checks, %d failures\n", checks, failures);
    if (failures == 0) printf("Tailscale settings tests passed\n");
    return failures == 0 ? 0 : 1;
}
