#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Isolated Loadout proof from published RC1 or an explicitly locked development SDK.

Public consumer sources and authored/catalogued identifiers only. Exercises actual
module callbacks, existing linked workflows and private-dependency controls. No game.
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
        raise AssertionError('Loadout proof must live outside the product checkout')
    pin = explicit_preflight_pin(args)
    work.mkdir(parents=True, exist_ok=False)
    sdk, public = verify_extract(args.archive.resolve(), args.sums.resolve(), work / 'sdk', pin=pin)
    if args.candidate:
        print('Explicit unpublished development compatibility preflight; SHA-256 ' + pin[1])
    source = work / 'source'
    (source / 'plugins').mkdir(parents=True)
    shutil.copytree(ROOT / 'plugins/loadout', source / 'plugins/loadout')
    (source / 'tools').mkdir()
    for name in ('loadout_catalogue_test.cpp', 'workflow_module.h'):
        shutil.copy2(ROOT / 'tools' / name, source / 'tools' / name)
    (source / 'data').mkdir()
    shutil.copy2(ROOT / 'data/loadout_slots.txt', source / 'data/loadout_slots.txt')
    build = work / 'plugin-build'
    configure = ['cmake', '-S', source / 'plugins/loadout', '-B', build,
                 '-DSCO_SDK_INCLUDE_DIR=' + str(sdk / 'include'), '-DSCO_WORKFLOW_TESTS=ON',
                 '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DSCO_LOADOUT_DEPENDENCY_TRACE=ON']
    configure += (['-G', args.generator, '-A', 'x64'] if os.name == 'nt'
                  else ['-G', 'Unix Makefiles'])
    run(configure, work)
    run(['cmake', '--build', build, '--config', 'Release'], work)
    focused = ['ctest', '--test-dir', build, '-C', 'Release', '--output-on-failure', '-V']
    print(run(focused, work).stdout)
    trace = trace_windows if os.name == 'nt' else trace_posix
    trace(build, source, sdk, public, feature='loadout')
    stage = work / 'stage'
    run(['cmake', '--install', build, '--config', 'Release', '--prefix', stage,
         '--component', 'loadout'], work)
    folder = stage / 'data/plugins/loadout'
    modules = [p for p in folder.iterdir() if p.suffix in ('.dll', '.so', '.dylib')]
    if len(modules) != 1 or (folder / 'disabled').exists():
        raise AssertionError('Loadout install inventory mismatch')
    manifest = folder / 'plugin.ini'
    if os.name != 'nt':
        manifest.write_text(re.sub(r'(?m)^entry[ \t]*=.*$', 'entry = ' + modules[0].name, manifest.read_text()))
    if {p.name for p in folder.iterdir()} != {'plugin.ini', modules[0].name}:
        raise AssertionError('Unexpected files in installed Loadout')
    if list(stage.rglob('loadout_slots.txt')):
        raise AssertionError('Optional curated catalogue was implicitly staged')
    checker_build = work / 'checker-build'
    configure = ['cmake', '-S', sdk, '-B', checker_build]
    if os.name == 'nt':
        configure += ['-G', args.generator, '-A', 'x64']
    run(configure, work)
    run(['cmake', '--build', checker_build, '--config', 'Release', '--target', 'sco-plugin-check'], work)
    checker = checker_build / ('Release/sco-plugin-check.exe' if os.name == 'nt' else 'sco-plugin-check')
    checker_result = run([checker, folder, '--invoke', 'loadout.slots', 'primary=FixtureClass'], work, False)
    checked = checker_result.stdout
    (work / 'sdk-checker.log').write_text(checked)
    print(checked)
    for token in ('loadout.slots -> unavailable', 'no XML/private equip fallback',
                  'loadout.reload_slots -> unavailable', 'loadout.progress -> not_found',
                  'loadout.cancel -> not_found', 'unload  ok'):
        if token not in checked:
            raise AssertionError('Missing-service checker reply absent: ' + token)
    # RC1's generic checker only permits OK/UNAVAILABLE for argument-free
    # commands. These two existing no-job commands correctly return NOT_FOUND.
    failures = [line for line in checked.splitlines() if line.startswith('FAIL ')]
    expected = ['FAIL loadout.progress returned not_found', 'FAIL loadout.cancel returned not_found']
    if args.candidate:
        if checker_result.returncode != 0 or failures or 'OK: 0 failure(s)' not in checked:
            raise AssertionError('Unexpected development checker result:\n' + checked)
        print('Development SDK checker: expected no-job results accepted; clean pass')
    else:
        if checker_result.returncode != 1 or failures != expected or 'FAILED: 2 failure(s)' not in checked:
            raise AssertionError('Unexpected published RC1 checker result:\n' + checked)
        print('Published RC1 checker: two documented no-job NOT_FOUND mismatches; not a clean checker pass')
    if os.name != 'nt':
        sanitized = work / 'sanitizer-build'
        run(['cmake', '-S', source / 'plugins/loadout', '-B', sanitized,
             '-DSCO_SDK_INCLUDE_DIR=' + str(sdk / 'include'), '-DSCO_WORKFLOW_TESTS=ON',
             '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer',
             '-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined',
             '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'], work)
        run(['cmake', '--build', sanitized], work)
        print(run(['ctest', '--test-dir', sanitized, '--output-on-failure', '-V'], work).stdout)
        cpp = source / 'plugins/loadout/loadout.cpp'
        original = cpp.read_text()
        cmake = source / 'plugins/loadout/CMakeLists.txt'
        cmake_original = cmake.read_text()
        marker = 'if(next_player_cookie<std::numeric_limits<uintptr_t>::max())'
        if marker not in original:
            raise AssertionError('Player publication red marker missing')
        cpp.write_text(original.replace(marker, 'if(false)'))
        run(['cmake', '--build', build, '--target', 'clean'], work)
        run(['cmake', '--build', build, '--target', 'loadout-catalogue-test'], work)
        red = run(['ctest', '--test-dir', build, '-R', '^loadout-curated-catalogue$', '--output-on-failure'], work, False)
        if not red.returncode or 'independent public-slot selectors published' not in red.stdout:
            raise AssertionError('Wrong missing-picker negative control:\n' + red.stdout)
        print('Loadout catalogue red arm: actual module missing selectors detected (expected)')
        cpp.write_text(original)
        mutants = {
            'private host header': ('#include "sco/plugins.h"\n', 'sco/plugins.h'),
            'product header': ('#include "version.h"\n', 'version.h'),
            'private host symbol': ('extern void ProcessLoadout();\nnamespace { struct Forbidden { Forbidden() { ProcessLoadout(); } } forbidden; }\n', 'ProcessLoadout'),
        }
        for label, (mutation, token) in mutants.items():
            cpp.write_text(original + '\n' + mutation)
            run(['cmake', '--build', build, '--target', 'clean'], work)
            result = run(['cmake', '--build', build, '--target', 'sc-offline-loadout'], work, False)
            if not result.returncode or token not in result.stdout:
                raise AssertionError('Wrong dependency negative result: ' + label + '\n' + result.stdout)
            print('Loadout boundary rejected: ' + label)
        foreign = work / 'foreign'
        foreign.mkdir()
        (foreign / 'private.h').write_text('#pragma once\n')
        (cpp.parent / 'transitive.h').write_text('#include <private.h>\n')
        cpp.write_text(original + '\n#include "transitive.h"\n')
        cmake.write_text(cmake_original + '\ntarget_include_directories(sco-feature-loadout SYSTEM PRIVATE "' + str(foreign) + '")\n')
        run(['cmake', '-S', source / 'plugins/loadout', '-B', build], work)
        run(['cmake', '--build', build, '--target', 'clean'], work)
        run(['cmake', '--build', build, '--target', 'sc-offline-loadout'], work)
        try:
            trace_posix(build, source, sdk, public, feature='loadout')
        except AssertionError as error:
            if 'private.h' not in str(error):
                raise
            print('Loadout boundary rejected: system-marked transitive foreign header')
        else:
            raise AssertionError('Dependency trace admitted transitive private header')
        cpp.write_text(original)
        cmake.write_text(cmake_original)
        (cpp.parent / 'transitive.h').unlink()
        run(['cmake', '-S', source / 'plugins/loadout', '-B', build], work)
        run(['cmake', '--build', build, '--target', 'clean'], work)
        run(['cmake', '--build', build], work)
        trace_posix(build, source, sdk, public, feature='loadout')
        print(run(focused, work).stdout)
    print('Loadout SDK source proof: PASS (synthetic; not run in game)')


def parse_args(argv=None):
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
    parser.add_argument('--generator', default='Visual Studio 18 2026')
    return parser.parse_args(argv)


if __name__ == '__main__':
    main(parse_args())
