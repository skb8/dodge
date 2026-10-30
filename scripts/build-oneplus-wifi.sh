#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
DEVICE_DIR=$(cd -- "$(dirname -- "$0")/.." && pwd)
KERNEL_DIR=${1:?Usage: build-oneplus-wifi.sh /path/to/oneplus13-kernel}
mkdir -p "$KERNEL_DIR"
cd "$KERNEL_DIR"
if [[ ! -d .repo ]]; then
    repo init -u https://github.com/OnePlusOSS/kernel_manifest.git \
        -b oneplus/sm8750 -m oneplus_13_b.xml --depth=1
fi
repo sync -c -j"${SYNC_JOBS:-4}" --no-clone-bundle --no-tags
mkdir -p "$DEVICE_DIR/artifacts"
repo manifest -r -o "$DEVICE_DIR/artifacts/oneplus-kernel-manifest.xml"
# The official entry point builds the matching kernel/platform/module symbol set.
# PIPEFAIL is inherited because upstream's wrapper pipes the build through tee.
env SHELLOPTS=errexit:pipefail bash kernel_platform/oplus/build/oplus_build_kernel.sh sun perf
python3 "$DEVICE_DIR/scripts/collect-wifi-modules.py" "$KERNEL_DIR" "$DEVICE_DIR/wifi/modules"
