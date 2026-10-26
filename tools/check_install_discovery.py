#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check install identity comparisons and real Windows junction/hardlink fixtures."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def junction(alias, target):
    env = os.environ.copy()
    env['SCO_TEST_ALIAS'] = str(alias)
    env['SCO_TEST_INSTALL'] = str(target)
    subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command',
                    "$ErrorActionPreference='Stop'; New-Item -ItemType Junction "
                    '-Path $env:SCO_TEST_ALIAS -Target $env:SCO_TEST_INSTALL | Out-Null'],
                   env=env, check=True)


def check(work):
    fixture = work / 'fixtures'
    drive = ''
    links = []
    literal_roots = []
    try:
        if os.name == 'nt':
            original = fixture / 'source' / 'LIVE' / 'Bin64'
            other = fixture / 'other' / 'LIVE' / 'Bin64'
            ptu = fixture / 'source' / 'PTU' / 'Bin64'
            shared = fixture / 'shared' / 'LIVE'
            for path in (original, other, ptu, shared, original / 'child'):
                path.mkdir(parents=True, exist_ok=True)
            synthetic = b'synthetic fixture, never executed\n'
            (original / 'StarCitizen.exe').write_bytes(synthetic)
            os.link(original / 'StarCitizen.exe', other / 'StarCitizen.exe')
            (ptu / 'StarCitizen.exe').write_bytes(synthetic)
            for alias, target in ((fixture / 'alias', original.parent),
                                  (shared / 'Bin64', original), (original / 'Bin64', original)):
                junction(alias, target)
                links.append(alias)
            assert os.path.samefile(original / 'StarCitizen.exe', other / 'StarCitizen.exe')
            assert os.path.samefile(original, shared / 'Bin64')
            assert not os.path.samefile(original.parent, shared)
            rooted = fixture / 'rooted'
            (rooted / 'Bin64').mkdir(parents=True)
            (rooted / 'Other').mkdir()
            (rooted / 'root-user').mkdir()
            (rooted / 'StarCitizen.exe').write_bytes(synthetic)
            (rooted / 'Other' / 'StarCitizen.exe').write_bytes(synthetic)
            junction(rooted / 'Other' / 'Bin64', rooted / 'Bin64')
            links.append(rooted / 'Other' / 'Bin64')
            for channel, leaf in (('literal-dot.', 'Bin64'), ('literal-space ', 'Bin64'),
                                  ('literal-bin-dot', 'Bin64.'), ('literal-bin-space', 'Bin64 ')):
                raw = '\\\\?\\' + str(fixture / channel)
                os.mkdir(raw)
                literal_roots.append(raw)
                os.mkdir(raw + '\\' + leaf)
                with open(raw + '\\' + leaf + '\\StarCitizen.exe', 'wb') as stream:
                    stream.write(synthetic)
            selected = next((letter + ':' for letter in 'ZYXWVUTSRQPONMLK'
                             if not os.path.exists(letter + ':\\')), '')
            if not selected:
                raise RuntimeError('No unused drive for the isolated root-directory fixture')
            subprocess.run(['subst.exe', selected, str(rooted)], check=True)
            drive = selected
        build_and_test(work, fixture, drive)
    finally:
        try:
            if drive:
                if not os.path.samefile(drive + '\\', fixture / 'rooted'):
                    raise RuntimeError('Fixture drive identity changed; refusing to remove another mapping')
                subprocess.run(['subst.exe', drive, '/D'], check=True)
                if os.path.exists(drive + '\\'):
                    raise RuntimeError('Fixture drive cleanup did not complete')
        finally:
            for link in reversed(links):
                os.rmdir(link)
            for raw in literal_roots:
                shutil.rmtree(raw)


def build_and_test(work, fixture, drive):
    build = work / 'build'
    subprocess.run(['cmake', '-S', str(ROOT / 'tools/install-discovery'), '-B', str(build),
                    '-DINSTALL_DISCOVERY_FIXTURE_DIR=' + str(fixture),
                    '-DINSTALL_DISCOVERY_DRIVE=' + drive], check=True)
    subprocess.run(['cmake', '--build', str(build), '--config', 'Release', '--parallel', '2'], check=True)
    subprocess.run(['ctest', '--test-dir', str(build), '-C', 'Release', '--output-on-failure'], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path)
    args = parser.parse_args()
    if args.work:
        work = args.work.resolve()
        work.mkdir(parents=True, exist_ok=False)
        check(work)
    else:
        with tempfile.TemporaryDirectory(prefix='sc-offline-install-') as folder:
            check(Path(folder))


if __name__ == '__main__':
    main()
