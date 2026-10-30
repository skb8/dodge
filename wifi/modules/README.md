# Prebuilt WLAN module package

Place the complete output of the separate **Build OnePlus WLAN modules** workflow
here: `.ko` files, `modules.dep`, `modules.alias`, `modules.softdep`, and `VERMAGIC`.
Keep its pinned source manifest as `source-manifest.xml` for reproducibility.
Commit the files to include them directly in ordinary recovery-image builds.
Do not combine modules from different kernels or omit their dependency closure.

Until a source-built package is committed, recovery can stage the installed
slot's compatible modules from vendor/vendor_dlkm at runtime. Firmware and
calibration still come from the phone's read-only modem/vendor partitions.
