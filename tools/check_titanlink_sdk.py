#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Isolated optional TitanLink archive/dependency/module/default proof; no game/network.

The scoped module workflow uses the public MIT IPC wire with synthetic spatial/UI
providers. Compiler traces cover every module translation unit. Only the named
TitanLink workflow and SDK checker are built; no host or full suites.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import sys
sys.dont_write_bytecode = True
from check_inspector_sdk import contained, run, verify_extract
ROOT = Path(__file__).resolve().parent.parent

def trace_posix(build, source, sdk, public, feature="titanlink"):
    commands = json.loads((build / 'compile_commands.json').read_text())
    commands = [c for c in commands if any(t in c['command'] for t in
                (f'CMakeFiles/sco-feature-{feature}.dir/', f'CMakeFiles/sc-offline-{feature}.dir/'))]
    if len(commands) != 4:
        raise AssertionError(f'Expected four {feature} translation units')
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
                ok = contained(p, source / 'plugins' / feature) and p.is_file()
            else:
                ok = p in metadata or any(contained(p, d) for d in system)
            if not ok:
                raise AssertionError(f'Non-public {feature} dependency: ' + p.name)
    print(f'{feature}: four full compiler dependency traces clean (including system headers)')

def trace_windows(build, source, sdk, public, feature="titanlink"):
    count = 0
    for target in (f"sco-feature-{feature}", f"sc-offline-{feature}"):
        directory = build / "dependencies" / target
        roots = [Path(p.strip()).resolve() for p in
                 (directory / "toolchain.txt").read_text(encoding="utf-8-sig").splitlines() if p.strip()]
        if not roots:
            raise AssertionError("Empty evaluated MSVC IncludePath")
        traces = list(directory.glob("*.json"))
        if len(traces) != (3 if target.startswith("sco-feature-") else 1):
            raise AssertionError(f"Expected all MSVC traces per {feature} target")
        data_rows = [json.loads(path.read_text(encoding="utf-8-sig"))["Data"] for path in traces]
        data = {"Source": data_rows[0]["Source"], "Includes": [row["Source"] for row in data_rows] + [inc for row in data_rows for inc in row["Includes"]]}
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
                ok = contained(p, source / "plugins" / feature) and p.is_file()
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
        raise AssertionError('TitanLink proof must live outside the host checkout')
    feature = ROOT / 'plugins/titanlink'
    manifest = (feature / 'plugin.ini').read_bytes()
    if not (feature / 'disabled').is_file():
        raise AssertionError('TitanLink optional folder must remain disabled')
    work.mkdir(parents=True, exist_ok=False)
    sdk, public = verify_extract(args.archive.resolve(), args.sums.resolve(), work / 'sdk')
    source = work / 'source'
    shutil.copytree(feature, source / 'plugins/titanlink')
    (source / 'tools').mkdir()
    for name in ('titanlink_workflow_test.cpp', 'workflow_module.h'):
        shutil.copy2(ROOT / 'tools' / name, source / 'tools' / name)
    build = work / 'plugin-build'
    configure = ['cmake', '-S', source / 'plugins/titanlink', '-B', build,
                 '-DSCO_SDK_INCLUDE_DIR=' + str(sdk / 'include'), '-DSCO_WORKFLOW_TESTS=ON',
                 '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DSCO_DISCORD_DEPENDENCY_TRACE=ON']
    configure += (['-G', 'Visual Studio 18 2026', '-A', 'x64'] if os.name == 'nt'
                  else ['-G', 'Unix Makefiles'])
    run(configure, work)
    run(['cmake', '--build', build, '--config', 'Release', '--target',
         'sc-offline-titanlink', 'titanlink-module-test'], work)
    print(run(['ctest', '--test-dir', build, '-C', 'Release', '-R',
               '^titanlink-module-workflow$', '--output-on-failure', '-V'], work).stdout)
    trace = trace_windows if os.name == 'nt' else trace_posix
    trace(build, source, sdk, public, feature='titanlink')
    stage = work / 'stage'
    run(['cmake', '--install', build, '--config', 'Release', '--prefix', stage,
         '--component', 'titanlink'], work)
    folder = stage / 'data/plugins/titanlink'
    modules = [p for p in folder.iterdir() if p.suffix in ('.dll', '.so', '.dylib')]
    if len(modules) != 1 or {p.name for p in folder.iterdir()} != {'plugin.ini', 'disabled', modules[0].name}:
        raise AssertionError('TitanLink install inventory/default mismatch')
    if not (folder / 'disabled').is_file() or (folder / 'plugin.ini').read_bytes() != manifest:
        raise AssertionError('Installed TitanLink policy metadata mismatch')
    print('titanlink installed folder: module + manifest + disabled; optional and excluded from host composition')
    checker_folder = work / 'checker-copy/titanlink'
    shutil.copytree(folder, checker_folder)
    (checker_folder / 'disabled').unlink()
    if os.name != 'nt':
        ini = checker_folder / 'plugin.ini'
        ini.write_text(re.sub(r'(?m)^entry[ \t]*=.*$', 'entry = ' + modules[0].name, ini.read_text()))
    checker_build = work / 'checker-build'
    configure = ['cmake', '-S', sdk, '-B', checker_build]
    if os.name == 'nt':
        configure += ['-G', 'Visual Studio 18 2026', '-A', 'x64']
    run(configure, work)
    run(['cmake', '--build', checker_build, '--config', 'Release', '--target', 'sco-plugin-check'], work)
    checker = checker_build / ('Release/sco-plugin-check.exe' if os.name == 'nt' else 'sco-plugin-check')
    for command, tokens in (
        ('titanlink.status', ('titanlink.status -> ok','G2 relocation refused','G7 cvar writes refused','OK: 0 failure(s)')),
        ('titanlink.pilot', ('titanlink.pilot -> unavailable','no private fallback','OK: 0 failure(s)')),
    ):
        checked = run([checker, checker_folder, '--invoke', command], work).stdout
        print(checked)
        for token in tokens:
            if token not in checked:
                raise AssertionError('Checker refusal/status missing: ' + token)
    if os.name != 'nt':
        cpp = source / 'plugins/titanlink/titanlink.cpp'
        original = cpp.read_text()
        mtime = cpp.stat().st_mtime_ns
        cmake = cpp.parent / 'CMakeLists.txt'
        cmake_original = cmake.read_text()
        mutants = {
            'private host header': ('#include "sco/plugins.h"\n', 'sco/plugins.h'),
            'product header': ('#include "version.h"\n', 'version.h'),
            'private host symbol': ('extern void Menu_RequestClearNpcs();\nnamespace { struct Forbidden { Forbidden() { Menu_RequestClearNpcs(); } } forbidden; }\n', 'Menu_RequestClearNpcs'),
        }
        try:
            for label, (mutation, token) in mutants.items():
                cpp.write_text(original + '\n' + mutation)
                os.utime(cpp, ns=(cpp.stat().st_atime_ns, mtime))
                run(['cmake', '--build', build, '--target', 'clean'], work)
                result = run(['cmake', '--build', build, '--target', 'sc-offline-titanlink'], work, False)
                if not result.returncode or token not in result.stdout:
                    raise AssertionError('Wrong negative-control result: ' + label + '\n' + result.stdout)
                print('TitanLink boundary rejected: ' + label)
            foreign = work / 'foreign'
            foreign.mkdir()
            (foreign / 'private.h').write_text('#pragma once\n')
            cpp.write_text(original + '\n#include "transitive.h"\n')
            (cpp.parent / 'transitive.h').write_text('#include <private.h>\n')
            cmake.write_text(cmake_original + '\ntarget_include_directories(sco-feature-titanlink SYSTEM PRIVATE "' + str(foreign) + '")\n')
            run(['cmake', '-S', cpp.parent, '-B', build], work)
            run(['cmake', '--build', build, '--target', 'clean'], work)
            run(['cmake', '--build', build, '--target', 'sc-offline-titanlink'], work)
            try:
                trace_posix(build, source, sdk, public, feature='titanlink')
            except AssertionError as error:
                if 'private.h' not in str(error):
                    raise
                print('TitanLink boundary rejected: system-marked transitive foreign header')
            else:
                raise AssertionError('Trace admitted transitive private header')
        finally:
            cpp.write_text(original)
            cmake.write_text(cmake_original)
            transitive = cpp.parent / 'transitive.h'
            if transitive.exists():
                transitive.unlink()
        # Leave the proof workspace with the actual module, not the final mutant.
        run(['cmake', '-S', cpp.parent, '-B', build], work)
        run(['cmake', '--build', build, '--target', 'clean'], work)
        run(['cmake', '--build', build, '--target', 'sc-offline-titanlink'], work)
    print('TitanLink SDK proof: PASS (synthetic; Windows/cross-process/graphics NOT VERIFIED; not run in game)')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--sums', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True, help='New directory outside checkout')
    main(parser.parse_args())
