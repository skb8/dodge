#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
DEVICE_DIR=$(cd -- "$(dirname -- "$0")/.." && pwd)
ANDROID_ROOT=${1:?Usage: apply-wifi.sh /path/to/fox_14.1}
ANDROID_ROOT=$(cd -- "$ANDROID_ROOT" && pwd)
apply() {
    local repo=$1 patch=$2
    if git -C "$repo" apply --reverse --check "$patch" >/dev/null 2>&1; then
        echo "Already applied: ${patch##*/}"
    else
        git -C "$repo" apply --check "$patch"
        git -C "$repo" apply "$patch"
    fi
}
# Preflight both source patches before changing either checkout.
for spec in 'bootable/recovery:orangefox-wifi.patch' 'external/wpa_supplicant_8:wpa-recovery.patch'; do
    dir=${spec%%:*}; patch=${spec#*:}
    git -C "$ANDROID_ROOT/$dir" apply --check "$DEVICE_DIR/patches/$patch" >/dev/null 2>&1 ||
        git -C "$ANDROID_ROOT/$dir" apply --reverse --check "$DEVICE_DIR/patches/$patch"
done
apply "$ANDROID_ROOT/bootable/recovery" "$DEVICE_DIR/patches/orangefox-wifi.patch"
apply "$ANDROID_ROOT/external/wpa_supplicant_8" "$DEVICE_DIR/patches/wpa-recovery.patch"
if ! grep -q 'recovery_available: true' "$ANDROID_ROOT/external/libnl/Android.bp"; then
    apply "$ANDROID_ROOT/external/libnl" "$DEVICE_DIR/patches/libnl-recovery.patch"
fi
mkdir -p "$ANDROID_ROOT/bootable/recovery/dodge_wifi"
cp "$DEVICE_DIR"/wifi/*.{cpp,hpp} "$ANDROID_ROOT/bootable/recovery/dodge_wifi/"
cp "$DEVICE_DIR/wifi/wifi.xml" "$ANDROID_ROOT/bootable/recovery/gui/theme/portrait_hdpi/pages/wifi.xml"
# Localized strings extend the existing language files without replacing their contents.
python3 "$DEVICE_DIR/scripts/install-wifi-strings.py" "$ANDROID_ROOT/bootable/recovery"
echo 'Wi-Fi source and theme installed.'
