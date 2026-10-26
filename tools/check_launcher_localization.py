#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check launcher UTF-8 catalogues, formatting, selection and pinned ImGui text semantics."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def system_font():
    candidates = [Path(os.environ.get('WINDIR', 'C:/Windows')) / 'Fonts/segoeui.ttf',
                  Path('/System/Library/Fonts/Supplemental/Arial.ttf'),
                  Path('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'),
                  Path('/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf')]
    return next((str(path) for path in candidates if path.is_file()), '')


def check(work, font):
    work.mkdir(parents=True, exist_ok=False)
    build = work / 'build'
    subprocess.run(['cmake', '-S', str(ROOT / 'tools/launcher-localization'), '-B', str(build),
                    '-DLOCALE_FONT=' + font], check=True)
    subprocess.run(['cmake', '--build', str(build), '--config', 'Release', '--parallel', '2'], check=True)
    subprocess.run(['ctest', '--test-dir', str(build), '-C', 'Release', '--output-on-failure'], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path)
    parser.add_argument('--font', type=Path)
    args = parser.parse_args()
    font = str(args.font.resolve()) if args.font else system_font()
    if args.work:
        check(args.work.resolve(), font)
    else:
        with tempfile.TemporaryDirectory(prefix='sc-offline-locale-') as folder:
            check(Path(folder) / 'proof', font)


if __name__ == '__main__':
    main()
