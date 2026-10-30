#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify a built/restored WLAN package against its immutable source lock."""
from pathlib import Path
import posixpath
import sys
import xml.etree.ElementTree as ET
lock, built, modules = map(Path, sys.argv[1:])
def projects(path):
    return {(p.attrib['name'], posixpath.normpath(p.get('path', p.attrib['name']))): p.attrib['revision']
            for p in ET.parse(path).getroot().findall('project')}
if projects(lock) != projects(built):
    raise SystemExit('WLAN package manifest does not match the locked source revisions')
files = list(modules.glob('*.ko'))
if not files or not any(p.name.startswith('qca_cld3_') or p.name == 'wlan.ko' for p in files):
    raise SystemExit('WLAN module package is empty')
for file in files:
    with file.open('rb') as stream:
        if stream.read(4) != b'\x7fELF':
            raise SystemExit(f'Not an ELF module: {file}')
if not (modules / 'VERMAGIC').read_text().strip():
    raise SystemExit('Missing module ABI metadata')
for line in (modules / 'modules.dep').read_text().splitlines():
    module, deps = line.split(':', 1)
    for name in [module, *deps.split()]:
        if not (modules / name).is_file():
            raise SystemExit(f'Missing dependency in WLAN package: {name}')
print(f'WLAN package verified: {len(files)} modules, exact source-lock match.')
