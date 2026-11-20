#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "pim/sshcrypto.h"
#include "pim/lock.h"
#include "pim/ssh.h"
#include "pim/sshkey.h"

static int failures;
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "%s:%d %s\n", __FILE__, __LINE__, #value); failures++; } } while (0)

static void from_hex(const char *hex, uint8_t *out, size_t length) {
    for (size_t i = 0; i < length; i++) {
        unsigned value = 0;
        sscanf(hex + 2 * i, "%2x", &value);
        out[i] = (uint8_t)value;
    }
}

static bool same(const uint8_t *a, const char *hex, size_t length) {
    uint8_t expected[128];
    from_hex(hex, expected, length);
    return memcmp(a, expected, length) == 0;
}

/* RFC 8439 section 2.3.2: the block function, with its own key, counter and
   nonce. Getting this wrong is the single most likely way to end up with a
   handshake that fails for no visible reason, so it is checked first. */
static void test_chacha20_block(void) {
    uint8_t key[32], nonce[12], out[64];
    from_hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key, 32);
    from_hex("000000090000004a00000000", nonce, 12);
    pim_chacha20_rfc_block(key, 1, nonce, out);
    CHECK(same(out,
               "10f1e7e4d13b5915500fdd1fa32071c4c7d1f4c733c068030422aa9ac3d46c4e"
               "d2826446079faa0914c2d705d98b02a2b5129cd1de164eb9cbd083e8a2503c4e",
               64));

    /* The same 20 rounds through the 64-bit-nonce form OpenSSH uses: counter and
       nonce together occupy the same four words, so a counter of 1 with an
       8-byte nonce must reproduce a 32-bit counter of 1 with 0x00000009 as the
       first nonce word. */
    {
        uint8_t nonce8[8], out8[64];
        memcpy(nonce8, nonce + 4, 8);
        pim_chacha20_block(key, (uint64_t)1 | ((uint64_t)0x09000000u << 32), nonce8, out8);
        CHECK(memcmp(out, out8, 64) == 0);
    }
}

/* RFC 8439 section 2.4.2: the stream cipher over a real message. */
static void test_chacha20_stream(void) {
    static const char plain[] =
        "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for "
        "the future, sunscreen would be it.";
    uint8_t key[32], nonce[12], out[114];
    uint32_t state_nonce;
    from_hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key, 32);
    from_hex("000000000000004a00000000", nonce, 12);
    /* done blockwise through the RFC form, since that is what the vector uses */
    for (size_t offset = 0; offset < sizeof(plain) - 1; offset += 64) {
        uint8_t stream[64];
        size_t chunk = sizeof(plain) - 1 - offset < 64 ? sizeof(plain) - 1 - offset : 64;
        pim_chacha20_rfc_block(key, (uint32_t)(1 + offset / 64), nonce, stream);
        for (size_t i = 0; i < chunk; i++) out[offset + i] = (uint8_t)plain[offset + i] ^ stream[i];
    }
    CHECK(same(out,
               "6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0b"
               "f91b65c5524733ab8f593dabcd62b3571639d624e65152ab8f530c359f0861d8"
               "07ca0dbf500d6a6156a38e088a22b65e52bc514d16ccf806818ce91ab7793736"
               "5af90bbf74a35be6b40b8eedf2785e42874d", 114));
    (void)state_nonce;
}

/* RFC 8439 section 2.5.2. */
static void test_poly1305(void) {
    static const char message[] = "Cryptographic Forum Research Group";
    uint8_t key[32], tag[16];
    from_hex("85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b", key, 32);
    pim_poly1305(key, (const uint8_t *)message, sizeof(message) - 1, tag);
    CHECK(same(tag, "a8061dc1305136c6c22b8baf0c0127a9", 16));
    CHECK(pim_poly1305_verify(key, (const uint8_t *)message, sizeof(message) - 1, tag));
    tag[0] ^= 1;
    CHECK(!pim_poly1305_verify(key, (const uint8_t *)message, sizeof(message) - 1, tag));

    /* an empty message, and one that is exactly one block, are the edges */
    {
        uint8_t zero_key[32] = {0}, zero_tag[16];
        pim_poly1305(zero_key, NULL, 0, zero_tag);
        CHECK(same(zero_tag, "00000000000000000000000000000000", 16));
    }
    {
        uint8_t block[16];
        memset(block, 0, sizeof(block));
        pim_poly1305(key, block, sizeof(block), tag);
        CHECK(pim_poly1305_verify(key, block, sizeof(block), tag));
    }
}

/* RFC 7748 section 6.1: Alice and Bob reach the same shared secret. */
static void test_x25519(void) {
    uint8_t alice_secret[32], bob_secret[32], alice_public[32], bob_public[32];
    uint8_t shared_a[32], shared_b[32];
    from_hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", alice_secret, 32);
    from_hex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", bob_secret, 32);
    pim_x25519_base(alice_public, alice_secret);
    pim_x25519_base(bob_public, bob_secret);
    CHECK(same(alice_public, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", 32));
    CHECK(same(bob_public, "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", 32));
    pim_x25519(shared_a, alice_secret, bob_public);
    pim_x25519(shared_b, bob_secret, alice_public);
    CHECK(same(shared_a, "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", 32));
    CHECK(memcmp(shared_a, shared_b, 32) == 0);
}

/* RFC 8032 section 7.1, test vector 2: a one-byte message. */
static void test_ed25519(void) {
    uint8_t secret[64], public_key[32], signature[64];
    const uint8_t message[1] = {0x72};
    from_hex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb", secret, 32);
    from_hex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", public_key, 32);
    memcpy(secret + 32, public_key, 32);

    pim_ed25519_sign(signature, message, sizeof(message), secret);
    CHECK(same(signature,
               "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
               "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00", 64));
    CHECK(pim_ed25519_verify(signature, message, sizeof(message), public_key));

    /* a tampered signature, message, or key must all be rejected */
    signature[0] ^= 1;
    CHECK(!pim_ed25519_verify(signature, message, sizeof(message), public_key));
    signature[0] ^= 1;
    {
        const uint8_t other[1] = {0x73};
        CHECK(!pim_ed25519_verify(signature, other, sizeof(other), public_key));
    }
    public_key[0] ^= 1;
    CHECK(!pim_ed25519_verify(signature, message, sizeof(message), public_key));
    public_key[0] ^= 1;

    /* the two halves of the secret are checked against each other */
    CHECK(pim_ed25519_key_consistent(secret));
    secret[40] ^= 1;                    /* corrupt the public half */
    CHECK(!pim_ed25519_key_consistent(secret));

    /* an empty message signs and verifies too, which SSH never needs but a
       parser bug would show up here */
    {
        uint8_t empty_sig[64];
        from_hex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb", secret, 32);
        memcpy(secret + 32, public_key, 32);
        pim_ed25519_sign(empty_sig, (const uint8_t *)"", 0, secret);
        CHECK(pim_ed25519_verify(empty_sig, (const uint8_t *)"", 0, public_key));
    }
}

/* The exchange hash and every derived key are SHA-256; this is the same
   implementation the key fingerprints use, checked again here because SSH
   depends on it. */
static void test_sha256_still_right(void) {
    uint8_t digest[32];
    pim_sha256((const uint8_t *)"abc", 3, digest);
    CHECK(same(digest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 32));
}

/* Randomness must actually be random: a weak source here would silently ruin
   every key exchange, so the smoke test is that two draws differ and neither
   is all zeroes. */
static void test_random(void) {
    uint8_t a[32], b[32], zero[32];
    memset(zero, 0, sizeof(zero));
    CHECK(pim_ssh_random(a, sizeof(a)));
    CHECK(pim_ssh_random(b, sizeof(b)));
    CHECK(memcmp(a, b, sizeof(a)) != 0);
    CHECK(memcmp(a, zero, sizeof(a)) != 0);
    CHECK(memcmp(b, zero, sizeof(b)) != 0);
}


/* ---- key files and known hosts ----

   Throwaway keys, generated for these tests and used nowhere else. */
static const char ED25519_KEY[] =
    "-----BEGIN OPENSSH PRIVATE KEY-----\n"
    "b3BlbnNzaC1rZXktdjEAAAAABG5vbmUAAAAEbm9uZQAAAAAAAAABAAAAMwAAAAtzc2gtZW\n"
    "QyNTUxOQAAACCmyWmqkmVgzarRlrmJebqIA97urW2hFzQEAF6og1rSIAAAAIiVCfQFlQn0\n"
    "BQAAAAtzc2gtZWQyNTUxOQAAACCmyWmqkmVgzarRlrmJebqIA97urW2hFzQEAF6og1rSIA\n"
    "AAAEAvuhGXy9ta5Zz8gToaCzOCV+M4xus7PaHZKRnFGqZ4nKbJaaqSZWDNqtGWuYl5uogD\n"
    "3u6tbaEXNAQAXqiDWtIgAAAABHRlc3QB\n"
    "-----END OPENSSH PRIVATE KEY-----\n";

static const char ENCRYPTED_KEY[] =
    "-----BEGIN OPENSSH PRIVATE KEY-----\n"
    "b3BlbnNzaC1rZXktdjEAAAAACmFlczI1Ni1jdHIAAAAGYmNyeXB0AAAAGAAAABChWrmTZS\n"
    "nVMK1KKr0vN28hAAAAIAAAAAEAAAAzAAAAC3NzaC1lZDI1NTE5AAAAIObkVNdyf5f75hUB\n"
    "jgI/JZSuvgvfHM9zrKDEx32myNuFAAAAkM4WRXmB+4dFQzC7ZYHno3UG6pNGbXXgNfwCuQ\n"
    "7fxTdLfbVqOLyqxcX3aJyHCCeN/ODwoq8+XqpZRHXmHlNLPFSaW9gkhTDK1IFWvaFczN7+\n"
    "ubblGzHroAIE0Je7xN5ml9dHkKMHZreOjMhvVvLCAC2xlp4buAKdQebVIPgiiUrjckx2Jc\n"
    "FVRkr42UbD3c5fQA==\n"
    "-----END OPENSSH PRIVATE KEY-----\n";

static const char RSA_KEY[] =
    "-----BEGIN OPENSSH PRIVATE KEY-----\n"
    "b3BlbnNzaC1rZXktdjEAAAAABG5vbmUAAAAEbm9uZQAAAAAAAAABAAABFwAAAAdzc2gtcn\n"
    "NhAAAAAwEAAQAAAQEAn7qjpVIInXpTf/zAu889uY5C94cgl6cnWnayFAx5rexicY+rsdc4\n"
    "qvqDhHOLwTcDbum/oXJrkkQmX8QWIeplrwwWVd0/zlK8MUe00mBMyx8lDXpGXl0OD09dpR\n"
    "i2vzXH97t9f5RdHTWRlGqc6xUIBOTspP2sE2967KyCfMcg0xn0G12+sxFS6GxOOxdTAHuu\n"
    "AEkY8K2NOV+5dzi+LLlWA5E7KK/bkZPAWieOOCahA/fgi7Jk2RiQHBK6y4wIUMQl4+jr5J\n"
    "5AbcbeKNP/Cf4GpA3a+NjszuOTjqjt/PGPDiH4Qns5fUxWxQAvh/FOIc6pCBswCiHHgTXC\n"
    "fhJdOeTGRQAAA8AadXUaGnV1GgAAAAdzc2gtcnNhAAABAQCfuqOlUgidelN//MC7zz25jk\n"
    "L3hyCXpydadrIUDHmt7GJxj6ux1ziq+oOEc4vBNwNu6b+hcmuSRCZfxBYh6mWvDBZV3T/O\n"
    "UrwxR7TSYEzLHyUNekZeXQ4PT12lGLa/Ncf3u31/lF0dNZGUapzrFQgE5Oyk/awTb3rsrI\n"
    "J8xyDTGfQbXb6zEVLobE47F1MAe64ASRjwrY05X7l3OL4suVYDkTsor9uRk8BaJ444JqED\n"
    "9+CLsmTZGJAcErrLjAhQxCXj6OvknkBtxt4o0/8J/gakDdr42OzO45OOqO388Y8OIfhCez\n"
    "l9TFbFAC+H8U4hzqkIGzAKIceBNcJ+El055MZFAAAAAwEAAQAAAQAa+G7NaW6e+JrEz67T\n"
    "h4jKniTcmup78POKKOSk0f5xyapNto6tNsQQysAOS1xbwahIwBtlbWaQigVYwklibKvCxO\n"
    "24jeVKN1ojkBIOO8h7u8SL5Auhv2CyYRpU2DlzS+d6AySLuEeIuRTd1KquXxy9htIWgebe\n"
    "WUKOWAqoew8l1cW5b14e42KaPE6ZraHOBiOvbMAvQibVgN18Y9LydJdNYJ4+J6yKmwRW2c\n"
    "svZabAX4GMDcfWfiiQrgi1wJsADkGemGn8J65uN96GHdmn/fW88w1U3mGq+Qb8B7tnpvQ8\n"
    "fHV2IrenFTymRYDZ5p3vksZioCDU7+NVDDHsZCAon2mBAAAAgHk60HAmOoyskuJ9A4+56i\n"
    "C8ddVDCu24J1kYwhGPj+AiaQ6z4qlcCqeBRLA2wjSbypMytsMYniXC/dJXh+Z85HdG7nKg\n"
    "hj4He0gGYkATnkha6miodSxcKZ1h9QRoYkyXL1M/qvjayojSHFju7E6IEpvvctRicq6cOh\n"
    "Kwq7EO0tG2AAAAgQDQq4OeOXsxNXhsGYmiwp7lleqofJh8UxYcGgv7o6UpfwrDTgGyhR4a\n"
    "PHOfEqePHGmNZ5pMm4UT4Ir0Qh412WmgO9dJtqTiA3pTVO96SHAg6BHbRfuiw6Vd8K1N6k\n"
    "63EziCJ91MwU2Jtsa/Ysu54jFPS4gQFon4+8FEz+J/uFffMQAAAIEAw/VbFeuCYxWTHi/k\n"
    "IPVE/BtVVWL0lXcFNTt2J/K4D2p6coP57lQLz/3SPSBjNpgnhPtNKppALb4tSix+IyRdxL\n"
    "MsIa48fWrBNm97lp59/M0J9xg+cHYPM8nIMEH1tkOJUWZabMMZmKU9NMA/uWWtOKtCNwO5\n"
    "TGT8Z0Xxwtalm1UAAAAEdGVzdAECAwQFBgc=\n"
    "-----END OPENSSH PRIVATE KEY-----\n";

#define KNOWN_HOSTS "test_known_hosts"
#define KEY_DIR_NEW "test_new_keys"

static char *read_whole(const char *path) {
    static char text[4096];
    FILE *file = fopen(path, "rb");
    size_t got;
    if (file == NULL) return NULL;
    got = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[got] = '\0';
    return got > 0 ? text : NULL;
}

static void test_private_key_parsing(void) {
    uint8_t secret[64];
    const char *why = "unset";

    CHECK(pim_ssh_parse_private_key(ED25519_KEY, secret, &why));
    CHECK(why == NULL);
    /* The two halves have to belong together, which also proves the base64 and
       the container parsing landed on the right bytes. */
    CHECK(pim_ed25519_key_consistent(secret));

    /* A signature made with the parsed key verifies against its own public half. */
    {
        uint8_t signature[64];
        static const uint8_t message[] = "hello";
        pim_ed25519_sign(signature, message, sizeof(message) - 1, secret);
        CHECK(pim_ed25519_verify(signature, message, sizeof(message) - 1, secret + 32));
    }

    /* The three ways a key can be unusable are each reported, not guessed at. */
    why = NULL;
    CHECK(!pim_ssh_parse_private_key(ENCRYPTED_KEY, secret, &why));
    CHECK(why != NULL && strstr(why, "passphrase") != NULL);
    why = NULL;
    CHECK(!pim_ssh_parse_private_key(RSA_KEY, secret, &why));
    CHECK(why != NULL && strstr(why, "ed25519") != NULL);
    why = NULL;
    CHECK(!pim_ssh_parse_private_key("not a key at all\n", secret, &why));
    CHECK(why != NULL);

    /* A truncated file must be refused rather than read past the end. */
    {
        char clipped[256];
        snprintf(clipped, sizeof(clipped), "%.120s", ED25519_KEY);
        CHECK(!pim_ssh_parse_private_key(clipped, secret, &why));
    }
}

static void test_known_hosts(void) {
    char fingerprint[PIM_SSH_KNOWN_FINGERPRINT_MAX + 1];
    remove(KNOWN_HOSTS);

    CHECK(!pim_ssh_known_host_find(KNOWN_HOSTS, "nas", fingerprint, sizeof(fingerprint)));
    CHECK(pim_ssh_known_host_remember(KNOWN_HOSTS, "nas", "SHA256:aaaa"));
    CHECK(pim_ssh_known_host_find(KNOWN_HOSTS, "nas", fingerprint, sizeof(fingerprint)));
    CHECK(strcmp(fingerprint, "SHA256:aaaa") == 0);

    /* a second host does not disturb the first */
    CHECK(pim_ssh_known_host_remember(KNOWN_HOSTS, "build", "SHA256:bbbb"));
    CHECK(pim_ssh_known_host_find(KNOWN_HOSTS, "nas", fingerprint, sizeof(fingerprint)));
    CHECK(strcmp(fingerprint, "SHA256:aaaa") == 0);
    CHECK(pim_ssh_known_host_find(KNOWN_HOSTS, "build", fingerprint, sizeof(fingerprint)));
    CHECK(strcmp(fingerprint, "SHA256:bbbb") == 0);

    /* remembering again replaces rather than appends */
    CHECK(pim_ssh_known_host_remember(KNOWN_HOSTS, "nas", "SHA256:cccc"));
    CHECK(pim_ssh_known_host_find(KNOWN_HOSTS, "nas", fingerprint, sizeof(fingerprint)));
    CHECK(strcmp(fingerprint, "SHA256:cccc") == 0);
    CHECK(pim_ssh_known_host_find(KNOWN_HOSTS, "build", fingerprint, sizeof(fingerprint)));

    CHECK(pim_ssh_known_host_forget(KNOWN_HOSTS, "nas"));
    CHECK(!pim_ssh_known_host_find(KNOWN_HOSTS, "nas", fingerprint, sizeof(fingerprint)));
    CHECK(pim_ssh_known_host_find(KNOWN_HOSTS, "build", fingerprint, sizeof(fingerprint)));

    /* a name with a space in it would corrupt the file, so it is refused */
    CHECK(!pim_ssh_known_host_remember(KNOWN_HOSTS, "two words", "SHA256:dddd"));
    CHECK(!pim_ssh_known_host_remember(KNOWN_HOSTS, "ok", "has space"));
    CHECK(!pim_ssh_known_host_find(KNOWN_HOSTS, "", fingerprint, sizeof(fingerprint)));
    remove(KNOWN_HOSTS);
}


/* A key made on the device has to be one OpenSSH would accept, which is best
   checked by reading it back with the same parser the client uses and by
   putting a signature through it. */
static void test_key_creation(void) {
    pim_ssh_keys_t keys;
    uint8_t secret[64], signature[64];
    const char *why = NULL;
    char *text;
    static const uint8_t message[] = "round trip";

    system("rm -rf " KEY_DIR_NEW);
    CHECK(mkdir(KEY_DIR_NEW, 0777) == 0);
    CHECK(pim_ssh_key_create(KEY_DIR_NEW, "onboard", "made here"));

    /* the scan sees it, with both halves and the right type */
    CHECK(pim_ssh_keys_scan(KEY_DIR_NEW, &keys) == 1);
    CHECK(strcmp(keys.keys[0].name, "onboard") == 0);
    CHECK(strcmp(keys.keys[0].type, "ssh-ed25519") == 0);
    CHECK(keys.keys[0].bits == 256);
    CHECK(keys.keys[0].has_private);
    CHECK(strncmp(keys.keys[0].fingerprint, "SHA256:", 7) == 0);
    CHECK(strcmp(keys.keys[0].comment, "made here") == 0);

    /* the private half parses, and signs something its own public half verifies */
    text = read_whole(KEY_DIR_NEW "/onboard");
    CHECK(text != NULL);
    if (text != NULL) {
        CHECK(pim_ssh_parse_private_key(text, secret, &why));
        CHECK(why == NULL);
        pim_ed25519_sign(signature, message, sizeof(message) - 1, secret);
        CHECK(pim_ed25519_verify(signature, message, sizeof(message) - 1, secret + 32));
    }

    /* two keys made in a row differ: the entropy is real */
    CHECK(pim_ssh_key_create(KEY_DIR_NEW, "second", NULL));
    {
        pim_ssh_keys_t again;
        CHECK(pim_ssh_keys_scan(KEY_DIR_NEW, &again) == 2);
        CHECK(strcmp(again.keys[0].fingerprint, again.keys[1].fingerprint) != 0);
    }

    /* an existing name is refused rather than overwritten, and a bad name too */
    CHECK(!pim_ssh_key_create(KEY_DIR_NEW, "onboard", NULL));
    CHECK(!pim_ssh_key_create(KEY_DIR_NEW, "../escape", NULL));

    /* A comment too long for the key's private section is refused, and the
       call returns: padding that section used to loop for ever once it was
       full, because a full buffer stops advancing. */
    {
        static char long_comment[2048];
        memset(long_comment, 'c', sizeof(long_comment) - 1);
        long_comment[sizeof(long_comment) - 1] = '\0';
        CHECK(!pim_ssh_key_create(KEY_DIR_NEW, "toolong", long_comment));
    }
    system("rm -rf " KEY_DIR_NEW);
}


/* The PIN is a lock on the front door: what the file must never contain is the
   PIN itself, and what it must resist is being read off by eye. */
#define LOCK_FILE "test_lock.cfg"

static void test_lock_pin(void) {
    char contents[256];
    FILE *file;

    remove(LOCK_FILE);
    CHECK(!pim_lock_is_set(LOCK_FILE));
    CHECK(!pim_lock_check(LOCK_FILE, "123456"));     /* nothing set yet */

    /* six digits, no more and no less */
    CHECK(pim_lock_pin_ok("000000"));
    CHECK(pim_lock_pin_ok("931724"));
    CHECK(!pim_lock_pin_ok("12345"));
    CHECK(!pim_lock_pin_ok("1234567"));
    CHECK(!pim_lock_pin_ok("12a456"));
    CHECK(!pim_lock_pin_ok(""));
    CHECK(!pim_lock_pin_ok(NULL));
    CHECK(!pim_lock_set(LOCK_FILE, "12345"));

    CHECK(pim_lock_set(LOCK_FILE, "931724"));
    CHECK(pim_lock_is_set(LOCK_FILE));
    CHECK(pim_lock_check(LOCK_FILE, "931724"));
    CHECK(!pim_lock_check(LOCK_FILE, "931725"));
    CHECK(!pim_lock_check(LOCK_FILE, "000000"));

    /* the PIN must not be anywhere in the file, in any obvious form */
    file = fopen(LOCK_FILE, "rb");
    CHECK(file != NULL);
    if (file != NULL) {
        size_t got = fread(contents, 1, sizeof(contents) - 1, file);
        fclose(file);
        contents[got] = '\0';
        CHECK(strstr(contents, "931724") == NULL);
        CHECK(strncmp(contents, "pin=", 4) == 0);
    }

    /* the same PIN set twice produces a different file: the salt is fresh */
    {
        char first[256], second[256];
        snprintf(first, sizeof(first), "%s", contents);
        CHECK(pim_lock_set(LOCK_FILE, "931724"));
        file = fopen(LOCK_FILE, "rb");
        if (file != NULL) {
            size_t got = fread(second, 1, sizeof(second) - 1, file);
            fclose(file);
            second[got] = '\0';
            CHECK(strcmp(first, second) != 0);
        }
        CHECK(pim_lock_check(LOCK_FILE, "931724"));   /* and still verifies */
    }

    /* changing it takes effect, and clearing it removes the lock */
    CHECK(pim_lock_set(LOCK_FILE, "246802"));
    CHECK(!pim_lock_check(LOCK_FILE, "931724"));
    CHECK(pim_lock_check(LOCK_FILE, "246802"));
    CHECK(pim_lock_clear(LOCK_FILE));
    CHECK(!pim_lock_is_set(LOCK_FILE));
    CHECK(!pim_lock_check(LOCK_FILE, "246802"));

    /* a damaged file is "no PIN set" rather than "any PIN works" */
    file = fopen(LOCK_FILE, "wb");
    if (file != NULL) { fprintf(file, "pin=50000:zz:yy\n"); fclose(file); }
    CHECK(!pim_lock_is_set(LOCK_FILE));
    CHECK(!pim_lock_check(LOCK_FILE, "246802"));
    file = fopen(LOCK_FILE, "wb");
    if (file != NULL) { fprintf(file, "nonsense\n"); fclose(file); }
    CHECK(!pim_lock_is_set(LOCK_FILE));
    remove(LOCK_FILE);
}

int main(void) {
    test_chacha20_block();
    test_chacha20_stream();
    test_poly1305();
    test_x25519();
    test_ed25519();
    test_sha256_still_right();
    test_random();
    test_private_key_parsing();
    test_known_hosts();
    test_key_creation();
    test_lock_pin();
    if (failures == 0) puts("ssh crypto tests passed");
    return failures == 0 ? 0 : 1;
}
