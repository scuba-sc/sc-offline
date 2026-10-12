#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""One focused crew dynamic-module proof against checksum-verified sdk-v2.0.0.

Copies only the crew consumer and fixture outside the checkout. Reuses the
established published-archive and exact dependency proofs from inspector #123.
No host checkout/submodule, game or runtime network is used.
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
        raise AssertionError('Crew proof must live outside the host checkout')
    if args.sanitizers and os.name == 'nt':
        raise AssertionError('This sanitizer fixture is POSIX-only; MSVC proof is separate')
    work.mkdir(parents=True, exist_ok=False)
    sdk, public = verify_extract(args.archive.resolve(), args.sums.resolve(), work / 'sdk')
    source = work / 'source'
    feature = source / 'plugins/crew'
    shutil.copytree(ROOT / 'plugins/crew', feature)
    (source / 'tools').mkdir()
    for name in ('crew_workflow_test.cpp', 'workflow_module.h'):
        shutil.copy2(ROOT / 'tools' / name, source / 'tools' / name)
    flags = []
    if args.sanitizers:
        for var in ('CMAKE_CXX_FLAGS', 'CMAKE_C_FLAGS', 'CMAKE_EXE_LINKER_FLAGS', 'CMAKE_MODULE_LINKER_FLAGS'):
            flags.append('-D' + var + '=-fsanitize=address,undefined -fno-omit-frame-pointer')
    generator = ['-G', 'Visual Studio 18 2026', '-A', 'x64'] if os.name == 'nt' else ['-G', 'Unix Makefiles']
    build = work / 'plugin-build'
    run(['cmake', '-S', feature, '-B', build, *generator,
         '-DSCO_SDK_INCLUDE_DIR=' + str(sdk / 'include'), '-DSCO_WORKFLOW_TESTS=ON',
         '-DSCO_CREW_DEPENDENCY_TRACE=ON', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'], work)
    run(['cmake', '--build', build, '--config', 'Release', '--target', 'crew-module-test'], work)
    test = ['ctest', '--test-dir', build, '-C', 'Release', '-R', '^crew-module-workflow$', '--output-on-failure', '-V']
    tested = run(test, work).stdout
    print(tested)
    if len(re.findall(r'^1: PASS ', tested, re.M)) != 8:
        raise AssertionError('Expected eight named crew workflow groups')
    trace = trace_windows if os.name == 'nt' else trace_posix
    trace(build, source, sdk, public, feature='crew')
    stage = work / 'stage'
    run(['cmake', '--install', build, '--config', 'Release', '--prefix', stage, '--component', 'crew'], work)
    folder = stage / 'data/plugins/crew'
    modules = [p for p in folder.iterdir() if p.suffix in ('.dll', '.so', '.dylib')]
    if len(modules) != 1 or (folder / 'disabled').exists():
        raise AssertionError('Crew inventory/default mismatch')
    manifest = folder / 'plugin.ini'
    if os.name != 'nt':
        manifest.write_text(re.sub(r'(?m)^entry[ \t]*=.*$', 'entry = ' + modules[0].name, manifest.read_text()))
    if {p.name for p in folder.iterdir()} != {'plugin.ini', modules[0].name}:
        raise AssertionError('Unexpected files in installed crew')
    checker_build = work / 'checker-build'
    run(['cmake', '-S', sdk, '-B', checker_build, *generator], work)
    run(['cmake', '--build', checker_build, '--config', 'Release', '--target', 'sco-plugin-check'], work)
    checker = checker_build / ('Release/sco-plugin-check.exe' if os.name == 'nt' else 'sco-plugin-check')
    checked = run([checker, folder, '--invoke', 'crew.target'], work).stdout
    print(checked)
    for token in ('crew.target -> unavailable', 'OK: 0 failure(s)'):
        if token not in checked:
            raise AssertionError('Missing-service checker reply missing: ' + token)
    if os.name != 'nt':
        cpp = feature / 'crew.cpp'
        original = cpp.read_text()
        cmake = feature / 'CMakeLists.txt'
        original_cmake = cmake.read_text()
        mtime = cpp.stat().st_mtime_ns

        def rebuild(mutation, success=False):
            cpp.write_text(original + '\n' + mutation)
            os.utime(cpp, ns=(cpp.stat().st_atime_ns, mtime))
            run(['cmake', '--build', build, '--target', 'clean'], work)
            return run(['cmake', '--build', build, '--target', 'sc-offline-crew'], work, success)

        mutants = {
            'private host header': ('#include "sco/plugins.h"\n', 'sco/plugins.h'),
            'product header': ('#include "version.h"\n', 'version.h'),
            'private host symbol': ('extern void Menu_RequestClearNpcs();\nnamespace { struct Forbidden { Forbidden() { Menu_RequestClearNpcs(); } } forbidden; }\n', 'Menu_RequestClearNpcs'),
        }
        try:
            for label, (mutation, token) in mutants.items():
                result = rebuild(mutation)
                if not result.returncode or token not in result.stdout:
                    raise AssertionError('Wrong negative-control result: ' + label + '\n' + result.stdout)
                print('Crew boundary rejected: ' + label)
            foreign = work / 'foreign'
            foreign.mkdir()
            (foreign / 'private.h').write_text('#pragma once\n')
            (feature / 'transitive.h').write_text('#include <private.h>\n')
            cmake.write_text(original_cmake + '\ntarget_include_directories(sco-feature-crew SYSTEM PRIVATE "' + str(foreign) + '")\n')
            run(['cmake', '-S', feature, '-B', build], work)
            rebuild('#include "transitive.h"\n', True)
            try:
                trace_posix(build, source, sdk, public, feature='crew')
            except AssertionError as error:
                if 'private.h' not in str(error):
                    raise
                print('Crew boundary rejected: system-marked transitive foreign header')
            else:
                raise AssertionError('Dependency trace admitted transitive private header')
            if args.sanitizers:
                # Separate instrumentation build: release traces stay strict and
                # need no exemption for compiler-owned sanitizer metadata.
                cmake.write_text(original_cmake)
                cpp.write_text(original)
                build = work / 'sanitized-build'
                run(['cmake', '-S', feature, '-B', build, *generator, *flags,
                     '-DSCO_SDK_INCLUDE_DIR=' + str(sdk / 'include'), '-DSCO_WORKFLOW_TESTS=ON'], work)
                test = ['ctest', '--test-dir', build, '-R', '^crew-module-workflow$', '--output-on-failure', '-V']
                run(['cmake', '--build', build, '--target', 'crew-module-test'], work)
                print(run(test, work).stdout)
                # Prove physically truncated tables catch the old offset-only rule.
                cpp.write_text(original.replace('(t)->size >= offsetof(type,member) + sizeof((t)->member)', '(t)->size > offsetof(type,member)'))
                run(['cmake', '-S', feature, '-B', build], work)
                run(['cmake', '--build', build, '--target', 'clean'], work)
                run(['cmake', '--build', build, '--target', 'crew-module-test'], work)
                result = run(test, work, False)
                if not result.returncode or 'AddressSanitizer: heap-buffer-overflow' not in result.stdout:
                    raise AssertionError('Partial-member negative control was not caught by ASan\n' + result.stdout)
                print('Crew guard rejected: offset-only predicate (ASan heap-buffer-overflow)')
        finally:
            cpp.write_text(original)
            cmake.write_text(original_cmake)
            (feature / 'transitive.h').unlink(missing_ok=True)
        run(['cmake', '-S', feature, '-B', build], work)
        run(['cmake', '--build', build, '--target', 'clean'], work)
        run(['cmake', '--build', build, '--target', 'crew-module-test'], work)
        print(run(test, work).stdout)
        print('Crew restored consumer: focused workflow PASS')
    print('Crew SDK proof: PASS (synthetic; not run in game)')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--sums', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True, help='New directory outside checkout')
    parser.add_argument('--sanitizers', action='store_true', help='POSIX ASan/UBSan fixture and guard mutant')
    main(parser.parse_args())
