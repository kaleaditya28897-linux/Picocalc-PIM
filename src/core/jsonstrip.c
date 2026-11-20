#include "pim/jsonstrip.h"

#include <string.h>

/* Walks a string starting at the opening quote and returns the index just past
   the closing one. A backslash escapes the next byte, so a quote inside a
   string does not end it. */
static size_t skip_string(const char *json, size_t at) {
    at++;                                   /* the opening quote */
    while (json[at] != '\0') {
        if (json[at] == '\\' && json[at + 1] != '\0') { at += 2; continue; }
        if (json[at] == '"') return at + 1;
        at++;
    }
    return at;
}

static size_t skip_spaces(const char *json, size_t at) {
    while (json[at] == ' ' || json[at] == '\t' || json[at] == '\n' || json[at] == '\r') at++;
    return at;
}

/* Returns the index just past a complete value - object, array, string, number,
   true, false or null - starting at `at`. */
static size_t skip_value(const char *json, size_t at) {
    int depth = 0;
    at = skip_spaces(json, at);
    if (json[at] == '"') return skip_string(json, at);
    if (json[at] != '{' && json[at] != '[') {
        /* A bare word or number: it ends where the next structural byte is. */
        while (json[at] != '\0' && json[at] != ',' && json[at] != '}' && json[at] != ']') at++;
        return at;
    }
    while (json[at] != '\0') {
        if (json[at] == '"') { at = skip_string(json, at); continue; }
        if (json[at] == '{' || json[at] == '[') depth++;
        else if (json[at] == '}' || json[at] == ']') {
            depth--;
            if (depth == 0) return at + 1;
        }
        at++;
    }
    return at;
}

/* True when the string starting at `at` is one of the names, and is being used
   as a member name - that is, the next thing after it is a colon. The index of
   the colon comes back in `colon`. */
static int key_matches(const char *json, size_t at, const char *const *keys,
                       size_t key_count, size_t *after_key, size_t *colon) {
    size_t end = skip_string(json, at);
    size_t next = skip_spaces(json, end);
    size_t length;
    size_t i;
    if (json[next] != ':') return 0;
    length = end - at - 2;                  /* the name without its quotes */
    for (i = 0; i < key_count; i++) {
        if (strlen(keys[i]) == length && memcmp(json + at + 1, keys[i], length) == 0) {
            *after_key = end;
            *colon = next;
            return 1;
        }
    }
    return 0;
}

/* Separators are left behind when members are removed - a doubled comma, or one
   against a brace. Rather than track where each object begins and ends, they
   are tidied afterwards, which is the same job with far less bookkeeping. */
static size_t tidy_separators(char *json) {
    size_t read = 0, write = 0;
    while (json[read] != '\0') {
        if (json[read] == '"') {
            size_t end = skip_string(json, read);
            while (read < end) json[write++] = json[read++];
            continue;
        }
        if (json[read] == ',') {
            size_t next = skip_spaces(json, read + 1);
            /* A comma before the end of an object or array, or before another
               comma, has nothing left to separate. */
            if (json[next] == '}' || json[next] == ']' || json[next] == ',' || json[next] == '\0') {
                read++;
                continue;
            }
            /* A comma straight after an opening brace is equally orphaned. */
            if (write > 0) {
                size_t back = write;
                while (back > 0 && (json[back - 1] == ' ' || json[back - 1] == '\t' ||
                                    json[back - 1] == '\n' || json[back - 1] == '\r')) back--;
                if (back > 0 && (json[back - 1] == '{' || json[back - 1] == '[')) {
                    read++;
                    continue;
                }
            }
        }
        json[write++] = json[read++];
    }
    json[write] = '\0';
    return write;
}

size_t pim_json_strip(char *json, const char *const *keys, size_t key_count) {
    size_t read = 0, write = 0;
    if (json == NULL) return 0;
    if (keys == NULL || key_count == 0) return strlen(json);

    while (json[read] != '\0') {
        if (json[read] == '"') {
            size_t after_key, colon;
            if (key_matches(json, read, keys, key_count, &after_key, &colon)) {
                /* Drop the name, the colon and the whole value. What it leaves
                   behind in the way of commas is tidied below. */
                (void)after_key;
                read = skip_value(json, colon + 1);
                continue;
            }
            {
                size_t end = skip_string(json, read);
                while (read < end) json[write++] = json[read++];
            }
            continue;
        }
        json[write++] = json[read++];
    }
    json[write] = '\0';
    return tidy_separators(json);
}

/* ---- walking an array without parsing the document ---- */

/* Finds a member by name at any depth and returns where its name begins and
   where the colon after it is. */
static bool find_member(const char *json, const char *key, size_t *name_at, size_t *colon_at) {
    size_t at = 0;
    size_t key_length = strlen(key);
    while (json[at] != '\0') {
        if (json[at] == '"') {
            size_t end = skip_string(json, at);
            size_t next = skip_spaces(json, end);
            if (json[next] == ':' && end - at - 2 == key_length &&
                memcmp(json + at + 1, key, key_length) == 0) {
                *name_at = at;
                *colon_at = next;
                return true;
            }
            at = end;
            continue;
        }
        at++;
    }
    return false;
}

bool pim_json_array_span(const char *json, const char *key, size_t *start, size_t *end) {
    size_t name_at, colon_at, value_at, value_end;
    if (json == NULL || key == NULL) return false;
    if (!find_member(json, key, &name_at, &colon_at)) return false;
    value_at = skip_spaces(json, colon_at + 1);
    if (json[value_at] != '[') return false;
    value_end = skip_value(json, value_at);
    if (value_end <= value_at + 1) return false;        /* unterminated */
    *start = value_at + 1;                              /* just inside the bracket */
    *end = value_end - 1;                               /* the closing bracket */
    return true;
}

bool pim_json_next_element(const char *json, size_t from, size_t span_end,
                           size_t *element_start, size_t *element_end) {
    size_t at = from;
    if (json == NULL) return false;
    /* Step over the separator left by the previous element, and any space. */
    while (at < span_end && (json[at] == ',' || json[at] == ' ' || json[at] == '\t' ||
                             json[at] == '\n' || json[at] == '\r')) at++;
    if (at >= span_end) return false;
    {
        size_t end = skip_value(json, at);
        if (end > span_end) end = span_end;             /* truncated input */
        if (end <= at) return false;
        *element_start = at;
        *element_end = end;
    }
    return true;
}

bool pim_json_blank_member(char *json, const char *key) {
    size_t name_at, colon_at, value_end, i;
    if (json == NULL || key == NULL) return false;
    if (!find_member(json, key, &name_at, &colon_at)) return false;
    value_end = skip_value(json, colon_at + 1);

    /* One separator has to go with it, or two would end up adjacent. The one
       before is preferred; a member that was first in its object has none, so
       the one after it is taken instead. */
    {
        size_t back = name_at;
        while (back > 0 && (json[back - 1] == ' ' || json[back - 1] == '\t' ||
                            json[back - 1] == '\n' || json[back - 1] == '\r')) back--;
        if (back > 0 && json[back - 1] == ',') {
            json[back - 1] = ' ';
        } else {
            size_t forward = skip_spaces(json, value_end);
            if (json[forward] == ',') json[forward] = ' ';
        }
    }
    for (i = name_at; i < value_end; i++) json[i] = ' ';
    return true;
}
