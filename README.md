# PicoDeck OS (PicoCalc PIM)

PicoDeck OS is C11 firmware that turns a ClockworkPi PicoCalc with a Raspberry
Pi Pico 2 W into a pocket SSH terminal and personal organiser. It joins your
**Tailscale** tailnet by itself, so you can SSH to any of your machines from
anywhere you have Wi-Fi, with no relay computer in between. Around that there is
a calendar, to-dos, notes, a journal, files, a text editor, tools and games, all
stored on a FAT32 SD card and driven entirely from the keyboard.

- [Which firmware](#which-firmware)
- [Getting started](#getting-started)
- [Tailscale](#tailscale)
- [Terminal and SSH](#terminal-and-ssh)
- [Wi-Fi and the web PIM](#wi-fi-and-the-web-pim)
- [Organiser, tools and games](#organiser-tools-and-games)
- [The startup PIN](#the-startup-pin)
- [What is on the card](#what-is-on-the-card)
- [Building](#building)
- [Tests](#tests)
- [How it works, and what went wrong on the way](#how-it-works-and-what-went-wrong-on-the-way)

## Which firmware

There are two images, both prebuilt and checksummed in `release/`:

| Image | What it is |
| --- | --- |
| `picocalc_pim-pico2w-tailscale.uf2` | **The full firmware.** Runs on FreeRTOS and carries the Tailscale client, so the device joins your tailnet itself. Use this one. |
| `picocalc_pim-pico2w.uf2` | The same organiser, terminal and SSH without Tailscale. Simpler and lighter; a tailnet machine has to be reached through a [relay](#without-tailscale-a-relay) on your LAN. |

Neither image contains a key or anything about your network: everything
personal is read from the SD card.

To flash: hold **BOOTSEL** on the Pico 2 W while plugging it into USB, then copy
the `.uf2` onto the drive that appears, or run:

```bash
tools/flash-uf2.sh                                         # the release image
picotool load -f -x release/picocalc_pim-pico2w-tailscale.uf2   # over USB while the firmware runs
```

Check a download with `sha256sum -c release/*.sha256`.

## Getting started

1. **Prepare a card.** Any FAT32 card works; the firmware creates `/PIM` and
   everything under it. To format one from scratch (this **erases** the device
   named, and only accepts a removable whole disk):

   ```bash
   tools/prepare-sd.sh --device /dev/sdX          # shows what it would do
   tools/prepare-sd.sh --device /dev/sdX --yes    # does it
   ```

   Insert the card before switching on. Without one the device still works, but
   nothing is saved.

2. **First boot.** If a startup PIN is set it is asked for first. Then, if a
   saved Wi-Fi network is in range, the device joins it, sets its clock over NTP
   and goes straight to Home. Otherwise it asks for the date and time (it has no
   battery-backed clock); **Sync (Wi-Fi)** on that screen fetches it instead.

3. **Join Wi-Fi.** Home → **Wi-Fi & web** → **Scan networks**, pick yours and
   type the password. It is remembered for next time.

4. **Join your tailnet.** Wi-Fi & web → **Tailscale** → **Connect**. With no key
   on the card the screen shows a login address: open it on your phone or
   computer, approve the device, and it finishes by itself. Details, and the
   auth-key alternative, are under [Tailscale](#tailscale).

5. **Make an SSH key.** Home → **Terminal** → **SSH keys** → **Create**. Install
   the public half on your servers (see [SSH keys](#ssh-keys)).

6. **Save a connection and connect.** Terminal → **New**, fill in the form, pick
   the key, **Save**, then **Connect**.

Everywhere: the arrows move, **Enter** opens or activates, **Esc** goes back.

## Tailscale

*Only in the Tailscale image.*

Wi-Fi & web → **Tailscale** shows the state (off, starting, registering, on the
tailnet), the `100.x` address the device was given, a line saying what it is
doing, the login address when one is needed, and how much memory is free.
**Connect** and **Disconnect** do what they say. The device appears on your
tailnet as an ordinary machine.

Wi-Fi has to be joined first: the tunnel runs over it. While Tailscale is on, the
web PIM is paused, because the two need the same memory; it comes back when you
disconnect. Turning Wi-Fi off, or joining a different network, disconnects
Tailscale cleanly first, and the radio no longer powers itself down while the
tunnel is up.

### Adding the device to your tailnet

Either way works; pick one.

- **Log in, no key.** Leave the settings file off the card, or leave out
  `authkey=`, and press Connect. The screen shows a `login.tailscale.com`
  address. Open it on any device signed in to your tailnet and approve it. The
  PicoCalc finishes registering on its own, and nothing secret is ever stored on
  the card.
- **An auth key.** In the admin console: Settings → Keys → **Generate auth
  key**. Make it **reusable** (so reflashing does not need a new one) and
  **pre-approved** if your tailnet requires device approval. Do **not** make it
  ephemeral, or the device disappears from the tailnet whenever it reboots.
  Giving it a tag such as `tag:picocalc` lets your ACLs limit what it can reach.
  Put the key on the card as below.

Once registered, the device keeps its identity in `/PIM/ts/` and reconnects
without the key or a login.

### `/PIM/tailscale.cfg`

```
# PicoDeck Tailscale settings. The auth key is a secret.
authkey=tskey-auth-...   # optional; without it the device asks you to log in
name=picocalc            # the name it has on the tailnet
derp=6                   # home relay region; 0 keeps the default (6)
```

Comments may start a line or follow a value. `derp` is the relay region used
when no direct path exists; `tailscale netcheck --format=json` on a nearby
machine reports the best one as `PreferredDERP`. The device works with
Tailscale's own control plane only; a self-hosted one such as Headscale needs
its own server key, which there is no way to supply.

Treat this file as a secret, and `/PIM/ts/` even more so, since it holds the
device's private keys. The web PIM refuses to list or serve either. If you ever
pasted a key somewhere you should not have, revoke it in the admin console and
generate a new one.

### Connecting to a tailnet machine

Save a connection whose **Address** is the machine's `100.x` address, or its
tailnet name (`nas`, or `nas.your-tailnet.ts.net`), and connect while Tailscale
is on. The connection goes **through the tunnel**: directly to the machine when
a direct path exists, otherwise via a Tailscale relay, and either way end-to-end
encrypted. The terminal and SSH client work the same as on the LAN.

If it fails, the message says so plainly. The usual causes are that Tailscale is
not connected yet, the machine is offline, or the machine is one of more than 20
on the tailnet (the device's peer table holds 20).

### Without Tailscale: a relay

The image without Tailscale reaches a tailnet machine through a **relay**: a
computer on your LAN that is on the tailnet and forwards a local port to it.

```bash
ssh -L 0.0.0.0:2200:nas.your-tailnet.ts.net:22 localhost
# or: socat TCP-LISTEN:2200,fork,reuseaddr TCP:nas.your-tailnet.ts.net:22
```

The connection form on the device has no relay fields. Relays come from the
importer below, which writes them into `hosts.cfg`; the device honours them,
keeps them when you edit a connection, and ignores them in favour of the tunnel
whenever Tailscale is on. `tools/picocalc-relay.sh` opens every forward that a
card's `hosts.cfg` asks for and keeps them up while you work.

## Terminal and SSH

Home → **Terminal** lists your saved connections by name. **New** opens the form:

| Field | Meaning |
| --- | --- |
| Name | What the list shows, and the title of the session |
| Kind | `ssh` or `telnet` |
| Address | Host name, LAN address, or tailnet address or name |
| Port | 22 for SSH, 23 for telnet (switching Kind moves the default) |
| User (ssh) | The login name |
| SSH key | One of the keys in `/PIM/ssh` |

**Save** keeps it (up to 12), **Connect** dials it, **Delete** removes it.
Connections live in `/PIM/hosts.cfg`.

### The terminal

A real screen terminal, 40 columns by 38 rows with 48 lines of scrollback:
VT100/ECMA-48 cursor addressing, scrolling regions, insert and delete, the
alternate screen (so full-screen programs such as `vi` and `mc` draw properly), DEC line
drawing, and 256-colour and truecolour mapped onto the current theme. Box
drawing, blocks and shading are painted directly, since the font has no glyphs
for them. SSH sessions read UTF-8; telnet sessions read CP437, which is what a
BBS draws with.

| Key | Does |
| --- | --- |
| **Esc** | Leaves the session |
| **Break** | Sends a real Escape to the host |
| **Page Up / Page Down** | Scroll back through history; typing returns to the live screen |
| Arrows, Home, End, Insert, Delete, F1–F10 | Sent as a VT100 sends them |
| Ctrl + letter | The control character |

Telnet negotiates terminal type and window size, so the host knows the real
size. Telnet is unencrypted; use it only for a BBS or a LAN machine you trust.

### SSH

| | |
| --- | --- |
| Key exchange | `curve25519-sha256` |
| Host key | `ssh-ed25519` |
| Cipher and MAC | `chacha20-poly1305@openssh.com` |
| Login | public key, **ed25519** only |

That is all it offers, so no server can talk it down to something weaker; every
current OpenSSH server accepts it. You get a pty and a shell at 40x38.

**Host keys are pinned.** The first time you connect to a machine, its
fingerprint is shown and nothing happens until you press `Y`; it is then kept in
`/PIM/known_hosts`. If that machine ever presents a different key, the session
stops with an error instead of asking. If you rebuilt the server on purpose,
delete its line from `known_hosts` (Files, or the web PIM).

Not supported: password login, agent, port or X11 forwarding, SFTP/SCP, running
a single command instead of a shell, and rekeying very long sessions.

### SSH keys

Keys live in `/PIM/ssh`: `<name>` is the private half, `<name>.pub` the public
one, in the usual OpenSSH formats. Home → Terminal → **SSH keys** lists them
with their type and `SHA256:` fingerprint, and lets you delete one.

- **Create** makes an ed25519 key on the device from the RP2350's hardware
  random source. The private half never leaves the card. The screen shows the
  fingerprint and the `.pub` file to install.
- **Bring your own:** copy an **unencrypted ed25519** pair onto the card. A key
  with a passphrase, or an RSA or ECDSA key, is refused with the reason.

To install the public key on a server, append the `.pub` line to
`~/.ssh/authorized_keys` there. The web PIM serves `.pub` files (it never serves
a private key), and `tools/picocalc-key-install.sh picocalc.pub host...` appends
it to **root's** `authorized_keys` on the hosts you name (every Linux machine on
the tailnet if you name none), leaving any that already have it untouched.

The card is removable and not encrypted, so use a key made for this device
alone, and remove it from your servers if you lose the card.

### Importing connections from `~/.ssh/config`

`tools/pim-hosts-import.py` turns your computer's SSH config into a `hosts.cfg`.
It asks `ssh -G` what each alias resolves to, so `Include`, `Match` and defaults
are handled by ssh itself, and clips everything to the device's limits.

```bash
tools/pim-hosts-import.py                                   # preview
tools/pim-hosts-import.py --private                         # write to your private folder
tools/pim-hosts-import.py --resolve-tailnet --out /media/sd/PIM/hosts.cfg --merge
tools/pim-hosts-import.py --relay 192.168.1.9 --print-forwards --out ...   # image without Tailscale
```

`--resolve-tailnet` writes `100.x` addresses instead of names, `--relay` routes
every host through one LAN machine and `--print-forwards` prints the commands to
run there. `--copy-keys DIR` copies the matching key pairs. Aliases that use
`ProxyJump` are warned about rather than guessed at.

Your host list and keys never belong in a repository. `--private` writes to
`$PICODECK_PRIVATE_DIR`, or `~/.local/share/picodeck`, with owner-only
permissions, and the tool refuses to write a host list or key inside any git
work tree unless you pass `--allow-in-repo`.

## Wi-Fi and the web PIM

Home → **Wi-Fi & web**:

| Button | Does |
| --- | --- |
| Join saved | Rejoins a remembered network (up to 4 are kept) |
| Scan networks | Lists nearby networks by signal; pick one and type the password |
| Manual entry | For a hidden network |
| Sync time | Sets the clock from `pool.ntp.org` |
| Time zone | The UTC offset (default UTC+05:30) |
| Forget saved | Removes a remembered network |
| Web address | Shows the web PIM's address and PIN again |
| Wi-Fi off | Powers the radio down |
| Tailscale | The [Tailscale](#tailscale) screen (Tailscale image only) |

Wi-Fi passwords are stored in plain text in `/PIM/wifi.cfg`. With no session
open and no tunnel up, the radio powers down after 15 idle minutes to save
battery.

### The web PIM

After joining, the device shows an address such as `http://192.168.1.20:8080`
and a 4-digit PIN that changes every time. Open it in a browser on the same
network, enter the PIN, and you can read and edit the agenda, to-dos, notes and
journal, browse and edit files on the card, download `calendar.ics`, and import
an `.ics` file. Changes go straight to the card. Ten wrong PINs lock the server
until the next connection. It is paused while Tailscale is on, and if there is
not enough memory to start it, Wi-Fi still works and the screen says so.

From the command line the PIN goes in a cookie:

```bash
curl -H 'Cookie: pim=PIN' -T notes.txt http://DEVICE:8080/upload/notes.txt   # up to 8 KiB
curl -H 'Cookie: pim=PIN' http://DEVICE:8080/file/notes.txt -o notes.txt
curl -H 'Cookie: pim=PIN' http://DEVICE:8080/calendar.ics -o pim.ics
curl -H 'Cookie: pim=PIN' --data-binary @phone.ics http://DEVICE:8080/import
tools/pim-sync.sh pull DEVICE PIN ./backup     # mirror the card's files to a folder
tools/pim-sync.sh push DEVICE PIN ./backup     # and back
```

`.ics` import takes events and to-dos, treats every time as local time and skips
duplicates. Calendar subscriptions (HTTPS URLs) are not supported; export the
file and import it instead.

The server is plain HTTP and the PIN is a speed bump, not encryption: use it on
networks you trust. It never lists or serves private SSH keys,
`tailscale.cfg`, `/PIM/ts/`, `wifi.cfg`, `hosts.cfg`, `lock.cfg` or `pim.dat`.

## Organiser, tools and games

**Home** shows the clock, battery, and: Calendar, To-Do List, Notes, Journal,
Files, Text editor, Tools, Wi-Fi & web, Today, Games, Theme, Terminal and Sound.

- **Calendar.** Arrow to a day and press Enter to add an appointment: start
  hour, minute and length with UP/DOWN, then a title and notes. Days with
  appointments are marked `*`. `,` and `.` change month, `<` and `>` the year,
  `T` jumps to today and `G` opens a date picker.
- **To-Do List.** Priorities and optional due dates.
- **Today.** Today's appointments plus due and overdue to-dos.
- **Notes** and **Journal.** The journal keeps one Markdown file per day
  (`/PIM/journal/DD-MM-YYYY.md`) and edits it like the text editor.
- **Files** and **Text editor.** Browse folders on the card, create and delete
  files and folders, and edit text files of any size: 40 columns, soft-wrapped,
  paged 4 KiB at a time, saved when you change page.
- **Tools.** Full-text **Search** across everything; a scientific
  **Calculator** (live result, `+ - * /` continues from the last answer, UP
  recalls earlier ones); a **Timer**; **Recurring** appointments (daily, weekly,
  monthly, yearly); **Backup** and restore of `/PIM` (the newest 5 are kept);
  and **Device PIN**.
- **Games.** Tetris, a dungeon crawler (8 floors, orcs, a boss, sword and
  armour), Space Invaders, Solitaire, Snake, Minesweeper and 2048, each with
  sound and a top-5 score table. Each game has a folder in `/PIM/games/` with
  its settings and scores, and the dungeon's floors are editable text maps
  (`floor1.txt`…`floor8.txt`, 16x12: `M` monster, `O` orc, `B` boss, `/` sword,
  `[` armour). Missing files are recreated from defaults; your edits are never
  overwritten.
- **Theme.** Amber, Green, Manila, Cyan, Violet, Red, Mono and Paper (Manila and
  Paper are light).
- **Sound.** Key clicks and game effects on or off.

Power: the backlight dims after 30 s and switches off after 2 minutes idle; any
key wakes it. A banner and a sound warn at 10% battery. The battery level comes
from the keyboard controller, which only reports once the device has run on
battery since power-up.

Reliability: a hardware watchdog reboots a hung device. If a start fails, or the
device restarts because of a fault, the next boot shows **Last start** with what
it was doing and why; a start that died while joining Wi-Fi skips the automatic
join once, so a faulty network cannot cause a boot loop.

## The startup PIN

Home → Tools → **Device PIN** sets a six-digit PIN asked for at every boot,
before anything else. It cannot be skipped, and wrong guesses earn a wait that
grows to a minute. What is stored, in `/PIM/lock.cfg`, is a random salt and
50,000 rounds of SHA-256, never the PIN.

Know its limit: **the card is not encrypted.** Anyone holding the card can read
your notes, `hosts.cfg`, `tailscale.cfg` and the SSH keys directly, and could
try all million PINs offline in hours. The PIN keeps a person out of the device,
not out of the card. If you lose the card, revoke the device in the Tailscale
admin console and remove its SSH key from your servers.

## What is on the card

| Path | Holds | Private? |
| --- | --- | --- |
| `/PIM/pim.dat` | The organiser's database (CRC-checked, written safely) | yes |
| `/PIM/appointments/`, `todos/`, `notes/`, `calendar/` | Readable exports, regenerated on every change | yes |
| `/PIM/journal/` | One Markdown file per day | yes |
| `/PIM/hosts.cfg` | Saved connections | yes |
| `/PIM/known_hosts` | Pinned SSH host keys | |
| `/PIM/ssh/` | SSH key pairs | **secret** |
| `/PIM/tailscale.cfg` | Tailscale settings and auth key | **secret** |
| `/PIM/ts/` | The device's Tailscale identity | **secret** |
| `/PIM/wifi.cfg` | Saved networks and their passwords | **secret** |
| `/PIM/lock.cfg` | The PIN's salted hash | |
| `/PIM/today.cfg`, `theme.cfg`, `sound.cfg` | Clock, theme and sound settings | |
| `/PIM/games/` | Game settings, scores and dungeon maps | |
| `/PIM/backup/` | Backups made from Tools | yes |

The exports are views of `pim.dat`: edit through the device or the web PIM, as
the next save rewrites them.

## Building

Install CMake, Ninja, Git and `picotool`, plus an ARM GCC. If you would rather
not install a cross compiler system-wide, a self-contained one works:

```bash
curl -L -o /tmp/arm-gcc.tar.gz https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v15.2.1-1.1/xpack-arm-none-eabi-gcc-15.2.1-1.1-linux-x64.tar.gz
mkdir -p build/toolchain && tar -xzf /tmp/arm-gcc.tar.gz -C build/toolchain
export PICO_TOOLCHAIN_PATH=$PWD/build/toolchain/xpack-arm-none-eabi-gcc-15.2.1-1.1
export PATH=$PICO_TOOLCHAIN_PATH/bin:$PATH
```

Then:

```bash
git submodule update --init --recursive     # pico-sdk, PicoCalc, pico-vfs, FreeRTOS-Kernel

cmake --preset pico2w-tailscale && cmake --build build/pico2w-tailscale   # the full firmware
cmake --preset pico2w-release   && cmake --build --preset pico2w-release  # without Tailscale
```

Each build leaves `picocalc_pim.uf2` in its build folder. The Tailscale build
also writes a log to USB serial (115200), useful when something goes wrong:
`cat /dev/ttyACM0`. Source paths are stripped from the binaries, so an image
never carries the path or user name of the machine that built it.

## Tests

Everything outside the hardware drivers is plain C and is tested on the build
machine:

```bash
cmake --preset host-debug && cmake --build --preset host-debug && ctest --preset host-debug
cmake --preset host-ui && cmake --build --preset host-ui && ctest --preset host-ui
tools/ssh-live-test.sh build/host-debug
```

Eleven suites: the organiser core and web PIM, the tools, saved connections,
the `~/.ssh/config` importer, SSH crypto against the RFC and NIST vectors along
with keys and the PIN, a live SSH session (run by the script below), the
terminal emulator, the Tailscale settings file, the startup fault report, the
JSON trimming that lets a network map fit, and **the UI simulation**: the real `ui.c` running headless on LVGL under AddressSanitizer
and UBSan, driven by key presses through every screen, checking output on the
card, colours pixel by pixel, memory leaks over 100 rounds, and a 12,000-key
fuzz run.

`tools/ssh-live-test.sh` is the one that matters most for SSH. It starts a
throwaway `sshd` on a spare local port with its own host key, then has the
client exchange keys, check the host key against `ssh-keygen`, log in, open a
shell, run a command and read the output. It never touches your real sshd or
`~/.ssh`.

## How it works, and what went wrong on the way

**Memory.** The Pico 2 W has 520 KB of RAM, and a tailnet of sixteen machines
sends a 32 KB network map that cJSON would need about 87 KB to hold as a tree.
The client therefore drops the fields it never reads and parses the peer list
one machine at a time; the interface uses shared styles instead of per-widget
ones; the terminal takes its buffers only while a session is open; and the web
PIM gives its 29 KB back while Tailscale runs. With all of that, about 100 KB is
free after the map is parsed. The receive buffer for map updates also grows by
what it needs rather than doubling: doubling once asked for 96 KB at a time and
was the cause of the old "lost in the control plane" reconnects.

**The freeze on direct connections.** The Tailscale client sends a packet
straight from the calling task if that task already holds lwIP's core lock, and
otherwise hands it to the network thread. It asks which case applies through a
port function, and the first port answered a different question ("is this the
network thread?"). A connection made from the screen's task, which holds the
lock while connecting, therefore handed its packet to the network thread and
waited for it while holding the lock that thread needed. Relayed connections
never took that path, which is why they worked while direct ones froze the
device. The port now asks the lock itself who holds it.

**Bounded connects.** lwIP's blocking `connect()` ignores `SO_SNDTIMEO`, so a
dead path held the screen's task for TCP's whole retry schedule. Connects are
now non-blocking with a deadline, and an unreachable host fails with a message.

**Relays and certificates.** The DERP relay is reached by the address the
control plane supplies, because resolving `derp*.tailscale.com` failed on this
board. Relay certificates are not checked (the firmware has no certificate
bundle), which is how Tailscale itself treats relays: the control plane is
authenticated by its Noise handshake against Tailscale's pinned key, and
everything relayed is WireGuard-encrypted to the far machine.

The Tailscale client is [MicroLink](https://github.com/CamM2325/microlink)
(MIT), vendored in `third_party/microlink/`. Every change made to it, and why,
is listed in its [PROVENANCE.md](third_party/microlink/PROVENANCE.md); the
original porting plan is in [docs/TAILSCALE.md](docs/TAILSCALE.md).
