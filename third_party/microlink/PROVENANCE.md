# MicroLink, vendored

Upstream: <https://github.com/CamM2325/microlink>
Commit: `ee5a2ba` (2026-11-17)
Licence: MIT (`LICENSE`), plus `x25519-license.txt` for the bundled X25519 code.

An unofficial Tailscale-compatible client for microcontrollers: the ts2021
control protocol, WireGuard on lwIP, DISCO/STUN NAT traversal and DERP relay
fallback. Upstream targets ESP-IDF.

## One file is modified; the rest are not

The port lives outside this directory, in `src/firmware/esp_compat/` (headers
shaped like the ESP-IDF ones upstream expects) and `src/firmware/ts_platform.c`
(their Pico implementations). Keeping upstream pristine is what makes pulling
their fixes a matter of recopying files rather than replaying a patch set, so
there is exactly one deviation:

**`src/ml_coord.c`** — one block added immediately before the network map is
parsed, which drops member names nothing in the client reads before cJSON sees
them. The reason is memory, and it is not a preference: a sixteen-machine
tailnet sends about 32 KB of JSON, cJSON's tree of it comes to about 114 KB,
and this board has roughly 110 KB free at that moment. The parse failed part
way through and the client retried for ever. The fields removed - `Services`,
`CapMap`, `Capabilities`, `Hostinfo`, `LastSeen`, `Created`, `UserProfiles`,
`SSHPolicy`, `Debug`, `ControlDialPlan`, `Tags`, `ComputedName` - are subtrees
of small values, which are cheap as text and expensive as nodes, so removing
them takes about three nodes in five out of the tree. Everything the client
reads is kept, which was checked field by field against its own source:
`Node`, `Peers`, `DERPMap`, `PacketFilter`, `DNSConfig`, the node and disco
keys, the addresses, allowed IPs, endpoints, `DERP`, `Online` and `KeyExpiry`.

The removal itself is ours and lives outside this tree, in
`src/core/jsonstrip.c`, with its own tests; the block here only calls it. When
updating, re-apply that block - it is marked with a `PicoDeck change` comment -
or drop it if upstream grows a streaming parser, which would be the better fix.

If anything else here ever has to change, record it here with the reason, so
the next person knows the difference is deliberate.

## What is included

| Area | Files |
| --- | --- |
| Control plane (ts2021) | `ml_coord.c`, `ml_coord_tls.c`, `ml_h2.c`, `ml_noise.c`, `ml_noise_aead.h` |
| WireGuard management | `ml_wg_mgr.c`, `wireguard_lwip/` |
| NAT traversal | `ml_stun.c`, `ml_derp.c` |
| Sockets over the tunnel | `ml_udp.c`, `ml_tcp.c`, `ml_net_io.c`, `ml_zerocopy.c` |
| Crypto | `nacl_box.c`, `wireguard_lwip/src/crypto/` |
| Peers, routes, ACLs | `ml_peer_nvs.c`, `ml_routes.c`, `ml_filter.c` |
| Entry points | `microlink.c`, `include/` |

`wireguard_lwip/src/wireguard-platform-esp32.c` is left out: its four functions
are implemented in `src/firmware/ts_platform.c` instead.

## What is left out, and why

| Files | Why |
| --- | --- |
| `ml_cellular.c`, `ml_at_socket.c`, `ml_at_validate.c` | SIMCom modem support; this device has Wi-Fi only |
| `ml_config_httpd.c`, `ml_config_html.h` | its own web UI; the firmware already has one |
| `ml_net_switch.c` | Wi-Fi/cellular failover, which needs the modem |
| `ml_ota.c` | firmware updates over the tailnet; this device updates by UF2 |
| `ml_nat66.c` | IPv6 NAT for exit-node use, which is not a goal here |

That removes roughly 6,000 lines. The excluded features are all switched off in
`src/firmware/esp_compat/sdkconfig.h`, so nothing left in the tree references
them.

## Dependencies this needs from elsewhere

- lwIP with `NO_SYS=0` and BSD sockets, and its `compat/posix` headers for
  `sys/socket.h`, `arpa/inet.h`, `netdb.h` and `sys/select.h`. Supplied by
  `pico_cyw43_arch_lwip_sys_freertos`.
- FreeRTOS (tasks, queues, event groups, semaphores): `third_party/FreeRTOS-Kernel`.
- mbedTLS for the DERP relay's TLS, including `net_sockets.c`: ships with the
  Pico SDK.
- cJSON for `MapResponse`: `third_party/cJSON` (MIT, v1.7.18).

## Updating

1. Clone upstream at the new commit somewhere outside the repository.
2. Recopy the files listed above; do not copy the excluded ones.
3. Rebuild with `cmake --preset pico2w-tailscale`. The compatibility headers are
   the only place a new ESP-IDF dependency can appear, so a new upstream call
   shows up as a missing declaration there rather than as a silent behaviour
   change.
4. Update the commit above.
