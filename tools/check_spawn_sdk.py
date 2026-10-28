#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Standalone spawn module proof from checksum-verified published SDK 2.4.0 (sdk-v2.4.0-rc.2).

Reuses the inspector archive/dependency machinery, never host include paths.
POSIX supports ASan/UBSan plus a physically partial-pointer red arm.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import sys

sys.dont_write_bytecode = True
from check_inspector_sdk import (SDK_VERSION, SDK_SHA256, contained, explicit_preflight_pin,
                                 run, trace_posix, trace_windows, verify_extract)

ROOT = Path(__file__).resolve().parent.parent


def main(args):
    work = args.work.resolve()
    if contained(work, ROOT):
        raise AssertionError('Spawn proof must live outside the host checkout')
    if args.sanitizers and os.name == 'nt':
        raise AssertionError('This sanitizer workflow is POSIX-only')
    pin = explicit_preflight_pin(args)
    work.mkdir(parents=True, exist_ok=False)
    sdk, public = verify_extract(args.archive.resolve(), args.sums.resolve(), work / 'sdk', pin=pin)
    if args.candidate:
        print('Explicit unpublished development compatibility preflight; SHA-256 ' + pin[1])
    source = work / 'source'
    (source / 'plugins').mkdir(parents=True)
    shutil.copytree(ROOT / 'plugins/spawn', source / 'plugins/spawn')
    shutil.copy2(ROOT / 'plugins/picker.h', source / 'plugins/picker.h')
    (source / 'tools').mkdir()
    for name in ('spawn_workflow_test.cpp', 'asop_selection_workflow_test.cpp', 'workflow_module.h'):
        shutil.copy2(ROOT / 'tools' / name, source / 'tools' / name)
    build = work / 'plugin-build'
    configure = ['cmake', '-S', source / 'plugins/spawn', '-B', build,
                 '-DSCO_SDK_INCLUDE_DIR=' + str(sdk / 'include'), '-DSCO_WORKFLOW_TESTS=ON',
                 '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DSCO_SPAWN_DEPENDENCY_TRACE=ON']
    configure += (['-G', 'Visual Studio 18 2026', '-A', 'x64'] if os.name == 'nt'
                  else ['-G', 'Unix Makefiles'])
    run(configure, work)
    run(['cmake', '--build', build, '--config', 'Release', '--target', 'spawn-module-test'], work)
    def workflow():
        print(run(['ctest', '--test-dir', build, '-C', 'Release', '-R',
                   '^spawn-module-workflow$', '--output-on-failure', '-V'], work).stdout)
    workflow()
    if (sdk / 'include' / 'sc_asop_selection.h').is_file():
        run(['cmake', '--build', build, '--config', 'Release', '--target', 'asop-selection-workflow-test'], work)
        print(run(['ctest', '--test-dir', build, '-C', 'Release', '-R',
                   '^asop-selection-workflow$', '--output-on-failure', '-V'], work).stdout)
    trace = trace_windows if os.name == 'nt' else trace_posix
    trace(build, source, sdk, public, feature='spawn')
    stage = work / 'stage'
    run(['cmake', '--install', build, '--config', 'Release', '--prefix', stage,
         '--component', 'spawn'], work)
    folder = stage / 'data/plugins/spawn'
    modules = [p for p in folder.iterdir() if p.suffix in ('.dll', '.so', '.dylib')]
    if len(modules) != 1 or (folder / 'disabled').exists():
        raise AssertionError('Spawn install inventory/default mismatch')
    manifest = folder / 'plugin.ini'
    if os.name != 'nt':
        manifest.write_text(re.sub(r'(?m)^entry[ \t]*=.*$', 'entry = ' + modules[0].name, manifest.read_text()))
    if {p.name for p in folder.iterdir()} != {'plugin.ini', modules[0].name}:
        raise AssertionError('Unexpected files in installed spawn')
    checker_build = work / 'checker-build'
    configure = ['cmake', '-S', sdk, '-B', checker_build]
    if os.name == 'nt':
        configure += ['-G', 'Visual Studio 18 2026', '-A', 'x64']
    run(configure, work)
    run(['cmake', '--build', checker_build, '--config', 'Release', '--target', 'sco-plugin-check'], work)
    checker = checker_build / ('Release/sco-plugin-check.exe' if os.name == 'nt' else 'sco-plugin-check')
    checked = run([checker, folder, '--invoke', 'spawn.status'], work).stdout
    print(checked)
    for token in ('spawn.status -> unavailable', 'Spawn observation unavailable; ownership unchanged', 'OK: 0 failure(s)'):
        if token not in checked:
            raise AssertionError('Missing-service checker reply missing: ' + token)
    if os.name != 'nt':
        cpp = source / 'plugins/spawn/spawn.cpp'
        original = cpp.read_text()
        mtime = cpp.stat().st_mtime_ns
        cmake = source / 'plugins/spawn/CMakeLists.txt'
        cmake_original = cmake.read_text()
        if args.sanitizers:
            # Trace/check the ordinary distributable module first. Instrument only
            # the development workflow: compiler sanitizer metadata is not SDK content.
            run(['cmake', '-S', source / 'plugins/spawn', '-B', build,
                 '-DCMAKE_CXX_FLAGS=-g -fsanitize=address,undefined -fno-omit-frame-pointer'], work)
            run(['cmake', '--build', build, '--target', 'clean'], work)
            run(['cmake', '--build', build, '--target', 'spawn-module-test'], work)
            workflow()
            broken = original.replace('>= offsetof(type, m) + sizeof((t)->m)', '> offsetof(type, m)')
            if broken == original:
                raise AssertionError('Partial-tail mutation no longer matches')
            cpp.write_text(broken)
            run(['cmake', '--build', build, '--target', 'clean'], work)
            run(['cmake', '--build', build, '--target', 'spawn-module-test'], work)
            result = run([build / 'spawn-module-test', build / 'test-plugins/spawn' / modules[0].name], work, False)
            if not result.returncode or 'AddressSanitizer: heap-buffer-overflow' not in result.stdout:
                raise AssertionError('Partial-tail red arm did not catch the actual overread: ' + result.stdout)
            print('Spawn partial-tail red arm: ASan heap-buffer-overflow (expected)')
            cpp.write_text(original)
            run(['cmake', '--build', build, '--target', 'clean'], work)
            run(['cmake', '--build', build, '--target', 'spawn-module-test'], work)
            workflow()
            run(['cmake', '-S', source / 'plugins/spawn', '-B', build, '-DCMAKE_CXX_FLAGS='], work)
        mutants = {
            'private host header': ('#include "sco/plugins.h"\n', 'sco/plugins.h'),
            'product header': ('#include "version.h"\n', 'version.h'),
            'private host symbol': ('extern void Menu_RequestSpawnClass();\nnamespace { struct Forbidden { Forbidden() { Menu_RequestSpawnClass(); } } forbidden; }\n', 'Menu_RequestSpawnClass'),
        }
        for label, (mutation, token) in mutants.items():
            cpp.write_text(original + '\n' + mutation)
            os.utime(cpp, ns=(cpp.stat().st_atime_ns, mtime))
            run(['cmake', '--build', build, '--target', 'clean'], work)
            result = run(['cmake', '--build', build, '--target', 'sc-offline-spawn'], work, False)
            if not result.returncode or token not in result.stdout:
                raise AssertionError('Wrong negative-control result: ' + label + '\n' + result.stdout)
            print('Spawn boundary rejected: ' + label)
        foreign = work / 'foreign'
        foreign.mkdir()
        (foreign / 'private.h').write_text('#pragma once\n')
        cpp.write_text(original + '\n#include "transitive.h"\n')
        (cpp.parent / 'transitive.h').write_text('#include <private.h>\n')
        cmake.write_text(cmake_original + '\ntarget_include_directories(sco-feature-spawn SYSTEM PRIVATE "' + str(foreign) + '")\n')
        run(['cmake', '-S', source / 'plugins/spawn', '-B', build], work)
        run(['cmake', '--build', build, '--target', 'clean'], work)
        run(['cmake', '--build', build, '--target', 'sc-offline-spawn'], work)
        try:
            trace_posix(build, source, sdk, public, feature='spawn')
        except AssertionError as error:
            if 'private.h' not in str(error):
                raise
            print('Spawn boundary rejected: system-marked transitive foreign header')
        else:
            raise AssertionError('Dependency trace admitted transitive private header')
        cpp.write_text(original)
        cmake.write_text(cmake_original)
        (cpp.parent / 'transitive.h').unlink()
        run(['cmake', '-S', source / 'plugins/spawn', '-B', build], work)
        run(['cmake', '--build', build, '--target', 'clean'], work)
        run(['cmake', '--build', build, '--target', 'spawn-module-test'], work)
        workflow()
        trace_posix(build, source, sdk, public, feature='spawn')
    identity = 'unpublished development compatibility preflight' if args.candidate else 'published RC2'
    print('Spawn SDK proof: PASS (' + identity + '; synthetic, not run in game)')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--sums', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True, help='New directory outside checkout')
    parser.add_argument('--sdk-version', default=SDK_VERSION)
    parser.add_argument('--sdk-sha256', default=SDK_SHA256)
    parser.add_argument('--candidate', action='store_true', help='Explicit unpublished preflight archive')
    parser.add_argument('--archive-sha256', help='Required independent locked SHA-256 with --candidate')
    parser.add_argument('--sdk-source', help='Exact full Core source commit for candidate')
    parser.add_argument('--sdk-profile', help='Candidate SDK profile (legacy)')
    parser.add_argument('--sanitizers', action='store_true', help='POSIX ASan/UBSan and physical partial-tail red arm')
    main(parser.parse_args())
