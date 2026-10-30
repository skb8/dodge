#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
DEVICE_DIR=$(cd -- "$(dirname -- "$0")/.." && pwd)
KERNEL_DIR=${1:?Usage: build-oneplus-wifi.sh /path/to/oneplus13-kernel}
mkdir -p "$KERNEL_DIR"
cd "$KERNEL_DIR"
if [[ ! -d .repo ]]; then
    repo init -u https://github.com/OnePlusOSS/kernel_manifest.git \
        -b "${ONEPLUS_MANIFEST_REVISION:-oneplus/sm8750}" -m oneplus_13_b.xml --depth=1
fi
if [[ -n "${ONEPLUS_LOCKED_MANIFEST:-}" ]]; then
    # Resolve once for the cache key and build those exact commits.
    cp "$ONEPLUS_LOCKED_MANIFEST" .repo/manifests/oneplus_13_b.xml
fi
repo sync -c -j"${SYNC_JOBS:-4}" --no-clone-bundle --no-tags
mkdir -p "$DEVICE_DIR/artifacts"
repo manifest -r -o "$DEVICE_DIR/artifacts/oneplus-kernel-manifest.xml"
# Preserve the official build environment while explicitly checking its result.
# Inherited errexit breaks upstream config/variant discovery in child shells.
env -u SHELLOPTS TERM=xterm bash "$DEVICE_DIR/scripts/run-oneplus-build.sh" "$KERNEL_DIR"
python3 "$DEVICE_DIR/scripts/collect-wifi-modules.py" "$KERNEL_DIR" "$DEVICE_DIR/wifi/modules"
