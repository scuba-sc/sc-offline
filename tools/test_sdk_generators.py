#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise real proof CLI defaults/overrides through the first CMake invocation."""
import importlib.util
import shutil
import subprocess
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))


class Configured(Exception):
    def __init__(self, command):
        self.command = [str(arg) for arg in command]


def first_configure(command, cwd):
    raise Configured(command)


def proof_module(feature):
    spec = importlib.util.spec_from_file_location('generator_' + feature, TOOLS / ('check_' + feature + '_sdk.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class GeneratorSelection(unittest.TestCase):
    def test_default_matches_hosted_toolchain_and_override_reaches_configure(self):
        for feature in ('inspector', 'npc', 'build', 'loadout'):
            for requested in (None, 'Visual Studio 17 2022'):
                with self.subTest(feature=feature, requested=requested), tempfile.TemporaryDirectory() as temp:
                    module = proof_module(feature)
                    root = Path(temp)
                    argv = ['--archive', str(root / 'sdk.zip'), '--sums', str(root / 'SHA256SUMS'),
                            '--work', str(root / 'proof')]
                    if requested:
                        argv += ['--generator', requested]
                    args = module.parse_args(argv)
                    expected = requested or 'Visual Studio 18 2026'
                    self.assertEqual(args.generator, expected)
                    with patch.object(module, 'os', SimpleNamespace(name='nt')), \
                         patch.object(module, 'verify_extract', return_value=(root / 'sdk', set())), \
                         patch.object(module, 'run', side_effect=first_configure), \
                         self.assertRaises(Configured) as caught:
                        module.main(args)
                    command = caught.exception.command
                    self.assertEqual(command[command.index('-G') + 1], expected)
                    self.assertEqual(command[command.index('-A') + 1], 'x64')

    @unittest.skipUnless(shutil.which('cmake'), 'CMake required for configure refusal')
    def test_missing_snapshot_contract_refuses_configure_before_build(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            sdk = root / 'sdk'
            (sdk / 'include').mkdir(parents=True)
            (sdk / 'SHA256SUMS').write_text('')
            for name in ('sco_api.h', 'sc_actors.h', 'sc_entities.h', 'sc_spatial.h'):
                (sdk / 'include' / name).write_text('// synthetic header fixture\n')
            configured = subprocess.run(['cmake', '-S', str(TOOLS.parent / 'plugins'), '-B', str(root / 'build'),
                                         '-DSCO_SDK_INCLUDE_DIR=' + str(sdk / 'include'),
                                         '-DSCO_ADDITIONAL_FEATURES=OFF'], capture_output=True, text=True, timeout=60)
            self.assertNotEqual(configured.returncode, 0)
            self.assertIn('SDK public header missing: sc_actor_stats.h', configured.stdout + configured.stderr)

    def test_nondefault_sdk_requires_candidate_before_extraction_or_configure(self):
        for overrides in (['--sdk-version', '2.5.0'], ['--sdk-sha256', '0' * 64],
                          ['--sdk-version', '2.5.0', '--sdk-sha256', '0' * 64]):
            with self.subTest(overrides=overrides), tempfile.TemporaryDirectory() as temp:
                module = proof_module('inspector')
                root = Path(temp)
                args = module.parse_args(['--archive', str(root / 'sdk.zip'), '--sums', str(root / 'SHA256SUMS'),
                                          '--work', str(root / 'proof'), *overrides])
                with patch.object(module, 'verify_extract') as extract, patch.object(module, 'run') as run:
                    with self.assertRaisesRegex(AssertionError, 'Non-default SDK identity requires --candidate'):
                        module.main(args)
                    extract.assert_not_called()
                    run.assert_not_called()
                    self.assertFalse(args.work.exists())

    def test_build_contracts_preflight_identity_lock(self):
        from check_inspector_sdk import SDK_VERSION, SDK_SHA256, explicit_preflight_pin
        def args(**kw):
            values = dict(sdk_version=SDK_VERSION, sdk_sha256=SDK_SHA256, archive_sha256=None, candidate=False)
            values.update(kw); return SimpleNamespace(**values)
        self.assertEqual(explicit_preflight_pin(args()), ('sco-sdk-2.4.0', SDK_SHA256))
        digest = '1' * 64
        self.assertEqual(explicit_preflight_pin(args(candidate=True, sdk_sha256=digest, archive_sha256=digest)), ('sco-sdk-2.4.0', digest))
        for invalid in (args(sdk_sha256=digest), args(archive_sha256=digest),
                        args(candidate=True, sdk_sha256=digest),
                        args(candidate=True, sdk_sha256=digest, archive_sha256='2' * 64),
                        args(candidate=True, archive_sha256=SDK_SHA256)):
            with self.assertRaises(AssertionError): explicit_preflight_pin(invalid)

    def test_build_contracts_preflight_rejects_mismatched_archive(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / 'sco-sdk-2.4.0.zip'; archive.write_bytes(b'not matching')
            sums = root / 'SHA256SUMS'; sums.write_text('0' * 64 + '  ' + archive.name + '\n')
            for tool in ('check_build_sdk.py', 'contracts_sdk_check.py'):
                work = root / (tool + '.work')
                extra = ['--slice', 'economy'] if tool.startswith('contracts') else []
                result = subprocess.run([sys.executable, str(TOOLS / tool), '--archive', str(archive),
                    '--sums', str(sums), '--work', str(work), '--candidate',
                    '--sdk-sha256', '0' * 64, '--archive-sha256', '0' * 64, *extra],
                    capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('SDK archive SHA256 mismatch', result.stderr)
                self.assertFalse((work / 'sdk').exists())

    def test_spawn_preflight_lock_refusals_precede_extraction(self):
        cases = [
            (['--sdk-sha256', '0' * 64], 'Non-default SDK identity requires --candidate'),
            (['--archive-sha256', '0' * 64], '--archive-sha256 requires --candidate'),
            (['--candidate', '--sdk-sha256', '0' * 64], '--candidate requires matching'),
            (['--candidate', '--sdk-sha256', '0' * 64, '--archive-sha256', '1' * 64], '--candidate requires matching'),
            (['--candidate', '--archive-sha256', '124751534277b89f0516af1897919c3485923811596e159a1f683bb80b1c243b'], '--candidate must select a non-default'),
        ]
        for extra, message in cases:
            with self.subTest(extra=extra), tempfile.TemporaryDirectory() as temp:
                work = Path(temp) / 'proof'
                result = subprocess.run([sys.executable, str(TOOLS / 'check_spawn_sdk.py'),
                    '--archive', 'missing.zip', '--sums', 'missing-sums', '--work', str(work), *extra],
                    capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)
                self.assertFalse(work.exists())

    def test_inspector_defaults_keep_published_rc_identity(self):
        module = proof_module('inspector')
        args = module.parse_args(['--archive', 'sdk.zip', '--sums', 'SHA256SUMS', '--work', 'proof'])
        self.assertEqual(args.sdk_version, '2.4.0')
        self.assertEqual(args.sdk_sha256, '124751534277b89f0516af1897919c3485923811596e159a1f683bb80b1c243b')
        self.assertFalse(args.candidate)


if __name__ == '__main__':
    unittest.main(verbosity=2)
