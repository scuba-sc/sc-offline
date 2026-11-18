#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the C++ launcher against the golden contract files in launcher-rs/tests/golden.

Builds tools/launcher-contract (it compiles the real launcher/launcher.cpp), runs it into a scratch
folder and requires the output to equal the committed golden tree byte for byte. The Rust crates
load the same golden tree, so a change on either side fails a check.

  python tools/check_launcher_contract.py             check (Windows only)
  python tools/check_launcher_contract.py --update    rewrite launcher-rs/tests/golden from the C++ launcher

The sco-core checkout that holds third_party/sqlite is external/sco-core, or --core <folder>,
or the SCO_CORE_DIR environment variable. Nothing here runs in game.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
GOLDEN = ROOT / 'launcher-rs' / 'tests' / 'golden'
# Files in the golden folder that people write, not the dump tool.
HAND_WRITTEN = {'.gitattributes', 'README.md'}


def build_and_dump(work, core):
    build = work / 'build'
    out = work / 'out'
    sqlite = Path(core) / 'third_party' / 'sqlite' / 'sqlite3.c'
    if not sqlite.is_file():
        raise SystemExit('missing %s: check out the sco-core submodule or pass --core <folder>' % sqlite)
    subprocess.run(['cmake', '-S', str(ROOT / 'tools' / 'launcher-contract'), '-B', str(build),
                    '-DSCO_CORE_DIR=' + str(Path(core).resolve())], check=True)
    subprocess.run(['cmake', '--build', str(build), '--config', 'Release', '--parallel', '2'], check=True)
    exe = next(build.rglob('launcher_contract_dump.exe'))
    subprocess.run([str(exe), str(out), str(ROOT)], check=True)
    return out


def tree(base):
    return {p.relative_to(base).as_posix(): p for p in sorted(base.rglob('*')) if p.is_file()}


def compare(out):
    new = tree(out)
    old = {k: v for k, v in tree(GOLDEN).items() if k not in HAND_WRITTEN}
    problems = []
    for name in sorted(set(new) | set(old)):
        if name not in old:
            problems.append('not committed: ' + name)
        elif name not in new:
            problems.append('no longer produced: ' + name)
        elif new[name].read_bytes() != old[name].read_bytes():
            problems.append('differs: ' + name)
    return problems


def update(out):
    for name, path in tree(GOLDEN).items():
        if name not in HAND_WRITTEN:
            path.unlink()
    for name, path in tree(out).items():
        target = GOLDEN / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    print('golden files rewritten: %d' % len(tree(out)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--work', type=Path, help='scratch folder (must not exist)')
    parser.add_argument('--core', default=os.environ.get('SCO_CORE_DIR', str(ROOT / 'external' / 'sco-core')))
    parser.add_argument('--update', action='store_true', help='rewrite the golden files instead of checking them')
    args = parser.parse_args()
    if os.name != 'nt':
        raise SystemExit('this check compiles the Windows launcher and runs on Windows only')

    def run(work):
        work.mkdir(parents=True, exist_ok=False)
        out = build_and_dump(work, args.core)
        if args.update:
            update(out)
            return 0
        problems = compare(out)
        if problems:
            print('The C++ launcher no longer matches launcher-rs/tests/golden:')
            for line in problems[:40]:
                print('  ' + line)
            if len(problems) > 40:
                print('  ... and %d more' % (len(problems) - 40))
            print('If the change is intended, run: python tools/check_launcher_contract.py --update')
            return 1
        print('launcher contract: %d golden files match' % len(tree(out)))
        return 0

    if args.work:
        return run(args.work.resolve())
    with tempfile.TemporaryDirectory(prefix='sc-offline-contract-') as folder:
        return run(Path(folder) / 'proof')


if __name__ == '__main__':
    sys.exit(main())
