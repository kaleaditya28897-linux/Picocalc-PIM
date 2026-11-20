# MicroLink, vendored

Upstream: <https://github.com/CamM2325/microlink>
Commit: `ee5a2ba` (2026-11-17)
Licence: MIT (`LICENSE`), plus `x25519-license.txt` for the bundled X25519 code.

An unofficial Tailscale-compatible client for microcontrollers: the ts2021
control protocol, WireGuard on lwIP, DISCO/STUN NAT traversal and DERP relay
fallback. Upstream targets ESP-IDF.

## What is modified, and why

The port lives outside this directory, in `src/firmware/esp_compat/` (headers
shaped like the ESP-IDF ones upstream expects) and `src/firmware/ts_platform.c`
(their Pico implementations). Keeping upstream pristine is what makes pulling
their fixes a matter of recopying files rather than replaying a patch set, so
the deviations are few, marked with a `PicoDeck` comment, and listed here. Each
is a thing that could not be done from outside this directory.

Checked with `diff --strip-trailing-cr` against the upstream checkout:

| File | Lines | What and why |
| --- | --- | --- |
| `src/ml_coord.c` | 454 | The network map is trimmed of fields nothing here reads, and the peer list is parsed one element at a time. A cJSON tree of a whole 32 KB map wants about 87 KB; this board has 520 KB in total and other things in it. The per-peer loop body was moved out verbatim into a function so it can be driven element by element. The long-poll buffer grows by what is needed rather than doubling, with `realloc`: doubling took a 33 KB map's buffer from 32 KB to 64 KB and the copy needed 96 KB at once, on a board with about 70 KB free, and that failed allocation was reported as the message being too big - the intermittent "exceeds 64 KB buffer" reconnect. The two failures are now reported apart. |
| `src/ml_tcp.c` | 76 | A bounded connect. `SO_SNDTIMEO` was set expecting it to limit `connect()`; on lwIP it does not. The socket is made non-blocking with `FIONBIO` (lwIP's `fcntl` flag values differ from newlib's, which is why `fcntl` is not used), and `select` waits out the timeout. |
| `src/ml_derp.c` | 28 | The relay is reached by the address the DERPMap supplies rather than by its host name, because resolving `derp*.tailscale.com` failed here every time while the control plane's own name resolved fine. Also: a failed read reporting no error is a receive timeout, not a dead connection. |
| `include/microlink_internal.h` | 6 | Two DERP nodes per region instead of four. The client only ever uses the first usable one, and the other two were 8 KB reserved permanently. |

Everything else here is upstream's, byte for byte, line endings included -
`.gitattributes` keeps git from normalising them, so a diff against upstream
shows only the above.

If anything else ever has to change, record it here with the reason, so the next
person knows the difference is deliberate. When updating, re-apply these blocks
or drop them where upstream has grown its own answer - a streaming parser in
place of the first one would be the better fix.

## Things worth knowing about the port

- **The lwIP core lock.** The client asks `sys_thread_tcpip(LWIP_CORE_LOCK_QUERY_HOLDER)`
  whether the calling task holds lwIP's core lock, and if so sends straight
  from that task instead of posting to the tcpip thread. On the Pico the core
  lock is the cyw43 async context's recursive mutex, so `ts_platform.c` answers
  by asking that mutex who holds it. An earlier version answered "is this the
  tcpip thread", which is a different question: a connect made from the UI task
  then waited on the tcpip thread while holding the lock it needed, and the
  device froze on the first direct (non-relay) connection.
- **Certificates.** The client is built without a certificate bundle, so it
  warns that relay TLS certificates are not verified. That is acceptable for
  the same reason it is in Tailscale itself: the control plane is
  authenticated by the Noise handshake against Tailscale's pinned server key,
  whatever the transport, and DERP relays are untrusted by design - everything
  carried over them is WireGuard-encrypted to the peer.
- **Warnings.** The client is compiled with `-w`, as upstream's code is not
  written for this compiler's warning set. Compiled with `-Wall -Wextra` it
  gives seven: three unused variables, two `strncpy` truncations that are
  terminated or zeroed beforehand, one `stringop-overread` in X25519 that
  passes the true array length, and the certificate warning above. All were
  read and none is a defect; re-check after updating.

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
