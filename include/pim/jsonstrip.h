#ifndef PIM_JSONSTRIP_H
#define PIM_JSONSTRIP_H

#include <stdbool.h>
#include <stddef.h>

/* Removing named members from a JSON document, in place.

   This exists for one reason. A Tailscale network map for a tailnet of sixteen
   machines is about 32 KB of JSON, and cJSON's tree costs roughly sixty bytes
   per node - so the tree comes to around 114 KB, which this board has not got.
   Most of those nodes are fields the client never reads: the services each peer
   advertises, its host information, its capability map, timestamps. Dropping
   them before parsing removes about three nodes in five, because a subtree of
   small values is cheap in text and expensive in nodes.

   The alternative was to parse the peer list one element at a time, which means
   restructuring the vendored client's netmap handling. This gets most of the
   benefit without touching how the map is read.

   Only members are removed: a key is a string followed by a colon, so a value
   that happens to equal one of the names is left alone. Strings and their
   escapes are respected, so a brace inside a string cannot unbalance it.

   The text is rewritten in place and shortened; the result is NUL-terminated
   and the new length is returned. Invalid JSON is not diagnosed - it is passed
   through as unchanged as possible, and the parser that follows will complain
   with better context than this could. */
size_t pim_json_strip(char *json, const char *const *keys, size_t key_count);


/* ---- walking an array without parsing the document ----

   The other half of fitting a network map into this board. Even trimmed, a tree
   of the whole thing is far larger than the text; a tree of one peer is not. So
   the peer list is found in the text, its elements are handed out one at a time
   to be parsed individually, and only then is the rest of the document parsed -
   with the list blanked, so it costs nothing.

   pim_json_array_span finds a named array and reports the span *inside* its
   brackets. False if there is no such member, or its value is not an array.

   pim_json_next_element walks that span: given where the last element ended it
   reports the next one's bounds, respecting nesting and strings, and returns
   false once the span is used up. Elements may be any JSON value.

   pim_json_blank_member overwrites a named member - its name, its value and one
   adjacent comma - with spaces, leaving a document of the same length that still
   parses and no longer has that member. The comma matters: blanking the pair on
   its own would leave two separators side by side, which is not JSON. Used so
   the rest of the document can be parsed with the expensive part already dealt
   with. */
bool pim_json_array_span(const char *json, const char *key, size_t *start, size_t *end);
bool pim_json_next_element(const char *json, size_t from, size_t span_end,
                           size_t *element_start, size_t *element_end);
bool pim_json_blank_member(char *json, const char *key);

#endif
