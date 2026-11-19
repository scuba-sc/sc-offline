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


# What the Rust menu hook leaves in a DLL: it loads sc_offline_menu.dll by name (log text, ASCII).
RUST_MENU_HOST = 'MZ host with the menu client; rust menu: sc_offline_menu.dll not found beside the launcher or the host'


def write(path, text=''):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8')


def make_tree(base, disabled=('creative', 'discord'), packs=None, plugins_in_tree=PLUGINS):
    for name in ('dinput8.dll', 'dinput8-rust-menu.dll', 'sc-offline.exe', 'sc-offline-classic.exe',
                 'sc_offline_menu.dll'):
        write(base / name, RUST_MENU_HOST if name == 'dinput8-rust-menu.dll' else '')
    plugins = base / 'data' / 'plugins'
    for plugin in plugins_in_tree:
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


def run(tree, *extra, source=ROOT):
    result = subprocess.run([sys.executable, '-I', str(SCRIPT), '--tree', str(tree), '--source', str(source), *extra],
                            capture_output=True, text=True, check=False)
    return result.returncode, result.stdout + result.stderr


def expect(condition, message, output=''):
    if not condition:
        sys.exit(f'FAIL: {message}\n{output}')


# Creative lives in its own repository (the external/sc-creative submodule). No product plugin invokes
# `creative.*` any more, so the "invoked but missing" cases use a synthetic source root: a probe plugin that
# invokes creative's commands, and creative's real sources from the submodule checkout.
PROBE_SOURCE = """// synthetic consumer of creative's commands
void Use(sco_api* api, sco_plugin* self) {
    sco_arg a{};
    api->invoke(self, "creative.noclip", &a, 1, nullptr, nullptr);
    Forward("creative.noclip_speed", &a, 1, nullptr, 0);
}
"""


def synthetic_source(base, with_creative=True):
    write(base / 'plugins' / 'probe' / 'probe.cpp', PROBE_SOURCE)
    if with_creative:
        creative = ROOT / 'external' / 'sc-creative' / 'plugins' / 'creative'
        expect(creative.is_dir(), 'creative submodule missing: git submodule update --init external/sc-creative')
        for pattern in ('*.cpp', '*.h'):
            for path in sorted(creative.glob(pattern)):
                write(base / 'external' / 'sc-creative' / 'plugins' / 'creative' / path.name,
                      path.read_text(encoding='utf-8'))
    return base


def main():
    with tempfile.TemporaryDirectory(prefix='sc-offline-pkg-') as folder:
        work = Path(folder)

        # 1. Shaped like the first diagnostic package: no loadout, slots or locations lists. Loadout and spawn
        # no longer invoke creative, so a disabled creative is not reported against them.
        code, out = run(make_tree(work / 'first', packs=FIRST_PACKS))
        expect(code == 1, 'first-package tree must fail', out)
        expect('command creative.' not in out, 'no product plugin may invoke creative commands', out)
        for name in ('items', 'outfits', 'loadout_slots', 'locations'):
            expect(f'required lists/{name}.txt is in no enabled data pack' in out, f'missing list {name} not named', out)
        expect('PASS' not in out.splitlines()[-1], 'verdict line must say FAIL', out)

        # 2. creative enabled and the packs provide the required lists: passes.
        code, out = run(make_tree(work / 'full', disabled=('discord',), packs=FULL_PACKS))
        expect(code == 0, 'complete tree must pass', out)
        expect('lists/items.txt (required) -> loadout_lists' in out, 'summary must list providers and packs', out)
        expect('creative.' not in out.split('lists:')[0].split('cross-plugin commands:')[1],
               'creative must not appear among resolved cross-plugin commands', out)

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

        # 6. --allow-command downgrades a missing provider to a warning (a probe invokes a disabled creative).
        source = synthetic_source(work / 'source-probe')
        tree = make_tree(work / 'allowed', disabled=('creative', 'discord'), packs=FULL_PACKS,
                         plugins_in_tree=['creative', 'probe'])
        code, out = run(tree, '--allow-command', 'creative.noclip', '--allow-command', 'creative.noclip_speed', source=source)
        expect(code == 0 and 'WARN: command creative.noclip is invoked by probe' in out,
               '--allow-command must downgrade to a warning', out)

        # 7. Missing package files are reported.
        tree = make_tree(work / 'missing', disabled=('discord',), packs=FULL_PACKS)
        (tree / 'dinput8.dll').unlink()
        (tree / 'dinput8-rust-menu.dll').unlink()
        (tree / 'sc-offline-classic.exe').unlink()
        (tree / 'sc_offline_menu.dll').unlink()
        (tree / 'data' / 'plugins' / 'spawn' / 'spawn.dll').unlink()
        code, out = run(tree)
        expect(code == 1 and 'dinput8.dll is missing' in out and 'spawn: entry file spawn.dll is missing' in out
               and 'dinput8-rust-menu.dll is missing' in out
               and 'sc-offline-classic.exe is missing' in out and 'sc_offline_menu.dll is missing' in out,
               'missing package files must fail', out)

        # 7b. The two game DLLs must be two different builds: the default one has no Rust menu hook.
        tree = make_tree(work / 'same-dlls', disabled=('discord',), packs=FULL_PACKS)
        (tree / 'dinput8.dll').write_text(RUST_MENU_HOST, encoding='utf-8')
        code, out = run(tree)
        expect(code == 1 and 'are the same bytes' in out and 'dinput8.dll names sc_offline_menu.dll' in out,
               'a default DLL built with the Rust menu hook must fail', out)
        tree = make_tree(work / 'no-hook', disabled=('discord',), packs=FULL_PACKS)
        (tree / 'dinput8-rust-menu.dll').write_text('MZ host without the hook', encoding='utf-8')
        code, out = run(tree)
        expect(code == 1 and 'dinput8-rust-menu.dll does not name sc_offline_menu.dll' in out,
               'a Rust menu DLL built without the hook must fail', out)
        tree = make_tree(work / 'wide-name', disabled=('discord',), packs=FULL_PACKS)
        (tree / 'dinput8-rust-menu.dll').write_bytes(b'MZ ' + 'sc_offline_menu.dll'.encode('utf-16-le'))
        code, out = run(tree)
        expect(code == 0, 'the UTF-16 spelling of the name counts', out)

        # 8. Usage error.
        code, _ = run(work / 'does-not-exist')
        expect(code == 2, 'a missing tree is a usage error')

        # 9. Creative's sources come from the submodule: its registered commands resolve for a plugin that
        # invokes them, and the check still fails when creative is absent, disabled, or its sources are missing.
        source = work / 'source-probe'
        both = ['creative', 'probe']
        code, out = run(make_tree(work / 'creative-on', disabled=(), packs=FULL_PACKS, plugins_in_tree=both), source=source)
        expect(code == 0 and 'creative.noclip: probe -> creative' in out and 'creative.noclip_speed: probe -> creative' in out,
               'creative commands must resolve from external/sc-creative sources', out)
        code, out = run(make_tree(work / 'creative-absent', disabled=(), packs=FULL_PACKS, plugins_in_tree=['probe']), source=source)
        expect(code == 1 and 'command creative.noclip is invoked by probe but no plugin in the tree registers it' in out,
               'an absent creative must fail the plugin that invokes its commands', out)
        code, out = run(make_tree(work / 'creative-off', disabled=('creative',), packs=FULL_PACKS, plugins_in_tree=both), source=source)
        expect(code == 1 and 'registered only by disabled plugin creative' in out,
               'a disabled creative must fail the plugin that invokes its commands', out)
        bare = synthetic_source(work / 'source-no-submodule', with_creative=False)
        code, out = run(make_tree(work / 'creative-nosrc', disabled=(), packs=FULL_PACKS, plugins_in_tree=both), source=bare)
        expect(code == 1 and 'creative: no sources in source-no-submodule/external/sc-creative/plugins/creative' in out
               and 'no plugin in the tree registers it' in out,
               'without the submodule checkout creative commands are not provided', out)
    print('package tree preflight tests: PASS (static, synthetic trees; not run in game)')


if __name__ == '__main__':
    main()
