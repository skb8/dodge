#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import sys
import xml.etree.ElementTree as ET
root = Path(__file__).resolve().parents[1]
for source in sorted((root / 'wifi' / 'languages').glob('*.xml')):
    target = Path(sys.argv[1]) / 'gui/theme/common/languages' / source.name
    if not target.exists():
        raise SystemExit(f'Missing OrangeFox language: {target}')
    text = target.read_text()
    additions = []
    existing = {node.attrib.get('name') for node in ET.fromstring(text).iter('string')}
    for node in ET.parse(source).getroot():
        if node.attrib['name'] not in existing:
            additions.append(ET.tostring(node, encoding='unicode'))
    if additions:
        if text.count('</resources>') != 1:
            raise SystemExit(f'Unexpected language format: {target}')
        target.write_text(text.replace('</resources>', '\n'.join(additions) + '\n</resources>'))
