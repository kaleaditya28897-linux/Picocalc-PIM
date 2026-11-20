#include "pim/hosts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_TELNET_PORT 23
#define DEFAULT_SSH_PORT 22

static void copy_text(char *out, size_t size, const char *text) {
    size_t length = strlen(text);
    if (length >= size) length = size - 1;
    memcpy(out, text, length);
    out[length] = '\0';
}

/* Config lines are tab separated, so a field may hold neither tabs nor newlines
   and nothing outside printable ASCII. */
static bool field_ok(const char *text, size_t limit, bool allow_empty) {
    size_t i, length = strlen(text);
    if (length > limit) return false;
    if (length == 0) return allow_empty;
    for (i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x20 || c > 0x7e) return false;
    }
    return true;
}

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }

static bool same_alias(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (lower((unsigned char)*a) != lower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

void pim_host_init(pim_host_t *host, pim_host_kind_t kind) {
    memset(host, 0, sizeof(*host));
    host->kind = (uint8_t)kind;
    host->port = kind == PIM_HOST_SSH ? DEFAULT_SSH_PORT : DEFAULT_TELNET_PORT;
}

void pim_hosts_init(pim_hosts_t *list) {
    memset(list, 0, sizeof(*list));
}

bool pim_host_valid(const pim_host_t *host) {
    if (!field_ok(host->alias, PIM_HOST_ALIAS_MAX, false)) return false;
    if (!field_ok(host->address, PIM_HOST_ADDRESS_MAX, false)) return false;
    if (!field_ok(host->user, PIM_HOST_USER_MAX, true)) return false;
    if (!field_ok(host->key, PIM_HOST_KEY_MAX, true)) return false;
    if (!field_ok(host->relay, PIM_HOST_ADDRESS_MAX, true)) return false;
    if (host->port == 0) return false;
    if (host->kind > PIM_HOST_SSH) return false;
    /* A relay needs a port of its own, and a port alone would go nowhere. */
    if (host->relay[0] != '\0' && host->relay_port == 0) return false;
    if (host->relay[0] == '\0' && host->relay_port != 0) return false;
    return true;
}

void pim_host_dial(const pim_host_t *host, const char **address, uint16_t *port) {
    bool relayed = host->relay[0] != '\0' && host->relay_port != 0;
    if (address != NULL) *address = relayed ? host->relay : host->address;
    if (port != NULL) *port = relayed ? host->relay_port : host->port;
}

void pim_host_summary(const pim_host_t *host, char *out, size_t out_size) {
    char target[PIM_HOST_ADDRESS_MAX + PIM_HOST_USER_MAX + 16];
    if (out_size == 0) return;
    if (host->kind == PIM_HOST_SSH && host->user[0] != '\0')
        snprintf(target, sizeof(target), "%s@%s:%u", host->user, host->address, host->port);
    else
        snprintf(target, sizeof(target), "%s:%u", host->address, host->port);
    if (host->relay[0] != '\0' && host->relay_port != 0)
        snprintf(out, out_size, "%s  %s %s via %s:%u", host->alias,
                 host->kind == PIM_HOST_SSH ? "ssh" : "telnet", target,
                 host->relay, host->relay_port);
    else
        snprintf(out, out_size, "%s  %s %s", host->alias,
                 host->kind == PIM_HOST_SSH ? "ssh" : "telnet", target);
}

const pim_host_t *pim_hosts_find(const pim_hosts_t *list, const char *alias) {
    size_t i;
    for (i = 0; i < list->count; i++) {
        if (same_alias(list->hosts[i].alias, alias)) return &list->hosts[i];
    }
    return NULL;
}

bool pim_hosts_put(pim_hosts_t *list, const pim_host_t *host) {
    size_t i;
    if (!pim_host_valid(host)) return false;
    for (i = 0; i < list->count; i++) {
        if (same_alias(list->hosts[i].alias, host->alias)) {
            list->hosts[i] = *host;
            return true;
        }
    }
    if (list->count >= PIM_HOST_MAX) return false;
    list->hosts[list->count++] = *host;
    return true;
}

bool pim_hosts_remove(pim_hosts_t *list, size_t index) {
    size_t i;
    if (index >= list->count) return false;
    for (i = index; i + 1 < list->count; i++) list->hosts[i] = list->hosts[i + 1];
    list->count--;
    memset(&list->hosts[list->count], 0, sizeof(list->hosts[list->count]));
    return true;
}

/* ------------------------------------------------------------------- files */

static char *split(char *text) {
    char *tab = strchr(text, '\t');
    if (tab == NULL) return NULL;
    *tab = '\0';
    return tab + 1;
}

static unsigned long port_of(const char *text) {
    char *end;
    unsigned long value = strtoul(text, &end, 10);
    if (*end != '\0' || value > 65535u) return 0;
    return value;
}

bool pim_hosts_load(const char *path, pim_hosts_t *list) {
    char line[320];
    FILE *file;
    pim_hosts_init(list);
    file = fopen(path, "rb");
    if (file == NULL) return false;
    while (fgets(line, sizeof(line), file) != NULL) {
        pim_host_t host;
        char *rest, *field;
        unsigned long port;
        size_t length = strlen(line);
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[--length] = '\0';
        if (strncmp(line, "host=", 5) != 0) continue;
        pim_host_init(&host, PIM_HOST_TELNET);
        rest = line + 5;
        /* alias \t kind \t address \t port \t user \t key \t relay \t relay port */
        field = split(rest); if (field == NULL) continue;
        copy_text(host.alias, sizeof(host.alias), rest);
        rest = field; field = split(rest); if (field == NULL) continue;
        host.kind = (uint8_t)(strcmp(rest, "ssh") == 0 ? PIM_HOST_SSH : PIM_HOST_TELNET);
        rest = field; field = split(rest); if (field == NULL) continue;
        copy_text(host.address, sizeof(host.address), rest);
        rest = field; field = split(rest); if (field == NULL) continue;
        port = port_of(rest);
        if (port == 0) continue;
        host.port = (uint16_t)port;
        rest = field; field = split(rest); if (field == NULL) continue;
        copy_text(host.user, sizeof(host.user), rest);
        rest = field; field = split(rest); if (field == NULL) continue;
        copy_text(host.key, sizeof(host.key), rest);
        rest = field; field = split(rest); if (field == NULL) continue;
        copy_text(host.relay, sizeof(host.relay), rest);
        host.relay_port = (uint16_t)port_of(field);
        if (host.relay[0] == '\0') host.relay_port = 0;
        /* A malformed record is dropped rather than allowed to break the list. */
        pim_hosts_put(list, &host);
    }
    fclose(file);
    return true;
}

bool pim_hosts_save(const char *path, const pim_hosts_t *list) {
    char temporary[160];
    FILE *file;
    bool ok = true;
    size_t i;
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    file = fopen(temporary, "wb");
    if (file == NULL) return false;
    for (i = 0; i < list->count && ok; i++) {
        const pim_host_t *host = &list->hosts[i];
        if (!pim_host_valid(host)) continue;
        ok = fprintf(file, "host=%s\t%s\t%s\t%u\t%s\t%s\t%s\t%u\n",
                     host->alias, host->kind == PIM_HOST_SSH ? "ssh" : "telnet",
                     host->address, host->port, host->user, host->key,
                     host->relay, host->relay_port) > 0;
    }
    if (fclose(file) != 0 || !ok) { remove(temporary); return false; }
    remove(path);
    return rename(temporary, path) == 0;
}
