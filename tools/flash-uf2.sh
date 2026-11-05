#!/usr/bin/env bash
set -euo pipefail

uf2=${1:-release/picocalc_pim-pico2w.uf2}
mount=${PICO_BOOTSEL_MOUNT:-"/run/media/${USER}/RPI-RP2"}

[[ -f "${uf2}" ]] || {
    echo "UF2 not found: ${uf2}. Build with: cmake --build --preset pico2w-release" >&2
    exit 1
}

if command -v picotool >/dev/null 2>&1 && picotool info >/dev/null 2>&1; then
    picotool load -f "${uf2}"
    picotool reboot
    exit 0
fi

[[ -d "${mount}" ]] || {
    echo "Put the Pico 2 W in BOOTSEL mode and set PICO_BOOTSEL_MOUNT to its RPI-RP2 mount path." >&2
    exit 1
}
cp "${uf2}" "${mount}/"
sync
echo "Flashed ${uf2}."
