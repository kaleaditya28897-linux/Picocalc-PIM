#!/usr/bin/env bash
# Opens the local port forwards that the PicoCalc's saved connections dial.
#
#   tools/picocalc-relay.sh [--jump HOST] [path to hosts.cfg]
#
# For the firmware built without Tailscale: that one cannot join the tailnet
# itself, so it reaches this workstation over the LAN and this workstation
# reaches the tailnet. The Tailscale firmware does not need this at all. Each forward binds 0.0.0.0 so the device can connect to it; nothing
# is published beyond the local network. Leave it running while you use the
# device and stop it with Ctrl-C.
#
# The connection list is read from the card rather than kept here, so no host
# names, addresses or ports of yours live in this repository. Entries with no
# relay port are skipped: those are dialled directly by the device.
#
# An entry whose address is a loopback one is a service on some other machine's
# own 127.0.0.1 - what ProxyJump used to reach. Name that machine with --jump
# and the far end of those forwards is resolved there.
set -uo pipefail

jump=""
if [[ ${1:-} == "--jump" ]]; then jump=${2:-}; shift 2; fi
# The card when it is in the reader; otherwise the copy in your private
# directory, which is where pim-hosts-import.py --private writes one. Neither
# path is in this repository.
private_dir=${PICODECK_PRIVATE_DIR:-${XDG_DATA_HOME:-${HOME}/.local/share}/picodeck}
hosts_file=${1:-}
if [[ -z "${hosts_file}" ]]; then
    for candidate in "/run/media/${USER}/PICOPIM/PIM/hosts.cfg" "${private_dir}/hosts.cfg"; do
        [[ -f "${candidate}" ]] && { hosts_file=${candidate}; break; }
    done
    hosts_file=${hosts_file:-${private_dir}/hosts.cfg}
fi

[[ -f "${hosts_file}" ]] || {
    echo "no connection list at ${hosts_file}" >&2
    echo "give the path, or mount the card first" >&2
    exit 2
}

lan=$(ip -4 -o addr show scope global | awk 'NR==1{split($4,a,"/"); print a[1]}')
echo "relaying on ${lan}, from ${hosts_file}"

pids=()
cleanup() { for pid in "${pids[@]:-}"; do kill "${pid}" 2>/dev/null || true; done; }
trap cleanup EXIT INT TERM

opened=0 skipped=0
while IFS=$'\t' read -r head kind address port user key relay relay_port; do
    [[ ${head:0:5} == "host=" ]] || continue
    alias=${head:5}
    [[ -n "${relay_port:-}" && "${relay_port}" != "0" ]] || { skipped=$((skipped + 1)); continue; }

    through="${address}"
    if [[ ${address} == 127.* || ${address} == localhost || ${address} == "::1" ]]; then
        if [[ -z "${jump}" ]]; then
            printf '  %-22s skipped: %s is a loopback address, needs --jump\n' "${lan}:${relay_port}" "${alias}"
            skipped=$((skipped + 1))
            continue
        fi
        through="${jump}"
    fi

    printf '  %-22s -> %-22s %s\n' "${lan}:${relay_port}" "${address}:${port}" "${alias}"
    ssh -N -o ExitOnForwardFailure=yes -o ConnectTimeout=10 \
        -L "0.0.0.0:${relay_port}:${address}:${port}" "${through}" &
    pids+=($!)
    opened=$((opened + 1))
done < "${hosts_file}"

echo
echo "${opened} forwards, ${skipped} skipped."
echo "A host that is switched off reports a failed forward and drops out; the"
echo "rest keep working. Ctrl-C closes them all."
[[ ${opened} -gt 0 ]] || exit 1
wait
