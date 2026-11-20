#!/usr/bin/env bash
# Puts the PicoCalc's public key into root's authorized_keys on hosts that are up.
#   tools/picocalc-key-install.sh /path/to/picocalc.pub [host ...]
# With no hosts named it tries every Linux node on the tailnet. Appending only:
# a host that already has the key is left alone, so this is safe to re-run when
# a machine that was switched off comes back.
set -uo pipefail

pub_file=${1:-/run/media/${USER}/PICOPIM/PIM/ssh/picocalc.pub}
shift || true
[[ -f "${pub_file}" ]] || { echo "no public key at ${pub_file}" >&2; exit 2; }
pub=$(cat "${pub_file}")
tag=$(awk '{print $3}' <<<"${pub}")
[[ -n "${tag}" ]] || { echo "the key has no comment to identify it by" >&2; exit 2; }

if [[ $# -gt 0 ]]; then
    hosts=("$@")
else
    mapfile -t hosts < <(tailscale status 2>/dev/null |
        awk '$4 == "linux" {print $2}' | grep -v "^$(hostname)$")
fi
[[ ${#hosts[@]} -gt 0 ]] || { echo "no hosts to do" >&2; exit 2; }

added=0 already=0 unreachable=0
for host in "${hosts[@]}"; do
    printf '%-18s ' "${host}"
    result=$(timeout 15 ssh -o BatchMode=yes -o ConnectTimeout=6 -o StrictHostKeyChecking=accept-new \
        "root@${host}" "umask 077; mkdir -p ~/.ssh
        if grep -qF '${tag}' ~/.ssh/authorized_keys 2>/dev/null; then echo already
        else printf '%s\n' '${pub}' >> ~/.ssh/authorized_keys && echo added; fi" 2>&1 | tail -1)
    case "${result}" in
        added)   echo "added";   added=$((added + 1)) ;;
        already) echo "already there"; already=$((already + 1)) ;;
        *)       echo "not reachable"; unreachable=$((unreachable + 1)) ;;
    esac
done
echo
echo "added ${added}, already had it ${already}, unreachable ${unreachable}"
echo "To undo on a host:  ssh root@HOST \"sed -i '/${tag}/d' ~/.ssh/authorized_keys\""
