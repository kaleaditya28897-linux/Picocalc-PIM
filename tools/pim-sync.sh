#!/usr/bin/env bash
# Sync the PicoCalc's /PIM files with a folder on this computer over its web server.
#   tools/pim-sync.sh pull  HOST PIN [DIR]   download everything (and calendar.ics) into DIR
#   tools/pim-sync.sh push  HOST PIN [DIR]   upload files from DIR that differ in size
# Turn Wi-Fi & web on first; HOST is the address and PIN the code shown on the device.
# Files over 8 KiB cannot be uploaded and are skipped with a warning.
set -euo pipefail

if [ $# -lt 3 ]; then
    sed -n '2,6p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi
mode=$1 host=$2 pin=$3 dir=${4:-picocalc-sync}
base="http://$host:8080"
curl_pim() { curl -fsS --max-time 30 -H "Cookie: pim=$pin" "$@"; }
mkdir -p "$dir"
manifest=$(curl_pim "$base/manifest")

case "$mode" in
pull)
    count=0
    while read -r path size; do
        [ -n "${path:-}" ] || continue
        mkdir -p "$dir/$(dirname "$path")"
        if curl_pim -o "$dir/$path" "$base/file/$path"; then count=$((count + 1))
        else echo "skipped $path (could not download)" >&2; fi
    done <<<"$manifest"
    curl_pim -o "$dir/calendar.ics" "$base/calendar.ics" || echo "calendar export failed" >&2
    echo "Pulled $count files into $dir"
    ;;
push)
    count=0
    while IFS= read -r -d '' file; do
        path=${file#"$dir"/}
        [ "$path" = calendar.ics ] && continue
        local_size=$(stat -c %s "$file")
        remote_size=$(awk -v p="$path" '$1 == p { print $2 }' <<<"$manifest")
        [ "$local_size" = "${remote_size:-}" ] && continue
        if [ "$local_size" -gt 8192 ]; then echo "skipped $path (over 8 KiB)" >&2; continue; fi
        if curl_pim -o /dev/null --data-binary "@$file" "$base/upload/$path"; then count=$((count + 1))
        else echo "failed $path" >&2; fi
    done < <(find "$dir" -maxdepth 3 -type f -regextype posix-extended -regex '.*/[A-Za-z0-9._-]+(/[A-Za-z0-9._-]+){0,2}' -print0)
    echo "Pushed $count files from $dir"
    ;;
*)
    echo "Unknown mode: $mode (use pull or push)" >&2
    exit 2
    ;;
esac
