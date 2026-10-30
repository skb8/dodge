#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -eo pipefail
DEVICE_DIR=$(cd -- "$(dirname -- "$0")/.." && pwd)
ANDROID_ROOT=${1:?Usage: build-recovery.sh /path/to/fox_14.1}
cd "$ANDROID_ROOT"
"$DEVICE_DIR/scripts/sync-wifi-deps.sh" "$ANDROID_ROOT"
"$DEVICE_DIR/scripts/apply-wifi.sh" "$ANDROID_ROOT"
# Keep envsetup/mka shell changes isolated. Never package an unverified image.
if bash "$DEVICE_DIR/scripts/run-android-build.sh" "$ANDROID_ROOT"; then
    :
else
    status=$?
    echo "Android build failed (exit $status)" >&2
    exit "$status"
fi
python3 "$DEVICE_DIR/scripts/verify-image.py" "$ANDROID_ROOT/out/target/product/dodge"
mkdir -p "$DEVICE_DIR/artifacts"
cp out/target/product/dodge/recovery.img "$DEVICE_DIR/artifacts/OrangeFox-dodge.img"
(cd "$DEVICE_DIR/artifacts" && sha256sum OrangeFox-dodge.img > SHA256SUMS)
repo manifest -r -o "$DEVICE_DIR/artifacts/recovery-manifest.xml"
