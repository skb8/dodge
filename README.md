# OnePlus 13 dodge Android device tree

## Working

- [X] Display
- [X] Touch (Even in FastbootD)
- [X] Decryption
- [X] Flashing
- [X] Backup & Restore
- [X] MTP/OTG Storage
- [X] ADB/FastbootD
- [X] Factory Reset
- [X] Vibrator
- [X] Display & Vibration Settings

## Not working
- [ ] ????????

# How To Build

### Clone & Sync Source
```
mkdir -p ~/OrangeFox_14
cd ~/OrangeFox_14
git clone https://gitlab.com/OrangeFox/sync.git
cd sync
./orangefox_sync.sh --branch 14.1 --path ~/fox_14.1
```
### Clone Device-tree
```
cd ~/fox_14.1/device
mkdir -p oneplus
cd oneplus
git clone https://gitlab.com/OrangeFox/device/dodge.git dodge
```
### BUILD!
```
cd ~/fox_14.1
source build/envsetup.sh
lunch twrp_dodge-ap2a-eng
mka adbd recoveryimage
```

## Initial Wi-Fi and automated images

Native Wi-Fi is under Settings → Wi-Fi, with connection state on the lock screen
and pull-down console. It uses the recovery theme; there is no camera/QR flow or
saved-networks menu. See [Wi-Fi implementation and limitations](wifi/README.md).

GitHub Actions → **Build OrangeFox dodge image** builds `recovery.img` on pushes
to the main branch `fox_14.1`. It does not rebuild the OnePlus kernel. Download
`OrangeFox-dodge-<commit>` for `OrangeFox-dodge.img`, `SHA256SUMS` and its source
manifest. It includes prebuilt modules when present in `wifi/modules/`; the
runtime can also load the installed slot's compatible modules from vendor_dlkm.

**Build OnePlus WLAN modules** is an experimental, manual-only workflow. It is
never started by a push or required by recovery builds. The current upstream
full-build entry point did not emit WLAN modules in the tested manifest; a
source-built WLAN package has not been published. Ordinary recovery builds use
compatible modules from the installed slot, or a complete prebuilt package
committed in `wifi/modules/`. Pull requests run host checks.

For a local build, sync OrangeFox as above, put this tree at
`device/oneplus/dodge`, and run from the Android source root:

```bash
# Optional: build source modules using the complete official OnePlus manifest.
device/oneplus/dodge/scripts/build-oneplus-wifi.sh /path/to/oneplus13-kernel

# Applies the pinned-source patches and builds/verifies the ready-to-download image.
device/oneplus/dodge/scripts/build-recovery.sh "$PWD"
```

Source integration is pinned/tested against OrangeFox recovery
`bcff7d6e9ee6f80387d1b036c0f9e58216053b69` and AOSP supplicant
`5c473a2f2e6f8fda0f8334e75b66a2c203bf8695` (`android-14.0.0_r1`).
The apply script checks patch compatibility and supports being run again.
It fails on incompatible upstream changes rather than silently skipping Wi-Fi.
A complete Android build needs substantially more storage than just this tree.

### Image type and installation

`OrangeFox-dodge.img` is a **recovery partition image**, not a standalone image
for `fastboot boot`. This device tree sets
`BOARD_EXCLUDE_KERNEL_FROM_RECOVERY_IMAGE := true`: its Android v4 header has
`kernel_size = 0`, and recovery uses the kernel from the phone's boot slot.
The 100 MiB file size is the configured recovery partition size. Renaming the
file to `boot.img` does not change its format. A loader can reject it with
`Failed to load/authenticate boot image: Bad Buffer Size` when used with
`fastboot boot`.

For installation, first confirm the device's recovery partition from bootloader
fastboot (these commands only read information):

```sh
fastboot getvar current-slot
fastboot getvar partition-size:recovery_a
fastboot getvar partition-size:recovery_b
```

On an unlocked OnePlus 13 with the expected recovery partitions, keep a matching
stock recovery image available, then install to the current recovery slot:

```sh
fastboot flash recovery OrangeFox-dodge.img
fastboot reboot recovery
```

This image belongs only in `recovery`; do not flash it to `boot`, `init_boot` or
`vendor_boot`. Neither compilation nor the image checks establish hardware boot
compatibility.

For temporary loading with `fastboot boot`, a separate image must include a
compatible kernel. Use the original `boot.img` from the phone's **installed
firmware** to obtain that prebuilt kernel. This does not require compiling the
full OnePlus kernel. The kernel-less recovery artifact cannot be used directly
for that mode.

### Actions caches

Recovery uses a compressed **3 GB ccache**, restored across commits and saved
also after failed compilations so completed work can be reused. The compiler's
content and source/header changes are still checked by ccache. Cache statistics
are shown at the end of the recovery job.

The OnePlus job resolves every project revision to an immutable commit before
looking up its **WLAN module-package cache**. Its key includes the complete source
lock and build/collection scripts; no prefix fallback is allowed for modules.
A hit skips the full kernel sync/build, then verifies module dependencies and the
built manifest against the source lock before publishing the package. A changed
OnePlus branch or build recipe invalidates that cache on the next manual run.
