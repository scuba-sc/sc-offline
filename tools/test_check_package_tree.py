#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Synthetic staged-package trees against tools/check_package_tree.py, derived from the real sources."""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / 'tools' / 'check_package_tree.py'
# The twelve plugins of the first diagnostic package.
PLUGINS = ['build', 'contracts', 'creative', 'crew', 'discord', 'inspector', 'loadout', 'mining',
           'npc', 'quantum', 'spawn', 'teleport']


def write(path, text=''):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8')


def make_tree(base, disabled=('creative', 'discord'), packs=None):
    for name in ('dinput8.dll', 'sc-offline.exe'):
        write(base / name)
    plugins = base / 'data' / 'plugins'
    for plugin in PLUGINS:
        write(plugins / plugin / 'plugin.ini',
              f'id = {plugin}\nname = {plugin}\nversion = 1.0.0\napi = 1.1\nkind = native\nentry = {plugin}.dll\n'
              + ('requires = game.creative\n' if plugin == 'creative' else ''))
        write(plugins / plugin / f'{plugin}.dll')
        if plugin in disabled:
            write(plugins / plugin / 'disabled')
    for pack, lists in (packs or {}).items():
        write(plugins / pack / 'plugin.ini', f'id = {pack}\nname = {pack}\nversion = 1.0.0\napi = 1.1\nkind = data\n')
        for name in lists:
            write(plugins / pack / 'lists' / f'{name}.txt', '[group]\nrow\n')
    return base


FIRST_PACKS = {'build_catalog': ['buildables'], 'picker_catalog': ['npcs', 'ships']}
FULL_PACKS = {**FIRST_PACKS, 'loadout_lists': ['items', 'outfits', 'loadout_slots'],
              'locations_pack': ['locations'], 'contract_pack': ['contract_scripts']}


def run(tree, *extra):
    result = subprocess.run([sys.executable, '-I', str(SCRIPT), '--tree', str(tree), '--source', str(ROOT), *extra],
                            capture_output=True, text=True, check=False)
    return result.returncode, result.stdout + result.stderr


def expect(condition, message, output=''):
    if not condition:
        sys.exit(f'FAIL: {message}\n{output}')


def main():
    with tempfile.TemporaryDirectory(prefix='sc-offline-pkg-') as folder:
        work = Path(folder)

        # 1. Shaped like the first diagnostic package: creative and discord disabled, no loadout, slots or locations lists.
        code, out = run(make_tree(work / 'first', packs=FIRST_PACKS))
        expect(code == 1, 'first-package tree must fail', out)
        for command in ('creative.noclip', 'creative.god', 'creative.ammo', 'creative.noclip_speed'):
            expect(f'command {command} is invoked by loadout' in out, f'{command} not named as invoked by loadout', out)
        for name in ('items', 'outfits', 'loadout_slots', 'locations'):
            expect(f'required lists/{name}.txt is in no enabled data pack' in out, f'missing list {name} not named', out)
        expect('PASS' not in out.splitlines()[-1], 'verdict line must say FAIL', out)

        # 2. creative enabled and the packs provide the required lists: passes.
        code, out = run(make_tree(work / 'full', disabled=('discord',), packs=FULL_PACKS))
        expect(code == 0, 'complete tree must pass', out)
        expect('creative.noclip: loadout -> creative' in out and 'lists/items.txt (required) -> loadout_lists' in out,
               'summary must list providers and packs', out)

        # 3. Two packs providing one list: ambiguous.
        packs = {**FULL_PACKS, 'second_items': ['items']}
        code, out = run(make_tree(work / 'ambiguous', disabled=('discord',), packs=packs))
        expect(code == 1 and 'lists/items.txt is ambiguous' in out and 'loadout_lists' in out and 'second_items' in out,
               'ambiguous list must fail and name both packs', out)

        # 4. A disabled provider is no provider (build is invoked by loadout and the host UI).
        code, out = run(make_tree(work / 'nobuild', disabled=('discord', 'build'), packs=FULL_PACKS))
        expect(code == 1 and 'command build.place is invoked by' in out and 'registered only by disabled plugin build' in out,
               'a disabled build plugin must fail its callers', out)

        # 5. A disabled data pack does not provide its lists; an optional list is only a warning.
        tree = make_tree(work / 'disabledpack', disabled=('discord',), packs=FULL_PACKS)
        write(tree / 'data' / 'plugins' / 'loadout_lists' / 'disabled')
        code, out = run(tree)
        expect(code == 1 and 'required lists/items.txt is in no enabled data pack' in out, 'disabled pack must not count', out)
        tree = make_tree(work / 'optional', disabled=('discord',), packs={k: v for k, v in FULL_PACKS.items() if k != 'contract_pack'})
        code, out = run(tree)
        expect(code == 0 and 'WARN: contracts: optional lists/contract_scripts.txt' in out, 'optional list absent is a warning', out)

        # 6. --allow-command downgrades a missing provider to a warning.
        code, out = run(make_tree(work / 'allowed', packs=FULL_PACKS),
                        *[a for c in ('creative.noclip', 'creative.god', 'creative.ammo', 'creative.noclip_speed', 'creative.ship_ammo')
                          for a in ('--allow-command', c)])
        expect(code == 0 and 'WARN: command creative.god' in out, '--allow-command must downgrade to a warning', out)

        # 7. Missing package files are reported.
        tree = make_tree(work / 'missing', disabled=('discord',), packs=FULL_PACKS)
        (tree / 'dinput8.dll').unlink()
        (tree / 'data' / 'plugins' / 'spawn' / 'spawn.dll').unlink()
        code, out = run(tree)
        expect(code == 1 and 'dinput8.dll is missing' in out and 'spawn: entry file spawn.dll is missing' in out,
               'missing package files must fail', out)

        # 8. Usage error.
        code, _ = run(work / 'does-not-exist')
        expect(code == 2, 'a missing tree is a usage error')
    print('package tree preflight tests: PASS (static, synthetic trees; not run in game)')


if __name__ == '__main__':
    main()
