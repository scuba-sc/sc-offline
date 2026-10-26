#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Standalone build module proof from checksum-verified published SDK 2.4.0 (sdk-v2.4.0-rc.1).

Reuses the inspector archive/dependency machinery, never host include paths.
Ships build.place/undo/clear; the G5 interactive commands must keep refusing.
The actual-module picker fixture exercises copied synthetic providers and public
command routes. The checker separately verifies missing native service refusals.
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


def main(args):
    work = args.work.resolve()
    if contained(work, ROOT):
        raise AssertionError('Build proof must live outside the host checkout')
    work.mkdir(parents=True, exist_ok=False)
    sdk, public = verify_extract(args.archive.resolve(), args.sums.resolve(), work / 'sdk')
    source = work / 'source'
    (source / 'plugins').mkdir(parents=True)
    shutil.copytree(ROOT / 'plugins/build', source / 'plugins/build')
    (source / 'tools').mkdir()
    for name in ('build_picker_test.cpp', 'workflow_module.h'):
        shutil.copy2(ROOT / 'tools' / name, source / 'tools' / name)
    build = work / 'plugin-build'
    configure = ['cmake', '-S', source / 'plugins/build', '-B', build,
                 '-DSCO_SDK_INCLUDE_DIR=' + str(sdk / 'include'), '-DSCO_BUILD_TEST=ON',
                 '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DSCO_BUILD_DEPENDENCY_TRACE=ON']
    configure += (['-G', args.generator, '-A', 'x64'] if os.name == 'nt'
                  else ['-G', 'Unix Makefiles'])
    run(configure, work)
    run(['cmake', '--build', build, '--config', 'Release', '--target', 'sc-offline-build', 'build-picker-test'], work)
    focused = ['ctest', '--test-dir', build, '-C', 'Release', '-R', '^build-picker$', '--output-on-failure', '-V']
    print(run(focused, work).stdout)
    trace = trace_windows if os.name == 'nt' else trace_posix
    trace(build, source, sdk, public, feature='build')
    stage = work / 'stage'
    run(['cmake', '--install', build, '--config', 'Release', '--prefix', stage,
         '--component', 'build'], work)
    folder = stage / 'data/plugins/build'
    modules = [p for p in folder.iterdir() if p.suffix in ('.dll', '.so', '.dylib')]
    if len(modules) != 1 or (folder / 'disabled').exists():
        raise AssertionError('Build install inventory/default mismatch')
    manifest = folder / 'plugin.ini'
    if os.name != 'nt':
        manifest.write_text(re.sub(r'(?m)^entry[ \t]*=.*$', 'entry = ' + modules[0].name, manifest.read_text()))
    if {p.name for p in folder.iterdir()} != {'plugin.ini', modules[0].name}:
        raise AssertionError('Unexpected files in installed build')
    checker_build = work / 'checker-build'
    configure = ['cmake', '-S', sdk, '-B', checker_build]
    if os.name == 'nt':
        configure += ['-G', args.generator, '-A', 'x64']
    run(configure, work)
    run(['cmake', '--build', checker_build, '--config', 'Release', '--target', 'sco-plugin-check'], work)
    checker = checker_build / ('Release/sco-plugin-check.exe' if os.name == 'nt' else 'sco-plugin-check')
    # The checker runs every argument-free command itself; place is invoked explicitly.
    checked = run([checker, folder, '--invoke', 'build.place', 'PlayerDeco_Flair_Heart_Table_1_a', '5'], work).stdout
    print(checked)
    for token in ('build.place -> unavailable', 'game.world missing/disabled; no private fallback',
                  'build.toggle -> unavailable', 'G5 has no production adapter; no private fallback',
                  'build.aim -> unavailable', 'build.drop -> unavailable',
                  'build.undo -> ok "Nothing retained to undo"', 'build.clear -> ok',
                  'G5 aim/freecam unavailable (no production adapter)', 'no across-reload persistence',
                  'OK: 0 failure(s)'):
        if token not in checked:
            raise AssertionError('Missing-service checker reply missing: ' + token)
    if os.name != 'nt':
        sanitized = work / 'sanitizer-build'
        run(['cmake', '-S', source / 'plugins/build', '-B', sanitized,
             '-DSCO_SDK_INCLUDE_DIR=' + str(sdk / 'include'), '-DSCO_BUILD_TEST=ON',
             '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer',
             '-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined',
             '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'], work)
        run(['cmake', '--build', sanitized, '--target', 'build-picker-test'], work)
        print(run(['ctest', '--test-dir', sanitized, '-R', '^build-picker$', '--output-on-failure', '-V'], work).stdout)
        cpp = source / 'plugins/build/build.cpp'
        original = cpp.read_text()
        mtime = cpp.stat().st_mtime_ns
        cmake = source / 'plugins/build/CMakeLists.txt'
        cmake_original = cmake.read_text()
        # The same fixture must detect the missing active picker owner.
        publish = source / 'plugins/build/picker.h'
        publish_original = publish.read_text()
        if 'Message("Choose a row;' not in publish_original:
            raise AssertionError('Picker publication red-arm marker missing')
        publish.write_text(publish_original.replace('state.api=api;', 'state.api=nullptr;'))
        run(['cmake', '--build', build, '--target', 'clean'], work)
        run(['cmake', '--build', build, '--target', 'build-picker-test'], work)
        red = run(focused, work, False)
        if not red.returncode or 'existing Build owner publishes grouped picker' not in red.stdout:
            raise AssertionError('Picker missing-owner red arm failed incorrectly:\n' + red.stdout)
        print('Build picker red arm: fixture detects missing active publication (expected)')
        publish.write_text(publish_original)
        run(['cmake', '--build', build, '--target', 'clean'], work)
        run(['cmake', '--build', build, '--target', 'build-picker-test'], work)
        print(run(focused, work).stdout)
        mutants = {
            'private host header': ('#include "sco/plugins.h"\n', 'sco/plugins.h'),
            'product header': ('#include "version.h"\n', 'version.h'),
            'private host symbol': ('extern bool PrefabsReady();\nnamespace { struct Forbidden { Forbidden() { (void)PrefabsReady(); } } forbidden; }\n', 'PrefabsReady'),
        }
        for label, (mutation, token) in mutants.items():
            cpp.write_text(original + '\n' + mutation)
            os.utime(cpp, ns=(cpp.stat().st_atime_ns, mtime))
            run(['cmake', '--build', build, '--target', 'clean'], work)
            result = run(['cmake', '--build', build, '--target', 'sc-offline-build'], work, False)
            if not result.returncode or token not in result.stdout:
                raise AssertionError('Wrong negative-control result: ' + label + '\n' + result.stdout)
            print('Build boundary rejected: ' + label)
        foreign = work / 'foreign'
        foreign.mkdir()
        (foreign / 'private.h').write_text('#pragma once\n')
        cpp.write_text(original + '\n#include "transitive.h"\n')
        (cpp.parent / 'transitive.h').write_text('#include <private.h>\n')
        cmake.write_text(cmake_original + '\ntarget_include_directories(sco-feature-build SYSTEM PRIVATE "' + str(foreign) + '")\n')
        run(['cmake', '-S', source / 'plugins/build', '-B', build], work)
        run(['cmake', '--build', build, '--target', 'clean'], work)
        run(['cmake', '--build', build, '--target', 'sc-offline-build'], work)
        try:
            trace_posix(build, source, sdk, public, feature='build')
        except AssertionError as error:
            if 'private.h' not in str(error):
                raise
            print('Build boundary rejected: system-marked transitive foreign header')
        else:
            raise AssertionError('Dependency trace admitted transitive private header')
        cpp.write_text(original)
        cmake.write_text(cmake_original)
        (cpp.parent / 'transitive.h').unlink()
        run(['cmake', '-S', source / 'plugins/build', '-B', build], work)
        run(['cmake', '--build', build, '--target', 'clean'], work)
        run(['cmake', '--build', build, '--target', 'build-picker-test'], work)
        trace_posix(build, source, sdk, public, feature='build')
        print(run(focused, work).stdout)
    print('Build SDK source proof: PASS (actual-module synthetic picker, absent-service checker; not run in game)')


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--sums', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True, help='New directory outside checkout')
    parser.add_argument('--generator', default='Visual Studio 18 2026',
                        help='CMake generator for Windows (default: Visual Studio 18 2026)')
    return parser.parse_args(argv)


if __name__ == '__main__':
    main(parse_args())
