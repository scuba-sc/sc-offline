#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Isolated creative build/install/check and one actual module workflow from SDK 2.1.0."""
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
        raise AssertionError('Creative proof must live outside the host checkout')
    work.mkdir(parents=True, exist_ok=False)
    sdk, public = verify_extract(args.archive.resolve(), args.sums.resolve(), work / 'sdk')
    if (ROOT / 'plugins/creative/plugin.ini').read_text() != (ROOT / 'data/plugins/creative/plugin.ini').read_text():
        raise AssertionError('Creative source/distribution manifests differ')
    if not (ROOT / 'data/plugins/creative/disabled').is_file():
        raise AssertionError('Creative distribution must stay disabled by default')
    source = work / 'source'
    shutil.copytree(ROOT / 'plugins/creative', source / 'plugins/creative')
    (source / 'tools').mkdir()
    for name in ('creative_module_test.cpp', 'workflow_module.h'):
        shutil.copy2(ROOT / 'tools' / name, source / 'tools' / name)
    build = work / 'plugin-build'
    configure = ['cmake', '-S', source / 'plugins/creative', '-B', build,
                 '-DSCO_SDK_INCLUDE_DIR=' + str(sdk / 'include'), '-DSCO_WORKFLOW_TESTS=ON',
                 '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DSCO_CREATIVE_DEPENDENCY_TRACE=ON']
    configure += (['-G', 'Visual Studio 18 2026', '-A', 'x64'] if os.name == 'nt'
                  else ['-G', 'Unix Makefiles'])
    run(configure, work)
    run(['cmake', '--build', build, '--config', 'Release', '--target',
         'sc-offline-creative', 'creative-module-test'], work)
    focused = ['ctest', '--test-dir', build, '-C', 'Release', '-R',
               '^creative-module-workflow$', '--output-on-failure', '-V']
    print(run(focused, work).stdout)
    trace = trace_windows if os.name == 'nt' else trace_posix
    trace(build, source, sdk, public, feature='creative')
    stage = work / 'stage'
    run(['cmake', '--install', build, '--config', 'Release', '--prefix', stage,
         '--component', 'creative'], work)
    folder = stage / 'data/plugins/creative'
    modules = [p for p in folder.iterdir() if p.suffix in ('.dll', '.so', '.dylib')]
    if len(modules) != 1 or not (folder / 'disabled').is_file():
        raise AssertionError('Creative install inventory/default mismatch')
    if {p.name for p in folder.iterdir()} != {'plugin.ini', 'disabled', modules[0].name}:
        raise AssertionError('Unexpected files in installed creative')
    print('creative install: native module, plugin.ini, disabled marker (off default)')
    # Keep the installed/default folder untouched; checker fixture models explicit user enablement.
    check_folder = work / 'checker-plugins/creative'
    shutil.copytree(folder, check_folder)
    (check_folder / 'disabled').unlink()
    manifest = check_folder / 'plugin.ini'
    if os.name != 'nt':
        manifest.write_text(re.sub(r'(?m)^entry[ \t]*=.*$', 'entry = ' + modules[0].name, manifest.read_text()))
    checker_build = work / 'checker-build'
    configure_checker = ['cmake', '-S', sdk, '-B', checker_build]
    if os.name == 'nt':
        configure_checker += ['-G', 'Visual Studio 18 2026', '-A', 'x64']
    run(configure_checker, work)
    run(['cmake', '--build', checker_build, '--config', 'Release', '--target', 'sco-plugin-check'], work)
    checker = checker_build / ('Release/sco-plugin-check.exe' if os.name == 'nt' else 'sco-plugin-check')
    checked = run([checker, check_folder, '--invoke', 'creative.status'], work).stdout
    print(checked)
    for token in ('creative.status -> unavailable', 'Creative status failed:', 'OK: 0 failure(s)'):
        if token not in checked:
            raise AssertionError('Missing-service checker reply missing: ' + token)
    if args.sanitize:
        if os.name == 'nt':
            raise AssertionError('Sanitizer fixture is POSIX development evidence only')
        sanitized = work / 'sanitizer-build'
        run(['cmake', '-S', source / 'plugins/creative', '-B', sanitized,
             '-DSCO_SDK_INCLUDE_DIR=' + str(sdk / 'include'), '-DSCO_WORKFLOW_TESTS=ON',
             '-DSCO_CREATIVE_SANITIZERS=ON', '-DCMAKE_BUILD_TYPE=Debug'], work)
        run(['cmake', '--build', sanitized, '--target', 'creative-module-test'], work)
        print(run(['ctest', '--test-dir', sanitized, '-R', '^creative-module-workflow$',
                   '--output-on-failure', '-V'], work).stdout)
    if os.name != 'nt':
        cpp = source / 'plugins/creative/creative.cpp'
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
            result = run(['cmake', '--build', build, '--target', 'sc-offline-creative'], work, False)
            if not result.returncode or token not in result.stdout:
                raise AssertionError('Wrong negative-control result: ' + label + '\n' + result.stdout)
            print('creative boundary rejected: ' + label)
        foreign = work / 'foreign'
        foreign.mkdir()
        (foreign / 'private.h').write_text('#pragma once\n')
        cpp.write_text(original + '\n#include "transitive.h"\n')
        (cpp.parent / 'transitive.h').write_text('#include <private.h>\n')
        cmake = source / 'plugins/creative/CMakeLists.txt'
        original_cmake = cmake.read_text()
        cmake.write_text(original_cmake + '\ntarget_include_directories(sco-feature-creative SYSTEM PRIVATE "' + str(foreign) + '")\n')
        run(['cmake', '-S', source / 'plugins/creative', '-B', build], work)
        run(['cmake', '--build', build, '--target', 'clean'], work)
        run(['cmake', '--build', build, '--target', 'sc-offline-creative'], work)
        try:
            trace_posix(build, source, sdk, public, feature='creative')
        except AssertionError as error:
            if 'private.h' not in str(error):
                raise
            print('creative boundary rejected: system-marked transitive foreign header')
        else:
            raise AssertionError('Dependency trace admitted transitive private header')
        cpp.write_text(original)
        cmake.write_text(original_cmake)
        (cpp.parent / 'transitive.h').unlink()
        run(['cmake', '-S', source / 'plugins/creative', '-B', build], work)
        run(['cmake', '--build', build, '--target', 'clean'], work)
        run(['cmake', '--build', build, '--target', 'sc-offline-creative', 'creative-module-test'], work)
    print('creative SDK release proof: PASS (synthetic; release-hook stand-in; not run in game)')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--sums', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True, help='New directory outside checkout')
    parser.add_argument('--sanitize', action='store_true', help='Also instrument the same focused module case on POSIX')
    main(parser.parse_args())
