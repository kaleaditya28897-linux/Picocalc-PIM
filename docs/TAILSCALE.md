# Putting the PicoCalc on a tailnet

Goal: the device gets its own `100.x.y.z` address and reaches the whole
infrastructure directly, instead of going through a LAN relay.

This is a port, not a new implementation. [MicroLink](https://github.com/CamM2325/microlink)
(MIT, C, actively developed) already implements a Tailscale-compatible client
for microcontrollers: the ts2021 Noise control protocol, registration by auth
key or login URL, map updates over a long-poll HTTP/2 stream, WireGuard on lwIP,
DISCO/STUN NAT traversal and DERP relay fallback. Its maintainer reports a direct
WireGuard path against hosted Tailscale and against Headscale over plain HTTP.

Figures below are from commit `ee5a2ba` (2026-11-17).

## What has to change, and what does not

The platform coupling is much lighter than the file sizes suggest. `ml_coord.c`
is 3,559 lines with 225 mentions of `esp_*`, but 204 of those are the `ESP_LOGx`
macros and the only real call in the file is `esp_timer_get_time()`.

Across the subset we need, the entire ESP-IDF API surface is:

| ESP-IDF | Pico replacement |
| --- | --- |
| `esp_timer_get_time()` | `time_us_64()` |
| `esp_fill_random()` | `get_rand_64()` over the RP2350 TRNG (see the caveat below) |
| `esp_read_mac()` | `cyw43_hal_get_mac()` |
| `esp_netif_*` (address, flags, link state, IPv6 link-local) | lwIP `netif` directly |
| `esp_crc32_le()` | a small CRC-32 in the shim |
| `esp_crt_bundle_attach()` | an mbedTLS CA bundle, or `ML_DERP_VERIFY_CERT=n` |
| `esp_vfs_eventfd_register()` | only needed by the `select()` wakeup; goes away under the raw-API option |
| NVS (`ml_peer_nvs.c`, 339 lines) | the peer cache as a file under `/PIM` |
| `ESP_LOGx`, `esp_err_t`, `ESP_OK` | a macro and a typedef in the shim |

Files that are already platform-free and port untouched: `nacl_box.c` (NaCl
crypto), `ml_noise.c` (Noise IK), `ml_h2.c` (HTTP/2), `ml_filter.c` (ACLs),
`ml_routes.c`, `ml_at_validate.c`. `wireguard_lwip` has a proper platform file
(`wireguard-platform-esp32.c`, 17 call sites) that we simply write our own
version of.

Files we do **not** need: `ml_cellular.c`, `ml_at_socket.c`, `ml_at_validate.c`,
`ml_config_httpd.c` + `ml_config_html.h` (its own web UI; this firmware has one),
`ml_net_switch.c`, `ml_ota.c`, `ml_nat66.c`. That removes roughly 6,000 lines.

## Threads and sockets: decided, option A

MicroLink runs four FreeRTOS tasks and does its network I/O with BSD sockets and
a `select()` loop:

| Task | Stack | Job |
| --- | --- | --- |
| `coord_task` | 12 KB | control plane: Noise, HTTP/2, registration, long poll |
| `derp_tx_task` | 14 KB | writes queued packets to the DERP relay over TLS |
| `net_io_task` | 8 KB | one `select()` loop for DISCO, STUN and WireGuard |
| `wg_mgr_task` | 8 KB | WireGuard handshakes, DISCO ping/pong, peers |

This firmware links `pico_cyw43_arch_lwip_poll`, which is lwIP with `NO_SYS=1`:
no threads, and **no BSD sockets at all**. So one of these has to happen.

**Option A — adopt FreeRTOS (recommended).** Link
`pico_cyw43_arch_lwip_sys_freertos` instead, so lwIP runs with `NO_SYS=0` and
sockets exist. MicroLink's tasks and `select()` loop then port with almost no
change to its logic, and future upstream fixes stay easy to merge. The cost is
contained but real: LVGL is not thread-safe, so every LVGL call has to stay on
one task, and SD access needs a mutex. Pinning every task to one core keeps it
deterministic; the second core stays available later.

**Option B — stay on the poll loop.** Keep `NO_SYS=1` and rewrite the I/O onto
lwIP's raw API (`udp_*`, `tcp_*` callbacks), turning the four tasks into state
machines driven from the existing `pim_wifi_poll()`. Nothing about the rest of
the firmware changes. The cost lands inside MicroLink: `ml_net_io.c`, `ml_tcp.c`,
`ml_udp.c` and the DERP writer all assume blocking reads, and the coordination
long-poll is written as straight-line blocking code that has to be inverted.
That also makes every future upstream merge a manual job.

**Option A was chosen and is built** (stage 0 below). Option B is recorded here
only so the reasoning survives: it changes their logic rather than our
scaffolding, which costs more over time because every upstream merge becomes
manual.

## Memory: the real constraint

Measured, not estimated. A release build of the current firmware
(`arm-none-eabi-size`) reports:

```
   text	   data	    bss
1158228	      0	 414632
```

Flash is a non-issue: 1.13 MB of 4 MB. **SRAM is the problem.** 405 KB of the
RP2350's 520 KB is already committed before any of this is added, leaving about
105 KB for the malloc heap and stacks. The largest consumers:

| Static allocation | Size | Note |
| --- | --- | --- |
| `work_mem_int` (LVGL heap) | 160.0 KB | `PIM_LV_MEM_KB`; the vendor default of 48 was too small for a 96-item list |
| `database` | 48.9 KB | the user's own data; stays |
| `ram_heap` (lwIP) | 48.0 KB | `MEM_SIZE`; WireGuard wants this bigger, not smaller |
| web `response` + `request` | 29.0 KB | only live while the web server runs |
| `memp_memory_PBUF_POOL` | 23.9 KB | `PBUF_POOL_SIZE`; also wants to grow |
| editor `text.0`/`text.3`/`text.1`/`journal_text` | 32.0 KB | one screen at a time uses these |
| `vt` (terminal) | 13.6 KB | screen, alt screen and 96 lines of scrollback |
| `ui` | 11.6 KB | |

What MicroLink needs, with the buffers tuned for a small tailnet rather than
left at their defaults:

| Item | Default | Tuned | Note |
| --- | --- | --- | --- |
| `ML_H2_BUFFER_SIZE_KB` | 512 | 32 | the Kconfig floor is 64 (~30 peers); a handful of peers needs far less, so the floor wants patching |
| `ML_JSON_BUFFER_SIZE_KB` | 512 | 32 | cJSON's DOM runs 2-3x the raw JSON |
| Task stacks | 42 KB | 42 KB | four tasks |
| mbedTLS session (DERP) | — | 20-40 KB | |
| Peers, endpoints, misc | — | ~10 KB | `ML_MAX_PEERS` 4-8 at ~200 B each |
| **Total** | | **~150 KB** | |

So roughly 150 KB needed against roughly 105 KB free: **it does not fit as
things stand.** About 50-60 KB has to be reclaimed first, and the candidates are
known:

- `PIM_LV_MEM_KB` 160 → 112 frees 48 KB. Needs checking against the case that
  forced it up: a 96-item to-do list.
- The web server's 29 KB of request/response buffers are dead weight whenever
  the web server is off, which is most of the time. Allocating them on demand,
  or sharing one buffer with the terminal, frees most of that.
- The editor's four text buffers total 32 KB for screens that are never both
  open. Sharing them frees around 20 KB.
- Terminal scrollback at 96 lines costs 7.7 KB; 48 lines would halve that.

That is enough headroom, but it is not comfortable, and lwIP's own pools want to
*grow* for a tunnel (every tunnelled packet is also an underlay packet). Treat
each step as measured rather than assumed: `arm-none-eabi-size` and
`arm-none-eabi-nm --print-size --size-sort` after every change, and build with
`-DPIM_ENABLE_TAILSCALE=ON` to see the real figure.

If it still will not fit, the biggest single lever is replacing cJSON's DOM with
a streaming parser for `MapResponse`, which would remove the 32 KB JSON buffer
and most of the H2 buffer with it.

## Security notes

- WireGuard keys need real entropy. The RP2350 has a hardware TRNG and the SDK
  exposes it through `pico_rand`, but the SDK samples the ring oscillator
  directly without the entropy checks the datasheet describes. The shim should
  read the TRNG the way the datasheet prescribes rather than calling
  `get_rand_64()` and hoping.
- `ML_DERP_VERIFY_CERT=n` removes the CA bundle from flash. Relayed traffic is
  still WireGuard-encrypted end to end, so this is not a confidentiality
  problem, but it lets something in the middle impersonate a relay and drop or
  delay traffic. Keep it on unless flash forces the issue.
- A Tailscale auth key compiled into the firmware ends up in the UF2. Put it on
  the SD card instead, or use the interactive login flow and show the URL on
  screen. Reusable keys survive re-registration; ephemeral ones do not.
- MicroLink cannot renew an expired node key after an interactive login, so
  disable key expiry for this device in the admin console.

## What it costs now that it links

Measured with `cmake --preset pico2w-tailscale`:

| | flash | .bss | free SRAM |
| --- | --- | --- | --- |
| default, polled lwIP | 1123.9 KB | 408.9 KB | 111.1 KB |
| FreeRTOS, sockets, and the whole Tailscale client | 1185.4 KB | 447.0 KB | **73.0 KB** |

**73 KB free is not enough to start it.** The control-plane buffers (32 + 32 KB
as configured), the four task stacks (42 KB) and an mbedTLS session (20-40 KB)
all come from the heap at run time, so they do not appear in `.bss` above: the
requirement is roughly 150 KB against 73 KB available. The firmware would reach
`vApplicationMallocFailedHook` and stop - loudly, which is the point of having
it, but stop.

So stage 4 is not optional, and the target is now exact: **free about 80 KB.**
The candidates, with measured sizes, are in the table further down.

## What the foundation alone cost

Measured, with `-DPIM_ENABLE_TAILSCALE=ON` (`cmake --preset pico2w-tailscale`),
before any Tailscale code exists:

| | flash | .bss | free SRAM |
| --- | --- | --- | --- |
| default, polled lwIP | 1131 KB | 405.0 KB | 115 KB |
| FreeRTOS + lwIP sockets | 1149 KB | 439.5 KB | 80.5 KB |
| difference | +18.2 KB | +34.6 KB | |

So the scaffolding costs 35 KB of RAM, and MicroLink's ~150 KB has to come from
the 80.5 KB left plus the 70-80 KB the reclamation list above can free. The two
numbers do meet, with little to spare.

## Staged plan

0. **Done: the FreeRTOS foundation, the vendored client and the shim.** The
   whole thing compiles and links: `cmake --preset pico2w-tailscale` produces a
   UF2. It will not run yet - see the measurement below - but every build-time
   question is now answered.

   The foundation: `PIM_ENABLE_TAILSCALE=OFF` by default, so
   the shipping firmware is untouched. With it on, the build links
   `pico_cyw43_arch_lwip_sys_freertos`, lwIP runs with `NO_SYS=0` and sockets,
   `main()` moves the LVGL loop into a task (the only task allowed to touch
   LVGL), and every lwIP call in `wifi.c` outside a callback is bracketed with
   `cyw43_arch_lwip_begin()`/`_end()` - no-ops in the polled build, the core lock
   in this one. Both configurations build; neither has run on hardware yet.
1. **Done: vendored** into `third_party/microlink/` at commit `ee5a2ba`, 17.5k
   lines, unmodified, with `PROVENANCE.md` listing what is in, what is out and
   how to update. cJSON 1.7.18 came with it.
2. **Done: the shim.** `src/firmware/esp_compat/` holds headers shaped like the
   ESP-IDF ones upstream includes, and `src/firmware/ts_platform.c` implements
   them. Upstream needed no edits at all.
3. **Done: the peer cache on SD**, by implementing the NVS blob API over files
   rather than touching `ml_peer_nvs.c`.
4. **Done: reclaimed the RAM**, measuring each step. The Tailscale build went
   from 48 KB free to 139 KB:

   | Change | Freed |
   | --- | --- |
   | The web server's request and response buffers onto the heap, held only while it is listening | 29 KB |
   | Four web handler scratch buffers sharing one, since the server serves one request at a time | 20 KB |
   | LVGL's heap 160 KB to 112 KB, in this build only - the shipping firmware keeps 160 | 48 KB |
   | Terminal scrollback 96 lines to 48 | 4 KB |

   The web server also hands its 29 KB over for the duration when the client
   starts (`pim_wifi_web_stop`), which puts about 168 KB in front of it.

   What it needs was measured rather than guessed: a synthetic MapResponse
   shaped like a 16-peer tailnet is 14.3 KB of JSON and becomes a **53.7 KB
   cJSON DOM - 3.76x the raw text**, peaking at 68 KB with both alive at once.
   Add 42 KB of task stacks and roughly 30 KB for an mbedTLS session and the
   requirement is about 141 KB. Real peer records carry more fields than the
   synthetic ones, so the margin is thinner than the numbers suggest, and if a
   netmap does not fit the answer is to parse `Peers[]` one element at a time
   (about 20 KB peak) rather than to shrink anything else.
5. **Done: wired into the UI.** Wi-Fi & web -> Tailscale: connect, disconnect,
   the state, the `100.x` address, the login address when the control plane
   wants one, and free memory, which is the number that explains a failure to
   start. The button only appears where a client exists. `/PIM/tailscale.cfg`
   holds the auth key, the device name and the home DERP region - read at run
   time, so the region is not a build-time constant any more.
6. **Done: dialling over the tunnel.** `pim_telnet_open` sends anything in
   `100.64.0.0/10` through `microlink_tcp_*` instead of lwIP, resolving names
   through the control plane's DNS, so a saved SSH connection to a tailnet
   machine needs no relay and neither the terminal nor the SSH client knows the
   difference.
7. **Bring-up on hardware.** The first run froze on the startup screen: no
   USB, no watchdog reboot, the display holding its last frame. The cause was
   not memory, which is what everything above had been arranged against.

   `pim_ui_start()` joins Wi-Fi as part of starting the interface, and `main()`
   calls it **before `vTaskStartScheduler()`**. In the polled build that is
   fine. Here the radio driver's worker task is created by `cyw43_arch_init()`
   and cannot run until the scheduler does, so the join waited on an answer
   that could never come. The one boot that did come up had the card out:
   no `wifi.cfg`, so no join, so no hang.

   Four things had to change, and three of them are about never being in this
   position again:

   - The Wi-Fi auto-start is now a one-shot LVGL timer, so it runs inside
     `lv_timer_handler` - the main loop in one build, the UI task in the other,
     after the scheduler in both.
   - **The startup report** (`pim/boottrace.h`): each stage leaves a breadcrumb
     in a watchdog scratch register, which survives a reset. A failed
     assertion, a stack overflow, an allocation that cannot be met or a hard
     fault records itself and **reboots** rather than spinning with interrupts
     off, and the next start says what happened, on screen, and skips the stage
     that killed the last one. A device with no console that freezes tells you
     nothing; one that comes back and reports is worth four hypotheses.
   - The watchdog is no longer **disabled** around the fifteen-second join. The
     idle task feeds it instead, which covers the blocking call while still
     firing if everything stops. Disabling it was what turned a fault into a
     device that stayed frozen.
   - `cyw43_wifi_scan`, `cyw43_wifi_scan_active` and `cyw43_wifi_leave` go
     straight into the driver and were called without its lock, which only
     matters once the driver has a task of its own. The SDK checks this in
     debug builds only, so a release build does not complain - it races. Task
     priorities were also inverted (lwIP above the driver); they are now
     idle < UI < lwIP < driver < timer.

   What remains is the next attempt: flash, watch the USB log, and confirm
   registration, a `100.x` address and `tailscale ping` from a laptop. Memory
   is still the expected failure, and it now reports itself as a number.

Steps 1-6 are verified from the build host: 8 host suites and the headless UI
run, with the Tailscale screen covered by a stand-in client. Step 7 needs the
device, a `tailscale` CLI on a laptop and the USB log, so it cannot be claimed
from a build host at all. The Tailscale build enables USB stdio for exactly
that reason; the shipping build does not.

## What works today, without any of this

A connection can name a **relay**: a LAN machine that forwards to the real host.
`tools/pim-hosts-import.py --relay HOST --print-forwards` writes the device's
`hosts.cfg` from this machine's `~/.ssh/config` and prints the forwarding
commands to run on the relay. See the Terminal section of the README.
