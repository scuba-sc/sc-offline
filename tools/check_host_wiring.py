#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Source-text guards for the host wiring fixed in the menu-organization work.

Reads source text only; it does not build the host and is not an in-game check.

 1. The menu starts whether or not the legacy ship spawner resolved (Menu_Start is never guarded
    by SpawnerReady): every plugin page and the M key depend on it.
 2. The product capabilities are set from src/feature_caps.h (tested against Core's registry by
    tools/offline-host/feature_caps_test.cpp), never by a fixed `SetCap("teleport", false)` with a
    reason that blames the game build.
 3. The "Your own ships" page section is shown only while Core's game.asop.selection.file
    capability is on (the shipped host never composes that provider).
 4. Every SC_OFFLINE_* variable the host reads is in the table of docs/data-files.md.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# docs/data-files.md is outside the change that added this check. Each name here is read by the
# host but not yet in that table; the check fails as soon as one is documented, so the entry must
# then be removed (a ratchet: the list can only shrink).
PENDING_DOCS = {'SC_OFFLINE_RELOCATION', 'SC_OFFLINE_MULTIPLAYER_BIND', 'SC_OFFLINE_MULTIPLAYER_PEERS'}
# The table row for the allow list must describe what the host does with it.
ALLOW_ROW_WRONG = 'extra IPv4 ranges a session may use'
ALLOW_ROW_PENDING = True


def text(path):
    return (ROOT / path).read_text(encoding='utf-8', errors='replace')


def strip_comments(source):
    return re.sub(r'//[^\n]*|/\*.*?\*/', '', source, flags=re.S)


def main():
    problems = []
    dll = strip_comments(text('src/dllmain.cpp'))

    # 1. menu start
    if not re.search(r'\bMenu_Start\s*\(', dll):
        problems.append('src/dllmain.cpp: Menu_Start is never called')
    for match in re.finditer(r'(?:if|while)\s*\([^;{}]*\bSpawnerReady\s*\(\s*\)[^;{}]*\)\s*\{?\s*Menu_Start', dll):
        problems.append('src/dllmain.cpp: Menu_Start is gated on SpawnerReady(): ' + match.group(0).strip())

    if re.search(r'if\s*\(\s*!\s*SpawnerReady\s*\(\s*\)\s*\)\s*\{?\s*return', dll):
        problems.append('src/dllmain.cpp: an early return on !SpawnerReady() can skip Menu_Start')
    # teleport capability only reports the read rows; the F7/F8 built-in gates on it and its routes refuse
    for path in (ROOT / 'src').rglob('*'):
        if path.suffix in ('.cpp', '.h') and 'third_party' not in path.parts and path.name != 'teleport_plugin.cpp':
            if 'kTeleportBuiltin' in strip_comments(path.read_text(encoding='utf-8', errors='replace')):
                problems.append(f'{path.relative_to(ROOT)}: kTeleportBuiltin is registered while teleport moves still refuse')

    # 2. product capabilities
    if 'feature_caps::Apply' not in dll:
        problems.append('src/dllmain.cpp: SetFeatureCaps does not go through feature_caps::Apply')
    if re.search(r'\bSetCap\s*\(', dll) or re.search(r'caps::Set\s*\(\s*"teleport"', dll):
        problems.append('src/dllmain.cpp: a capability is set outside src/feature_caps.h')
    caps = text('src/feature_caps.h')
    if re.search(r'game\.[a-z]+', re.findall(r'kQuantumHeldReason\s*=\s*"([^"]*)"', caps)[0] if 'kQuantumHeldReason' in caps else ''):
        problems.append('src/feature_caps.h: a player-facing reason names a code identifier')
    if 'unavailable on this game build' in strip_comments(caps):
        problems.append('src/feature_caps.h: a reason blames the game build')

    # 3. ASOP section
    menu = strip_comments(text('src/menu.cpp'))
    if 'Has("game.asop.selection.file")' not in menu or 'SkipAsopPage' not in menu:
        problems.append('src/menu.cpp: the ASOP section is not tied to game.asop.selection.file')

    # 4. documented variables
    docs = text('docs/data-files.md')
    read = set()
    for path in list((ROOT / 'src').glob('*.cpp')) + list((ROOT / 'src/builtins').glob('*.cpp')):
        read.update(re.findall(r'GetEnvironmentVariable[AW]?\(\s*L?"(SC_OFFLINE_[A-Z0-9_]+)"', path.read_text(encoding='utf-8', errors='replace')))
    for name in sorted(read):
        documented = f'`{name}`' in docs
        if name in PENDING_DOCS:
            if documented:
                problems.append(f'docs/data-files.md now documents {name}: remove it from PENDING_DOCS')
        elif not documented:
            problems.append(f'docs/data-files.md: {name} is read by the host but not in the table')
    stale = PENDING_DOCS - read
    if stale:
        problems.append('PENDING_DOCS names variables the host no longer reads: ' + ', '.join(sorted(stale)))
    if ALLOW_ROW_WRONG in docs:
        if not ALLOW_ROW_PENDING:
            problems.append('docs/data-files.md: SC_OFFLINE_MULTIPLAYER_ALLOW row still says "' + ALLOW_ROW_WRONG + '"')
    elif ALLOW_ROW_PENDING:
        problems.append('docs/data-files.md: the allow row is corrected; set ALLOW_ROW_PENDING = False')

    for problem in problems:
        print('check_host_wiring: ' + problem)
    if problems:
        sys.exit(1)
    print(f'check_host_wiring: ok ({len(read)} host variables, {len(PENDING_DOCS)} doc gaps still pending)')


if __name__ == '__main__':
    main()
