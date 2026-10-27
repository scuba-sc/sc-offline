#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build/check/install NPC from published RC1 or an explicitly locked development SDK.

Uses the inspector proof's archive and compiler-boundary checks (step 1B).
Windows emits the release DLL; POSIX additionally runs source-mutation controls.
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
        raise AssertionError('NPC proof must live outside the host checkout')
    pin = explicit_preflight_pin(args)
    work.mkdir(parents=True, exist_ok=False)
    sdk, public = verify_extract(args.archive.resolve(), args.sums.resolve(), work / 'sdk', pin=pin)
    if args.candidate:
        print('Explicit unpublished development compatibility preflight; SHA-256 ' + pin[1])
    source = work / 'source'
    (source / 'plugins').mkdir(parents=True)
    shutil.copy2(ROOT / 'plugins/CMakeLists.txt', source / 'plugins/CMakeLists.txt')
    shutil.copy2(ROOT / 'plugins/picker.h', source / 'plugins/picker.h')
    for feature in ('npc', 'inspector'):
        shutil.copytree(ROOT / 'plugins' / feature, source / 'plugins' / feature)
    (source / 'tools').mkdir()
    for name in ('inspector_test.cpp', 'npc_workflow_test.cpp', 'workflow_module.h'):
        shutil.copy2(ROOT / 'tools' / name, source / 'tools' / name)
    build = work / 'plugin-build'
    configure = ['cmake', '-S', source / 'plugins', '-B', build,
                 '-DSCO_SDK_INCLUDE_DIR=' + str(sdk / 'include'), '-DSCO_WORKFLOW_TESTS=ON',
                 '-DSCO_ADDITIONAL_FEATURES=OFF',
                 '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DSCO_NPC_DEPENDENCY_TRACE=ON']
    configure += (['-G', args.generator, '-A', 'x64'] if os.name == 'nt'
                  else ['-G', 'Unix Makefiles'])
    run(configure, work)
    run(['cmake', '--build', build, '--config', 'Release', '--target',
         'sc-offline-npc', 'npc-module-test'], work)
    print(run(['ctest', '--test-dir', build, '-C', 'Release', '-R',
               '^npc-module-workflow$', '--output-on-failure', '-V'], work).stdout)
    trace = trace_windows if os.name == 'nt' else trace_posix
    trace(build, source, sdk, public, feature='npc')
    stage = work / 'stage'
    run(['cmake', '--install', build, '--config', 'Release', '--prefix', stage,
         '--component', 'npc'], work)
    folder = stage / 'data/plugins/npc'
    modules = [p for p in folder.iterdir() if p.suffix in ('.dll', '.so', '.dylib')]
    if len(modules) != 1 or (folder / 'disabled').exists():
        raise AssertionError('NPC install inventory/default mismatch')
    manifest = folder / 'plugin.ini'
    if os.name != 'nt':
        manifest.write_text(re.sub(r'(?m)^entry[ \t]*=.*$', 'entry = ' + modules[0].name, manifest.read_text()))
    if {p.name for p in folder.iterdir()} != {'plugin.ini', modules[0].name}:
        raise AssertionError('Unexpected files in installed NPC')
    checker_build = work / 'checker-build'
    configure = ['cmake', '-S', sdk, '-B', checker_build]
    if os.name == 'nt':
        configure += ['-G', args.generator, '-A', 'x64']
    run(configure, work)
    run(['cmake', '--build', checker_build, '--config', 'Release', '--target', 'sco-plugin-check'], work)
    checker = checker_build / ('Release/sco-plugin-check.exe' if os.name == 'nt' else 'sco-plugin-check')
    checked = run([checker, folder, '--invoke', 'npc.observe'], work).stdout
    print(checked)
    for token in ('npc.observe -> unavailable', 'NPC visibility/transform observation unavailable; ownership unchanged',
                  'OK: 0 failure(s)'):
        if token not in checked:
            raise AssertionError('Missing-service checker reply missing: ' + token)
    if os.name != 'nt':
        cpp = source / 'plugins/npc/npc.cpp'
        original = cpp.read_text()
        mtime = cpp.stat().st_mtime_ns
        mutants = {
            'private host header': ('#include "sco/plugins.h"\n', 'sco/plugins.h'),
            'product header': ('#include "version.h"\n', 'version.h'),
            'private host symbol': ('extern void Menu_RequestClearNpcs();\nnamespace { struct Forbidden { Forbidden() { Menu_RequestClearNpcs(); } } forbidden; }\n', 'Menu_RequestClearNpcs'),
        }
        for label, (mutation, token) in mutants.items():
            cpp.write_text(original + '\n' + mutation)
            os.utime(cpp, ns=(cpp.stat().st_atime_ns, mtime))
            run(['cmake', '--build', build, '--target', 'clean'], work)
            result = run(['cmake', '--build', build, '--target', 'sc-offline-npc'], work, False)
            if not result.returncode or token not in result.stdout:
                raise AssertionError('Wrong negative-control result: ' + label + '\n' + result.stdout)
            print('NPC boundary rejected: ' + label)
        foreign = work / 'foreign'
        foreign.mkdir()
        (foreign / 'private.h').write_text('#pragma once\n')
        cpp.write_text(original + '\n#include "transitive.h"\n')
        (cpp.parent / 'transitive.h').write_text('#include <private.h>\n')
        cmake = source / 'plugins/CMakeLists.txt'
        cmake.write_text(cmake.read_text() + '\ntarget_include_directories(sco-feature-npc SYSTEM PRIVATE "' + str(foreign) + '")\n')
        run(['cmake', '-S', source / 'plugins', '-B', build], work)
        run(['cmake', '--build', build, '--target', 'clean'], work)
        run(['cmake', '--build', build, '--target', 'sc-offline-npc'], work)
        try:
            trace_posix(build, source, sdk, public, feature='npc')
        except AssertionError as error:
            if 'private.h' not in str(error):
                raise
            print('NPC boundary rejected: system-marked transitive foreign header')
        else:
            raise AssertionError('Dependency trace admitted transitive private header')
        cpp.write_text(original)
    print('NPC SDK source proof: PASS (synthetic, not run in game)')


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
    parser.add_argument('--generator', default='Visual Studio 18 2026',
                        help='CMake generator for Windows (default: Visual Studio 18 2026)')
    return parser.parse_args(argv)


if __name__ == '__main__':
    main(parse_args())
