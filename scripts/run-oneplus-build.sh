#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Use the same setup and prepare_vendor invocation as OnePlus's wrapper.
# Do not export errexit into its config-discovery child shells: their optional
# probes rely on ordinary bash semantics. Check each build result explicitly.
set +e
set +u
set -o pipefail
cd "${1:?Usage: run-oneplus-build.sh /path/to/oneplus13-kernel}" || exit 1
source kernel_platform/oplus/build/oplus_setup.sh sun perf || exit 1
init_build_environment || exit 1
mkdir -p "$ANDROID_PRODUCT_OUT" "$TOPDIR/LOGDIR" || exit 1
build_start_time || exit 1
./kernel_platform/build/android/prepare_vendor.sh sun perf 2>&1 | tee "$TOPDIR/LOGDIR/dodge-wifi-build.log"
status=$?
build_end_time
if [[ "$status" != 0 ]]; then
    echo "OnePlus kernel build failed (exit $status)" >&2
fi
exit "$status"
