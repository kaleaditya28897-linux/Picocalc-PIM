/* Removing members from a JSON document in place.

   This runs on a network map arriving from a control plane, so the cases that
   matter are the awkward ones: a name that also appears as a value, a brace
   inside a string, nesting, and what the separators look like afterwards. The
   result has to be valid JSON, so every case here is parsed again with cJSON
   and compared against what the same removal produces by other means. */

#include "pim/jsonstrip.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>

static int checks, failures;

#define CHECK(expr) do { \
    checks++; \
    if (!(expr)) { printf("FAIL line %d: %s\n", __LINE__, #expr); failures++; } \
} while (0)

static const char *const drop[] = {"Services", "CapMap", "Hostinfo"};
#define DROP_COUNT (sizeof(drop) / sizeof(drop[0]))

/* Strips a copy and returns it, so the literals stay intact. */
static char *stripped(const char *json) {
    static char buffer[8192];
    snprintf(buffer, sizeof(buffer), "%s", json);
    pim_json_strip(buffer, drop, DROP_COUNT);
    return buffer;
}

/* The result must still be JSON, which is the only thing the caller cares
   about: whatever it keeps is handed straight to a parser. */
static int parses(const char *json) {
    cJSON *root = cJSON_Parse(json);
    int ok = root != NULL;
    cJSON_Delete(root);
    return ok;
}

static void test_basics(void) {
    CHECK(strcmp(stripped("{\"a\":1,\"Services\":[1,2],\"b\":2}"), "{\"a\":1,\"b\":2}") == 0);
    /* First member, last member, only member. */
    CHECK(strcmp(stripped("{\"Services\":[1],\"b\":2}"), "{\"b\":2}") == 0);
    CHECK(strcmp(stripped("{\"a\":1,\"Services\":[1]}"), "{\"a\":1}") == 0);
    CHECK(strcmp(stripped("{\"Services\":[1]}"), "{}") == 0);
    /* Several in a row leave no stray commas behind. */
    CHECK(strcmp(stripped("{\"Services\":1,\"CapMap\":2,\"Hostinfo\":3,\"k\":4}"), "{\"k\":4}") == 0);
    CHECK(strcmp(stripped("{\"k\":4,\"Services\":1,\"CapMap\":2,\"Hostinfo\":3}"), "{\"k\":4}") == 0);
}

static void test_values_of_every_shape(void) {
    CHECK(strcmp(stripped("{\"Services\":\"text\",\"k\":1}"), "{\"k\":1}") == 0);
    CHECK(strcmp(stripped("{\"Services\":null,\"k\":1}"), "{\"k\":1}") == 0);
    CHECK(strcmp(stripped("{\"Services\":true,\"k\":1}"), "{\"k\":1}") == 0);
    CHECK(strcmp(stripped("{\"Services\":-12.5e3,\"k\":1}"), "{\"k\":1}") == 0);
    CHECK(strcmp(stripped("{\"Services\":{\"a\":{\"b\":[1,{\"c\":2}]}},\"k\":1}"), "{\"k\":1}") == 0);
    /* A brace or a bracket inside a string must not unbalance the skip. */
    CHECK(strcmp(stripped("{\"Services\":{\"a\":\"}]}\"},\"k\":1}"), "{\"k\":1}") == 0);
    CHECK(strcmp(stripped("{\"Services\":\"a\\\"}\",\"k\":1}"), "{\"k\":1}") == 0);
}

static void test_only_member_names(void) {
    /* The same word as a value, or inside an array, is not a member name. */
    CHECK(strcmp(stripped("{\"k\":\"Services\"}"), "{\"k\":\"Services\"}") == 0);
    CHECK(strcmp(stripped("{\"k\":[\"Services\",\"CapMap\"]}"), "{\"k\":[\"Services\",\"CapMap\"]}") == 0);
    /* A name that merely starts the same is left alone. */
    CHECK(strcmp(stripped("{\"ServicesExtra\":1,\"k\":2}"), "{\"ServicesExtra\":1,\"k\":2}") == 0);
    CHECK(strcmp(stripped("{\"Service\":1,\"k\":2}"), "{\"Service\":1,\"k\":2}") == 0);
}

static void test_nested_and_whitespace(void) {
    CHECK(strcmp(stripped("{\"peers\":[{\"n\":1,\"Services\":[1]},{\"n\":2,\"Services\":[2]}]}"),
                 "{\"peers\":[{\"n\":1},{\"n\":2}]}") == 0);
    CHECK(parses(stripped("{ \"a\" : 1 , \"Services\" : [ 1 , 2 ] , \"b\" : 2 }")));
    CHECK(parses(stripped("{\n  \"Services\": {\n    \"x\": 1\n  },\n  \"b\": 2\n}")));
    /* A member removed from every object in an array still leaves valid JSON. */
    CHECK(parses(stripped("{\"p\":[{\"Services\":1},{\"Services\":2},{\"Services\":3}]}")));
    CHECK(strcmp(stripped("{\"p\":[{\"Services\":1},{\"Services\":2}]}"), "{\"p\":[{},{}]}") == 0);
}

/* The real shape: a network map with the fields that make it expensive. */
static void test_netmap_shape(void) {
    static char netmap[8192];
    const char *peer =
        "{\"ID\":%d,\"Name\":\"host%d.example.ts.net.\","
        "\"Key\":\"nodekey:aaaa\",\"Addresses\":[\"100.64.1.%d/32\"],"
        "\"AllowedIPs\":[\"100.64.1.%d/32\"],\"Endpoints\":[\"10.0.0.%d:41641\"],"
        "\"DERP\":\"127.3.3.40:6\",\"Online\":true,"
        "\"Hostinfo\":{\"OS\":\"linux\",\"Hostname\":\"host%d\",\"IPNVersion\":\"1.78.1\"},"
        "\"Services\":[{\"Proto\":\"peerapi4\",\"Port\":38188},{\"Proto\":\"peerapi6\",\"Port\":38188}],"
        "\"CapMap\":{\"https://tailscale.com/cap/ssh\":[]}}";
    size_t at = 0;
    int i;
    cJSON *root, *peers;

    at += (size_t)snprintf(netmap + at, sizeof(netmap) - at, "{\"Seq\":1,\"Peers\":[");
    for (i = 0; i < 6; i++) {
        at += (size_t)snprintf(netmap + at, sizeof(netmap) - at, i ? "," : "");
        at += (size_t)snprintf(netmap + at, sizeof(netmap) - at, peer, i, i, i, i, i, i);
    }
    at += (size_t)snprintf(netmap + at, sizeof(netmap) - at,
                           "],\"DNSConfig\":{\"Domains\":[\"example.ts.net\"]}}");

    CHECK(parses(netmap));
    {
        size_t before = strlen(netmap);
        size_t after = pim_json_strip(netmap, drop, DROP_COUNT);
        CHECK(after < before);
        CHECK(after == strlen(netmap));
        CHECK(parses(netmap));
        printf("  netmap %u bytes -> %u (%u%% smaller)\n", (unsigned)before, (unsigned)after,
               (unsigned)(100u - 100u * after / before));
    }
    /* Everything the client actually reads has to survive. */
    root = cJSON_Parse(netmap);
    CHECK(root != NULL);
    peers = cJSON_GetObjectItem(root, "Peers");
    CHECK(peers != NULL && cJSON_GetArraySize(peers) == 6);
    if (peers != NULL) {
        cJSON *first = cJSON_GetArrayItem(peers, 0);
        CHECK(cJSON_GetObjectItem(first, "ID") != NULL);
        CHECK(cJSON_GetObjectItem(first, "Key") != NULL);
        CHECK(cJSON_GetObjectItem(first, "Addresses") != NULL);
        CHECK(cJSON_GetObjectItem(first, "AllowedIPs") != NULL);
        CHECK(cJSON_GetObjectItem(first, "Endpoints") != NULL);
        CHECK(cJSON_GetObjectItem(first, "DERP") != NULL);
        CHECK(cJSON_GetObjectItem(first, "Online") != NULL);
        CHECK(cJSON_GetObjectItem(first, "Name") != NULL);
        /* And the expensive parts have to be gone. */
        CHECK(cJSON_GetObjectItem(first, "Services") == NULL);
        CHECK(cJSON_GetObjectItem(first, "Hostinfo") == NULL);
        CHECK(cJSON_GetObjectItem(first, "CapMap") == NULL);
    }
    CHECK(cJSON_GetObjectItem(root, "DNSConfig") != NULL);
    CHECK(cJSON_GetObjectItem(root, "Seq") != NULL);
    cJSON_Delete(root);
}

static void test_degenerate(void) {
    static char empty[8] = "";
    CHECK(pim_json_strip(empty, drop, DROP_COUNT) == 0);
    CHECK(pim_json_strip(NULL, drop, DROP_COUNT) == 0);
    /* No names to remove means no change. */
    {
        static char text[32] = "{\"Services\":1}";
        CHECK(pim_json_strip(text, NULL, 0) == strlen("{\"Services\":1}"));
        CHECK(strcmp(text, "{\"Services\":1}") == 0);
    }
    /* Truncated input must not read past the end. */
    {
        static char cut[64] = "{\"a\":1,\"Services\":[1,2";
        pim_json_strip(cut, drop, DROP_COUNT);
        CHECK(strlen(cut) <= strlen("{\"a\":1,\"Services\":[1,2"));
    }
    {
        static char cut2[64] = "{\"Services\":\"unterminated";
        pim_json_strip(cut2, drop, DROP_COUNT);
        CHECK(strlen(cut2) <= strlen("{\"Services\":\"unterminated"));
    }
}

/* ---- walking an array one element at a time ---- */

static void test_array_span(void) {
    const char *doc = "{\"a\":1,\"Peers\":[{\"n\":1},{\"n\":2}],\"b\":2}";
    size_t start = 0, end = 0;
    CHECK(pim_json_array_span(doc, "Peers", &start, &end));
    /* The span is what sits inside the brackets. */
    CHECK(strncmp(doc + start, "{\"n\":1},{\"n\":2}", end - start) == 0);
    CHECK(doc[start - 1] == '[');
    CHECK(doc[end] == ']');
    /* Absent, and present but not an array. */
    CHECK(!pim_json_array_span(doc, "Nope", &start, &end));
    CHECK(!pim_json_array_span("{\"Peers\":5}", "Peers", &start, &end));
    CHECK(!pim_json_array_span("{\"Peers\":{\"a\":1}}", "Peers", &start, &end));
    /* A name that only appears as a value is not a member. */
    CHECK(!pim_json_array_span("{\"k\":\"Peers\"}", "Peers", &start, &end));
    /* An empty array has an empty span, and still counts as found. */
    CHECK(pim_json_array_span("{\"Peers\":[]}", "Peers", &start, &end));
    CHECK(end == start);
}

static void test_elements(void) {
    const char *doc = "{\"Peers\":[{\"n\":1},{\"n\":2},{\"n\":[3,{\"x\":\"],\"}]}]}";
    size_t start = 0, end = 0, at, es = 0, ee = 0;
    int found = 0;
    CHECK(pim_json_array_span(doc, "Peers", &start, &end));
    at = start;
    while (pim_json_next_element(doc, at, end, &es, &ee)) {
        found++;
        /* Each element must be a complete object, braces balanced - the third
           one hides a bracket and a comma inside a string. */
        CHECK(doc[es] == '{' && doc[ee - 1] == '}');
        at = ee;
    }
    CHECK(found == 3);

    /* Elements need not be objects. */
    {
        const char *nums = "{\"v\":[1,2.5,-3e4,true,null,\"s\"]}";
        int n = 0;
        CHECK(pim_json_array_span(nums, "v", &start, &end));
        at = start;
        while (pim_json_next_element(nums, at, end, &es, &ee)) { n++; at = ee; }
        CHECK(n == 6);
    }
    /* An empty array hands out nothing. */
    {
        const char *empty = "{\"v\":[]}";
        CHECK(pim_json_array_span(empty, "v", &start, &end));
        CHECK(!pim_json_next_element(empty, start, end, &es, &ee));
    }
    /* Whitespace between elements. */
    {
        const char *spaced = "{\"v\":[ {\"a\":1} , {\"b\":2} ]}";
        int n = 0;
        CHECK(pim_json_array_span(spaced, "v", &start, &end));
        at = start;
        while (pim_json_next_element(spaced, at, end, &es, &ee)) { n++; at = ee; }
        CHECK(n == 2);
    }
}

/* Each element has to parse on its own, which is the whole point. */
static void test_elements_parse_alone(void) {
    const char *doc = "{\"Peers\":[{\"ID\":7,\"Name\":\"a\"},{\"ID\":8,\"Name\":\"b\"}]}";
    size_t start = 0, end = 0, at, es = 0, ee = 0;
    int ids[2] = {0, 0}, n = 0;
    CHECK(pim_json_array_span(doc, "Peers", &start, &end));
    at = start;
    while (pim_json_next_element(doc, at, end, &es, &ee) && n < 2) {
        cJSON *one = cJSON_ParseWithLength(doc + es, ee - es);
        CHECK(one != NULL);
        if (one != NULL) {
            cJSON *id = cJSON_GetObjectItem(one, "ID");
            CHECK(id != NULL);
            if (id != NULL) ids[n] = (int)id->valuedouble;
            cJSON_Delete(one);
        }
        n++;
        at = ee;
    }
    CHECK(n == 2 && ids[0] == 7 && ids[1] == 8);
}

static void test_blank_member(void) {
    /* In the middle: the comma before it goes too. */
    {
        char doc[128] = "{\"a\":1,\"Peers\":[{\"n\":1}],\"b\":2}";
        size_t before = strlen(doc);
        CHECK(pim_json_blank_member(doc, "Peers"));
        CHECK(strlen(doc) == before);          /* same length, so offsets hold */
        CHECK(parses(doc));
        CHECK(strstr(doc, "Peers") == NULL);
        {
            cJSON *root = cJSON_Parse(doc);
            CHECK(root != NULL && cJSON_GetObjectItem(root, "Peers") == NULL);
            CHECK(root != NULL && cJSON_GetObjectItem(root, "a") != NULL);
            CHECK(root != NULL && cJSON_GetObjectItem(root, "b") != NULL);
            cJSON_Delete(root);
        }
    }
    /* First member: there is no comma before, so the one after goes. */
    {
        char doc[128] = "{\"Peers\":[1,2],\"b\":2}";
        CHECK(pim_json_blank_member(doc, "Peers"));
        CHECK(parses(doc));
        CHECK(strstr(doc, "Peers") == NULL);
    }
    /* Only member: no comma either side. */
    {
        char doc[128] = "{\"Peers\":[1,2]}";
        CHECK(pim_json_blank_member(doc, "Peers"));
        CHECK(parses(doc));
    }
    /* Last member. */
    {
        char doc[128] = "{\"a\":1,\"Peers\":[1,2]}";
        CHECK(pim_json_blank_member(doc, "Peers"));
        CHECK(parses(doc));
        CHECK(strstr(doc, "Peers") == NULL);
    }
    /* Absent leaves the document alone. */
    {
        char doc[128] = "{\"a\":1}";
        CHECK(!pim_json_blank_member(doc, "Peers"));
        CHECK(strcmp(doc, "{\"a\":1}") == 0);
    }
}

/* The two together, on a netmap: peers one at a time, then the remainder. */
static void test_netmap_two_passes(void) {
    char doc[512] =
        "{\"Node\":{\"ID\":1},"
        "\"Peers\":[{\"ID\":11},{\"ID\":12},{\"ID\":13}],"
        "\"DERPMap\":{\"Regions\":{\"6\":{\"RegionID\":6}}}}";
    size_t start = 0, end = 0, at, es = 0, ee = 0;
    int peers = 0;
    cJSON *rest;

    CHECK(pim_json_array_span(doc, "Peers", &start, &end));
    at = start;
    while (pim_json_next_element(doc, at, end, &es, &ee)) {
        cJSON *one = cJSON_ParseWithLength(doc + es, ee - es);
        CHECK(one != NULL);
        cJSON_Delete(one);
        peers++;
        at = ee;
    }
    CHECK(peers == 3);

    CHECK(pim_json_blank_member(doc, "Peers"));
    rest = cJSON_Parse(doc);
    CHECK(rest != NULL);
    if (rest != NULL) {
        /* Everything else survives, and the peers are gone from the tree. */
        CHECK(cJSON_GetObjectItem(rest, "Peers") == NULL);
        CHECK(cJSON_GetObjectItem(rest, "Node") != NULL);
        CHECK(cJSON_GetObjectItem(rest, "DERPMap") != NULL);
        cJSON_Delete(rest);
    }
}

int main(void) {
    test_basics();
    test_values_of_every_shape();
    test_only_member_names();
    test_nested_and_whitespace();
    test_netmap_shape();
    test_degenerate();
    test_array_span();
    test_elements();
    test_elements_parse_alone();
    test_blank_member();
    test_netmap_two_passes();
    printf("%d checks, %d failures\n", checks, failures);
    if (failures == 0) printf("json strip tests passed\n");
    return failures == 0 ? 0 : 1;
}
