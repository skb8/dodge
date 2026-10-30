#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
DEVICE_DIR=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$DEVICE_DIR"
mkdir -p .tests-build
SANITIZER_FLAGS=()
if [[ "${WIFI_SANITIZE:-1}" == 1 ]]; then
    SANITIZER_FLAGS=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
TMPDIR="$DEVICE_DIR/.tests-build" "${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    "${SANITIZER_FLAGS[@]}" -g \
    wifi/wifi_core.cpp tests/wifi_core_test.cpp -o .tests-build/wifi_core_test
.tests-build/wifi_core_test
python3 - <<'PY'
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
for path in [Path('wifi/wifi.xml'), *Path('wifi/languages').glob('*.xml')]:
    ET.parse(path)
for path in Path('recovery/root/system/bin').glob('dodge-wifi-*'):
    subprocess.run(['bash', '-n', str(path)], check=True)
for path in Path('scripts').glob('*.sh'):
    subprocess.run(['bash', '-n', str(path)], check=True)
# The generated list must have no hand-maintained divergence.
before = Path('wifi/wifi.xml').read_bytes()
subprocess.run(['python3', 'scripts/make-wifi-theme.py'], check=True)
assert before == Path('wifi/wifi.xml').read_bytes(), 'Regenerate wifi.xml'
print('XML, shell syntax and generated theme checks passed.')
PY
