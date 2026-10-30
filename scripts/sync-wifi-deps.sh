#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
ANDROID_ROOT=${1:?Usage: sync-wifi-deps.sh /path/to/fox_14.1}
cd "$ANDROID_ROOT"
if [[ -d external/wpa_supplicant_8/.git || -f external/wpa_supplicant_8/.git ]]; then
    exit 0
fi
[[ -d .repo ]] || { echo 'Missing repo checkout and external/wpa_supplicant_8' >&2; exit 1; }
# The minimal TWRP manifest removes this project; re-add the official pinned source.
if ! repo list -p | grep -qx external/wpa_supplicant_8; then
mkdir -p .repo/local_manifests
cat > .repo/local_manifests/dodge-wifi.xml <<'XML'
<?xml version="1.0" encoding="UTF-8"?>
<manifest>
  <remote name="dodge-aosp" fetch="https://android.googlesource.com"/>
  <project name="platform/external/wpa_supplicant_8"
           path="external/wpa_supplicant_8"
           remote="dodge-aosp"
           revision="5c473a2f2e6f8fda0f8334e75b66a2c203bf8695"/>
</manifest>
XML
fi
repo sync -c -j2 --no-clone-bundle --no-tags external/wpa_supplicant_8
