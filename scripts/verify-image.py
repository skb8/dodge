#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail packaging when required Wi-Fi pieces were silently omitted from recovery."""
from pathlib import Path
import os
import sys
p = Path(sys.argv[1])
image = p / 'recovery.img'
if not image.is_file() or not 0 < image.stat().st_size <= 104857600:
    raise SystemExit('Missing/oversized recovery.img (partition: 100 MiB)')
ramdisk = p / 'recovery/root'
for name in ['system/bin/wpa_supplicant_dodge', 'system/etc/wpa_supplicant_dodge.conf',
             'system/bin/dodge-wifi-stage', 'system/bin/dodge-wifi-dhcp',
             'system/bin/dodge-wifi-lease', 'system/bin/dodge-wifi-clear',
             'twres/pages/wifi.xml']:
    file = ramdisk / name
    if not file.is_file():
        raise SystemExit(f'Missing ramdisk component: {file}')
    if name.startswith('system/bin/') and not os.access(file, os.X_OK):
        raise SystemExit(f'Not executable: {file}')
if not any((ramdisk / x).is_file() for x in ['system/bin/busybox', 'sbin/busybox']):
    raise SystemExit('BusyBox is required for DHCP')
init_files = list(ramdisk.glob('**/init*.rc'))
if not any('service dodge_wifi_supplicant' in f.read_text() for f in init_files):
    raise SystemExit('Wi-Fi init services were not installed')
print('Recovery image and Wi-Fi ramdisk components verified.')

# Check the supplicant's complete dynamic dependency closure inside this ramdisk.
import re
import subprocess
search = [ramdisk / d for d in ['system/lib64', 'vendor/lib64', 'lib64', 'system/lib', 'lib']]
pending = [ramdisk / 'system/bin/wpa_supplicant_dodge']
seen = set()
while pending:
    file = pending.pop()
    if file in seen:
        continue
    seen.add(file)
    dynamic = subprocess.check_output(['readelf', '-d', str(file)], text=True)
    for name in re.findall(r'\(NEEDED\).*?\[(.*?)\]', dynamic):
        dependency = next((directory / name for directory in search if (directory / name).is_file()), None)
        if dependency is None:
            raise SystemExit(f'Missing native Wi-Fi dependency {name} required by {file.name}')
        pending.append(dependency)
print('Native Wi-Fi dynamic dependency closure verified.')
