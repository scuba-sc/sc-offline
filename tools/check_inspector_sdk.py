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
# Published SDK 2.4.0 RC1; final signed release approval remains separate.
SDK_VERSION = '2.4.0'
SDK_ROOT = 'sco-sdk-' + SDK_VERSION
SDK_ARCHIVE = SDK_ROOT + '.zip'
SDK_SHA256 = '124751534277b89f0516af1897919c3485923811596e159a1f683bb80b1c243b'


def run(argv, cwd, success=True):
    p = subprocess.run([str(x) for x in argv], cwd=cwd, text=True,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, input="")
    if success and p.returncode:
        raise RuntimeError(p.stdout)
    return p


def contained(path, root):
    return path == root or root in path.parents


def verify_extract(archive, sums, destination, pin=None):
    sdk_root, sdk_sha256 = pin or (SDK_ROOT, SDK_SHA256)
    if archive.name != sdk_root + '.zip':
        raise AssertionError(f'Expected published {sdk_root}.zip, got {archive.name}')
    rows = [line.split() for line in sums.read_text().splitlines() if line.strip()]
    hashes = [row[0] for row in rows if row[1].lstrip('*') == archive.name]
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if len(hashes) != 1 or digest != hashes[0] or digest != sdk_sha256:
        raise AssertionError('Published SDK SHA256 mismatch')
    with zipfile.ZipFile(archive) as z:
        for item in z.infolist():
            if not item.filename.startswith(sdk_root + '/'):
                raise AssertionError('Unexpected SDK archive root: ' + item.filename)
            if not contained((destination / item.filename).resolve(), destination.resolve()):
                raise AssertionError('Unsafe SDK archive path')
        z.extractall(destination)
    sdk = destination / sdk_root
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
    if sdk_root == 'sco-sdk-2.4.0':
        contracts = {
            'sc_game_build.h': ('sc_game_build_v1',),
            'sc_actor_stats.h': ('sc_actor_stats_v1',),
            'sc_inventory.h': ('sc_inventory_v1', 'category'),
            'sc_input.h': ('sc_input_v1', 'by_id', 'by_name'),
            'sc_gamerules.h': ('sc_gamerules_v1', 'list_params'),
        }
        for header, tokens in contracts.items():
            path = (sdk / 'include' / header).resolve()
            if path not in public or not path.is_file():
                raise AssertionError(f'SDK public header missing: {header}')
            text = path.read_text(encoding='utf-8')
            for token in tokens:
                if token not in text:
                    raise AssertionError(f'SDK contract {header} missing {token}')
    print(f'SDK archive {sdk_root}: SHA256 OK; extracted inventory verified')
    return sdk, public


def own_source(p, source, feature, shared):
    # A feature's own folder, the shared picker header, and only the sibling folders it names.
    allowed = [source / 'plugins' / feature] + [source / 'plugins' / s for s in shared]
    return (any(contained(p, d) for d in allowed) or p == source / 'plugins/picker.h') and p.is_file()


def trace_posix(build, source, sdk, public, feature="inspector", shared=()):
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
                ok = own_source(p, source, feature, shared)
            else:
                ok = p in metadata or any(contained(p, d) for d in system)
            if not ok:
                raise AssertionError(f'Non-public {feature} dependency: ' + p.name)
    print(f'{feature}: two full compiler dependency traces clean (including system headers)')


def trace_windows(build, source, sdk, public, feature="inspector", shared=()):
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
                ok = own_source(p, source, feature, shared)
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
    sdk_root = 'sco-sdk-' + args.sdk_version
    pin = (sdk_root, args.sdk_sha256.lower())
    published_pin = args.sdk_version == SDK_VERSION and pin[1] == SDK_SHA256
    if not published_pin and not args.candidate:
        raise AssertionError('Non-default SDK identity requires --candidate and a locked --archive-sha256')
    if args.candidate:
        if not args.archive_sha256:
            raise AssertionError('--candidate requires --archive-sha256 from the accepted candidate lock')
        if args.archive_sha256.lower() != args.sdk_sha256.lower():
            raise AssertionError('--archive-sha256 and --sdk-sha256 must identify the same locked candidate archive')
        if published_pin:
            raise AssertionError('--candidate must explicitly select candidate coordinates, not the published default')
    work.mkdir(parents=True, exist_ok=False)
    sdk, public = verify_extract(args.archive.resolve(), args.sums.resolve(), work / 'sdk', pin=pin)
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
                 '-DSCO_SDK_VERSION=' + args.sdk_version, '-DSCO_SDK_SHA256=' + args.sdk_sha256,
                 '-DSCO_SDK_ARCHIVE=' + str(args.archive.resolve()),
                 '-DSCO_ADDITIONAL_FEATURES=OFF',
                 '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DSCO_INSPECTOR_DEPENDENCY_TRACE=ON']
    if os.name == 'nt':
        configure += ['-G', args.generator, '-A', 'x64']
    else:
        configure += ['-G', 'Unix Makefiles']
    run(configure, work)
    run(['cmake', '--build', build, '--config', 'Release', '--target',
         'sc-offline-inspector', 'inspector-test', 'inspector-module-test'], work)
    p = run(['ctest', '--test-dir', build, '-C', 'Release', '-R',
             '^inspector-(module-)?workflow$', '--output-on-failure', '-V'], work)
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
        configure += ['-G', args.generator, '-A', 'x64']
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
    identity = ('explicit unpublished candidate' if args.candidate else 'published RC1 proof; final approval separate')
    print(f'inspector SDK {identity} {args.sdk_version} SHA-256 {args.sdk_sha256}: PASS (synthetic, not run in game)')


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--sums', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True, help='New directory outside checkout')
    parser.add_argument('--generator', default='Visual Studio 18 2026',
                        help='CMake generator for Windows (default: Visual Studio 18 2026)')
    parser.add_argument('--sdk-version', default=SDK_VERSION,
                        help='Exact SDK archive version (default: published 2.4.0 RC1)')
    parser.add_argument('--sdk-sha256', default=SDK_SHA256,
                        help='Expected archive SHA-256 for the selected version')
    parser.add_argument('--candidate', action='store_true',
                        help='Label this as an explicitly selected, unpublished candidate archive')
    parser.add_argument('--archive-sha256', default=None,
                        help='Required with --candidate; independently locked archive SHA-256')
    return parser.parse_args(argv)


if __name__ == '__main__':
    main(parse_args())
