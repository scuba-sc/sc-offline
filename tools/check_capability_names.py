#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Every game.* capability name sc-offline asks for must exist in the pinned Core.

A misspelt or invented name (the build plugin once asked for game.prefabs.spawn
instead of Core's game.entities.prefabs) is never granted, so the feature is
refused in game while synthetic tests that answer every name still pass. This
check reads the string literals passed to Cap(), Has(), has() and similar
capability lookups under plugins/ and src/, and compares each game.* name with
the capability names in Core's sdk/status_registry.json and the names Core sets
in its source.

Core is read from --core (default: $SCO_HOST_SOURCE_DIR, then external/sco-core).
"""
import argparse
import json
import os
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
LOOKUP = re.compile(r'\b(?:Cap|Has|has|CapReady|HasCap|Capability)\s*\(\s*(?:[^,()]*,\s*)*"(game\.[a-z0-9_.]+)"')
CORE_SET = re.compile(r'"(game\.[a-z0-9_]+(?:\.[a-z0-9_]+)+)"')


def core_names(core):
    names = set()
    registry = core / 'sdk' / 'status_registry.json'
    if registry.is_file():
        text = registry.read_text(encoding='utf-8')
        names.update(re.findall(r'"name":\s*"(game\.[a-z0-9_.]+)"', text))
    for folder in ('src', 'include'):
        for path in (core / folder).rglob('*'):
            if path.suffix in ('.c', '.cc', '.cpp', '.h', '.hpp'):
                names.update(CORE_SET.findall(path.read_text(encoding='utf-8', errors='replace')))
    return names


def used_names():
    found = {}
    for folder in ('plugins', 'src'):
        for path in (ROOT / folder).rglob('*'):
            if path.suffix not in ('.c', '.cc', '.cpp', '.h', '.hpp') or 'third_party' in path.parts:
                continue
            for number, line in enumerate(path.read_text(encoding='utf-8', errors='replace').splitlines(), 1):
                for name in LOOKUP.findall(line):
                    found.setdefault(name, []).append(f'{path.relative_to(ROOT).as_posix()}:{number}')
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, help='Core source folder (has sdk/status_registry.json)')
    args = parser.parse_args()
    core = args.core or Path(os.environ.get('SCO_HOST_SOURCE_DIR') or ROOT / 'external' / 'sco-core')
    if not (core / 'sdk' / 'status_registry.json').is_file():
        sys.exit(f'check_capability_names: no Core source at {core} (pass --core or set SCO_HOST_SOURCE_DIR)')
    known = core_names(core)
    used = used_names()
    unknown = {name: where for name, where in used.items() if name not in known}
    for name, where in sorted(unknown.items()):
        print(f'unknown capability {name}: {", ".join(where)}')
    if unknown:
        sys.exit(f'check_capability_names: {len(unknown)} game.* capability name(s) not in Core at {core}')
    print(f'check_capability_names: {len(used)} game.* capability names, all known to Core at {core}')


if __name__ == '__main__':
    main()
