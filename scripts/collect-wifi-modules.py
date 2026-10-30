#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Select WLAN modules and their transitive dependencies from the official build."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
root, target = map(Path, sys.argv[1:])
dist = root / 'kernel_platform/out/msm-kernel-sun-perf/dist'
if not dist.is_dir():
    raise SystemExit(f'Missing official kernel dist: {dist}')
modules = {}
for path in sorted(dist.rglob('*.ko')):
    name = subprocess.check_output(['modinfo', '-F', 'name', str(path)], text=True).strip()
    if name in modules and modules[name].read_bytes() != path.read_bytes():
        raise SystemExit(f'Ambiguous module in dist: {name}')
    modules[name] = path
seeds = [name for name, path in modules.items()
         if path.name.startswith('qca_cld3_') or path.name == 'wlan.ko']
if not seeds:
    raise SystemExit('No source-built WLAN driver found')
builtins = set()
for file in dist.rglob('modules.builtin'):
    builtins.update(Path(line).stem.replace('-', '_') for line in file.read_text().splitlines())
closure = set()
def collect(name):
    if name in closure or name in builtins:
        return
    if name not in modules:
        raise SystemExit(f'Missing module dependency: {name}')
    closure.add(name)
    deps = subprocess.check_output(['modinfo', '-F', 'depends', str(modules[name])], text=True).strip()
    for dep in filter(None, deps.split(',')):
        collect(dep.replace('-', '_'))
    soft = subprocess.check_output(['modinfo', '-F', 'softdep', str(modules[name])], text=True)
    for dep in soft.split():
        if dep not in ['pre:', 'post:']:
            collect(dep.replace('-', '_'))
for seed in seeds:
    collect(seed)
# Only clean this script's generated destination after the complete closure was verified.
if target.exists():
    shutil.rmtree(target)
target.mkdir(parents=True)
vermagic = set()
for name in sorted(closure):
    shutil.copy2(modules[name], target / modules[name].name)
    vermagic.add(subprocess.check_output(['modinfo', '-F', 'vermagic', str(modules[name])], text=True).strip())
if len(vermagic) != 1:
    raise SystemExit('Mixed kernel ABIs in WLAN module closure')
(target / 'VERMAGIC').write_text(next(iter(vermagic)) + '\n')
# libmodprobe consumes plain-text dependency metadata, without host depmod's hierarchy.
lines = []
for name in sorted(closure):
    deps = subprocess.check_output(['modinfo', '-F', 'depends', str(modules[name])], text=True).strip()
    paths = [modules[d.replace('-', '_')].name for d in filter(None, deps.split(','))
             if d.replace('-', '_') in closure]
    lines.append(modules[name].name + ': ' + ' '.join(paths))
(target / 'modules.dep').write_text('\n'.join(lines) + '\n')
for name in ['modules.alias', 'modules.softdep']:
    source = dist / name
    if source.exists():
        lines = [line for line in source.read_text().splitlines()
                 if len(line.split()) > 2 and
                 (line.split()[-1].replace('-', '_') in closure if name == 'modules.alias'
                  else line.split()[1].replace('-', '_') in closure)]
        (target / name).write_text('\n'.join(lines) + '\n')
    else:
        (target / name).write_text('')
print(f'Staged {len(closure)} WLAN/dependency modules; no unrelated drivers.')
