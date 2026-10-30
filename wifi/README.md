# Initial native Wi-Fi support

The device tree targets OrangeFox 14.1. Recovery does not contain the Android
Framework, Settings, WifiManager, WifiPickerTracker or ConnectivityService.
The Settings interaction is adapted to OrangeFox's own widgets and a native
nl80211/wpa_supplicant control-socket backend; Java/Kotlin is not embedded.

## User interface and connection state

Settings → Wi-Fi → Enable Wi-Fi → select an access point. Open/OWE networks
connect immediately; WPA/WPA2-Personal and WPA3-SAE open a masked password page
with a show-password toggle. The list groups BSSIDs by SSID/security, orders by
signal and shows at most 32 entries. Refresh, disconnect and turn-off controls
are on the same page. Enterprise EAP, WEP, hidden SSIDs and static IP configuration
are outside this initial implementation. There is no saved-networks menu, QR
scanner, DPP provisioning, camera integration or password persistence.

A single worker serializes control requests and publishes snapshots on the GUI
thread. Selection uses the displayed scan snapshot, so a later scan cannot
change the target of a password submission. A successful SELECT_NETWORK reply
only acknowledges the request; it does not report a successful connection.
The worker waits for COMPLETED, then DHCP, then a separate internet probe.
Authentication rejection, association timeout, missing driver/supplicant and
DHCP failure have separate states. These states and the SSID are also displayed
on the lock screen and console pull-down overlay. English and Russian strings
are included; other languages fall back to English.

IPv4 DHCP is provided by BusyBox udhcpc. Callback session IDs reject stale lease
notifications after disconnect/reconnect; disconnect clears addresses/routes.
Android's netd resolver is unavailable in recovery, so the probe queries the
DHCP DNS server directly and requests the conventional HTTP generate_204
endpoint over wlan0. HTTP 204 reports internet availability; 200/redirect reports
that sign-in is required; timeout/DNS failure reports an unverified connection.
This is an initial reachability/captive-portal check, not Android NetworkMonitor's
full HTTP+HTTPS validation. A captive portal login browser and IPv6 are not included.

Passwords are not passed to a shell, command line or property service. SSIDs use
hex SET_NETWORK values; quoted passwords are escaped and control characters are
rejected. The config has update_config=0, no SAVE_CONFIG is issued, and the active
network is removed on disconnect. The input is cleared on submission, cancel,
leaving the password page, or locking. wpa_supplicant does not use -K/key logging.

## Official Settings reference

Reviewed the actual `android17-release` branch at commit
`213829fb67f5e070029e0513a088f31bc8f5c1ed`, rather than a GitHub mirror:

- [NetworkProviderSettings.java](https://android.googlesource.com/platform/packages/apps/Settings/+/213829fb67f5e070029e0513a088f31bc8f5c1ed/src/com/android/settings/network/NetworkProviderSettings.java)
  — network selection, onSubmit/connect and asynchronous connection result handling.
- [WifiDialog2.kt](https://android.googlesource.com/platform/packages/apps/Settings/+/213829fb67f5e070029e0513a088f31bc8f5c1ed/src/com/android/settings/wifi/WifiDialog2.kt)
  — connect/cancel interaction and validation before submission.

This is an independent native implementation of that interaction. In Android,
actual connectivity validation belongs to ConnectivityService/NetworkMonitor,
not these two Settings UI files.

## Driver and firmware

The official OnePlus manifest is `OnePlusOSS/kernel_manifest`, branch
`oneplus/sm8750`, file `oneplus_13_b.xml`. It supplies common kernel, MSM kernel,
and `android_kernel_modules_and_devicetree_oneplus_sm8750` together. The WLAN
sources are `vendor/qcom/opensource/wlan/{qcacld-3.0,qca-wifi-host-cmn,fw-api,platform}`.
The manifest revision observed during implementation for the modules repository
was `d50b305f7da9e14715a25120a4ac7b1a4b8b97c3` on
`oneplus/sm8750_b_16.0.0_oneplus_13`.

`scripts/build-oneplus-wifi.sh` syncs the full manifest, builds through OnePlus's
`oplus_build_kernel.sh sun perf`, and stages only WLAN and its transitive module
dependencies. Source manifests and module vermagic accompany the build artifacts.
No kernel or camera driver is added to recovery.img. Source modules are optional
for manual local builds, and enabled by default in the push/dispatch workflow.

This recovery explicitly excludes the kernel from its image. The kernel comes
from the device's boot slot. A source-built module must match that kernel's ABI,
symbol CRCs, signature policy and firmware. Updating to a new OnePlus source
branch does not make its modules compatible with every installed firmware.
The loader therefore tries the installed slot's module dependency set first,
then the source-built set; the kernel can reject incompatible modules. It never
force-loads modules or replaces boot/vendor_dlkm. Source-built modules are useful
with a matching boot kernel and are not a universal replacement for stock ones.

On explicit enable, recovery stages vendor Wi-Fi configuration/module metadata,
restores the previous vendor/vendor_dlkm mount state, mounts modem firmware read-only,
and loads WLAN through libmodprobe. It uses stock firmware/calibration, not invented
or redistributable firmware blobs. Firmware stays mounted while Wi-Fi is in use.
A device without readable slot firmware or a compatible module reports unavailable.
This flow requires testing on a OnePlus 13; host tests cannot establish radio support.

## Validation

`scripts/test-wifi.sh` runs security/parser/configuration tests and a real fake-
supplicant Unix-datagram exchange with timeout isolation under ASan/UBSan. It also
checks XML, shell syntax and theme generation. `WIFI_SANITIZE=0` and
`DODGE_TEST_NO_SOCKETS=1` are explicit local fallbacks for a restricted runtime;
CI uses neither fallback. The release build also audits required ramdisk files,
executable permissions, native Wi-Fi dynamic libraries and the 100 MiB image limit.

Hardware checks still required: open/WPA2/WPA3 association, wrong password, DHCP
failure and renewal, AP loss/reconnect, repeated off/on, locked-screen updates,
portal/unreachable internet, and coexistence with decryption/MTP/fastbootd.
