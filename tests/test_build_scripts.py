# SPDX-License-Identifier: GPL-3.0-or-later
"""Regression checks for build failures being reported as successful and unsafe cache reuse."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
(ROOT / ".tests-build").mkdir(exist_ok=True)

class BuildFailureTests(unittest.TestCase):
    def run_build(self, envsetup, pipeline=False):
        with tempfile.TemporaryDirectory(dir=ROOT / '.tests-build') as directory:
            work = Path(directory)
            scripts = work / 'device/scripts'
            scripts.mkdir(parents=True)
            for name in ['build-recovery.sh', 'run-android-build.sh', 'verify-image.py']:
                shutil.copy2(ROOT / 'scripts' / name, scripts / name)
            for name in ['sync-wifi-deps.sh', 'apply-wifi.sh']:
                file = scripts / name
                file.write_text('#!/bin/bash\nexit 0\n')
                file.chmod(0o755)
            android = work / 'android'
            (android / 'build').mkdir(parents=True)
            (android / 'build/envsetup.sh').write_text(envsetup)
            command = ['bash', str(scripts / 'build-recovery.sh'), str(android)]
            if pipeline:
                command = ['bash', '--noprofile', '--norc', '-e', '-o', 'pipefail', '-c',
                           'bash "$1" "$2" 2>&1 | tee "$3"', 'ci',
                           str(scripts / 'build-recovery.sh'), str(android), str(work / 'build.log')]
            return subprocess.run(command, capture_output=True, text=True)

    def test_envsetup_disables_errexit_then_lunch_fails(self):
        result = self.run_build('set +e\nlunch() { return 3; }\nmka() { return 0; }\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Android lunch failed', result.stderr)

    def test_compile_failure_survives_workflow_log_pipeline(self):
        workflow = (ROOT / '.github/workflows/recovery.yml').read_text()
        self.assertIn('defaults:\n  run:\n    shell: bash', workflow)
        result = self.run_build('set +e\nlunch() { return 0; }\nmka() { return 7; }\n', pipeline=True)
        self.assertEqual(result.returncode, 7)
        self.assertIn('Recovery compilation failed', result.stdout)

    def test_compile_failure_is_propagated(self):
        result = self.run_build('set +e\nlunch() { return 0; }\nmka() { return 7; }\n')
        self.assertEqual(result.returncode, 7)
        self.assertIn('Recovery compilation failed', result.stderr)

    def test_shell_exits_zero_without_an_image_is_not_a_success(self):
        result = self.run_build('exit 0\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Missing/oversized recovery.img', result.stderr)

    def test_success_status_without_an_image_is_not_packaged(self):
        result = self.run_build('lunch() { return 0; }\nmka() { return 0; }\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Missing/oversized recovery.img', result.stderr)

class KernelRunnerTests(unittest.TestCase):
    def test_headless_terminal_and_kernel_failure_status(self):
        with tempfile.TemporaryDirectory(dir=ROOT / '.tests-build') as directory:
            work = Path(directory)
            device = work / 'device/scripts'
            device.mkdir(parents=True)
            shutil.copy2(ROOT / 'scripts/build-oneplus-wifi.sh', device / 'build-oneplus-wifi.sh')
            (device / 'collect-wifi-modules.py').write_text('raise SystemExit("must not collect a failed build")')
            kernel = work / 'kernel'
            (kernel / '.repo').mkdir(parents=True)
            upstream = kernel / 'kernel_platform/oplus/build'
            upstream.mkdir(parents=True)
            (upstream / 'oplus_build_kernel.sh').write_text(
                '#!/bin/bash\n[[ "$TERM" == xterm ]] || exit 99\necho "kernel failure fixture"\nexit 7\n')
            tools = work / 'bin'
            tools.mkdir()
            repo = tools / 'repo'
            repo.write_text('#!/bin/bash\nexit 0\n')
            repo.chmod(0o755)
            import os
            env = dict(os.environ, PATH=str(tools) + ':' + os.environ['PATH'], TERM='dumb')
            result = subprocess.run(['bash', str(device / 'build-oneplus-wifi.sh'), str(kernel)],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 7)
            self.assertIn('kernel failure fixture', result.stdout)

class ModuleCacheTests(unittest.TestCase):
    def check_package(self, locked='a' * 40, built='a' * 40, missing_dependency=False):
        with tempfile.TemporaryDirectory(dir=ROOT / '.tests-build') as directory:
            work = Path(directory)
            modules = work / 'modules'
            modules.mkdir()
            for filename in ['qca_cld3_test.ko', 'cnss2.ko']:
                (modules / filename).write_bytes(b'\x7fELFtest-fixture')
            (modules / 'VERMAGIC').write_text('test-kernel')
            (modules / 'modules.dep').write_text('qca_cld3_test.ko: cnss2.ko\ncnss2.ko:\n')
            if missing_dependency:
                (modules / 'cnss2.ko').unlink()
            for name, sha in [('lock', locked), ('built', built)]:
                root = ET.Element('manifest')
                ET.SubElement(root, 'project', name='vendor-kernel', path='./', revision=sha)
                ET.ElementTree(root).write(work / f'{name}.xml')
            return subprocess.run(['python3', str(ROOT / 'scripts/check-wlan-cache.py'),
                                   str(work / 'lock.xml'), str(work / 'built.xml'), str(modules)],
                                  capture_output=True, text=True)

    def test_exact_lock_is_accepted(self):
        self.assertEqual(self.check_package().returncode, 0)

    def test_different_source_revision_is_rejected(self):
        result = self.check_package(built='b' * 40)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('does not match', result.stderr)

    def test_missing_module_dependency_is_rejected(self):
        result = self.check_package(missing_dependency=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Missing dependency', result.stderr)

if __name__ == '__main__':
    (ROOT / '.tests-build').mkdir(exist_ok=True)
    unittest.main()
