#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build tools/menu_preview and draw every menu page from a panel dump into PNG files.

Windows only (Direct3D WARP and WIC). The dump comes from the plugins' synthetic workflow tests run
with SCO_PANEL_DUMP=<file> (tools/panel_dump.h). The pictures show the product's shell, theme and
widget renderer drawing the node trees those tests produced; they are not screenshots of the game.

  python tools/menu_preview/run.py --dump panels.txt --out preview/ [--host-source external/sco-core]
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
IMGUI = ROOT / 'src/third_party/imgui'


def build(work, compiler, host):
    binary = work / 'menu_preview.exe'
    sources = [ROOT / 'tools/menu_preview/menu_preview.cpp', ROOT / 'src/menu_shell.cpp',
               host / 'src/widgets/imgui_adapter.cpp']
    sources += [IMGUI / f'{name}.cpp' for name in ('imgui', 'imgui_draw', 'imgui_tables', 'imgui_widgets', 'imgui_impl_dx11')]
    subprocess.run([compiler, '-std=c++20', '-O1', '-Wall', '-Wextra', '-DUNICODE',
                    '-I', str(ROOT / 'src'), '-I', str(IMGUI), '-I', str(host / 'include'), '-I', str(host / 'src/widgets'),
                    *map(str, sources), '-o', str(binary),
                    '-ld3d11', '-ld3dcompiler', '-ldxgi', '-lwindowscodecs', '-lole32', '-luuid'], check=True)
    return binary


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--dump', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--host-source', type=Path, default=ROOT / 'external/sco-core')
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
    parser.add_argument('--width', type=int, default=1000)
    parser.add_argument('--height', type=int, default=800)
    parser.add_argument('--developer', action='store_true', help='Include the developer pages')
    args = parser.parse_args()
    if os.name != 'nt':
        raise SystemExit('menu preview needs Windows (Direct3D WARP and WIC)')
    host = args.host_source.resolve()
    if not (host / 'src/widgets/imgui_adapter.cpp').is_file():
        raise SystemExit(f'host source missing: {host} (initialize external/sco-core or pass --host-source)')
    with tempfile.TemporaryDirectory(prefix='sc-offline-preview-') as folder:
        binary = build(Path(folder), args.cxx, host)
        command = [str(binary), str(args.dump.resolve()), str(args.out.resolve()),
                   '--width', str(args.width), '--height', str(args.height)]
        if args.developer:
            command.append('--developer')
        subprocess.run(command, check=True)


if __name__ == '__main__':
    main()
