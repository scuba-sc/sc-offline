#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise real proof CLI defaults/overrides through the first CMake invocation."""
import importlib.util
import hashlib
import json
import zipfile
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
    def test_posix_dependency_trace_flag_is_compiler_specific(self):
        from check_inspector_sdk import posix_dependency_trace_flags
        self.assertEqual(posix_dependency_trace_flags('Debian clang version 19.1.7'), [])
        self.assertEqual(posix_dependency_trace_flags('Apple clang version 16.0.0'), [])
        self.assertEqual(posix_dependency_trace_flags('gcc version 14.2.0'),
                         ['-fno-canonical-system-headers'])

    def test_compiler_system_symlink_header_stays_in_known_search_root(self):
        from check_inspector_sdk import compiler_system_dependency
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            system = root / 'compiler' / 'include'
            project = root / 'checkout'
            outside = root / 'uapi' / 'types.h'
            system.mkdir(parents=True)
            project.mkdir()
            outside.parent.mkdir()
            outside.write_text('// toolchain-owned header\n')
            system_header = system / 'types.h'
            system_header.symlink_to(outside)
            project_header = project / 'types.h'
            project_header.symlink_to(outside)
            self.assertTrue(compiler_system_dependency(system_header, [system]))
            self.assertFalse(compiler_system_dependency(project_header, [system]))

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

    def test_npc_configure_passes_verified_sdk_identity(self):
        for candidate in (False, True):
            with self.subTest(candidate=candidate), tempfile.TemporaryDirectory() as temp:
                module = proof_module('npc')
                root = Path(temp)
                archive = root / 'sdk.zip'
                argv = ['--archive', str(archive), '--sums', str(root / 'SHA256SUMS'),
                        '--work', str(root / 'proof')]
                digest = 'a' * 64
                if candidate:
                    argv += ['--candidate', '--sdk-version', '2.4.1-dev.1', '--sdk-sha256', digest,
                             '--archive-sha256', digest, '--sdk-source', 'b' * 40, '--sdk-profile', 'legacy']
                args = module.parse_args(argv)
                with patch.object(module, 'verify_extract', return_value=(root / 'sdk', set())) as verify, \
                     patch.object(module, 'run', side_effect=first_configure), \
                     self.assertRaises(Configured) as caught:
                    module.main(args)
                command = caught.exception.command
                self.assertIn('-DSCO_SDK_VERSION=' + args.sdk_version, command)
                self.assertIn('-DSCO_SDK_SHA256=' + args.sdk_sha256, command)
                self.assertIn('-DSCO_SDK_ARCHIVE=' + str(archive.resolve()), command)
                self.assertEqual(verify.call_count, 1)

    def test_loadout_isolated_copy_contains_only_needed_shared_picker(self):
        with tempfile.TemporaryDirectory() as temp:
            module = proof_module('loadout')
            root = Path(temp)
            args = module.parse_args(['--archive', str(root / 'sdk.zip'),
                                      '--sums', str(root / 'SHA256SUMS'), '--work', str(root / 'proof')])
            with patch.object(module, 'verify_extract', return_value=(root / 'sdk', set())), \
                 patch.object(module, 'run', side_effect=first_configure), \
                 self.assertRaises(Configured):
                module.main(args)
            source = root / 'proof/source'
            picker = source / 'plugins/picker.h'
            self.assertEqual(picker.read_bytes(), (TOOLS.parent / 'plugins/picker.h').read_bytes())
            from check_inspector_sdk import own_source
            self.assertTrue(own_source(picker, source, 'loadout', ()))
            unrelated = source / 'plugins/unrelated.h'
            unrelated.write_text('// not allowed\n')
            self.assertFalse(own_source(unrelated, source, 'loadout', ()))

    @unittest.skipUnless(shutil.which('cmake'), 'CMake required for configure refusal')
    def test_missing_snapshot_contract_refuses_configure_before_build(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            sdk = root / 'sdk'
            (sdk / 'include').mkdir(parents=True)
            (sdk / 'SHA256SUMS').write_text('')
            for name in ('sco_api.h', 'sc_actors.h', 'sc_entities.h', 'sc_spatial.h'):
                (sdk / 'include' / name).write_text('// synthetic header fixture\n')
            archive = root / 'sco-sdk-2.4.1-dev.1.zip'
            archive.write_bytes(b'synthetic archive for configure guard')
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            configured = subprocess.run(['cmake', '-S', str(TOOLS.parent / 'plugins'), '-B', str(root / 'build'),
                                         '-DSCO_SDK_INCLUDE_DIR=' + str(sdk / 'include'),
                                         '-DSCO_SDK_VERSION=2.4.1-dev.1', '-DSCO_SDK_SHA256=' + digest,
                                         '-DSCO_SDK_ARCHIVE=' + str(archive),
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
            values = dict(sdk_version=SDK_VERSION, sdk_sha256=SDK_SHA256, archive_sha256=None, candidate=False, sdk_source=None, sdk_profile=None)
            values.update(kw); return SimpleNamespace(**values)
        self.assertEqual(explicit_preflight_pin(args()), ('sco-sdk-2.4.0', SDK_SHA256))
        digest = '1' * 64
        self.assertEqual(explicit_preflight_pin(args(candidate=True, sdk_sha256=digest, archive_sha256=digest, sdk_source='a' * 40, sdk_profile='legacy')), ('sco-sdk-2.4.0', digest, 'a' * 40, 'legacy'))
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
                    '--sdk-sha256', '0' * 64, '--archive-sha256', '0' * 64, '--sdk-source', 'a' * 40, '--sdk-profile', 'legacy', *extra],
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
            (['--candidate', '--archive-sha256', '0df4995624e9b094d24e9c5534b1ea7357f517c83a0a8c8ed6ad7d4d475afcbc'], '--candidate must select a non-default'),
            (['--candidate', '--sdk-sha256', '0' * 64, '--archive-sha256', '0' * 64], '--candidate requires full lowercase --sdk-source'),
            (['--candidate', '--sdk-sha256', '0' * 64, '--archive-sha256', '0' * 64, '--sdk-source', 'a' * 40], '--candidate requires --sdk-profile legacy'),
            (['--candidate', '--sdk-sha256', '0' * 64, '--archive-sha256', '0' * 64, '--sdk-source', 'a' * 40, '--sdk-profile', 'core'], '--candidate requires --sdk-profile legacy'),
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

    def test_travel_preflight_lock_refusals_precede_extraction(self):
        cases = [
            (['--sdk-sha256', '0' * 64], 'Non-default SDK identity requires --candidate'),
            (['--archive-sha256', '0' * 64], '--archive-sha256 requires --candidate'),
            (['--candidate', '--sdk-sha256', '0' * 64], '--candidate requires matching'),
            (['--candidate', '--sdk-sha256', '0' * 64, '--archive-sha256', '1' * 64], '--candidate requires matching'),
            (['--candidate', '--archive-sha256', '0df4995624e9b094d24e9c5534b1ea7357f517c83a0a8c8ed6ad7d4d475afcbc'], '--candidate must select a non-default'),
            (['--candidate', '--sdk-sha256', '0' * 64, '--archive-sha256', '0' * 64], '--candidate requires full lowercase --sdk-source'),
            (['--candidate', '--sdk-sha256', '0' * 64, '--archive-sha256', '0' * 64, '--sdk-source', 'a' * 40], '--candidate requires --sdk-profile legacy'),
        ]
        for feature in ('quantum', 'teleport', 'npc', 'loadout', 'creative', 'discord', 'crew', 'mining'):
            for extra, message in cases:
                with self.subTest(feature=feature, extra=extra), tempfile.TemporaryDirectory() as temp:
                    work = Path(temp) / 'proof'
                    result = subprocess.run([sys.executable, str(TOOLS / ('check_' + feature + '_sdk.py')),
                        '--archive', 'missing.zip', '--sums', 'missing-sums', '--work', str(work), *extra],
                        capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(message, result.stderr)
                    self.assertFalse(work.exists())

    def test_travel_default_reaches_published_stable_verifier(self):
        from check_inspector_sdk import SDK_ROOT, SDK_SHA256
        import check_teleport_sdk as travel
        for feature in ('teleport', 'quantum'):
            with self.subTest(feature=feature), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                module = proof_module(feature)
                argv = ['--archive', str(root / 'sdk.zip'), '--sums', str(root / 'SHA256SUMS'),
                        '--work', str(root / 'proof')]
                verifier = travel if feature == 'quantum' else module
                with patch.object(verifier, 'verify_extract', side_effect=Configured([])) as extract:
                    with self.assertRaises(Configured):
                        if feature == 'quantum':
                            module.main(argv, spec=module.QUANTUM)
                        else:
                            module.main(argv)
                    self.assertEqual(extract.call_args.kwargs['pin'], (SDK_ROOT, SDK_SHA256))

    def test_other_consumers_default_to_published_stable_verifier(self):
        from check_inspector_sdk import SDK_ROOT, SDK_VERSION, SDK_SHA256
        for feature in ('npc', 'loadout', 'creative', 'discord', 'crew', 'mining'):
            with self.subTest(feature=feature), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                module = proof_module(feature)
                args = SimpleNamespace(archive=root / 'sdk.zip', sums=root / 'SHA256SUMS', work=root / 'proof',
                    sdk_version=SDK_VERSION, sdk_sha256=SDK_SHA256, candidate=False, archive_sha256=None,
                    generator='Visual Studio 18 2026', sanitizers=False, sanitize=False)
                with patch.object(module, 'verify_extract', side_effect=Configured([])) as extract:
                    with self.assertRaises(Configured):
                        module.main(args)
                    self.assertEqual(extract.call_args.kwargs['pin'], (SDK_ROOT, SDK_SHA256))

    def test_travel_preflight_rejects_archive_digest_mismatch(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / 'sco-sdk-2.4.0.zip'; archive.write_bytes(b'not matching')
            sums = root / 'SHA256SUMS'; sums.write_text('0' * 64 + '  ' + archive.name + '\n')
            for feature in ('quantum', 'teleport', 'npc', 'loadout', 'creative', 'discord', 'crew', 'mining'):
                work = root / feature
                result = subprocess.run([sys.executable, str(TOOLS / ('check_' + feature + '_sdk.py')),
                    '--archive', str(archive), '--sums', str(sums), '--work', str(work), '--candidate',
                    '--sdk-sha256', '0' * 64, '--archive-sha256', '0' * 64, '--sdk-source', 'a' * 40, '--sdk-profile', 'legacy'], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('SDK archive SHA256 mismatch', result.stderr)
                self.assertFalse((work / 'sdk').exists())

    def test_candidate_source_and_profile_metadata_enforced(self):
        from check_inspector_sdk import verify_extract
        source = 'a' * 40
        def fixture(root, compatibility, overrides=None):
            files = {'include/sco_api.h': b'fixture'}
            records = {'COMPATIBILITY.json': compatibility,
                'sdk-catalogue.json': {'source_sha': source},
                'sdk-guides.json': {'source_sha': source},
                'sdk-workflows.json': {'source_sha': source}}
            records.update(overrides or {})
            files.update({name: json.dumps(value).encode() for name, value in records.items() if value is not None})
            sums = ''.join(hashlib.sha256(data).hexdigest() + '  ' + name + '\n' for name, data in files.items())
            archive = root / 'sco-sdk-2.5.0.zip'
            with zipfile.ZipFile(archive, 'w') as z:
                for name, data in files.items(): z.writestr('sco-sdk-2.5.0/' + name, data)
                z.writestr('sco-sdk-2.5.0/SHA256SUMS', sums)
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            checksum = root / 'SHA256SUMS'; checksum.write_text(digest + '  ' + archive.name + '\n')
            return archive, checksum, ('sco-sdk-2.5.0', digest, source, 'legacy')
        good = {'profile': 'legacy', 'source_commits': {'core': source, 'game-sc': source}}
        cases = [(good, {}, None), (None, {}, 'metadata missing'),
                 ({'source_commits': good['source_commits']}, {}, 'profile mismatch'),
                 ({'profile': 'core', 'source_commits': good['source_commits']}, {}, 'profile mismatch'),
                 ({'profile': 'legacy', 'source_commits': {'core': 'b' * 40, 'game-sc': source}}, {}, 'source mismatch'),
                 ({'profile': 'legacy', 'source_commits': {'core': source}}, {}, 'source mismatch'),
                 (good, {'sdk-guides.json': {'source_sha': 'b' * 40}}, 'source mismatch'),
                 (good, {'sdk-workflows.json': None}, 'metadata missing')]
        for compatibility, overrides, error in cases:
            with self.subTest(error=error, overrides=overrides), tempfile.TemporaryDirectory() as temp:
                root = Path(temp); archive, sums, pin = fixture(root, compatibility, overrides)
                if error:
                    with self.assertRaisesRegex(AssertionError, error): verify_extract(archive, sums, root / 'out', pin)
                else:
                    verify_extract(archive, sums, root / 'out', pin)
        # The published/default path retains compatibility with older kits that
        # have no candidate source/profile metadata, while still verifying inventory.
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive, sums, pin = fixture(root, None, {'sdk-catalogue.json': None,
                'sdk-guides.json': None, 'sdk-workflows.json': None})
            verify_extract(archive, sums, root / 'out', pin[:2])

    def test_candidate_identity_required_by_shared_entrypoints(self):
        for tool in ('check_inspector_sdk.py', 'check_build_sdk.py', 'contracts_sdk_check.py'):
            for extra, message in (([], '--candidate requires full lowercase --sdk-source'),
                                   (['--sdk-source', 'a' * 40], '--candidate requires --sdk-profile legacy')):
                with self.subTest(tool=tool, extra=extra), tempfile.TemporaryDirectory() as temp:
                    work = Path(temp) / 'proof'
                    result = subprocess.run([sys.executable, str(TOOLS / tool), '--archive', 'missing.zip',
                        '--sums', 'missing-sums', '--work', str(work), '--candidate',
                        '--sdk-sha256', '0' * 64, '--archive-sha256', '0' * 64,
                        *(['--slice', 'economy'] if tool == 'contracts_sdk_check.py' else []), *extra],
                        capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(message, result.stderr)
                    self.assertFalse(work.exists())

    def test_inspector_defaults_keep_published_stable_identity(self):
        module = proof_module('inspector')
        args = module.parse_args(['--archive', 'sdk.zip', '--sums', 'SHA256SUMS', '--work', 'proof'])
        self.assertEqual(args.sdk_version, '2.4.0')
        self.assertEqual(args.sdk_sha256, '0df4995624e9b094d24e9c5534b1ea7357f517c83a0a8c8ed6ad7d4d475afcbc')
        self.assertFalse(args.candidate)


if __name__ == '__main__':
    unittest.main(verbosity=2)
