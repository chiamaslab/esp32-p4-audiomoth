#!/usr/bin/env bash
# Read the live board's DHCP address and run the acoustic streaming bench.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
mkdir -p bench
port="${1:?usage: verify.sh /dev/cu.usbmodem...}"
python3 -u tools/monitor.py --until-ip "$port" | tee bench/serial.log
ip="$(sed -En 's/.*ip=([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+) input=.*/\1/p' bench/serial.log |
      awk '$0 != "0.0.0.0" {ip=$0} END {print ip}')"
if [[ -z "$ip" ]]; then
    echo 'No DHCP address in bench/serial.log. Check the board boot and Ethernet link.' >&2
    exit 1
fi
python3 -u tools/check_stream.py "$ip" --clip bench/native-test.wav | tee bench/stream-check.json
