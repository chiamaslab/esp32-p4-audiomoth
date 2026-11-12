#!/usr/bin/env bash
# Run on the computer connected to the NANO's USB-C programming port.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
port="${1:?usage: flash.sh /dev/cu.usbmodem...}"
mkdir -p "$root/bench"
cd "$root/images"
shasum -a 256 -c SHA256SUMS
tool=(esptool --chip esp32p4 --port "$port")
id="$("${tool[@]}" flash-id)"
echo "$id"
mac="$(awk '/^MAC:/ {print $2; exit}' <<< "$id")"
[[ "$mac" =~ ^([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}$ ]]
[[ "$id" =~ revision\ v3\.([1-9][0-9]?)\) && "$id" == *"Detected flash size: 16MB"* ]] || {
    echo 'Expected an ESP32-P4 revision v3.1 through v3.99 with 16 MB flash.' >&2; exit 1;
}
backup="$root/bench/preflash-${mac//:/}.bin"
if [[ ! -f "$backup" || "$(wc -c < "$backup")" -ne 16777216 ]]; then
    echo "Saving the current 16 MB flash to $backup"
    "${tool[@]}" --baud 921600 read-flash 0 0x1000000 "$backup.partial" ||
        "${tool[@]}" --baud 115200 read-flash 0 0x1000000 "$backup.partial"
    [[ "$(wc -c < "$backup.partial")" -eq 16777216 ]]
    mv "$backup.partial" "$backup"
fi
"${tool[@]}" --baud 115200 write-flash 0x2000 bootloader.bin 0x8000 partition-table.bin 0x10000 bat_streamer.bin
bash "$root/tools/verify.sh" "$port"
