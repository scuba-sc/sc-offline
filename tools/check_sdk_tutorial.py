#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Copy tutorial-owned NPC/inspector sources, then exercise isolated SDK proofs.

Only the archive supplies SDK headers/checker. Generated copies use distinct IDs;
no host checkout, submodule, installed feature or published pin is modified.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def owned(text):
    # Rename the exact feature tokens in source, paths and fixture expectations.
    # Uppercase SDK types/capability names and native export names stay unchanged.
    for name in ('check_inspector_sdk.py', 'check_npc_sdk.py',
                 'inspector_test.cpp', 'npc_workflow_test.cpp',
                 'inspector_test:', 'npc_workflow_test:'):
        text = text.replace(name, name.replace('inspector', 'tutorial_inspector').replace('npc', 'tutorial_npc'))
    # Module import uses the filename without .py. Do not rewrite public API
    # identifiers such as spawn_npc or SDK capability strings.
    text = text.replace('from check_inspector_sdk import', 'from check_tutorial_inspector_sdk import')
    return re.sub(r'\b(inspector|npc)\b', lambda m: 'tutorial_' + m.group(), text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--sums', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path, help='New root outside checkout')
    parser.add_argument('--feature', choices=('npc', 'inspector', 'both'), default='both')
    parser.add_argument('--prepare-only', action='store_true',
                        help='Copy owned examples only; build/check commands are separate')
    args = parser.parse_args()
    work = args.work.resolve()
    if work == ROOT or ROOT in work.parents:
        parser.error('Tutorial must live outside the product checkout')
    # Fail before making copies if inputs are missing; checksum verification is
    # performed by each existing archive proof before extraction or compilation.
    archive, sums = args.archive.resolve(strict=True), args.sums.resolve(strict=True)
    work.mkdir(parents=True, exist_ok=False)
    source = work / 'owned-source'
    files = ['plugins/CMakeLists.txt', 'plugins/picker.h', 'tools/check_inspector_sdk.py',
             'tools/check_npc_sdk.py', 'tools/inspector_test.cpp',
             'tools/npc_workflow_test.cpp', 'tools/workflow_module.h']
    for feature in ('npc', 'inspector'):
        files.extend(str(path.relative_to(ROOT)) for path in
                     sorted((ROOT / 'plugins' / feature).iterdir())
                     if path.is_file() and path.suffix in ('.cpp', '.h', '.inc', '.ini'))
    for name in files:
        destination = source / owned(name)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(owned((ROOT / name).read_text()))
    # Preserve the project license for independently edited/copied source.
    shutil.copy2(ROOT / 'LICENSE', source / 'LICENSE')
    if args.prepare_only:
        print(f'Tutorial-owned source: {source}; build/check/tests NOT RUN')
        return
    selected = ('npc', 'inspector') if args.feature == 'both' else (args.feature,)
    for feature in selected:
        subprocess.run([sys.executable, str(source / 'tools' / owned(f'check_{feature}_sdk.py')),
                        '--archive', str(archive), '--sums', str(sums),
                        '--work', str(work / f'{feature}-proof')], check=True)
        print(f'tutorial_{feature}: copied source + named dynamic fixture + checker PASS', flush=True)
    print('SDK tutorial: PASS (synthetic; Windows/game/release NOT VERIFIED)', flush=True)


if __name__ == '__main__':
    main()
