#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build/check/install inspector from a verified published SDK, outside the host.

Pass the release zip and its adjacent release SHA256SUMS. Windows uses MSVC;
POSIX also proves compiler dependency traces and source-mutation controls.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent
# The one published SDK these proofs consume: sdk-v2.1.0 (signed approval at sco-core 8ef24243).
SDK_ROOT = 'sco-sdk-2.1.0'
SDK_ARCHIVE = SDK_ROOT + '.zip'
SDK_SHA256 = 'f5ee92708360c9df1a6b9986c78acab25ea49da460b37b89b47e5e2091326f64'


def run(argv, cwd, success=True):
    p = subprocess.run([str(x) for x in argv], cwd=cwd, text=True,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, input="")
    if success and p.returncode:
        raise RuntimeError(p.stdout)
    return p


def contained(path, root):
    return path == root or root in path.parents


def verify_extract(archive, sums, destination):
    if archive.name != SDK_ARCHIVE:
        raise AssertionError(f'Expected published {SDK_ARCHIVE}, got {archive.name}')
    rows = [line.split() for line in sums.read_text().splitlines() if line.strip()]
    hashes = [row[0] for row in rows if row[1].lstrip('*') == archive.name]
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if len(hashes) != 1 or digest != hashes[0] or digest != SDK_SHA256:
        raise AssertionError('Published SDK SHA256 mismatch')
    with zipfile.ZipFile(archive) as z:
        for item in z.infolist():
            if not item.filename.startswith(SDK_ROOT + '/'):
                raise AssertionError('Unexpected SDK archive root: ' + item.filename)
            if not contained((destination / item.filename).resolve(), destination.resolve()):
                raise AssertionError('Unsafe SDK archive path')
        z.extractall(destination)
    sdk = destination / SDK_ROOT
    public, listed = set(), set()
    for line in (sdk / 'SHA256SUMS').read_text().splitlines():
        digest, name = line.split(None, 1)
        name = name.lstrip('*')
        path = (sdk / name).resolve()
        if not contained(path, sdk.resolve()) or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise AssertionError('Extracted SDK checksum mismatch: ' + name)
        listed.add(path)
        if name.startswith('include/'):
            public.add(path)
    extracted = {p.resolve() for p in sdk.rglob('*') if p.is_file()} - {(sdk / 'SHA256SUMS').resolve()}
    if extracted != listed:
        raise AssertionError('Extracted SDK files not covered by its SHA256SUMS: ' +
                             ', '.join(sorted(str(p) for p in extracted ^ listed)))
    if not public:
        raise AssertionError('Empty published header inventory')
    print('published SDK: SHA256 OK; extracted inventory verified')
    return sdk, public


def trace_posix(build, source, sdk, public, feature="inspector"):
    commands = json.loads((build / 'compile_commands.json').read_text())
    commands = [c for c in commands if any(t in c['command'] for t in
                (f'CMakeFiles/sco-feature-{feature}.dir/', f'CMakeFiles/sc-offline-{feature}.dir/'))]
    if len(commands) != 2:
        raise AssertionError(f'Expected two {feature} translation units')
    for command in commands:
        argv = shlex.split(command['command'])
        i = argv.index('-o'); del argv[i:i + 2]
        argv.remove('-c')
        # Include system-marked dependencies too. Discover the same compiler's toolchain.
        # Do not bless plugin-supplied -I/-isystem directories as toolchain roots.
        # Ask the compiler with only target/sysroot/standard selection retained.
        probe_argv = [argv[0]]
        i = 1
        while i < len(argv):
            flag = argv[i]
            if flag in ('-arch', '-isysroot', '--sysroot', '-target'):
                probe_argv.extend(argv[i:i + 2]); i += 2; continue
            if flag.startswith(('-std=', '-stdlib=', '--sysroot=', '--target=')):
                probe_argv.append(flag)
            i += 1
        probe = run(probe_argv + ['-E', '-x', 'c++', '-v', '-'], command['directory']).stdout
        section = probe.split('#include <...> search starts here:')[-1].split('End of search list.')[0]
        system = []
        for line in section.splitlines():
            text = line.strip().replace(' (framework directory)', '')
            if text and Path(text).is_absolute():
                p = Path(text).resolve()
                if not contained(p, sdk) and not contained(p, source) and not contained(p, ROOT):
                    system.append(p)
        if not system:
            raise AssertionError('Compiler toolchain include roots not discovered')
        metadata = set()
        if shutil.which("xcrun"):
            sysroot = Path(run(["xcrun", "--show-sdk-path"], build).stdout.strip()).resolve()
            metadata.add(sysroot / "SDKSettings.json")
        deps = shlex.split(run(argv + ['-M'], command['directory']).stdout.replace('\\\n', ' '))[1:]
        if not deps:
            raise AssertionError('Empty compiler dependency trace')
        for dep in deps:
            p = Path(dep).resolve()
            if contained(p, sdk):
                ok = p in public
            elif contained(p, ROOT):
                ok = False
            elif contained(p, source):
                ok = (contained(p, source / 'plugins' / feature) or p == source / 'plugins/picker.h') and p.is_file()
            else:
                ok = p in metadata or any(contained(p, d) for d in system)
            if not ok:
                raise AssertionError(f'Non-public {feature} dependency: ' + p.name)
    print(f'{feature}: two full compiler dependency traces clean (including system headers)')


def trace_windows(build, source, sdk, public, feature="inspector"):
    count = 0
    for target in (f"sco-feature-{feature}", f"sc-offline-{feature}"):
        directory = build / "dependencies" / target
        roots = [Path(p.strip()).resolve() for p in
                 (directory / "toolchain.txt").read_text(encoding="utf-8-sig").splitlines() if p.strip()]
        if not roots:
            raise AssertionError("Empty evaluated MSVC IncludePath")
        traces = list(directory.glob("*.json"))
        if len(traces) != 1:
            raise AssertionError(f"Expected one MSVC trace per {feature} target")
        data = json.loads(traces[0].read_text(encoding="utf-8-sig"))["Data"]
        dependencies = [data["Source"]] + data["Includes"]
        if not data["Includes"]:
            raise AssertionError("Empty MSVC include trace")
        for dependency in dependencies:
            p = Path(dependency).resolve()
            if contained(p, sdk):
                ok = p in public
            elif contained(p, ROOT):
                ok = False
            elif contained(p, source):
                ok = (contained(p, source / "plugins" / feature) or p == source / "plugins/picker.h") and p.is_file()
            else:
                ok = any(contained(p, d) for d in roots)
            if not ok:
                raise AssertionError(f"Non-public {feature} dependency: " + p.name)
        count += 1
    if count != 2:
        raise AssertionError(f"Missing MSVC {feature} traces")
    print(f"{feature}: two full MSVC compiler dependency traces clean")


def main(args):
    work = args.work.resolve()
    if contained(work, ROOT):
        raise AssertionError("Inspector proof must live outside the host checkout")
    work.mkdir(parents=True, exist_ok=False)
    sdk, public = verify_extract(args.archive.resolve(), args.sums.resolve(), work / 'sdk')
    source = work / 'source'
    (source / 'plugins').mkdir(parents=True)
    shutil.copy2(ROOT / 'plugins/CMakeLists.txt', source / 'plugins/CMakeLists.txt')
    for feature in ('npc', 'inspector'):
        shutil.copytree(ROOT / 'plugins' / feature, source / 'plugins' / feature)
    (source / 'tools').mkdir()
    for name in ('inspector_test.cpp', 'npc_workflow_test.cpp', 'workflow_module.h'):
        shutil.copy2(ROOT / 'tools' / name, source / 'tools' / name)
    build = work / 'plugin-build'
    configure = ['cmake', '-S', source / 'plugins', '-B', build,
                 '-DSCO_SDK_INCLUDE_DIR=' + str(sdk / 'include'), '-DSCO_WORKFLOW_TESTS=ON',
                 '-DSCO_ADDITIONAL_FEATURES=OFF',
                 '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DSCO_INSPECTOR_DEPENDENCY_TRACE=ON']
    if os.name == 'nt':
        configure += ['-G', 'Visual Studio 18 2026', '-A', 'x64']
    else:
        configure += ['-G', 'Unix Makefiles']
    run(configure, work)
    run(['cmake', '--build', build, '--config', 'Release', '--target',
         'sc-offline-inspector', 'inspector-module-test'], work)
    p = run(['ctest', '--test-dir', build, '-C', 'Release', '-R',
             '^inspector-module-workflow$', '--output-on-failure', '-V'], work)
    print(p.stdout)
    trace = trace_windows if os.name == 'nt' else trace_posix
    trace(build, source, sdk, public)
    stage = work / 'stage'
    run(['cmake', '--install', build, '--config', 'Release', '--prefix', stage, '--component', 'inspector'], work)
    folder = stage / 'data/plugins/inspector'
    modules = [p for p in folder.iterdir() if p.suffix in ('.dll', '.so', '.dylib')]
    if len(modules) != 1 or (folder / 'disabled').exists():
        raise AssertionError('Inspector install inventory/default mismatch')
    manifest = folder / 'plugin.ini'
    if os.name != 'nt':
        manifest.write_text(re.sub(r'(?m)^entry[ \t]*=.*$', 'entry = ' + modules[0].name, manifest.read_text()))
    if {p.name for p in folder.iterdir()} != {'plugin.ini', modules[0].name}:
        raise AssertionError('Unexpected files in installed inspector')
    checker_build = work / 'checker-build'
    configure = ['cmake', '-S', sdk, '-B', checker_build]
    if os.name == 'nt':
        configure += ['-G', 'Visual Studio 18 2026', '-A', 'x64']
    run(configure, work)
    run(['cmake', '--build', checker_build, '--config', 'Release', '--target', 'sco-plugin-check'], work)
    checker = checker_build / ('Release/sco-plugin-check.exe' if os.name == 'nt' else 'sco-plugin-check')
    checked = run([checker, folder, '--invoke', 'inspector.read'], work).stdout
    print(checked)
    for token in ('inspector.read -> ok', 'actor=unavailable', 'health.maximum=unavailable', 'OK: 0 failure(s)'):
        if token not in checked:
            raise AssertionError('Missing-service checker reply missing: ' + token)
    if os.name != 'nt':
        cpp = source / 'plugins/inspector/inspector.cpp'
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
            result = run(['cmake', '--build', build, '--target', 'sc-offline-inspector'], work, False)
            if not result.returncode or token not in result.stdout:
                raise AssertionError('Wrong negative-control result: ' + label + '\n' + result.stdout)
            print('inspector boundary rejected: ' + label)
        # An absolute, system-marked transitive include can compile but must fail the trace.
        foreign = work / 'foreign'
        foreign.mkdir()
        (foreign / 'private.h').write_text('#pragma once\n')
        cpp.write_text(original + '\n#include "transitive.h"\n')
        (cpp.parent / 'transitive.h').write_text('#include <private.h>\n')
        cmake = source / 'plugins/CMakeLists.txt'
        cmake.write_text(cmake.read_text() + '\ntarget_include_directories(sco-feature-inspector SYSTEM PRIVATE "' + str(foreign) + '")\n')
        run(['cmake', '-S', source / 'plugins', '-B', build], work)
        run(['cmake', '--build', build, '--target', 'clean'], work)
        run(['cmake', '--build', build, '--target', 'sc-offline-inspector'], work)
        try:
            trace_posix(build, source, sdk, public)
        except AssertionError as error:
            if 'private.h' not in str(error):
                raise
            print('inspector boundary rejected: system-marked transitive foreign header')
        else:
            raise AssertionError('Dependency trace admitted transitive private header')
        cpp.write_text(original)
    print('inspector SDK release proof: PASS (synthetic, not run in game)')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--sums', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True, help='New directory outside checkout')
    main(parser.parse_args())
