# PicoCalc PIM instructions

## Architecture

- This is C11 firmware for the PicoCalc's RP2350 Pico 2 W. `src/firmware/main.c` initializes the Pico SDK, official PicoCalc LVGL display/keyboard drivers, and FAT32 SD storage.
- `src/core/` is deliberately hardware-free C. It owns PDA models, calendar calculations, CRC-checked persistence, and the state for Tetris, the dungeon crawler, and Space Invaders. Keep all logic here so it remains host-testable.
- `src/firmware/ui.c` owns LVGL screen construction and translates UI events into core operations. Keep Pico SDK and LVGL calls out of `src/core/`.
- The SD card mounts at `/`; the persistent snapshot is `/PIM/pim.dat`. Saving writes a temporary file before renaming it, so retain that recovery behavior.
- `third_party/` contains pinned submodules for `pico-sdk`, official `PicoCalc` drivers, and `pico-vfs`. Do not modify their contents for PIM behavior.

## Commands

```bash
git submodule update --init --recursive
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

Build the Pico 2 W firmware with:

```bash
cmake --preset pico2w-release
cmake --build --preset pico2w-release
```

The generated UF2 is `build/pico2w-release/picocalc_pim.uf2`. Flash it through `tools/flash-uf2.sh`; format an SD card through `tools/prepare-sd.sh --device /dev/sdX --yes`.

## Conventions

- Use C11 and the existing fixed-capacity data models; RAM is bounded on the Pico 2 W.
- Validate dates and times before core mutations, and persist after every successful PDA mutation.
- Keep all UI surfaces amber-on-black by applying `crt_style()` to new LVGL controls.
- Controls must remain reachable through LVGL's default keyboard group. `Esc` returns home, and `Enter` activates the current control.
- Add core behavior with cases in `tests/test_pim.c`, not firmware-only checks.
