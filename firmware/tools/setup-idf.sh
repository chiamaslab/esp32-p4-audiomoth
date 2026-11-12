#!/usr/bin/env bash
# Install the official P4 toolchain in the normal per-user ESP-IDF directories.
set -euo pipefail
sdk="${BAT_IDF_PATH:-$HOME/esp/esp-idf-v5.5.4}"
if [[ ! -d "$sdk/.git" ]]; then
    mkdir -p "$(dirname "$sdk")"
    git clone --branch v5.5.4 --depth 1 --shallow-submodules --recursive https://github.com/espressif/esp-idf.git "$sdk"
else
    [[ "$(git -C "$sdk" describe --tags --exact-match HEAD)" == v5.5.4 ]]
    git -C "$sdk" submodule update --init --recursive --depth 1
fi
"$sdk/install.sh" esp32p4
