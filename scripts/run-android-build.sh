#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Android envsetup can change shell options/functions or exit the shell. Keep it
# in a child process, so the caller always performs its own image verification.
set -eo pipefail
cd "${1:?Usage: run-android-build.sh /path/to/fox_14.1}"
source build/envsetup.sh || { echo 'Android envsetup failed' >&2; exit 1; }
export ALLOW_MISSING_DEPENDENCIES=false
lunch twrp_dodge-ap2a-eng || { echo 'Android lunch failed' >&2; exit 1; }
set -eo pipefail
mka -j"${BUILD_JOBS:-4}" adbd recoveryimage || { status=$?; echo 'Recovery compilation failed' >&2; exit "$status"; }
