# PicoDeck OS (PicoCalc PIM)

PicoCalc PIM is a C11 firmware PDA for the ClockworkPi PicoCalc with a Raspberry Pi Pico 2 W. It uses the official PicoCalc LVGL display and keyboard drivers, stores its PDA database on a FAT32 SD card, and has an amber-on-black CRT interface.

## Features

- A combined calendar/appointment app: arrow to a day and press Enter, then pick the start hour, minute and length with UP/DOWN (no typing dates), then enter a title and notes. Days with appointments show `*`. Journal entries use the same selected day.
- Priority to-dos, notes, and a free-text journal: one file per day (`/PIM/journal/DD-MM-YYYY.md`) edited like the text editor. Old database journals are migrated automatically.
- Date and time entry: scroll the calendar continuously with the arrows, `,` `.` change month, `<` `>` change year, `T` jumps to today and `G` (or Go to) opens a DD-MM-YYYY picker; in the pickers you can type digits or use UP/DOWN. Set today stores the current date in `/PIM/today.cfg` (the device has no clock).
- Theme switcher (Home → Theme): Amber, Green, Manila, Cyan, Violet, Red, Mono and Paper (light themes: Manila and Paper). The choice is saved in `/PIM/theme.cfg`.
- Sound toggle (Home → Sound: ON/OFF): key clicks and game effects on the speakers, saved in `/PIM/sound.cfg`.
- Startup PIN (Home → Tools → **Device PIN**): six digits, asked for before anything else is reachable. Stored salted and iterated in `/PIM/lock.cfg`, never in the clear. It keeps a person out of your calendar, notes and saved connections; it does **not** encrypt the card, so a card reader still sees everything. Three wrong tries earn a growing wait.
- Battery saving: the backlight dims after 30 s and turns off after 2 min idle (any key wakes it, except during games); Wi-Fi powers off after 15 idle minutes or via Wi-Fi → Wi-Fi off.
- Reliability: an 8 s hardware watchdog reboots a hung device, and large buffers no longer live on the 4 KB stack.
- Battery monitor on Home: percentage and charging state read from the PicoCalc keyboard MCU power gauge (refreshed every 15 s; shows "no data" if unavailable). The MCU only reports a valid level when running on battery first, then plugging in USB.
- Auto start: at boot the PicoCalc scans for a saved Wi-Fi network; if one is in range it joins, syncs the time over NTP and opens Home directly. Otherwise (or if the sync fails) it shows the manual time screen below.
- Startup clock: without a network, every boot asks for the date and time (prefilled from the last session, cannot be skipped). The clock then runs from that moment, is shown on Home, and drives "today" in Calendar and Journal. It is saved to `/PIM/today.cfg`.
- Seven graphical games under Home -> Games: Tetris, Dungeon RPG (8 floors, orcs, a boss, sword/armor pickups), Space Invaders, Solitaire, Snake, Minesweeper and 2048. Each has sound effects, a top-5 score table, and a folder on the SD card (`/PIM/games/<game>/`) with `game.cfg`, `scores.txt` and, for the dungeon, editable `floor1.txt`..`floor8.txt` (16x12 maps; `M` monster, `O` orc, `B` boss, `/` sword, `[` armor). Missing files are created from defaults and never overwritten; to get the new dungeon on an older card, delete `/PIM/games/dungeon/`'s files.
- To-dos have optional due dates; Home -> Today lists today's appointments and due/overdue to-dos. A low-battery banner and sound appear at 10% or less.
- The web server has a file browser with subfolders (up to 3 levels) and `GET /manifest`; `tools/pim-sync.sh pull|push HOST PIN [DIR]` mirrors SD files to or from a laptop.
- Home → Tools: full-text **Search** (appointments, to-dos, notes, journal, files), a scientific **Calculator** (live result while you type, `+ - * /` continues the last answer, UP recalls earlier expressions), a basic stopwatch/**Timer**, **Recurring** appointments (daily/weekly/monthly/yearly), and **Backup**/restore of `/PIM` (newest 5 kept).
- Keyboard-first controls: arrows navigate and control games, `Enter` activates/fires/drops, and `Esc` returns.
- SD file manager and plain-text editor for `.txt`, `.c`, `.h`, and other small text files in `/PIM`.
- SD file manager with folder navigation plus named-file and directory creation/deletion. The 8 px terminal editor shows 40 columns, soft-wraps long lines and pages through files of any size (Prev/Next, 4 KiB per page, edits autosaved when you change page).
- Wi-Fi (Home → Wi-Fi & web): network scanner with on-device password entry, saved networks, NTP time sync with a time zone setting, a browser interface to your PIM, and `.ics` calendar import/export.
- Home → **Terminal**: a VT100/ANSI terminal over **SSH** (curve25519, ed25519 keys, chacha20-poly1305, pinned host keys) or telnet, with saved connections and SSH keys kept on the card.
- **Tailscale**, in the firmware built with `-DPIM_ENABLE_TAILSCALE=ON`: the device joins a tailnet itself over WireGuard, with no relay machine and nothing in the middle. A saved connection to a `100.x` address is then dialled straight through the tunnel, so SSH to a machine anywhere on the tailnet works from the device alone. Wi-Fi & web → Tailscale connects it and shows the address it was given.
- Data is a CRC-checked snapshot at `/PIM/pim.dat`; a failed or missing SD card does not destroy in-memory data.

## Build and flash

Install the ARM Embedded Toolchain, CMake, Ninja, Git, and `picotool`, then initialize the pinned sources:

```bash
git submodule update --init --recursive   # pico-sdk, PicoCalc, pico-vfs, FreeRTOS-Kernel
cmake --preset pico2w-release
cmake --build --preset pico2w-release
```

If you would rather not install a system-wide cross compiler, a self-contained
one works just as well; point the SDK at it and nothing else changes:

```bash
curl -L -o /tmp/arm-gcc.tar.gz https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v15.2.1-1.1/xpack-arm-none-eabi-gcc-15.2.1-1.1-linux-x64.tar.gz
mkdir -p build/toolchain && tar -xzf /tmp/arm-gcc.tar.gz -C build/toolchain
export PICO_TOOLCHAIN_PATH=$PWD/build/toolchain/xpack-arm-none-eabi-gcc-15.2.1-1.1
export PATH=$PICO_TOOLCHAIN_PATH/bin:$PATH
```

There is a second firmware configuration, `cmake --preset pico2w-tailscale`,
which swaps the cooperative LVGL loop for FreeRTOS and lwIP with BSD sockets so
that it can carry the Tailscale client. It works: the device registers with the
control plane, holds a WireGuard tunnel and runs SSH sessions over it. It is
still a **bring-up build** rather than a release - it logs over USB, reports
boot faults on screen, and gives LVGL a smaller heap - so the shipping firmware
remains `pico2w-release`, which carries none of it and is unaffected.

What that cost, and what paid for it, is in
[docs/TAILSCALE.md](docs/TAILSCALE.md): a tailnet of sixteen machines sends a
32 KB network map, cJSON's tree of it wants about 87 KB, and this board has
520 KB in total. The answers were to drop the fields the client never reads, to
parse the peer list one element at a time rather than all at once, and to stop
reserving memory that was not being used - shared LVGL styles instead of
per-widget ones, and the terminal's buffers taken only while a session is open.

The build produces `build/pico2w-release/picocalc_pim.uf2`; a prebuilt, checksummed image is included at `release/picocalc_pim-pico2w.uf2`. Hold BOOTSEL while connecting the Pico 2 W, then run:

```bash
tools/flash-uf2.sh
```

## SD card setup

The storage utility creates one FAT32 partition and its `PIM/` directory. It only accepts a removable whole disk, but it **erases the selected device**:

```bash
tools/prepare-sd.sh --device /dev/sdX
tools/prepare-sd.sh --device /dev/sdX --yes
```

Insert the card before starting the PicoCalc. If the card is absent or unreadable, the PDA continues without persistence for that session.

The firmware also maintains human-readable exports after each PIM change:

| Directory | Format |
| --- | --- |
| `/PIM/appointments/` | `appointments.csv` (`date,time,duration_minutes,title,description`) |
| `/PIM/todos/` | `todos.csv` |
| `/PIM/notes/` | one Markdown file per note |
| `/PIM/journal/` | one `DD-MM-YYYY.md` file per entry |
| `/PIM/calendar/` | `calendar.md` overview |

These are generated views of the crash-safe `pim.dat` database. Edit them for reference or export, but use the PDA interface for PIM changes because the next save regenerates them.

## Wi-Fi and web

Open **Wi-Fi & web** on the home screen.

- **Scan networks** lists nearby networks by signal; pick one and type its password. **Manual entry** is for hidden networks. Joined networks are remembered (up to four) in `/PIM/wifi.cfg` on the SD card, **Join saved** reconnects and **Forget saved** removes one. Passwords are stored in plain text on the SD card.
- **Sync time** joins your most recent saved network if needed and sets the clock from `pool.ntp.org`. **Time zone** sets the UTC offset (default UTC+05:30). The startup screen has a **Sync (Wi-Fi)** button that does the same before you press Start.
- After connecting (and from **Web address**) the screen shows `http://DEVICE:8080` and a 4-digit PIN that changes every connection.

### Browser interface

Open the address on a phone or PC on the same network and enter the PIN. You can view and edit the agenda, to-dos, notes, journal and any text or `.c` file in `/PIM`, download `calendar.ics`, and import an `.ics` file from `/import`. Changes are saved to the SD card straight away and the PDA returns to Home so its lists refresh. After 10 wrong PINs the server locks until you reconnect.

Command-line use needs the PIN as a cookie:

```bash
curl -H 'Cookie: pim=PIN' -T notes.txt http://DEVICE:8080/upload/notes.txt   # up to 8 KiB
curl -H 'Cookie: pim=PIN' http://DEVICE:8080/file/notes.txt -o notes.txt
curl -H 'Cookie: pim=PIN' http://DEVICE:8080/calendar.ics -o pim.ics
curl -H 'Cookie: pim=PIN' --data-binary @phone.ics http://DEVICE:8080/import
```

`.ics` import takes events and to-dos; all times (floating, UTC or TZID) are treated as local time, and duplicates are skipped. Subscribing to a calendar URL is not supported because those are HTTPS and the firmware has no TLS. Export from your phone or calendar service and import the file instead.

The server speaks plain HTTP: the PIN is a speed bump, not encryption, so only use it on a network you trust. Firmware updates still require the BOOTSEL UF2 process.

### Terminal

Home → **Terminal** keeps a list of saved connections in `/PIM/hosts.cfg`. **New** asks for:

| Field | Meaning |
| --- | --- |
| Name | What the list shows, and the title in the session's status line |
| Kind | `telnet` or `ssh`; both connect |
| Address | The real host name, which is what SSH will authenticate |
| Port | 23 for telnet, 22 for ssh |
| User | The SSH login; telnet ignores it |
| SSH key | One of the keys in `/PIM/ssh` |

The session is a real screen terminal, not a line printer: 40 columns by 38 rows
of cells, 48 lines of scrollback, VT100 and ECMA-48 cursor addressing, scrolling
regions, insert/delete, the alternate screen (so full-screen programs such as
`vi` and `mc` draw correctly), DEC line drawing, UTF-8 and 256-colour and
truecolour SGR folded onto the current theme's brightness levels. Box drawing,
blocks and shading are painted directly, since the 8x8 font has no glyphs for
them. It answers cursor-position and device-attribute requests, and negotiates
ECHO, SGA, BINARY, TERMINAL-TYPE (`ansi`, then `vt100`) and NAWS, so the host is
told the real window size instead of assuming 80x24.

Bytes above 127 are read as **CP437**, which is what a BBS draws its boxes and
blocks with, so nothing is lost or guessed at. The two encodings cannot be told
apart by looking (`0xcd 0xbb` is both a valid UTF-8 character and a pair of
CP437 box glyphs), so it is a setting rather than a heuristic:
`pim_vt_set_encoding()` switches a session to UTF-8, which is what an SSH
session to a Linux host will want. In CP437 mode a UTF-8 host's accented
characters appear as two CP437 glyphs; plain ASCII is identical either way.

Keys: **Esc** leaves the session, **Break** sends a real Escape to the host,
**Page Up/Down** browse the scrollback (typing returns to the newest output),
and the arrows, Home, End, Insert, Delete, F1–F10 and Ctrl with a letter all go
to the host as the sequences a VT100 sends. Enter sends CR LF, as NVT asks.
Text is sent in the clear, so treat a telnet session as public.

### Tailscale and SSH keys

The device can join a tailnet itself, in the firmware built with
`-DPIM_ENABLE_TAILSCALE=ON` (`cmake --preset pico2w-tailscale`). **Wi-Fi & web
-> Tailscale** connects and disconnects and shows the state, the `100.x`
address it was given, and how much memory is free - the number that explains a
refusal to start, since parsing a network map needs a large run of it. The web
PIM pauses while the tunnel is up, because the two want the same memory.

What it needs goes on the card, in `/PIM/tailscale.cfg`:

```
authkey=tskey-auth-...   # optional; without it the device asks you to log in
name=picocalc            # the name it appears under on the tailnet
derp=6                   # home relay region; `tailscale netcheck --format=json`
                         # reports PreferredDERP. 0 keeps the default
verifyderp=0             # relay certificates are not checked; see below
```

Two ways to add it to a tailnet:

- **Interactive login, no key.** Leave `authkey` out, or leave the file off the
  card entirely, and press Connect. The control plane answers with a login
  address, which the screen shows in full; open it on any machine, approve the
  device, and it finishes by itself. Nothing secret is ever stored on the card.
- **An auth key.** Admin console -> Settings -> Keys -> Generate auth key. Make
  it **reusable** so a reflash does not need a new one, **pre-approved** if your
  tailnet requires device approval, and give it a tag such as `tag:picocalc` so
  an ACL can limit what it reaches. Do not make it ephemeral, or the node
  disappears on reboot. Put it in `authkey=`.

The key is a secret and the device's own identity - its machine, WireGuard and
DISCO keys, kept in `/PIM/ts` - is more so. The web server refuses to list or
serve either, and `.gitignore` covers both in case a copy ever lands in a
checkout. A key compiled into the firmware would travel in every copy of the
UF2, which is why it is read off the card instead.

Once the tunnel is up, a saved connection whose address is in `100.64.0.0/10`,
or a name the tailnet's DNS knows, is dialled **through the tunnel** - no relay,
no forwards on another machine. Neither the terminal nor the SSH client knows
the difference.

Relayed traffic through DERP stays WireGuard-encrypted end to end, so
`verifyderp=0` does not expose its contents; what it gives up is noticing
something posing as a relay in order to drop or delay packets.

Two numbers worth knowing. The peer table holds **20** machines
(`CONFIG_ML_MAX_PEERS`): a table smaller than the tailnet churns, and a machine
that has been pushed out of it simply cannot be reached - which looks like SSH
failing rather than like a full table. And the relay is reached by the address
the control plane supplies rather than by its name, because resolving
`derp*.tailscale.com` failed on this board every time while the control plane's
own name resolved fine; an address needs no resolver at all.

**A known limitation.** Direct paths work to peers on the local network, and the
relay works for everything, but a direct path to a peer behind a symmetric NAT
can be validated by DISCO and still carry no data - the device then commits to
it and waits instead of falling back to the relay. Peers reached over DERP or a
LAN endpoint are solid; a peer behind such a NAT may hang on first connection
until the mapping is punched from its side. This is the next thing to fix, and
the shape of the fix is Tailscale's own rule: carry data over the relay until a
direct path has actually passed some, then switch.

#### Without the Tailscale firmware: a relay

The shipping firmware does not carry the client, so a tailnet host has to be
reached through a **relay**: a LAN machine that *is* on your tailnet, forwarding
a local port to it.

The connection form on the device has no relay fields - they were taken out when
the tunnel arrived, since the device now joins the tailnet itself and the tunnel
is preferred over a relay whenever it is up. A relay saved by the importer below
is still honoured, and still carried through when you edit a connection on the
device, so existing cards keep working. On a LAN machine, for example:

```bash
ssh -L 2200:nas.tail1a2b.ts.net:22 localhost   # or: socat TCP-LISTEN:2200,fork,reuseaddr TCP:nas.tail1a2b.ts.net:22
```

then save a connection whose address is `nas.tail1a2b.ts.net` and whose relay is
that machine and port. The alias keeps the real name, which is what SSH checks
and what you recognise in the list, while the device dials the relay.

`tools/pim-hosts-import.py` writes `hosts.cfg` from this computer's
`~/.ssh/config`. It asks `ssh -G` what each alias really resolves to, so
Include, Match and defaults are handled by ssh itself, and it clips names, ports
and key names to the device's limits so the result cannot be rejected:

```bash
tools/pim-hosts-import.py                                   # preview on stdout
tools/pim-hosts-import.py --resolve-tailnet \
    --relay 192.168.1.9 --print-forwards \
    --out /mnt/sdcard/PIM/hosts.cfg --merge
```

Generated host lists and copied keys are kept **outside the repository**.
`--private` writes to `$PICODECK_PRIVATE_DIR`, or
`${XDG_DATA_HOME:-~/.local/share}/picodeck`, creating it `0700` with the host
list `0600`; `--copy-keys` with no path puts key pairs in its `ssh/` folder. The
tool refuses to write a host list or a key anywhere inside a git work tree
unless you pass `--allow-in-repo`, so none of it can be committed by accident.
If your home directory is itself synced somewhere, point `PICODECK_PRIVATE_DIR`
at a path outside that sync. `.gitignore` carries matching rules as a backstop.

`--resolve-tailnet` writes each host's `100.x` address instead of its MagicDNS
name, since the device cannot resolve those. `--relay` routes every imported
host through one LAN machine and assigns it a local port, and `--print-forwards`
prints the `ssh -L` or `socat` command to run there. `--copy-keys DIR` copies the
matching key pair onto the card. It warns rather than guesses when an alias uses
`ProxyJump`, which the device cannot do, or resolves to loopback.

SSH identities live in `/PIM/ssh`: `<name>.pub` is the public key in the usual
one-line OpenSSH format, `<name>` the private half. Home → Terminal → **SSH
keys** lists what it finds with the type, size and the same `SHA256:`
fingerprint `ssh-keygen -lf` prints, and lets you delete one or attach it to a
connection. **Terminal → SSH keys → Create** makes an ed25519 key pair on the device: the
seed comes from the RP2350's hardware random source, the private half is written
to the card and never leaves it, and the screen shows the fingerprint and the
name of the `.pub` file to install on the server. The format is OpenSSH's own -
`ssh-keygen -y` on a key made this way derives exactly the public half the
device wrote.

Alternatively, copy an existing pair onto the card, or send one to the device:

```bash
curl -H 'Cookie: pim=PIN' --data-binary @~/.ssh/id_ed25519 \
    http://DEVICE:8080/upload/ssh/picocalc
```

The web server will accept a key but never hand a **private** one back:
`/PIM/ssh` is left out of the file listing and the manifest, and the download and
editor routes answer 403 for anything there that is not a `.pub`. Public halves
are served, because installing one on a server is the whole point of having it.
An upload crosses the network in the clear, so for a private key the SD card is
the better route - or better still, make the key on the device and never move it
at all. The card is removable and unencrypted, so use a key made
for this device alone and nothing else.

Getting the device onto the tailnet itself is a port of an existing MIT-licensed
C client; [docs/TAILSCALE.md](docs/TAILSCALE.md) has the plan, the memory budget
and the one architectural decision it needs.

### SSH

SSH works. One algorithm of each kind, all of them what a current OpenSSH
prefers anyway:

| | |
| --- | --- |
| key exchange | `curve25519-sha256` |
| host key | `ssh-ed25519` |
| cipher and MAC | `chacha20-poly1305@openssh.com` |
| authentication | `publickey` with an ed25519 key, or a password |

Nothing else is offered or accepted. That keeps the client small enough to read
in one sitting and leaves no weaker option for a server to talk it into.

Set a connection's **Kind** to `ssh`, give it a **User** and pick an **SSH key**,
and Connect opens a real session: a pty, a shell, and the terminal in front of
it. The window size is sent as 40x38, and again if it changes.

**Host keys are pinned.** The first time a host is seen, its fingerprint is
shown in the terminal and nothing goes further until you press `Y`; it is then
remembered in `/PIM/known_hosts`. If a known host ever presents a different key
the session stops with an error rather than asking, because that is the one
moment where a question teaches the wrong habit.

Keys must be **unencrypted ed25519** (`ssh-keygen -t ed25519`). A key with a
passphrase, or an RSA or ECDSA key, is refused with the reason rather than
half-read. Bytes above 127 in an SSH session are read as UTF-8, unlike the CP437
of a BBS.

Not implemented, and not pretended: agent forwarding, port forwarding, X11,
SFTP and SCP, running a single command instead of a shell, and rekeying a
long-lived session.

The crypto is SHA-256 and ChaCha20-Poly1305 written here against the RFC 8439
and NIST vectors, with X25519 and Ed25519 from
[tweetnacl](third_party/tweetnacl) because curve arithmetic is not worth writing
by hand. The whole stack costs 48 KB of flash and 22 KB of RAM.

### The startup PIN, and what it is worth

Home → Tools → **Device PIN** sets a six digit PIN that is asked for at every
boot, before the clock screen and before Home. `Esc` does not skip it, and
neither does the web server's "something changed" hook; until the PIN is
accepted there is nowhere else to go. Three wrong attempts start a wait that
grows to a minute, so guessing by hand is pointless.

What is stored in `/PIM/lock.cfg` is a random 16-byte salt and 50,000 rounds of
SHA-256 over salt and PIN, never the PIN itself. The rounds cost about a second
on the device, once per boot.

Be clear about the limit, though. Six digits is a million possibilities, and an
offline attacker with the card can try them all: at the measured 70 ms per guess
on a desktop that is hours on one core, less on many, and less again on a GPU,
because SHA-256 is exactly what graphics cards are good at. **The card is not
encrypted.** Anything on it - notes, the PIM database, `hosts.cfg`, and the SSH
private keys in `/PIM/ssh` - can be read directly by anyone holding it. The PIN
is a lock on the front door, not a safe, and both screens that mention it say so.

If the keys are what you care about, the thing that would actually help is
encrypting them at rest with a passphrase entered at connect time; the
primitives for that (ChaCha20-Poly1305 and SHA-256) are already here and
vectored.

`tools/ssh-live-test.sh` is the test that matters: it starts a throwaway sshd on
a spare port with its own host key and its own `authorized_keys`, then has the
client complete a key exchange, verify the host key against what `ssh-keygen`
says, authenticate, open a shell, run a command and read the output back. It
touches neither the real sshd nor `~/.ssh`, and skips when there is no sshd to
talk to.

## Tests

The application core is hardware-independent C and runs on the build host:

```bash
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

Ten suites: the PIM core, the tools, SSH (against the RFC vectors and a real
`sshd`), saved connections, the terminal emulator, the startup PIN, the
Tailscale settings file and what counts as an auth key, the startup fault
report, and the JSON trimming and array walking that make a network map fit.

## UI simulation tests

`cmake --preset host-ui && cmake --build --preset host-ui && ctest --preset host-ui` runs the real `ui.c` headlessly on LVGL (clang + ASan/UBSan) with synthetic key presses. It checks every screen, the SD file output, the amber-on-black palette pixel by pixel, memory leaks and a 12,000-key fuzz run. The terminal is covered there too: the cell grid is drawn rather than built from labels, so the tests read it back as text through `pim_ui_terminal_text()`.
