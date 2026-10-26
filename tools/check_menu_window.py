#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Headless menu size/file and actual pinned ImGui tab overflow regressions."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def check(work, compiler):
    work.mkdir(parents=True, exist_ok=True)
    binary = work / ('menu_window_test.exe' if os.name == 'nt' else 'menu_window_test')
    sources = [ROOT / 'tools/menu_window_test.cpp']
    sources += [ROOT / f'src/third_party/imgui/{name}.cpp'
                for name in ('imgui', 'imgui_draw', 'imgui_tables', 'imgui_widgets')]
    subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-I', str(ROOT / 'src'), *map(str, sources), '-o', str(binary)],
                   cwd=work, check=True)
    subprocess.run([str(binary), str(work)], cwd=work, check=True)
    subprocess.run([str(binary), str(work), '--restore'], cwd=work, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
    args = parser.parse_args()
    if args.work:
        check(args.work.resolve(), args.cxx)
    else:
        with tempfile.TemporaryDirectory(prefix='sc-offline-menu-') as folder:
            check(Path(folder), args.cxx)


if __name__ == '__main__':
    main()
