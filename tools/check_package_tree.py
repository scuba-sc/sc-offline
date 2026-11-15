#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package preflight: inspect a STAGED package folder before it is shipped.

    python tools/check_package_tree.py --tree <staged package root> [--source <repo root>]
                                       [--allow-command NAME ...]

The tree is the layout the release job assembles: dinput8.dll, sc-offline.exe and
data/plugins/<folder>/plugin.ini, where a plugin is DISABLED by a file named `disabled` in its
folder, and a data pack (kind = data) carries lists/*.txt. The first in-game run shipped menu
controls whose commands no enabled plugin registered, and plugins whose required list files were
in no pack; this check turns both into a failure before release. Exit 0 = pass, 1 = problems
(one `ERROR:` line each, warnings as `WARN:`), 2 = usage. Python 3 stdlib only, read-only.

What it checks, all derived from the plugin SOURCES in --source (default: this repo):

a. Command providers. For every ENABLED native plugin in the tree (and the host's own UI in src/),
   each command it invokes by name must be registered by an ENABLED native plugin in the tree.
   --allow-command NAME downgrades one name to a warning (a control that is meant to be inert).
b. Required lists. For every ENABLED native plugin, each lists/<name>.txt it reads through the
   content service must resolve to exactly one ENABLED data pack. A required list with no pack,
   or any used list with several packs (ambiguous), is an error; an optional list with no pack is
   a warning. LIST_POLICY below classifies each (plugin, list).
c. Capabilities. A `requires =` name in an enabled plugin's plugin.ini must be a capability set by
   the host (SetCap("...") in src/), a game/sco service name published by the framework headers
   (external/sco-core/include/*.h, `#define ...NAME "x"`), or a plugin in the tree for the part
   before the first dot. Skipped with a warning if the framework headers are absent.
d. A summary is printed: enabled and disabled plugins, data packs, every cross-plugin command
   with its provider, every list with its pack, warnings.

Also reported: a missing dinput8.dll / sc-offline.exe, an enabled native plugin without its entry
file, an unreadable plugin.ini.

How commands are found (patterns; comments are stripped first, string contents kept)
  Registered by plugin P: any whole string literal shaped `<word>.<word>[...]` whose first
    component is P's id, in P's source files, and which is not an invoke argument (below). P's
    sources are plugins/<id>/*.cpp|*.h|*.inc (test files excluded), plus plugins/spatial/*.h for
    quantum and teleport (shared travel code, `#ifdef TRAVEL_TELEPORT` selects the names) and
    plugins/picker.h. The registration call itself (register_command / RegisterBuiltinCommand /
    a Toggle(...) or CommandBuilder(...) helper) is not parsed: a literal with P's prefix counts.
  Invoked: the command-shaped literals in the first two arguments of `->invoke(` / `.invoke(`
    (self comes first), the first argument of the helpers `Invoke(` and `Forward(`, and the
    last argument of `BindBuiltinHotkey(` (a hotkey bound to a command). A conditional such as
    `Forward(a ? "x.y" : "x.z", ...)` yields both names.
Blind spots
  * A command name built at run time (concatenation, a variable, a table read from a file) is
    invisible; so is an invoke through another helper name.
  * A literal with the plugin's prefix that is not really a registration (a status string that is
    exactly a command name, a panel id) counts as "registered": a false negative, never a false alarm.
  * Commands registered with a prefix other than the plugin id are not seen as provided.
  * Host built-in commands (src/builtins) are NOT counted as providers: at this revision the host
    registers no feature built-ins (docs/decisions.md), so counting them would hide a missing plugin.
  * Gating by capability, argument shapes and runtime availability are not checked: "registered"
    means the plugin has the command, not that it loads, admits or works in game.
  * Lists: only literal `lists/<name>.txt` text is seen; LIST_POLICY is maintained by hand, and a
    list the sources mention that the table does not classify is an error so it cannot go unnoticed.
  * Capabilities: whether a game service is actually PUBLISHED depends on admission at run time;
    this only catches names nothing in the repo could ever provide (typos, removed services).
TODO: nothing deferred; check c is deliberately name-existence only for the reason above.
"""
import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# (plugin id, list name) -> (policy, where the source decides it). `required`: its absence makes a
# core function answer a hard "unavailable"/"not found" with no fallback. `optional`: a picker or
# an explicitly tolerated absence (a warning). `unused`: the name is mentioned only on a path this
# plugin does not compile.
LIST_POLICY = {
    # build/picker.h:84 - only the build picker's catalogue; build.place takes any class name.
    ('build', 'buildables'): ('optional', 'plugins/build/picker.h:84'),
    # contracts.cpp:52 - only the reference-metadata command needs the exact list record.
    ('contracts', 'contract_scripts'): ('optional', 'plugins/contracts/contracts.cpp:52'),
    # crew/picker.inc:67 and npc/picker.inc:28 - the NPC picker; commands take class names.
    ('crew', 'npcs'): ('optional', 'plugins/crew/picker.inc:67'),
    ('npc', 'npcs'): ('optional', 'plugins/npc/picker.inc:28'),
    # spawn/picker.inc:37 and spawn.cpp:82 - the ship picker and spawn.pack_list; spawn.ship takes a class.
    ('spawn', 'ships'): ('optional', 'plugins/spawn/picker.inc:37'),
    # loadout.cpp:492/576 - loadout.items and equip: "No enabled data pack supplies lists/items.txt".
    ('loadout', 'items'): ('required', 'plugins/loadout/loadout.cpp:492'),
    # loadout.cpp:521 - loadout.wear: "No enabled data pack supplies lists/outfits.txt".
    ('loadout', 'outfits'): ('required', 'plugins/loadout/loadout.cpp:521'),
    # loadout.cpp:224 - the SQ42 build picker; an absent section is reported as "absent", not an error.
    ('loadout', 'buildables'): ('optional', 'plugins/loadout/loadout.cpp:224'),
    # slot_catalogue.h:111 / loadout.cpp:465 - the curated weapon and attachment slots: "unavailable".
    ('loadout', 'loadout_slots'): ('required', 'plugins/loadout/slot_catalogue.h:111'),
    # spatial/travel_plugin.h:218 - quantum.travel: "No enabled lists/locations.txt; no base-file fallback".
    ('quantum', 'locations'): ('required', 'plugins/spatial/travel_plugin.h:218'),
    # spatial/travel_plugin.h:196 - LoadCuratedPlaces is inside #ifndef TRAVEL_TELEPORT.
    ('teleport', 'locations'): ('unused', 'plugins/spatial/travel_plugin.h:196'),
}

SHARED_SOURCES = {'quantum': ['spatial/*.h'], 'teleport': ['spatial/*.h']}
COMMON_SOURCES = ['picker.h']
FILE_SUFFIXES = {'h', 'hpp', 'cpp', 'inc', 'ini', 'txt', 'dll', 'exe', 'db', 'xml', 'toml', 'json', 'md',
                 'lua', 'png', 'jpg', 'log', 'cfg', 'py', 'sh', 'zip', 'pdb'}
COMMAND_RE = re.compile(r'^[a-z][a-z0-9_]*(?:\.[a-z0-9_]+)+$')
LIST_RE = re.compile(r'lists/([a-z0-9_]+)\.txt')
CALL_RE = re.compile(r'(?<![A-Za-z0-9_])(Invoke|Forward|invoke|BindBuiltinHotkey)\s*\(')


class Problems:
    def __init__(self):
        self.errors, self.warnings = [], []

    def error(self, text):
        self.errors.append(text)

    def warn(self, text):
        self.warnings.append(text)


def strip_comments(text):
    """Remove // and /* */ comments; string and char literals are kept intact."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def string_literals(text):
    """(start, end, value) of each plain string literal in comment-stripped text."""
    found = []
    for m in re.finditer(r'"((?:[^"\\\n]|\\.)*)"', text):
        found.append((m.start(), m.end(), m.group(1)))
    return found


def command_shaped(value):
    return bool(COMMAND_RE.match(value)) and value.rsplit('.', 1)[-1] not in FILE_SUFFIXES


def call_arguments(text, open_paren):
    """Top-level arguments (start, end) of the call whose '(' is at open_paren."""
    args, depth, start, i, n = [], 0, open_paren + 1, open_paren, len(text)
    while i < n:
        c = text[i]
        if c in '"\'':
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            i = j
        elif c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
            if depth == 0:
                args.append((start, i))
                return args
        elif c == ',' and depth == 1:
            args.append((start, i))
            start = i + 1
        i += 1
    return args


def read_sources(paths):
    sources = {}
    for path in paths:
        try:
            sources[path] = strip_comments(path.read_text(encoding='utf-8', errors='replace'))
        except OSError:
            pass
    return sources


def plugin_source_files(source, plugin_id):
    base = source / 'plugins' / plugin_id
    files = []
    if base.is_dir():
        for pattern in ('*.cpp', '*.h', '*.inc'):
            files += [p for p in sorted(base.glob(pattern)) if 'test' not in p.name]
    for pattern in SHARED_SOURCES.get(plugin_id, []):
        files += [p for p in sorted((source / 'plugins').glob(pattern)) if 'test' not in p.name]
    if base.is_dir():
        files += [source / 'plugins' / name for name in COMMON_SOURCES if (source / 'plugins' / name).is_file()]
    return files


def host_source_files(source):
    src = source / 'src'
    files = sorted(src.glob('*.cpp')) + sorted(src.glob('*.h')) + sorted((src / 'builtins').glob('*.cpp'))
    return [p for p in files if 'third_party' not in p.parts]


def analyse_commands(sources):
    """-> (registered literals, invoked names) for one source set."""
    registered, invoked = set(), set()
    for text in sources.values():
        spans = []
        for m in CALL_RE.finditer(text):
            args = call_arguments(text, m.end() - 1)
            before = text[max(0, m.start() - 2):m.start()]
            member = before.endswith('->') or before.endswith('.')
            if m.group(1) == 'BindBuiltinHotkey':
                chosen = args[-1:]
            elif member:
                chosen = args[:2]
            else:
                chosen = args[:1]
            for a, b in chosen:
                for s, e, value in string_literals(text[a:b]):
                    if command_shaped(value):
                        invoked.add(value)
                        spans.append((a + s, a + e))
        for s, e, value in string_literals(text):
            if command_shaped(value) and not any(a <= s and e <= b for a, b in spans):
                registered.add(value)
    return registered, invoked


def analyse_lists(sources):
    used = set()
    for text in sources.values():
        used.update(LIST_RE.findall(text))
    return used


def parse_manifest(path):
    values = {}
    for raw in path.read_text(encoding='utf-8-sig', errors='replace').splitlines():
        line = raw.strip()
        if line.startswith('[') or not line:
            if line.startswith('['):
                break
            continue
        if line[0] in ';#' or '=' not in line:
            continue
        key, value = line.split('=', 1)
        values.setdefault(key.strip(), value.strip())
    return values


def load_tree(tree, problems):
    plugins, packs = [], []
    root = tree / 'data' / 'plugins'
    for name in ('dinput8.dll', 'sc-offline.exe'):
        if not (tree / name).is_file():
            problems.error(f'package: {name} is missing from {tree.name}')
    if not root.is_dir():
        problems.error('package: data/plugins is missing')
        return plugins, packs
    for folder in sorted(p for p in root.iterdir() if p.is_dir()):
        manifest = folder / 'plugin.ini'
        if not manifest.is_file():
            problems.warn(f'{folder.name}: no plugin.ini; ignored')
            continue
        try:
            values = parse_manifest(manifest)
        except OSError as error:
            problems.error(f'{folder.name}: plugin.ini unreadable ({error})')
            continue
        item = {'folder': folder.name, 'id': values.get('id', folder.name), 'kind': values.get('kind', ''),
                'entry': values.get('entry', ''), 'requires': [r.strip() for r in values.get('requires', '').split(',') if r.strip()],
                'enabled': not (folder / 'disabled').exists(), 'path': folder}
        (packs if item['kind'] == 'data' else plugins).append(item)
    return plugins, packs


def framework_names(source):
    include = source / 'external' / 'sco-core' / 'include'
    names = set()
    if not include.is_dir():
        return None
    for header in include.rglob('*.h'):
        try:
            text = header.read_text(encoding='utf-8', errors='replace')
        except OSError:
            continue
        names.update(re.findall(r'#define\s+\w*NAME\s+"([a-z0-9_.]+)"', text))
    return names


def host_capabilities(source):
    caps = set()
    for path in host_source_files(source):
        try:
            caps.update(re.findall(r'SetCap\(\s*"([^"]+)"', path.read_text(encoding='utf-8', errors='replace')))
        except OSError:
            pass
    return caps


def check(tree, source, allow=()):
    problems = Problems()
    plugins, packs = load_tree(tree, problems)
    native = [p for p in plugins if p['kind'] == 'native']
    enabled = [p for p in native if p['enabled']]
    enabled_ids = {p['id'] for p in enabled}
    report = {'enabled': [p['id'] for p in enabled], 'disabled': [p['id'] for p in plugins if not p['enabled']],
              'packs': [(p['id'], p['enabled']) for p in packs], 'commands': [], 'lists': [], 'notes': []}
    for p in enabled:
        if p['entry'] and not (p['path'] / p['entry']).is_file():
            problems.error(f"{p['id']}: entry file {p['entry']} is missing from its folder")

    per_plugin = {}
    for p in native:
        files = plugin_source_files(source, p['id'])
        if not files:
            report['notes'].append(f"{p['id']}: no sources in {source.name}/plugins/{p['id']}; not analysed")
            continue
        sources = read_sources(files)
        registered, invoked = analyse_commands(sources)
        per_plugin[p['id']] = {'registered': {c for c in registered if c.split('.')[0] == p['id']},
                               'invoked': invoked, 'lists': analyse_lists(sources)}
    host_registered, host_invoked = analyse_commands(read_sources(host_source_files(source)))

    # a. commands
    provider = {}
    for p in enabled:
        for command in per_plugin.get(p['id'], {}).get('registered', ()):
            provider.setdefault(command, []).append(p['id'])
    invokers = {}
    for p in enabled:
        for command in per_plugin.get(p['id'], {}).get('invoked', ()):
            invokers.setdefault(command, []).append(p['id'])
    for command in host_invoked:
        invokers.setdefault(command, []).append('host')
    for command in sorted(invokers):
        who = sorted(set(invokers[command]))
        if all(w in provider.get(command, []) for w in who):
            continue   # each invoker registers it itself
        provided = provider.get(command)
        if provided:
            report['commands'].append((command, who, provided))
            continue
        known = sorted(p['id'] for p in native if command in per_plugin.get(p['id'], {}).get('registered', ()))
        reason = (f"registered only by disabled plugin {', '.join(known)}" if known
                  else 'no plugin in the tree registers it')
        message = f"command {command} is invoked by {', '.join(who)} but {reason}"
        (problems.warn if command in allow else problems.error)(message + (' (allowed)' if command in allow else ''))

    # b. lists
    pack_lists = {}
    for pack in packs:
        if pack['enabled']:
            for f in sorted((pack['path'] / 'lists').glob('*.txt')) if (pack['path'] / 'lists').is_dir() else []:
                pack_lists.setdefault(f.stem, []).append(pack['id'])
    seen = set()
    for p in enabled:
        for name in sorted(per_plugin.get(p['id'], {}).get('lists', ())):
            policy = LIST_POLICY.get((p['id'], name))
            if policy is None:
                problems.error(f"{p['id']}: lists/{name}.txt appears in its sources but is not classified in LIST_POLICY")
                continue
            if policy[0] == 'unused':
                continue
            seen.add((p['id'], name))
            packs_with = pack_lists.get(name, [])
            if len(packs_with) > 1:
                problems.error(f"{p['id']}: lists/{name}.txt is ambiguous: enabled packs {', '.join(packs_with)}")
            elif not packs_with:
                if policy[0] == 'required':
                    problems.error(f"{p['id']}: required lists/{name}.txt is in no enabled data pack ({policy[1]})")
                else:
                    problems.warn(f"{p['id']}: optional lists/{name}.txt is in no enabled data pack ({policy[1]})")
            else:
                report['lists'].append((p['id'], name, policy[0], packs_with[0]))
    for (plugin_id, name), policy in LIST_POLICY.items():
        if plugin_id in per_plugin and policy[0] != 'unused' and name not in per_plugin[plugin_id]['lists']:
            problems.warn(f'LIST_POLICY: {plugin_id} no longer mentions lists/{name}.txt; update {policy[1]}')

    # c. capabilities
    names = framework_names(source)
    if names is None:
        problems.warn('capabilities: external/sco-core/include is absent; `requires` names not checked')
    else:
        known = names | host_capabilities(source)
        for p in enabled:
            for name in p['requires']:
                head = name.split('.')[0]
                if name in known or ('.' in name and head in enabled_ids):
                    continue
                problems.error(f"{p['id']}: requires {name}, which no host capability, framework service or enabled plugin provides")
    return problems, report


def print_report(tree, report, problems):
    print(f'package: {tree.name}')
    print(f"enabled plugins ({len(report['enabled'])}): {', '.join(report['enabled']) or '-'}")
    print(f"disabled plugins ({len(report['disabled'])}): {', '.join(report['disabled']) or '-'}")
    print(f"data packs ({len(report['packs'])}): "
          + (', '.join(name + ('' if on else ' [disabled]') for name, on in report['packs']) or '-'))
    print('cross-plugin commands:')
    for command, who, provided in sorted(report['commands']):
        print(f"  {command}: {', '.join(who)} -> {', '.join(provided)}")
    if not report['commands']:
        print('  (none resolved)')
    print('lists:')
    for plugin_id, name, policy, pack in sorted(report['lists']):
        print(f'  {plugin_id}: lists/{name}.txt ({policy}) -> {pack}')
    if not report['lists']:
        print('  (none resolved)')
    for note in report['notes']:
        print(f'note: {note}')
    for line in problems.warnings:
        print(f'WARN: {line}')
    for line in problems.errors:
        print(f'ERROR: {line}')
    print(f"package preflight: {'FAIL' if problems.errors else 'PASS'} "
          f'({len(problems.errors)} error(s), {len(problems.warnings)} warning(s); static, not run in game)')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--tree', type=Path, required=True, help='staged package root')
    parser.add_argument('--source', type=Path, default=REPO, help='repo root the plugin sources are read from')
    parser.add_argument('--allow-command', action='append', default=[], metavar='NAME',
                        help='report a missing provider of NAME as a warning')
    args = parser.parse_args(argv)
    if not args.tree.is_dir():
        print(f'package tree not found: {args.tree.name}', file=sys.stderr)
        return 2
    problems, report = check(args.tree.resolve(), args.source.resolve(), set(args.allow_command))
    print_report(args.tree, report, problems)
    return 1 if problems.errors else 0


if __name__ == '__main__':
    sys.exit(main())
