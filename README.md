# PicoDeck OS (PicoCalc PIM)

PicoCalc PIM is a C11 firmware PDA for the ClockworkPi PicoCalc with a Raspberry Pi Pico 2 W. It uses the official PicoCalc LVGL display and keyboard drivers, stores its PDA database on a FAT32 SD card, and has an amber-on-black CRT interface.

## Features

- A combined calendar/appointment app: arrow to a day and press Enter, then pick the start hour, minute and length with UP/DOWN (no typing dates), then enter a title and notes. Days with appointments show `*`. Journal entries use the same selected day.
- Priority to-dos, notes, and a free-text journal: one file per day (`/PIM/journal/DD-MM-YYYY.md`) edited like the text editor. Old database journals are migrated automatically.
- Date and time entry: scroll the calendar continuously with the arrows, `,` `.` change month, `<` `>` change year, `T` jumps to today and `G` (or Go to) opens a DD-MM-YYYY picker; in the pickers you can type digits or use UP/DOWN. Set today stores the current date in `/PIM/today.cfg` (the device has no clock).
- Theme switcher (Home → Theme): Amber, Green, Manila, Cyan, Violet, Red, Mono and Paper (light themes: Manila and Paper). The choice is saved in `/PIM/theme.cfg`.
- Sound toggle (Home → Sound: ON/OFF): key clicks and game effects on the speakers, saved in `/PIM/sound.cfg`.
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
- Wi-Fi (Home → Wi-Fi & web): network scanner with on-device password entry, saved networks, NTP time sync with a time zone setting, a browser interface to your PIM, `.ics` calendar import/export, and a Telnet client for BBSs (Home → Telnet).
- Data is a CRC-checked snapshot at `/PIM/pim.dat`; a failed or missing SD card does not destroy in-memory data.

## Build and flash

Install the ARM Embedded Toolchain, CMake, Ninja, Git, and `picotool`, then initialize the pinned sources:

```bash
git submodule update --init --recursive
cmake --preset pico2w-release
cmake --build --preset pico2w-release
```

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

### Telnet (BBS)

Home → **Telnet (BBS)**: enter a host and port (default 23) while connected to Wi-Fi. The terminal is 40 columns wide with 96 lines of scrollback. Colour and cursor codes are ignored (non-ASCII art shows as `#`). Up/Down scroll, typing returns to the newest line, Enter sends CR LF and Esc disconnects. Text is sent in the clear.

## Tests

The application core is hardware-independent C and runs on the build host:

```bash
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

## UI simulation tests

`cmake --preset host-ui && cmake --build --preset host-ui && ctest --preset host-ui` runs the real `ui.c` headlessly on LVGL (clang + ASan/UBSan) with synthetic key presses. It checks every screen, the SD file output, the amber-on-black palette pixel by pixel, memory leaks and a 12,000-key fuzz run.
