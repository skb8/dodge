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

GitHub Actions → **Build OrangeFox dodge image** builds the official OnePlus WLAN
module dependency set and then OrangeFox `recovery.img` on pushes to `fox_14.1`.
The main branch is `fox_14.1`. Manual **Run workflow** supports disabling the
source-driver build to build recovery against the installed slot's modules.
Download `OrangeFox-dodge-<commit>` for `OrangeFox-dodge.img`, `SHA256SUMS` and the
pinned recovery source manifest. WLAN modules, their source manifest and build
logs are separate artifacts. Pull requests run host validation only.

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
OnePlus branch or build recipe therefore triggers a fresh driver build.
