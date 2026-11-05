# PicoCalc PIM

PicoCalc PIM is a C11 firmware PDA for the ClockworkPi PicoCalc with a Raspberry Pi Pico 2 W. It uses the official PicoCalc LVGL display and keyboard drivers, stores its PDA database on a FAT32 SD card, and has an amber-on-black CRT interface.

## Features

- Calendar, appointments, priority to-dos, notes, and one journal entry per day with moods.
- Three classic games: Tetris, a turn-based dungeon crawler, and Space Invaders.
- Keyboard-first controls: arrows navigate and control games, `Enter` activates/fires/drops, and `Esc` returns.
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

## Tests

The application core is hardware-independent C and runs on the build host:

```bash
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```
