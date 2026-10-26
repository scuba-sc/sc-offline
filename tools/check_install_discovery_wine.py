#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the production install identity helper against owned Wine C:/Z: aliases."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def check(work):
    prefix = work / 'prefix'
    env = os.environ.copy()
    env.update(WINEPREFIX=str(prefix), WINEARCH='win64', WINEDEBUG='-all',
               WINEDLLOVERRIDES='mscoree,mshtml=')
    wine = shutil.which('wine') or shutil.which('wine64')
    compiler = shutil.which('x86_64-w64-mingw32-g++')
    if not wine or not compiler:
        raise RuntimeError('Wine and x86_64-w64-mingw32-g++ are required')
    subprocess.run([wine, '--version'], env=env, check=True)
    exe = work / 'install-discovery-wine.exe'
    subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unknown-pragmas', '-municode', '-static',
                    str(ROOT / 'tools/install_discovery_wine_test.cpp'),
                    '-lpathcch', '-o', str(exe)], check=True)
    try:
        subprocess.run([wine, 'wineboot', '-u'], env=env, check=True, timeout=120)
        fixture = work / 'fixtures'
        original = fixture / 'source/LIVE/Bin64'
        other = fixture / 'other/LIVE/Bin64'
        ptu = fixture / 'source/PTU/Bin64'
        shared = fixture / 'shared/LIVE'
        for path in (original, other, ptu, shared):
            path.mkdir(parents=True)
        (original / 'StarCitizen.exe').write_bytes(b'synthetic fixture, never executed\n')
        os.link(original / 'StarCitizen.exe', other / 'StarCitizen.exe')
        os.link(original / 'StarCitizen.exe', ptu / 'StarCitizen.exe')
        (shared / 'Bin64').symlink_to(original, target_is_directory=True)
        alias = prefix / 'drive_c/sco-install-alias-fixture'
        alias.symlink_to(original.parent, target_is_directory=True)
        assert os.path.samefile(alias / 'Bin64', original)
        assert os.path.samefile(other / 'StarCitizen.exe', original / 'StarCitizen.exe')
        assert os.path.samefile(shared / 'Bin64', original)
        assert not os.path.samefile(shared, original.parent)
        # A fresh Wine prefix maps Z: to the host root. Verify rather than replacing a mapping.
        assert os.path.samefile(prefix / 'dosdevices/z:', '/')
        zroot = 'Z:' + str(fixture).replace('/', '\\')
        subprocess.run([wine, str(exe), r'C:\sco-install-alias-fixture\Bin64', zroot],
                       env=env, check=True, timeout=60)
    finally:
        server = shutil.which('wineserver')
        if server:
            subprocess.run([server, '-k'], env=env, check=True, timeout=30)
            subprocess.run([server, '-w'], env=env, check=True, timeout=30)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path)
    args = parser.parse_args()
    if args.work:
        work = args.work.resolve()
        work.mkdir(parents=True, exist_ok=False)
        check(work)
    else:
        with tempfile.TemporaryDirectory(prefix='sco-install-wine-') as directory:
            check(Path(directory))


if __name__ == '__main__':
    main()
