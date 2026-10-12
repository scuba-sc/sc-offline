#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""One focused Discord module/settings/lifecycle and published sdk-v2.0.0 boundary proof.

No host checkout headers, game, Discord process, network, or privacy exception.
The checker deliberately loads disabled folders: it is not host discovery proof.
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
        raise AssertionError('Discord proof must live outside the host checkout')
    feature = ROOT / 'plugins/discord'
    manifest = (feature / 'plugin.ini').read_bytes()
    if manifest.decode().replace('\r\n', '\n') != (ROOT / 'data/plugins/discord/plugin.ini').read_text():
        raise AssertionError('Discord source/release manifests differ')
    for folder in (feature, ROOT / 'data/plugins/discord'):
        if (folder / 'disabled').read_bytes() != b'':
            raise AssertionError('Discord must ship an empty disabled marker')
    for name in ('show_location', 'show_ship', 'show_buttons'):
        if not re.search(r'(?m)^' + name + r'\s*= bool default false\b', manifest.decode()):
            raise AssertionError('Unsafe Discord preference default: ' + name)
    # Regression tripwire, not a sandbox: keep removed custom transport/game reads absent.
    forbidden = r'\b(?:CreateFile\w*|WriteFile|ReadFile|PeekNamedPipe|CreateThread|CreateEvent\w*|WaitForSingleObject|socket|connect|WSAStartup|WinHttp\w*|player_pose|zone_name|PlayerShip|ClassOf)\s*\('
    for cpp in feature.glob('*.cpp'):
        if re.search(forbidden, cpp.read_text()):
            raise AssertionError('Custom transport/game read restored: ' + cpp.name)
    work.mkdir(parents=True, exist_ok=False)
    sdk, public = verify_extract(args.archive.resolve(), args.sums.resolve(), work / 'sdk')
    source = work / 'source'
    shutil.copytree(feature, source / 'plugins/discord')
    (source / 'tools').mkdir()
    for name in ('discord_test.cpp', 'workflow_module.h'):
        shutil.copy2(ROOT / 'tools' / name, source / 'tools' / name)
    build = work / 'plugin-build'
    configure = ['cmake', '-S', source / 'plugins/discord', '-B', build,
                 '-DSCO_SDK_INCLUDE_DIR=' + str(sdk / 'include'), '-DSCO_WORKFLOW_TESTS=ON',
                 '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DSCO_DISCORD_DEPENDENCY_TRACE=ON']
    configure += (['-G', 'Visual Studio 18 2026', '-A', 'x64'] if os.name == 'nt'
                  else ['-G', 'Unix Makefiles'])
    run(configure, work)
    run(['cmake', '--build', build, '--config', 'Release', '--target',
         'sc-offline-discord', 'discord-module-test'], work)
    print(run(['ctest', '--test-dir', build, '-C', 'Release', '-R',
               '^discord-module-workflow$', '--output-on-failure', '-V'], work).stdout)
    trace = trace_windows if os.name == 'nt' else trace_posix
    trace(build, source, sdk, public, feature='discord')
    stage = work / 'stage'
    run(['cmake', '--install', build, '--config', 'Release', '--prefix', stage,
         '--component', 'discord'], work)
    folder = stage / 'data/plugins/discord'
    modules = [p for p in folder.iterdir() if p.suffix in ('.dll', '.so', '.dylib')]
    if len(modules) != 1 or {p.name for p in folder.iterdir()} != {'plugin.ini', 'disabled', modules[0].name}:
        raise AssertionError('Discord install inventory/default mismatch')
    if (folder / 'disabled').read_bytes() != b'' or (folder / 'plugin.ini').read_bytes() != manifest:
        raise AssertionError('Installed Discord policy metadata mismatch')
    print('discord installed folder: module + manifest + disabled; preferences off')
    if os.name != 'nt':
        ini = folder / 'plugin.ini'
        ini.write_text(re.sub(r'(?m)^entry[ \t]*=.*$', 'entry = ' + modules[0].name, ini.read_text()))
    checker_build = work / 'checker-build'
    configure = ['cmake', '-S', sdk, '-B', checker_build]
    if os.name == 'nt':
        configure += ['-G', 'Visual Studio 18 2026', '-A', 'x64']
    run(configure, work)
    run(['cmake', '--build', checker_build, '--config', 'Release', '--target', 'sco-plugin-check'], work)
    checker = checker_build / ('Release/sco-plugin-check.exe' if os.name == 'nt' else 'sco-plugin-check')
    checked = run([checker, folder, '--invoke', 'discord.status'], work).stdout
    print(checked)
    for token in ('discord.status -> unavailable', 'transport and fields await owner privacy/security approval',
                  'nothing transmitted', 'OK: 0 failure(s)'):
        if token not in checked:
            raise AssertionError('Checker refusal missing: ' + token)
    if os.name != 'nt':
        cpp = source / 'plugins/discord/discord.cpp'
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
                result = run(['cmake', '--build', build, '--target', 'sc-offline-discord'], work, False)
                if not result.returncode or token not in result.stdout:
                    raise AssertionError('Wrong negative-control result: ' + label + '\n' + result.stdout)
                print('Discord boundary rejected: ' + label)
            foreign = work / 'foreign'
            foreign.mkdir()
            (foreign / 'private.h').write_text('#pragma once\n')
            cpp.write_text(original + '\n#include "transitive.h"\n')
            (cpp.parent / 'transitive.h').write_text('#include <private.h>\n')
            cmake.write_text(cmake_original + '\ntarget_include_directories(sco-feature-discord SYSTEM PRIVATE "' + str(foreign) + '")\n')
            run(['cmake', '-S', cpp.parent, '-B', build], work)
            run(['cmake', '--build', build, '--target', 'clean'], work)
            run(['cmake', '--build', build, '--target', 'sc-offline-discord'], work)
            try:
                trace_posix(build, source, sdk, public, feature='discord')
            except AssertionError as error:
                if 'private.h' not in str(error):
                    raise
                print('Discord boundary rejected: system-marked transitive foreign header')
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
        run(['cmake', '--build', build, '--target', 'sc-offline-discord'], work)
    print('Discord SDK refusal proof: PASS (synthetic; transport/privacy approval BLOCKED; not run in game)')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--sums', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True, help='New directory outside checkout')
    main(parser.parse_args())
