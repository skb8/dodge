#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Resolve the small official manifest before restoring a binary module cache.

Every project revision, including mutable OnePlus branches, becomes an immutable
commit. The exact same locked manifest is used for the real source sync.
"""
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
checkout, output = map(lambda p: Path(p).resolve(), sys.argv[1:])
if not checkout.exists():
    subprocess.run(['git', 'clone', '--depth=1', '--branch', 'oneplus/sm8750',
                    'https://github.com/OnePlusOSS/kernel_manifest.git', str(checkout)], check=True)
else:
    subprocess.run(['git', '-C', str(checkout), 'fetch', '--depth=1', 'origin', 'oneplus/sm8750'], check=True)
    subprocess.run(['git', '-C', str(checkout), 'checkout', '--detach', 'FETCH_HEAD'], check=True)
manifest_revision = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], text=True).strip()
tree = ET.parse(checkout / 'oneplus_13_b.xml')
root = tree.getroot()
if root.findall('include') or root.findall('extend-project'):
    raise SystemExit('Manifest layout changed: update the source-lock resolver before caching')
remotes = {r.attrib['name']: r.attrib['fetch'] for r in root.findall('remote')}
default = root.find('default')
if default is None:
    raise SystemExit('Missing manifest defaults')
for project in root.findall('project'):
    revision = project.get('revision', default.get('revision', ''))
    if not re.fullmatch(r'[0-9a-f]{40}', revision):
        remote = project.get('remote', default.get('remote'))
        url = remotes[remote].rstrip('/') + '/' + project.attrib['name']
        ref = revision if revision.startswith('refs/') else 'refs/heads/' + revision
        answer = subprocess.check_output(['git', 'ls-remote', '--exit-code', url, ref], text=True).splitlines()
        if len(answer) != 1:
            raise SystemExit(f'Ambiguous project revision: {project.attrib["name"]} {revision}')
        revision = answer[0].split()[0]
    if not re.fullmatch(r'[0-9a-f]{40}', revision):
        raise SystemExit('Expected an immutable source commit')
    project.set('revision', revision)
output.mkdir(parents=True, exist_ok=True)
ET.indent(tree)
tree.write(output / 'oneplus_13_b.xml', encoding='UTF-8', xml_declaration=True)
(output / 'manifest-revision').write_text(manifest_revision + '\n')
print(f'Locked {len(root.findall("project"))} official kernel projects.')
