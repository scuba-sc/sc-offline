#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Standalone teleport module proof from checksum-verified published SDK 2.4.0 (sdk-v2.4.0-rc.1).

Reuses the inspector archive/dependency machinery, never host include paths. The
shared travel code lives in plugins/spatial, the only sibling folder the trace admits.
Moves go through game.relocation 1.0; the SDK checker has no relocation provider, so
F7 (capture) and F8 (move) answer unavailable there. POSIX also proves the relocation
gate with a red arm: the travel model test must fail without the capability check.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import sys

sys.dont_write_bytecode = True
from check_inspector_sdk import contained, run, trace_posix, trace_windows, verify_extract

ROOT = Path(__file__).resolve().parent.parent

TELEPORT = {
    'feature': 'teleport',
    'test_option': 'SCO_TRAVEL_TEST', 'test_target': 'travel-model-test', 'test_name': 'travel-model',
    'test_source': 'travel_workflow_test.cpp',
    # Argument-free commands run by the checker itself: OK or UNAVAILABLE only.
    'checks': [
        ([], ('setting relocation (bool)', 'teleport.save -> unavailable', 'nothing saved',
              'teleport.go -> unavailable', 'game.relocation is off', 'teleport.inspect -> unavailable',
              'teleport.status -> ok', 'game.relocation off', 'OK: 0 failure(s)')),
        # Capability advertised but no relocation/spatial provider: still no move.
        (['--cap', 'game.relocation'], ('teleport.go -> unavailable', 'Saved spot unavailable',
                                        'teleport.status -> ok', 'game.relocation on', 'OK: 0 failure(s)')),
    ],
    # Removing the fresh capability check must turn the synthetic test red.
    'red_arm': ('plugins/spatial/travel_model.h', ' || !api->has(SC_RELOCATION_NAME)', ''),
}


def prove(args, spec):
    feature = spec['feature']
    work = args.work.resolve()
    if contained(work, ROOT):
        raise AssertionError(f'{feature} proof must live outside the host checkout')
    work.mkdir(parents=True, exist_ok=False)
    sdk, public = verify_extract(args.archive.resolve(), args.sums.resolve(), work / 'sdk')
    source = work / 'source'
    (source / 'plugins').mkdir(parents=True)
    for folder in (feature, 'spatial'):
        shutil.copytree(ROOT / 'plugins' / folder, source / 'plugins' / folder)
    (source / 'tools').mkdir()
    extra_tests = spec.get('extra_tests', ())
    for test_source in [spec['test_source']] + [test[0] for test in extra_tests]:
        shutil.copy2(ROOT / 'tools' / test_source, source / 'tools' / test_source)
    build = work / 'plugin-build'
    configure = ['cmake', '-S', source / 'plugins' / feature, '-B', build,
                 '-DSCO_SDK_INCLUDE_DIR=' + str(sdk / 'include'), f'-D{spec["test_option"]}=ON',
                 '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', f'-DSCO_{feature.upper()}_DEPENDENCY_TRACE=ON']
    configure += (['-G', 'Visual Studio 18 2026', '-A', 'x64'] if os.name == 'nt'
                  else ['-G', 'Unix Makefiles', '-DCMAKE_BUILD_TYPE=Release'])
    run(configure, work)
    target = f'sc-offline-{feature}'

    def build_all():
        run(['cmake', '--build', build, '--config', 'Release', '--target', target], work)
        for test_target in [spec['test_target']] + [test[1] for test in extra_tests]:
            run(['cmake', '--build', build, '--config', 'Release', '--target', test_target], work)

    def workflow():
        names = [spec['test_name']] + [test[2] for test in extra_tests]
        print(run(['ctest', '--test-dir', build, '-C', 'Release', '-R', '^(' + '|'.join(names) + ')$',
                   '--output-on-failure', '-V'], work).stdout)
    build_all()
    workflow()
    trace = trace_windows if os.name == 'nt' else trace_posix
    trace(build, source, sdk, public, feature=feature, shared=('spatial',))
    stage = work / 'stage'
    run(['cmake', '--install', build, '--config', 'Release', '--prefix', stage, '--component', feature], work)
    folder = stage / 'data/plugins' / feature
    modules = [p for p in folder.iterdir() if p.suffix in ('.dll', '.so', '.dylib')]
    if len(modules) != 1 or (folder / 'disabled').exists():
        raise AssertionError(f'{feature} install inventory/default mismatch')
    manifest = folder / 'plugin.ini'
    text = manifest.read_text()
    if '(REFUSE)' in text or not re.search(r'(?m)^relocation = bool default true ', text):
        raise AssertionError(f'{feature} manifest must declare relocation = bool default true and no REFUSE name')
    if os.name != 'nt':
        manifest.write_text(re.sub(r'(?m)^entry[ \t]*=.*$', 'entry = ' + modules[0].name, text))
    if {p.name for p in folder.iterdir()} != {'plugin.ini', modules[0].name}:
        raise AssertionError(f'Unexpected files in installed {feature}')
    checker_build = work / 'checker-build'
    configure = ['cmake', '-S', sdk, '-B', checker_build]
    if os.name == 'nt':
        configure += ['-G', 'Visual Studio 18 2026', '-A', 'x64']
    run(configure, work)
    run(['cmake', '--build', checker_build, '--config', 'Release', '--target', 'sco-plugin-check'], work)
    checker = checker_build / ('Release/sco-plugin-check.exe' if os.name == 'nt' else 'sco-plugin-check')
    for extra, tokens in spec['checks']:
        checked = run([checker, folder] + extra, work).stdout
        print(checked)
        for token in tokens:
            if token not in checked:
                raise AssertionError('Missing-service checker reply missing: ' + token)
    if os.name != 'nt':
        cpp = source / 'plugins' / feature / f'{feature}.cpp'
        original = cpp.read_text()
        mtime = cpp.stat().st_mtime_ns
        cmake = source / 'plugins' / feature / 'CMakeLists.txt'
        cmake_original = cmake.read_text()
        gate_path, gate_text, gate_broken = spec['red_arm']
        gate = source / gate_path
        gate_original = gate.read_text()
        if gate_text not in gate_original:
            raise AssertionError('Relocation red-arm mutation no longer matches')
        gate.write_text(gate_original.replace(gate_text, gate_broken))
        run(['cmake', '--build', build, '--target', 'clean'], work)
        run(['cmake', '--build', build, '--target', spec['test_target']], work)
        result = run(['ctest', '--test-dir', build, '-R', '^' + spec['test_name'] + '$', '--output-on-failure'], work, False)
        if not result.returncode:
            raise AssertionError('Relocation gate red arm passed without the gate:\n' + result.stdout)
        print(f'{feature} relocation red arm: workflow fails without the gate (expected)')
        gate.write_text(gate_original)
        run(['cmake', '--build', build, '--target', 'clean'], work)
        build_all()
        workflow()
        mutants = {
            'private host header': ('#include "sco/plugins.h"\n', 'sco/plugins.h'),
            'product header': ('#include "version.h"\n', 'version.h'),
            'private host symbol': ('extern void TeleportBuiltinRelocate();\nnamespace { struct Forbidden { Forbidden() { TeleportBuiltinRelocate(); } } forbidden; }\n', 'TeleportBuiltinRelocate'),
        }
        for label, (mutation, token) in mutants.items():
            cpp.write_text(original + '\n' + mutation)
            os.utime(cpp, ns=(cpp.stat().st_atime_ns, mtime))
            run(['cmake', '--build', build, '--target', 'clean'], work)
            result = run(['cmake', '--build', build, '--target', target], work, False)
            if not result.returncode or token not in result.stdout:
                raise AssertionError('Wrong negative-control result: ' + label + '\n' + result.stdout)
            print(f'{feature} boundary rejected: ' + label)
        foreign = work / 'foreign'
        foreign.mkdir()
        (foreign / 'private.h').write_text('#pragma once\n')
        cpp.write_text(original + '\n#include "transitive.h"\n')
        (cpp.parent / 'transitive.h').write_text('#include <private.h>\n')
        cmake.write_text(cmake_original + f'\ntarget_include_directories(sco-feature-{feature} SYSTEM PRIVATE "' + str(foreign) + '")\n')
        run(['cmake', '-S', source / 'plugins' / feature, '-B', build], work)
        run(['cmake', '--build', build, '--target', 'clean'], work)
        run(['cmake', '--build', build, '--target', target], work)
        try:
            trace_posix(build, source, sdk, public, feature=feature, shared=('spatial',))
        except AssertionError as error:
            if 'private.h' not in str(error):
                raise
            print(f'{feature} boundary rejected: system-marked transitive foreign header')
        else:
            raise AssertionError('Dependency trace admitted transitive private header')
        cpp.write_text(original)
        cmake.write_text(cmake_original)
        (cpp.parent / 'transitive.h').unlink()
        run(['cmake', '-S', source / 'plugins' / feature, '-B', build], work)
        run(['cmake', '--build', build, '--target', 'clean'], work)
        build_all()
        workflow()
        trace_posix(build, source, sdk, public, feature=feature, shared=('spatial',))
    print(f'{feature.capitalize()} SDK release proof: PASS (synthetic providers and checker with absent services, not run in game)')


def main(argv=None, spec=TELEPORT):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--sums', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True, help='New directory outside checkout')
    prove(parser.parse_args(argv), spec)


if __name__ == '__main__':
    main()
