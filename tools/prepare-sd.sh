#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: tools/prepare-sd.sh --device /dev/sdX [--label PICOPIM] [--yes]

Creates a single FAT32 partition and PIM/ storage directory on a removable SD
card. The selected device is completely erased.
EOF
}

device=
label=PICOPIM
confirm=false
while [[ $# -gt 0 ]]; do
    case "$1" in
        --device) device=${2:?missing device}; shift 2 ;;
        --label) label=${2:?missing label}; shift 2 ;;
        --yes) confirm=true; shift ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done

[[ -n "${device}" && -b "${device}" ]] || { usage >&2; exit 2; }
[[ "$(lsblk -dnro TYPE "${device}")" == "disk" ]] ||
    { echo "Use a whole disk, not a partition." >&2; exit 2; }
[[ "$(lsblk -dnro RM "${device}")" == 1 ]] ||
    { echo "Refusing non-removable device: ${device}" >&2; exit 2; }
[[ "${device}" != /dev/nvme* && "${device}" != /dev/mmcblk* ]] ||
    { echo "Refusing ${device}; use a USB SD-card reader." >&2; exit 2; }

echo "The following removable disk will be erased:"
lsblk -o NAME,SIZE,MODEL,TRAN,RM,MOUNTPOINTS "${device}"
[[ "${confirm}" == true ]] || {
    echo "Re-run with --yes after confirming the device." >&2; exit 1;
}
if lsblk -nrpo MOUNTPOINT "${device}" | grep -q .; then
    echo "Unmount all partitions on ${device} first." >&2; exit 1
fi

sudo wipefs --all "${device}"
sudo parted --script "${device}" mklabel msdos mkpart primary fat32 1MiB 100% set 1 lba on
sudo partprobe "${device}"
if [[ "${device}" =~ [0-9]$ ]]; then partition="${device}p1"; else partition="${device}1"; fi
for _ in {1..10}; do [[ -b "${partition}" ]] && break; sleep 1; done
[[ -b "${partition}" ]] || { echo "Partition creation failed." >&2; exit 1; }
sudo mkfs.fat -F 32 -n "${label}" "${partition}"

mount_dir=$(mktemp -d)
trap 'rmdir "${mount_dir}"' EXIT
sudo mount "${partition}" "${mount_dir}"
sudo mkdir -p "${mount_dir}/PIM"
sudo umount "${mount_dir}"
echo "Prepared ${partition} as FAT32 (${label}) with PIM/."
