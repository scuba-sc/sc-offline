#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check both Windows configure invocations without requiring MSVC or loading a module."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent


class CheckerReached(Exception):
    pass


def load(name):
    spec = importlib.util.spec_from_file_location('windows_proof_' + name, TOOLS / name)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class WindowsGeneratorProofs(unittest.TestCase):
    def test_titanlink_rejects_an_empty_individual_msvc_include_trace(self):
        module = load('check_titanlink_sdk.py')
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, sdk, compiler, build = (root / name for name in ('source', 'sdk', 'compiler', 'build'))
            (source / 'plugins/titanlink').mkdir(parents=True)
            (sdk / 'include').mkdir(parents=True)
            compiler.mkdir()
            header = sdk / 'include/public.h'
            header.write_text('// public fixture\n')
            rows = []
            for target, count in (('sco-feature-titanlink', 3), ('sc-offline-titanlink', 1)):
                directory = build / 'dependencies' / target
                directory.mkdir(parents=True)
                (directory / 'toolchain.txt').write_text(str(compiler) + '\n')
                for i in range(count):
                    src = source / 'plugins/titanlink' / (target + str(i) + '.cpp')
                    src.touch()
                    path = directory / (str(i) + '.json')
                    path.write_text(json.dumps({'Data': {'Source': str(src), 'Includes': [str(header)]}}))
                    rows.append((path, src))
            with patch.object(module, 'ROOT', root / 'unrelated-checkout'), contextlib.redirect_stdout(io.StringIO()):
                module.trace_windows(build, source, sdk, {header.resolve()})
                path, src = rows[1]
                path.write_text(json.dumps({'Data': {'Source': str(src), 'Includes': []}}))
                with self.assertRaisesRegex(AssertionError, 'Empty MSVC include trace'):
                    module.trace_windows(build, source, sdk, {header.resolve()})

    def test_plugin_and_checker_receive_explicit_or_default_settings(self):
        files = ['check_' + n + '_sdk.py' for n in
                 ('build', 'creative', 'crew', 'discord', 'inspector', 'loadout',
                  'mining', 'npc', 'spawn', 'teleport', 'titanlink')]
        files += ['contracts_sdk_check.py', 'check_quantum_sdk.py']
        for filename in files:
            for override in (False, True):
                with self.subTest(script=filename, override=override), tempfile.TemporaryDirectory() as temp:
                    wrapper = load(filename)
                    # Quantum deliberately shares teleport's proof implementation.
                    module = __import__('check_teleport_sdk') if filename == 'check_quantum_sdk.py' else wrapper
                    root = Path(temp)
                    sdk = root / 'sdk'
                    argv = ['--archive', str(root / 'sdk.zip'), '--sums', str(root / 'SHA256SUMS'),
                            '--work', str(root / 'proof')]
                    if filename == 'contracts_sdk_check.py':
                        argv += ['--slice', 'metadata']
                    if override:
                        argv += ['--generator', 'Visual Studio 17 2022', '--toolset', 'v143,host=x64',
                                 '--try-compile-config', 'Release']
                    calls = []
                    operations = []

                    def run(command, cwd, success=True):
                        command = [str(item) for item in command]
                        operations.append(command)
                        if command[:2] == ['cmake', '-S']:
                            calls.append(command)
                            if len(calls) == 2:
                                raise CheckerReached()
                        if command[:2] == ['cmake', '--install']:
                            stage = Path(command[command.index('--prefix') + 1])
                            feature = command[command.index('--component') + 1]
                            folder = stage / 'data/plugins' / feature
                            folder.mkdir(parents=True)
                            shutil.copy2(ROOT / 'plugins' / feature / 'plugin.ini', folder / 'plugin.ini')
                            (folder / 'mock.dll').write_bytes(b'')
                            if feature in ('creative', 'discord', 'titanlink'):
                                (folder / 'disabled').write_bytes(b'')
                            if feature == 'mining':
                                for name in ('import_settings.py', 'README.md'):
                                    shutil.copy2(ROOT / 'plugins/mining' / name, folder / name)
                        # Crew verifies eight named workflow groups; quantum checks its extra name.
                        return SimpleNamespace(stdout=('1: PASS fixture\n' * 8) + 'quantum-concurrency\n', returncode=0)

                    public = {(sdk / 'include/sc_mining.h').resolve()}
                    with patch.object(module, 'os', SimpleNamespace(name='nt')), \
                         patch.object(module, 'verify_extract', return_value=(sdk, public)), \
                         patch.object(module, 'trace_windows', create=True) as trace, \
                         patch.object(module, 'run', side_effect=run), \
                         contextlib.redirect_stdout(io.StringIO()), self.assertRaises(CheckerReached):
                        if filename == 'check_quantum_sdk.py':
                            wrapper.main(argv, spec=wrapper.QUANTUM)
                        elif filename == 'check_teleport_sdk.py':
                            module.main(argv)
                        else:
                            module.main(module.parse_args(argv))
                    self.assertEqual(len(calls), 2)
                    if filename != 'check_mining_sdk.py':
                        trace.assert_called_once()
                    for command in operations:
                        if command[:2] in (['cmake', '--build'], ['cmake', '--install']):
                            self.assertEqual(command[command.index('--config') + 1], 'Release')
                        if command[0] == 'ctest':
                            self.assertEqual(command[command.index('-C') + 1], 'Release')
                    if filename == 'check_titanlink_sdk.py':
                        self.assertIn('-DSCO_TITANLINK_DEPENDENCY_TRACE=ON', calls[0])
                        self.assertNotIn('-DSCO_DISCORD_DEPENDENCY_TRACE=ON', calls[0])
                    expected = 'Visual Studio 17 2022' if override else 'Visual Studio 18 2026'
                    for command in calls:
                        self.assertEqual(command[command.index('-G') + 1], expected)
                        self.assertEqual(command[command.index('-A') + 1], 'x64')
                        if override:
                            self.assertEqual(command[command.index('-T') + 1], 'v143,host=x64')
                            self.assertIn('-DCMAKE_TRY_COMPILE_CONFIGURATION=Release', command)
                        else:
                            self.assertNotIn('-T', command)
                            self.assertFalse(any(x.startswith('-DCMAKE_TRY_COMPILE_CONFIGURATION=') for x in command))
                        self.assertFalse(any(x.startswith('-DCMAKE_MSVC_RUNTIME_LIBRARY=') for x in command))

    def test_windows_only_options_do_not_change_posix_commands(self):
        for filename in ('check_crew_sdk.py', 'check_discord_sdk.py', 'contracts_sdk_check.py'):
            with self.subTest(script=filename), tempfile.TemporaryDirectory() as temp:
                module = load(filename)
                root = Path(temp)
                argv = ['--archive', str(root / 'sdk.zip'), '--sums', str(root / 'SHA256SUMS'),
                        '--work', str(root / 'proof'), '--generator', 'Visual Studio 17 2022',
                        '--toolset', 'v143,host=x64', '--try-compile-config', 'Release']
                if filename == 'contracts_sdk_check.py':
                    argv += ['--slice', 'metadata']
                seen = []
                def capture(command, cwd):
                    seen.extend(map(str, command))
                    raise CheckerReached()
                with patch.object(module, 'os', SimpleNamespace(name='posix')), \
                     patch.object(module, 'verify_extract', return_value=(root / 'sdk', set())), \
                     patch.object(module, 'run', side_effect=capture), self.assertRaises(CheckerReached):
                    module.main(module.parse_args(argv))
                self.assertEqual(seen[seen.index('-G') + 1], 'Unix Makefiles')
                self.assertNotIn('-T', seen)
                self.assertFalse(any(x.startswith('-DCMAKE_TRY_COMPILE_CONFIGURATION=') for x in seen))

    def test_tutorial_passes_options_to_both_copied_proofs(self):
        import sys
        module = load('check_sdk_tutorial.py')
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive, sums = root / 'sdk.zip', root / 'SHA256SUMS'
            archive.touch(); sums.touch()
            argv = ['tutorial', '--archive', str(archive), '--sums', str(sums),
                    '--work', str(root / 'tutorial'), '--generator', 'Visual Studio 17 2022',
                    '--toolset', 'v143,host=x64', '--try-compile-config', 'Release']
            with patch.object(sys, 'argv', argv), patch.object(module.subprocess, 'run') as run, \
                 contextlib.redirect_stdout(io.StringIO()):
                module.main()
            self.assertEqual(run.call_count, 2)
            for call in run.call_args_list:
                command = call.args[0]
                self.assertEqual(command[command.index('--generator') + 1], 'Visual Studio 17 2022')
                self.assertEqual(command[command.index('--toolset') + 1], 'v143,host=x64')
                self.assertEqual(command[command.index('--try-compile-config') + 1], 'Release')


if __name__ == '__main__':
    unittest.main()
