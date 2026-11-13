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
            # MSVC traces are resolved before containment checks. Match that
            # physical root when the system temporary directory is a symlink.
            root = Path(temp).resolve()
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
                private_header = source / 'plugins/private.h'
                private_header.touch()
                path.write_text(json.dumps({'Data': {'Source': str(src), 'Includes': [str(private_header)]}}))
                with self.assertRaisesRegex(AssertionError, 'Non-public titanlink dependency'):
                    module.trace_windows(build, source, sdk, {header.resolve()})
                path.write_text(json.dumps({'Data': {'Source': str(src), 'Includes': []}}))
                with self.assertRaisesRegex(AssertionError, 'Empty MSVC include trace'):
                    module.trace_windows(build, source, sdk, {header.resolve()})

    def test_titanlink_trace_fixture_accepts_a_noncanonical_temporary_root(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            (root / 'existing').mkdir()
            (root / 'fixture').mkdir()
            alias = root / 'existing' / '..' / 'fixture'
            with patch.object(tempfile, 'TemporaryDirectory', return_value=contextlib.nullcontext(str(alias))):
                self.test_titanlink_rejects_an_empty_individual_msvc_include_trace()

    def test_titanlink_trace_fixture_accepts_a_symlinked_temporary_root(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            physical = root / 'physical'
            physical.mkdir()
            alias = root / 'alias'
            try:
                alias.symlink_to(physical, target_is_directory=True)
            except OSError as exc:
                self.skipTest(f'directory symlink unavailable: {exc}')
            with patch.object(tempfile, 'TemporaryDirectory', return_value=contextlib.nullcontext(str(alias))):
                self.test_titanlink_rejects_an_empty_individual_msvc_include_trace()

    def test_shared_msvc_trace_checker_accepts_single_and_multiple_translation_units(self):
        module = load('check_inspector_sdk.py')
        inventories = (
            {
                'sco-feature-loadout': ['loadout.cpp'],
                'sc-offline-loadout': ['external.cpp'],
            },
            {
                'sco-feature-loadout': ['loadout.cpp', 'outfit_import.cpp', 'outfit_recipe.cpp'],
                'sc-offline-loadout': ['external.cpp'],
            },
        )
        for sources_by_target in inventories:
            with self.subTest(feature_sources=len(sources_by_target['sco-feature-loadout'])), \
                 tempfile.TemporaryDirectory() as temp:
                root = Path(temp).resolve()
                build, source, sdk, public, _ = make_msvc_trace_fixture(root, sources_by_target)
                with patch.object(module, 'ROOT', root / 'checkout'), contextlib.redirect_stdout(io.StringIO()):
                    module.trace_windows(build, source, sdk, public, feature='loadout')

    def test_shared_msvc_trace_checker_uses_compile_commands_without_visual_studio_projects(self):
        module = load('check_inspector_sdk.py')
        sources_by_target = {
            'sco-feature-loadout': ['loadout.cpp', 'outfit_import.ixx', 'outfit_recipe.cppm', 'generated.unit'],
            'sc-offline-loadout': ['external.cpp'],
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            build, source, sdk, public, _ = make_msvc_trace_fixture(root, sources_by_target)
            commands = []
            for target, names in sources_by_target.items():
                (build / (target + '.vcxproj')).unlink()
                for name in names:
                    path = source / 'plugins/loadout' / name
                    commands.append({
                        'directory': str(build),
                        'file': str(path.resolve()),
                        'command': f'cl /FoCMakeFiles/{target}.dir/Release/{Path(name).stem}.obj /c {path}',
                    })
                    if name == names[0]:
                        commands.append({
                            'directory': str(build),
                            'file': str(path.resolve()),
                            'command': f'cl /FoCMakeFiles/{target}.dir/Debug/{Path(name).stem}.obj '
                                       f'/DCMAKE_INTDIR="Debug" /c {path}',
                        })
            (build / 'compile_commands.json').write_text(json.dumps(commands))
            with patch.object(module, 'ROOT', root / 'checkout'), contextlib.redirect_stdout(io.StringIO()):
                module.trace_windows(build, source, sdk, public, feature='loadout')

    def test_shared_msvc_trace_checker_rejects_a_missing_expected_source_trace(self):
        module = load('check_inspector_sdk.py')
        sources_by_target = {
            'sco-feature-loadout': ['loadout.cpp', 'outfit_import.cpp', 'outfit_recipe.cpp'],
            'sc-offline-loadout': ['external.cpp'],
        }
        emitted = {
            'sco-feature-loadout': ['loadout.cpp', 'outfit_import.cpp'],
            'sc-offline-loadout': ['external.cpp'],
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            build, source, sdk, public, _ = make_msvc_trace_fixture(root, sources_by_target, emitted)
            with patch.object(module, 'ROOT', root / 'checkout'), contextlib.redirect_stdout(io.StringIO()), \
                 self.assertRaisesRegex(AssertionError, 'Missing MSVC loadout traces.*outfit_recipe.cpp'):
                module.trace_windows(build, source, sdk, public, feature='loadout')

    def test_shared_msvc_trace_checker_rejects_unexpected_and_duplicate_sources(self):
        module = load('check_inspector_sdk.py')
        sources_by_target = {
            'sco-feature-loadout': ['loadout.cpp', 'outfit_import.cpp', 'outfit_recipe.cpp'],
            'sc-offline-loadout': ['external.cpp'],
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            build, source, sdk, public, trace_dirs = make_msvc_trace_fixture(root, sources_by_target)
            rogue = root / 'unexpected.cpp'
            rogue.touch()
            trace = trace_dirs['sco-feature-loadout'] / 'extra.json'
            unexpected = json.loads((trace_dirs['sco-feature-loadout'] / 'trace-0.json').read_text())
            unexpected['Data']['Source'] = str(rogue)
            trace.write_text(json.dumps(unexpected))
            with patch.object(module, 'ROOT', root / 'checkout'), contextlib.redirect_stdout(io.StringIO()), \
                 self.assertRaisesRegex(AssertionError, 'Unexpected MSVC loadout trace source'):
                module.trace_windows(build, source, sdk, public, feature='loadout')
            trace.unlink()
            duplicate = trace_dirs['sco-feature-loadout'] / 'duplicate.json'
            original = trace_dirs['sco-feature-loadout'] / 'trace-0.json'
            duplicate.write_text(original.read_text())
            with patch.object(module, 'ROOT', root / 'checkout'), contextlib.redirect_stdout(io.StringIO()), \
                 self.assertRaisesRegex(AssertionError, 'Duplicate MSVC loadout trace source'):
                module.trace_windows(build, source, sdk, public, feature='loadout')

    def test_shared_msvc_trace_checker_counts_duplicate_inventory_entries_and_same_basenames(self):
        module = load('check_inspector_sdk.py')
        sources_by_target = {
            'sco-feature-loadout': ['same.cpp', 'same.cpp', 'left/same.cpp', 'right/same.cpp'],
            'sc-offline-loadout': ['external.cpp'],
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            build, source, sdk, public, trace_dirs = make_msvc_trace_fixture(root, sources_by_target)
            trace_dir = trace_dirs['sco-feature-loadout']
            with patch.object(module, 'ROOT', root / 'checkout'), contextlib.redirect_stdout(io.StringIO()):
                module.trace_windows(build, source, sdk, public, feature='loadout')
            repeated_source = str((source / 'plugins/loadout/same.cpp').resolve())
            matching = []
            for trace in trace_dir.glob('*.json'):
                if json.loads(trace.read_text())['Data']['Source'] == repeated_source:
                    matching.append(trace)
            self.assertEqual(len(matching), 2)
            missing_path = matching.pop()
            missing_data = missing_path.read_text()
            missing_path.unlink()
            with patch.object(module, 'ROOT', root / 'checkout'), contextlib.redirect_stdout(io.StringIO()), \
                 self.assertRaisesRegex(AssertionError, r'same.cpp \(1 missing\)'):
                module.trace_windows(build, source, sdk, public, feature='loadout')
            missing_path.write_text(missing_data)
            extra = trace_dir / 'extra-duplicate.json'
            extra.write_text(missing_data)
            with patch.object(module, 'ROOT', root / 'checkout'), contextlib.redirect_stdout(io.StringIO()), \
                 self.assertRaisesRegex(AssertionError, 'Duplicate MSVC loadout trace source: same.cpp'):
                module.trace_windows(build, source, sdk, public, feature='loadout')

    def test_shared_msvc_trace_checker_rejects_malformed_and_nested_trace_paths(self):
        module = load('check_inspector_sdk.py')
        sources_by_target = {
            'sco-feature-loadout': ['loadout.cpp'],
            'sc-offline-loadout': ['external.cpp'],
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            build, source, sdk, public, trace_dirs = make_msvc_trace_fixture(root, sources_by_target)
            trace = trace_dirs['sco-feature-loadout'] / 'trace-0.json'
            original = json.loads(trace.read_text())
            trace.write_text('{ invalid json')
            with patch.object(module, 'ROOT', root / 'checkout'), contextlib.redirect_stdout(io.StringIO()), \
                 self.assertRaisesRegex(AssertionError, 'Malformed MSVC loadout trace'):
                module.trace_windows(build, source, sdk, public, feature='loadout')
            empty_includes = json.loads(json.dumps(original))
            empty_includes['Data']['Includes'] = []
            trace.write_text(json.dumps(empty_includes))
            with patch.object(module, 'ROOT', root / 'checkout'), contextlib.redirect_stdout(io.StringIO()), \
                 self.assertRaisesRegex(AssertionError, 'Empty or malformed MSVC include trace'):
                module.trace_windows(build, source, sdk, public, feature='loadout')
            trace.write_text(json.dumps(original))
            nested = trace_dirs['sco-feature-loadout'] / 'nested'
            nested.mkdir()
            (nested / 'extra.json').write_text(trace.read_text())
            with patch.object(module, 'ROOT', root / 'checkout'), contextlib.redirect_stdout(io.StringIO()), \
                 self.assertRaisesRegex(AssertionError, 'Unexpected MSVC loadout trace path'):
                module.trace_windows(build, source, sdk, public, feature='loadout')

    def test_shared_msvc_trace_checker_rejects_duplicate_json_keys_recursively(self):
        module = load('check_inspector_sdk.py')
        sources_by_target = {
            'sco-feature-loadout': ['loadout.cpp'],
            'sc-offline-loadout': ['external.cpp'],
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            build, source, sdk, public, trace_dirs = make_msvc_trace_fixture(root, sources_by_target)
            private_root = root / 'checkout'
            private_header = private_root / 'src/private.h'
            private_header.parent.mkdir(parents=True)
            private_header.write_text('// synthetic private header\n')
            trace = trace_dirs['sco-feature-loadout'] / 'trace-0.json'
            original = trace.read_text()
            original_data = json.loads(original)

            def duplicate(raw, key, hidden_value):
                marker = json.dumps(key) + ':'
                offset = raw.find(marker)
                self.assertGreaterEqual(offset, 0, f'missing JSON member {key}')
                return raw[:offset] + marker + json.dumps(hidden_value) + ', ' + raw[offset:]

            hidden_data = json.loads(json.dumps(original_data['Data']))
            hidden_data['Includes'] = [str(private_header)]
            hidden_data['ImportedModules'] = [{'Name': 'private::host_api', 'BMI': str(root / 'private.ifc')}]
            cases = (
                ('root Data', duplicate(original, 'Data', hidden_data)),
                ('root Version', duplicate(original, 'Version', '1.1')),
                ('Data Includes', duplicate(original, 'Includes', [str(private_header)])),
                ('Data ImportedModules', duplicate(original, 'ImportedModules', hidden_data['ImportedModules'])),
                ('Data ImportedHeaderUnits', duplicate(original, 'ImportedHeaderUnits',
                                                        [{'Header': str(private_header), 'BMI': str(root / 'private.ifc')}])),
            )
            for label, raw in cases:
                trace.write_text(raw)
                with self.subTest(duplicate=label), patch.object(module, 'ROOT', private_root), \
                     contextlib.redirect_stdout(io.StringIO()), \
                     self.assertRaisesRegex(AssertionError, 'Malformed MSVC loadout trace'):
                    module.trace_windows(build, source, sdk, public, feature='loadout')

            (build / 'sco-feature-loadout.vcxproj').unlink()
            (build / 'sc-offline-loadout.vcxproj').unlink()
            commands = []
            for target, names in sources_by_target.items():
                for name in names:
                    path = source / 'plugins/loadout' / name
                    commands.append({
                        'directory': str(build),
                        'file': str(path.resolve()),
                        'command': f'cl /FoCMakeFiles/{target}.dir/Release/{Path(name).stem}.obj /c {path}',
                    })
            compile_path = build / 'compile_commands.json'
            raw = json.dumps(commands)
            valid_compile_command = json.loads(raw)[0]
            invalid_compile_command = json.loads(json.dumps(valid_compile_command))
            invalid_compile_command['file'] = str(private_root / 'missing.cpp')
            target_marker = json.dumps(valid_compile_command)
            raw = raw.replace(target_marker, duplicate(target_marker, 'file', invalid_compile_command['file']), 1)
            compile_path.write_text(raw)
            trace.write_text(original)
            with patch.object(module, 'ROOT', private_root), contextlib.redirect_stdout(io.StringIO()), \
                 self.assertRaisesRegex(AssertionError, 'Malformed MSVC compile_commands.json'):
                module.trace_windows(build, source, sdk, public, feature='loadout')

    def test_shared_msvc_trace_checker_preserves_private_and_outside_toolchain_guards(self):
        module = load('check_inspector_sdk.py')
        sources_by_target = {
            'sco-feature-loadout': ['loadout.cpp'],
            'sc-offline-loadout': ['external.cpp'],
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            build, source, sdk, public, trace_dirs = make_msvc_trace_fixture(root, sources_by_target)
            private_checkout = root / 'checkout'
            private_checkout.mkdir()
            private_header = private_checkout / 'src/private.h'
            private_header.parent.mkdir()
            private_header.write_text('// private host fixture\n')
            trace = trace_dirs['sco-feature-loadout'] / 'trace-0.json'
            data = json.loads(trace.read_text())
            data['Data']['Includes'] = [str(private_header)]
            trace.write_text(json.dumps(data))
            with patch.object(module, 'ROOT', private_checkout), contextlib.redirect_stdout(io.StringIO()), \
                 self.assertRaisesRegex(AssertionError, 'Non-public loadout dependency'):
                module.trace_windows(build, source, sdk, public, feature='loadout')
            outside = root / 'foreign/private.h'
            outside.parent.mkdir()
            outside.write_text('// outside toolchain fixture\n')
            data['Data']['Includes'] = [str(outside)]
            trace.write_text(json.dumps(data))
            with patch.object(module, 'ROOT', private_checkout), contextlib.redirect_stdout(io.StringIO()), \
                 self.assertRaisesRegex(AssertionError, 'Non-public loadout dependency'):
                module.trace_windows(build, source, sdk, public, feature='loadout')

    def test_shared_msvc_trace_checker_accepts_v12_empty_import_fields_and_null_provider(self):
        module = load('check_inspector_sdk.py')
        sources_by_target = {
            'sco-feature-loadout': ['loadout.cpp'],
            'sc-offline-loadout': ['external.cpp'],
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            build, source, sdk, public, trace_dirs = make_msvc_trace_fixture(root, sources_by_target)
            # The documented v1.2 schema includes both arrays and an empty provider name.
            with patch.object(module, 'ROOT', root / 'checkout'), contextlib.redirect_stdout(io.StringIO()):
                module.trace_windows(build, source, sdk, public, feature='loadout')
            for trace_dir in trace_dirs.values():
                trace = trace_dir / 'trace-0.json'
                document = json.loads(trace.read_text())
                document['Data']['ProvidedModule'] = None
                trace.write_text(json.dumps(document))
            # Null is also accepted for the provider field; no modules are imported.
            with patch.object(module, 'ROOT', root / 'checkout'), contextlib.redirect_stdout(io.StringIO()):
                module.trace_windows(build, source, sdk, public, feature='loadout')

    def test_shared_msvc_trace_checker_rejects_missing_v12_fields(self):
        module = load('check_inspector_sdk.py')
        sources_by_target = {
            'sco-feature-loadout': ['loadout.cpp'],
            'sc-offline-loadout': ['external.cpp'],
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            build, source, sdk, public, trace_dirs = make_msvc_trace_fixture(root, sources_by_target)
            trace = trace_dirs['sco-feature-loadout'] / 'trace-0.json'
            original = json.loads(trace.read_text())
            for field in ('Source', 'ProvidedModule', 'Includes', 'ImportedHeaderUnits', 'ImportedModules'):
                document = json.loads(json.dumps(original))
                del document['Data'][field]
                trace.write_text(json.dumps(document))
                with self.subTest(field=field), patch.object(module, 'ROOT', root / 'checkout'), \
                     contextlib.redirect_stdout(io.StringIO()), \
                     self.assertRaisesRegex(AssertionError, 'Malformed MSVC loadout v1.2 trace missing Data fields'):
                    module.trace_windows(build, source, sdk, public, feature='loadout')

    def test_shared_msvc_trace_checker_rejects_unknown_versions_and_misplaced_fields(self):
        module = load('check_inspector_sdk.py')
        sources_by_target = {
            'sco-feature-loadout': ['loadout.cpp'],
            'sc-offline-loadout': ['external.cpp'],
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            build, source, sdk, public, trace_dirs = make_msvc_trace_fixture(root, sources_by_target)
            trace = trace_dirs['sco-feature-loadout'] / 'trace-0.json'
            original = json.loads(trace.read_text())
            for version in (None, 1.2, '', '1.0', '1.1', '2.0'):
                document = json.loads(json.dumps(original))
                if version is None:
                    del document['Version']
                    message = 'Malformed MSVC loadout trace top-level fields'
                else:
                    document['Version'] = version
                    message = 'Unsupported MSVC loadout sourceDependencies version'
                trace.write_text(json.dumps(document))
                with self.subTest(version=version), patch.object(module, 'ROOT', root / 'checkout'), \
                     contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(AssertionError, message):
                    module.trace_windows(build, source, sdk, public, feature='loadout')
            for field in ('ImportedHeaderUnits', 'ImportedModules'):
                document = json.loads(json.dumps(original))
                document[field] = [{'Name': 'private::host_api', 'BMI': str(root / 'private.ifc')}]
                trace.write_text(json.dumps(document))
                with self.subTest(misplaced=field), patch.object(module, 'ROOT', root / 'checkout'), \
                     contextlib.redirect_stdout(io.StringIO()), \
                     self.assertRaisesRegex(AssertionError, 'Misplaced MSVC loadout dependency fields outside Data'):
                    module.trace_windows(build, source, sdk, public, feature='loadout')
            document = json.loads(json.dumps(original))
            document['Other'] = 'unknown top-level field'
            trace.write_text(json.dumps(document))
            with patch.object(module, 'ROOT', root / 'checkout'), contextlib.redirect_stdout(io.StringIO()), \
                 self.assertRaisesRegex(AssertionError, 'Malformed MSVC loadout trace top-level fields'):
                module.trace_windows(build, source, sdk, public, feature='loadout')
            document = json.loads(json.dumps(original))
            document['Data']['UnrecognizedDependencyField'] = []
            trace.write_text(json.dumps(document))
            with patch.object(module, 'ROOT', root / 'checkout'), contextlib.redirect_stdout(io.StringIO()), \
                 self.assertRaisesRegex(AssertionError, 'Malformed MSVC loadout v1.2 trace has unknown Data fields'):
                module.trace_windows(build, source, sdk, public, feature='loadout')

    def test_shared_msvc_trace_checker_rejects_unsupported_or_malformed_provider(self):
        module = load('check_inspector_sdk.py')
        sources_by_target = {
            'sco-feature-loadout': ['loadout.cpp'],
            'sc-offline-loadout': ['external.cpp'],
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            build, source, sdk, public, trace_dirs = make_msvc_trace_fixture(root, sources_by_target)
            trace = trace_dirs['sco-feature-loadout'] / 'trace-0.json'
            original = json.loads(trace.read_text())
            for provider in ('private::host_api', '  ', 7, {}):
                document = json.loads(json.dumps(original))
                document['Data']['ProvidedModule'] = provider
                trace.write_text(json.dumps(document))
                message = ('Unsupported MSVC loadout provided module' if isinstance(provider, str) and provider.strip()
                           else 'Malformed MSVC loadout ProvidedModule')
                with self.subTest(provider=provider), patch.object(module, 'ROOT', root / 'checkout'), \
                     contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(AssertionError, message):
                    module.trace_windows(build, source, sdk, public, feature='loadout')

    def test_shared_msvc_trace_checker_rejects_imported_host_header_units_and_modules(self):
        module = load('check_inspector_sdk.py')
        sources_by_target = {
            'sco-feature-loadout': ['loadout.cpp'],
            'sc-offline-loadout': ['external.cpp'],
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            build, source, sdk, public, trace_dirs = make_msvc_trace_fixture(root, sources_by_target)
            private_root = root / 'checkout'
            private_header = private_root / 'src/private_api.h'
            private_header.parent.mkdir(parents=True)
            private_header.write_text('// synthetic private host header unit\n')
            private_bmi = private_root / 'ifc/private_host.ifc'
            private_bmi.parent.mkdir(parents=True)
            private_bmi.write_text('synthetic IFC fixture\n')
            trace = trace_dirs['sco-feature-loadout'] / 'trace-0.json'
            original = json.loads(trace.read_text())
            payloads = {
                'ImportedHeaderUnits': [{'Header': str(private_header), 'BMI': str(private_bmi)}],
                'ImportedModules': [{'Name': 'private::host_api', 'BMI': str(private_bmi)}],
            }
            for field, payload in payloads.items():
                document = json.loads(json.dumps(original))
                document['Data'][field] = payload
                trace.write_text(json.dumps(document))
                with self.subTest(field=field), patch.object(module, 'ROOT', private_root), \
                     contextlib.redirect_stdout(io.StringIO()), \
                     self.assertRaisesRegex(AssertionError, f'Unsupported MSVC loadout {field} dependencies'):
                    module.trace_windows(build, source, sdk, public, feature='loadout')

    def test_shared_msvc_trace_checker_rejects_malformed_import_fields(self):
        module = load('check_inspector_sdk.py')
        sources_by_target = {
            'sco-feature-loadout': ['loadout.cpp'],
            'sc-offline-loadout': ['external.cpp'],
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            build, source, sdk, public, trace_dirs = make_msvc_trace_fixture(root, sources_by_target)
            trace = trace_dirs['sco-feature-loadout'] / 'trace-0.json'
            original = json.loads(trace.read_text())
            for field in ('ImportedHeaderUnits', 'ImportedModules'):
                for value in (None, '', {}, 'private::host_api'):
                    document = json.loads(json.dumps(original))
                    document['Data'][field] = value
                    trace.write_text(json.dumps(document))
                    with self.subTest(field=field, value=value), patch.object(module, 'ROOT', root / 'checkout'), \
                         contextlib.redirect_stdout(io.StringIO()), \
                         self.assertRaisesRegex(AssertionError, f'Malformed MSVC loadout {field} trace'):
                        module.trace_windows(build, source, sdk, public, feature='loadout')

    def test_shared_msvc_trace_checker_rejects_blank_relative_and_missing_paths(self):
        module = load('check_inspector_sdk.py')
        sources_by_target = {
            'sco-feature-loadout': ['loadout.cpp'],
            'sc-offline-loadout': ['external.cpp'],
        }
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            build, source, sdk, public, trace_dirs = make_msvc_trace_fixture(root, sources_by_target)
            trace = trace_dirs['sco-feature-loadout'] / 'trace-0.json'
            original = json.loads(trace.read_text())
            invalid_includes = ['', '   ', 'relative.h', str(root / 'missing.h'), str(root / 'compiler')]
            for dependency in invalid_includes:
                data = json.loads(json.dumps(original))
                data['Data']['Includes'] = [dependency]
                trace.write_text(json.dumps(data))
                with self.subTest(include=dependency), patch.object(module, 'ROOT', root / 'checkout'), \
                     contextlib.redirect_stdout(io.StringIO()), self.assertRaises(AssertionError):
                    module.trace_windows(build, source, sdk, public, feature='loadout')
            invalid_sources = ['', '   ', 'relative.cpp', str(root / 'missing.cpp')]
            for traced_source in invalid_sources:
                data = json.loads(json.dumps(original))
                data['Data']['Source'] = traced_source
                trace.write_text(json.dumps(data))
                with self.subTest(source=traced_source), patch.object(module, 'ROOT', root / 'checkout'), \
                     contextlib.redirect_stdout(io.StringIO()), self.assertRaises(AssertionError):
                    module.trace_windows(build, source, sdk, public, feature='loadout')

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


def make_msvc_trace_fixture(root, sources_by_target, emitted_sources=None):
    """Write target-project inventories and representative MSVC sourceDependencies JSONs."""
    module = load('check_inspector_sdk.py')
    build, source, sdk, compiler = (root / name for name in ('build', 'source', 'sdk', 'compiler'))
    public_dir = sdk / 'include'
    public_dir.mkdir(parents=True)
    compiler.mkdir()
    public_header = public_dir / 'sco_api.h'
    public_header.write_text('// SDK public fixture\n')
    compiler_header = compiler / 'stddef.h'
    compiler_header.write_text('// compiler root fixture\n')
    source_dir = source / 'plugins/loadout'
    source_dir.mkdir(parents=True)
    trace_dirs = {}
    ns = '{http://schemas.microsoft.com/developer/msbuild/2003}'
    for target, names in sources_by_target.items():
        paths = []
        for name in names:
            path = source_dir / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('// consumer fixture\n')
            paths.append(path.resolve())
        project = module.ET.Element(ns + 'Project')
        group = module.ET.SubElement(project, ns + 'ItemGroup')
        for path in paths:
            module.ET.SubElement(group, ns + 'ClCompile', Include=str(path))
        build.mkdir(exist_ok=True)
        module.ET.ElementTree(project).write(build / (target + '.vcxproj'), encoding='utf-8')
        trace_dir = build / 'dependencies' / target
        trace_dir.mkdir(parents=True)
        (trace_dir / 'toolchain.txt').write_text(str(compiler) + '\n')
        trace_dirs[target] = trace_dir
        selected = names if emitted_sources is None else emitted_sources.get(target, [])
        for index, name in enumerate(selected):
            trace = {
                'Version': '1.2',
                'Data': {
                    'Source': str((source_dir / name).resolve()),
                    'ProvidedModule': '',
                    'Includes': [str(public_header.resolve()), str(compiler_header.resolve())],
                    'ImportedHeaderUnits': [],
                    'ImportedModules': [],
                },
            }
            (trace_dir / f'trace-{index}.json').write_text(json.dumps(trace))
    return build, source, sdk, {public_header.resolve()}, trace_dirs


if __name__ == '__main__':
    unittest.main()
