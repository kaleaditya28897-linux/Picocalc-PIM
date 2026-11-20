#ifndef PIM_HOSTS_H
#define PIM_HOSTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Saved connections, kept in /PIM/hosts.cfg: an alias, where to dial and which
   SSH key to offer. Hardware-free and storage-agnostic apart from the two file
   calls at the bottom.

   `relay` exists because the PicoCalc cannot join a Tailscale network itself:
   there is no WireGuard on the device. A machine on the LAN that *is* on the
   tailnet forwards a local port to the tailnet host (ssh -L, socat, or
   `tailscale configure`-style forwarding), and the alias records both the real
   name, which is what SSH authenticates and what the user recognises, and the
   relay actually dialled. */

#define PIM_HOST_MAX 12
#define PIM_HOST_ALIAS_MAX 20
#define PIM_HOST_ADDRESS_MAX 64
#define PIM_HOST_USER_MAX 24
#define PIM_HOST_KEY_MAX 24

typedef enum { PIM_HOST_TELNET = 0, PIM_HOST_SSH = 1 } pim_host_kind_t;

typedef struct {
    char alias[PIM_HOST_ALIAS_MAX + 1];
    char address[PIM_HOST_ADDRESS_MAX + 1];
    char user[PIM_HOST_USER_MAX + 1];       /* SSH login; unused by telnet */
    char key[PIM_HOST_KEY_MAX + 1];         /* name of a key in /PIM/ssh */
    char relay[PIM_HOST_ADDRESS_MAX + 1];   /* LAN host that reaches `address` */
    uint16_t port, relay_port;
    uint8_t kind;                           /* pim_host_kind_t */
} pim_host_t;

typedef struct {
    size_t count;
    pim_host_t hosts[PIM_HOST_MAX];
} pim_hosts_t;

void pim_hosts_init(pim_hosts_t *list);
void pim_host_init(pim_host_t *host, pim_host_kind_t kind);

/* True when the record is complete enough to dial and to write back out. */
bool pim_host_valid(const pim_host_t *host);

/* Where a connection actually goes: the relay when one is set, else the address. */
void pim_host_dial(const pim_host_t *host, const char **address, uint16_t *port);

/* "name  ssh bob@box:22 via 10.0.0.9:2200", clipped to out_size. */
void pim_host_summary(const pim_host_t *host, char *out, size_t out_size);

/* Stores a record, replacing any with the same alias (case-insensitive) and
   otherwise appending. False when the record is invalid or the list is full. */
bool pim_hosts_put(pim_hosts_t *list, const pim_host_t *host);
bool pim_hosts_remove(pim_hosts_t *list, size_t index);
const pim_host_t *pim_hosts_find(const pim_hosts_t *list, const char *alias);

bool pim_hosts_load(const char *path, pim_hosts_t *list);
bool pim_hosts_save(const char *path, const pim_hosts_t *list);

#endif
