#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Isolated optional voxel bridge archive/module/checker proof; never a release composition."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def run(argv, cwd, ok=True):
    p = subprocess.run([str(x) for x in argv], cwd=cwd, text=True,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if ok and p.returncode:
        raise AssertionError(p.stdout)
    return p


def contains(path, root):
    return path == root or root in path.parents


def trace(build, source, sdk, public):
    """Full -M trace, toolchain roots discovered without consumer include flags."""
    rows = json.loads((build / 'compile_commands.json').read_text())
    rows = [r for r in rows if 'CMakeFiles/sc-offline-voxel_bridge.dir/' in r['command']]
    if len(rows) != 1:
        raise AssertionError('Expected exactly one module translation unit')
    for row in rows:
        argv = shlex.split(row['command'])
        i = argv.index('-o'); del argv[i:i+2]; argv.remove('-c')
        probe = [argv[0]]
        i = 1
        while i < len(argv):
            flag = argv[i]
            if flag in ('-arch', '-isysroot', '--sysroot', '-target'):
                probe += argv[i:i+2]; i += 2; continue
            if flag.startswith(('-std=', '-stdlib=', '--sysroot=', '--target=')):
                probe.append(flag)
            i += 1
        result = subprocess.run(probe + ['-E', '-x', 'c++', '-v', '-'], input='',
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        roots = result.stdout.split('#include <...> search starts here:')[-1].split('End of search list.')[0]
        roots = [Path(line.strip().replace(' (framework directory)', '')).resolve()
                 for line in roots.splitlines() if line.strip().startswith('/')]
        if result.returncode or not roots:
            raise AssertionError('No compiler toolchain roots')
        metadata = set()
        if shutil.which('xcrun'):
            metadata.add(Path(run(['xcrun', '--show-sdk-path'], build).stdout.strip()).resolve() / 'SDKSettings.json')
        deps = shlex.split(run(argv + ['-M'], row['directory']).stdout.replace('\\\n', ' '))[1:]
        if not deps:
            raise AssertionError('Empty trace')
        for dep in deps:
            p = Path(dep).resolve()
            if contains(p, sdk):
                allowed = p in public
            elif contains(p, source):
                allowed = contains(p, source / 'plugins/voxel_bridge')
            elif contains(p, ROOT):
                allowed = False
            else:
                allowed = p in metadata or any(contains(p, r) for r in roots)
            if not allowed:
                raise AssertionError('Non-public voxel dependency: ' + p.name)
    print('voxel_bridge: full compiler dependency trace clean (including system headers)')


def main(args):
    work = args.work.resolve()
    if contains(work, ROOT):
        raise AssertionError('Proof must be outside host checkout')
    work.mkdir(parents=True, exist_ok=False)
    spec = importlib.util.spec_from_file_location('sdk_verifier', ROOT / 'tools/check_inspector_sdk.py')
    assert spec and spec.loader
    verifier = importlib.util.module_from_spec(spec); spec.loader.exec_module(verifier)
    sdk, public = verifier.verify_extract(args.archive.resolve(), args.sums.resolve(), work / 'sdk')
    print('archive SHA256: ' + hashlib.sha256(args.archive.read_bytes()).hexdigest())
    source = work / 'source'
    shutil.copytree(ROOT / 'plugins/voxel_bridge', source / 'plugins/voxel_bridge')
    (source / 'tools').mkdir(parents=True)
    for name in ('voxel_bridge_workflow_test.cpp', 'workflow_module.h'):
        shutil.copy2(ROOT / 'tools' / name, source / 'tools' / name)
    build = work / 'build'
    run(['cmake', '-S', source / 'plugins/voxel_bridge', '-B', build,
         '-DSCO_SDK_INCLUDE_DIR=' + str(sdk / 'include'), '-DSCO_WORKFLOW_TESTS=ON',
         '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'], work)
    run(['cmake', '--build', build, '--config', 'Release', '--target', 'voxel-bridge-module-test'], work)
    print(run(['ctest', '--test-dir', build, '-C', 'Release', '-R',
               '^voxel-bridge-module-workflow$', '--output-on-failure', '-V'], work).stdout)
    if os.name != 'nt':
        trace(build, source, sdk, public)
    else:
        print('MSVC dependency trace NOT VERIFIED by this local POSIX proof')
    stage = work / 'stage'
    run(['cmake', '--install', build, '--config', 'Release', '--prefix', stage, '--component', 'voxel_bridge'], work)
    folder = stage / 'data/plugins/voxel_bridge'
    modules = [p for p in folder.iterdir() if p.suffix in ('.dll', '.so', '.dylib')]
    if len(modules) != 1 or {p.name for p in folder.iterdir()} != {'disabled', 'plugin.ini', modules[0].name}:
        raise AssertionError('Optional install inventory/default mismatch')
    print('voxel_bridge: staged module + manifest + disabled; optional only')
    # Enable ONLY a checker copy, not the installed folder. Folder still equals ID.
    checked = work / 'checker-plugins/voxel_bridge'
    shutil.copytree(folder, checked)
    (checked / 'disabled').unlink()
    manifest = checked / 'plugin.ini'
    manifest.write_text(re.sub(r'(?m)^entry\s*=.*$', 'entry = ' + modules[0].name, manifest.read_text()))
    checker_build = work / 'checker-build'
    run(['cmake', '-S', sdk, '-B', checker_build], work)
    run(['cmake', '--build', checker_build, '--config', 'Release', '--target', 'sco-plugin-check'], work)
    checker = checker_build / ('Release/sco-plugin-check.exe' if os.name == 'nt' else 'sco-plugin-check')
    output = run([checker, checked, '--invoke', 'voxel_bridge.status'], work).stdout
    print(output)
    for token in ('voxel_bridge.status -> ok', 'G2 relocation not approved', 'OK: 0 failure(s)'):
        if token not in output:
            raise AssertionError('Missing checker proof: ' + token)
    if os.name != 'nt':
        cpp = source / 'plugins/voxel_bridge/voxel_bridge.cpp'
        original = cpp.read_text(); mtime = cpp.stat().st_mtime_ns
        for label, mutant, token in (
            ('private header', '#include "sco/plugins.h"\n', 'sco/plugins.h'),
            ('private host symbol', 'extern void Menu_RequestClearNpcs();\nnamespace { struct Forbidden { Forbidden() { Menu_RequestClearNpcs(); } } forbidden; }\n', 'Menu_RequestClearNpcs')):
            cpp.write_text(original + '\n' + mutant)
            os.utime(cpp, ns=(cpp.stat().st_atime_ns, mtime))
            run(['cmake', '--build', build, '--target', 'clean'], work)
            result = run(['cmake', '--build', build, '--target', 'sc-offline-voxel_bridge'], work, False)
            if not result.returncode or token not in result.stdout:
                raise AssertionError('Boundary did not reject ' + label + '\n' + result.stdout)
            print('voxel_bridge boundary rejected: ' + label)
        cpp.write_text(original)
        foreign = work / 'foreign'; foreign.mkdir()
        (foreign / 'private.h').write_text('#pragma once\n')
        (cpp.parent / 'transitive.h').write_text('#include <private.h>\n')
        cpp.write_text(original + '\n#include "transitive.h"\n')
        cmake = cpp.parent / 'CMakeLists.txt'; original_cmake = cmake.read_text()
        cmake.write_text(original_cmake + '\ntarget_include_directories(sc-offline-voxel_bridge SYSTEM PRIVATE "' + str(foreign) + '")\n')
        run(['cmake', '-S', cpp.parent, '-B', build], work)
        run(['cmake', '--build', build, '--target', 'clean'], work)
        run(['cmake', '--build', build, '--target', 'sc-offline-voxel_bridge'], work)
        try:
            trace(build, source, sdk, public)
        except AssertionError as error:
            if 'private.h' not in str(error):
                raise
            print('voxel_bridge boundary rejected: SYSTEM transitive foreign header')
        else:
            raise AssertionError('Trace admitted SYSTEM transitive foreign header')
        cpp.write_text(original); cmake.write_text(original_cmake); (cpp.parent / 'transitive.h').unlink()
        run(['cmake', '-S', cpp.parent, '-B', build], work)
        run(['cmake', '--build', build, '--target', 'clean'], work)
        run(['cmake', '--build', build, '--target', 'voxel-bridge-module-test'], work)
        print(run(['ctest', '--test-dir', build, '-R', '^voxel-bridge-module-workflow$', '--output-on-failure'], work).stdout)
        trace(build, source, sdk, public)
    print('voxel_bridge SDK proof: PASS (synthetic; NOT VERIFIED Windows/game/cross-process)')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--sums', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    main(parser.parse_args())
