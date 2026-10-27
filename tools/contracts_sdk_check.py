#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""One contracts slice from a checksum-verified SDK, with isolated native-module proof.

Local integrated SDKs are explicitly development evidence, never published-pin or
Windows/gameplay certification. Reuses established archive and full-dependency gates.
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
        raise AssertionError('Contracts proof must be outside the host checkout')
    if args.sanitizers and os.name == 'nt':
        raise AssertionError('Sanitizer proof requires POSIX')
    pin = explicit_preflight_pin(args)
    work.mkdir(parents=True, exist_ok=False)
    sdk, public = verify_extract(args.archive.resolve(), args.sums.resolve(), work / 'sdk', pin=pin)
    if args.candidate:
        print('Explicit unpublished development compatibility preflight; SHA-256 ' + pin[1])
    source = work / 'source'
    (source / 'plugins').mkdir(parents=True)
    shutil.copytree(ROOT / 'plugins/contracts', source / 'plugins/contracts')
    (source / 'tools').mkdir()
    for name in ('contracts_workflow_test.cpp', 'contracts_metadata_test.cpp', 'workflow_module.h'):
        shutil.copy2(ROOT / 'tools' / name, source / 'tools' / name)
    build = work / 'plugin-build'
    configure = ['cmake', '-S', source / 'plugins/contracts', '-B', build,
                 '-DSCO_SDK_INCLUDE_DIR=' + str(sdk / 'include'), '-DSCO_WORKFLOW_TESTS=ON',
                 '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DSCO_CONTRACTS_DEPENDENCY_TRACE=ON']
    configure += (['-G', 'Visual Studio 18 2026', '-A', 'x64'] if os.name == 'nt'
                  else ['-G', 'Unix Makefiles'])
    run(configure, work)

    def compile_module():
        run(['cmake', '--build', build, '--config', 'Release', '--target', 'contracts-module-test', 'contracts-metadata-test'], work)

    def workflow():
        print(run(['ctest', '--test-dir', build, '-C', 'Release', '-R',
                   '^contracts-' + args.slice + '$', '--output-on-failure', '-V'], work).stdout)

    compile_module()
    workflow()
    trace = trace_windows if os.name == 'nt' else trace_posix
    trace(build, source, sdk, public, feature='contracts')
    stage = work / 'stage'
    run(['cmake', '--install', build, '--config', 'Release', '--prefix', stage,
         '--component', 'contracts'], work)
    folder = stage / 'data/plugins/contracts'
    modules = [p for p in folder.iterdir() if p.suffix in ('.dll', '.so', '.dylib')]
    if len(modules) != 1 or (folder / 'disabled').exists():
        raise AssertionError('Contracts install inventory mismatch')
    manifest = folder / 'plugin.ini'
    if os.name != 'nt':
        manifest.write_text(re.sub(r'(?m)^entry[ \t]*=.*$', 'entry = ' + modules[0].name, manifest.read_text()))
    if {p.name for p in folder.iterdir()} != {'plugin.ini', modules[0].name}:
        raise AssertionError('Unexpected contracts installation files')
    checker_build = work / 'checker-build'
    command = ['cmake', '-S', sdk, '-B', checker_build]
    if os.name == 'nt':
        command += ['-G', 'Visual Studio 18 2026', '-A', 'x64']
    run(command, work)
    run(['cmake', '--build', checker_build, '--config', 'Release', '--target', 'sco-plugin-check'], work)
    checker = checker_build / ('Release/sco-plugin-check.exe' if os.name == 'nt' else 'sco-plugin-check')
    checked = run([checker, folder, '--invoke', 'contracts.status'], work).stdout
    print(checked)
    for token in ('contracts.status -> unavailable', 'no private fallback', 'OK: 0 failure(s)'):
        if token not in checked:
            raise AssertionError('Missing-service checker did not refuse: ' + token)
    cpp = source / 'plugins/contracts/contracts.cpp'
    original = cpp.read_text()
    cmake = source / 'plugins/contracts/CMakeLists.txt'
    cmake_original = cmake.read_text()
    mtime = cpp.stat().st_mtime_ns
    if os.name != 'nt' and args.boundary:
        mutants = {
            'private host header': ('#include "sco/plugins.h"\n', 'sco/plugins.h'),
            'product header': ('#include "version.h"\n', 'version.h'),
            'private host symbol': ('extern void ProcessContracts();\nnamespace { struct Forbidden { Forbidden() { ProcessContracts(); } } forbidden; }\n', 'ProcessContracts'),
        }
        for label, (mutation, token) in mutants.items():
            cpp.write_text(original + '\n' + mutation)
            os.utime(cpp, ns=(cpp.stat().st_atime_ns, mtime))
            run(['cmake', '--build', build, '--target', 'clean'], work)
            result = run(['cmake', '--build', build, '--target', 'sc-offline-contracts'], work, False)
            if not result.returncode or token not in result.stdout:
                raise AssertionError('Boundary negative control failed: ' + label + '\n' + result.stdout)
            print('Contracts boundary rejected: ' + label)
        foreign = work / 'foreign'
        foreign.mkdir()
        (foreign / 'private.h').write_text('#pragma once\n')
        (cpp.parent / 'transitive.h').write_text('#include <private.h>\n')
        cpp.write_text(original + '\n#include "transitive.h"\n')
        cmake.write_text(cmake_original + '\ntarget_include_directories(sco-feature-contracts SYSTEM PRIVATE "' + str(foreign) + '")\n')
        run(['cmake', '-S', source / 'plugins/contracts', '-B', build], work)
        run(['cmake', '--build', build, '--target', 'clean'], work)
        run(['cmake', '--build', build, '--target', 'sc-offline-contracts'], work)
        try:
            trace_posix(build, source, sdk, public, feature='contracts')
        except AssertionError as error:
            if 'private.h' not in str(error):
                raise
            print('Contracts boundary rejected: system-marked transitive foreign header')
        else:
            raise AssertionError('Foreign transitive include was admitted')
        cpp.write_text(original)
        cmake.write_text(cmake_original)
        (cpp.parent / 'transitive.h').unlink()
        run(['cmake', '-S', source / 'plugins/contracts', '-B', build], work)
        run(['cmake', '--build', build, '--target', 'clean'], work)
        compile_module()
        trace_posix(build, source, sdk, public, feature='contracts')
    if args.sanitizers:
        run(['cmake', '-S', source / 'plugins/contracts', '-B', build,
             '-DCMAKE_CXX_FLAGS=-g -fsanitize=address,undefined -fno-omit-frame-pointer'], work)
        run(['cmake', '--build', build, '--target', 'clean'], work)
        compile_module()
        workflow()
        # Development instrumentation only; ordinary staged/checker module stays untouched.
        broken = original.replace('>= offsetof(type, m) + sizeof((t)->m)', '> offsetof(type, m)')
        if broken == original:
            raise AssertionError('Guard mutation no longer matches')
        cpp.write_text(broken)
        run(['cmake', '--build', build, '--target', 'clean'], work)
        compile_module()
        module = build / 'test-plugins/contracts' / modules[0].name
        executable = build / ('contracts-metadata-test' if args.slice == 'metadata' else 'contracts-module-test')
        failed = run([executable, module] + ([] if args.slice == 'metadata' else [args.slice]), work, False)
        if not failed.returncode or 'AddressSanitizer: heap-buffer-overflow' not in failed.stdout:
            raise AssertionError('Physical partial-pointer red arm did not catch overread:\n' + failed.stdout)
        print('Contracts partial-pointer red arm: ASan heap-buffer-overflow (expected)')
        cpp.write_text(original)
        run(['cmake', '--build', build, '--target', 'clean'], work)
        compile_module()
        workflow()
    print('Contracts ' + args.slice + ' SDK proof: PASS (synthetic; not run in game; storage policy pending)')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--sums', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True, help='New isolated directory')
    parser.add_argument('--sdk-version', default=SDK_VERSION)
    parser.add_argument('--sdk-sha256', default=SDK_SHA256)
    parser.add_argument('--candidate', action='store_true', help='Explicit unpublished preflight archive')
    parser.add_argument('--archive-sha256', help='Required locked digest with --candidate')
    parser.add_argument('--sdk-source', help='Exact full Core source commit for candidate')
    parser.add_argument('--sdk-profile', help='Candidate SDK profile (legacy)')
    parser.add_argument('--slice', choices=('read', 'accept', 'progress', 'script', 'economy', 'metadata'), required=True)
    parser.add_argument('--boundary', action='store_true', help='POSIX boundary negative controls')
    parser.add_argument('--sanitizers', action='store_true', help='POSIX physical-pointer red/green probe')
    main(parser.parse_args())
