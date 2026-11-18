#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Every capability and service name sc-offline asks for must exist in the pinned Core.

A misspelt or invented name (the build plugin once asked for game.prefabs.spawn
instead of Core's game.entities.prefabs) is never granted, so the feature is
refused in game while synthetic tests that answer every name still pass.

Names used: string literals passed to any call whose callee is Cap, Has, has,
CapReady, HasCap, or whose name contains "cap" or "capability" in any case (for
example a local lambda called capability(...) or WorldCapability(...)), plus
literal service names given to query_service().

Names known: Core's sdk/status_registry.json is the only source of service names
and versions. For capabilities it is the source too, plus the first literal
argument of caps::Set / caps::SetFromSignatures and the kCap* name constants in
Core's own source (a few capabilities, such as vfs.pak, are declared only
there). A quoted game.* string elsewhere in Core (a service name such as
game.world.control, an event name) is NOT a capability.

Reported but not failing: used capabilities that Core leaves OFF by default, so a
reviewer sees which features depend on an opt-in switch.

Core is read from --core (default: $SCO_HOST_SOURCE_DIR, then external/sco-core).
Run with --self-test to check the checker itself against a synthetic Core.
"""
import argparse
import json
import os
from pathlib import Path
import re
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
SOURCE_SUFFIXES = ('.c', '.cc', '.cpp', '.h', '.hpp')
# callee name, optional leading non-literal arguments, then the first string literal
CALLEE = r'(?:Cap|Has|has|CapReady|HasCap|[A-Za-z_]*[Cc]ap(?:ability)?[A-Za-z_]*)'
LOOKUP = re.compile(r'\b' + CALLEE + r'\s*\(\s*(?:[^,()"]*,\s*)*"([a-z][a-z0-9_]*(?:\.[a-z0-9_]+)+)"')
QUERY = re.compile(r'\bquery_service\s*\(\s*"([a-z][a-z0-9_.]*)"\s*,\s*([^,()]+)')
CORE_SET = re.compile(r'caps::Set(?:FromSignatures)?\s*\(\s*"([a-z][a-z0-9_.]*)"')
CORE_CONST = re.compile(r'\bkCap\w*\s*=\s*"([a-z][a-z0-9_.]*)"')


def load_registry(core):
    registry = json.loads((core / 'sdk' / 'status_registry.json').read_text(encoding='utf-8'))
    caps, services = {}, {}
    for service in registry.get('services', []):
        if service.get('service'):
            services[service['service']] = service.get('version')
        for cap in service.get('capabilities', []):
            if cap.get('name'):
                caps[cap['name']] = (cap.get('default') or '').upper()
    return caps, services


def core_names(core):
    names, services = load_registry(core)
    names = dict(names)
    for folder in ('src', 'include'):
        for path in (core / folder).rglob('*'):
            if path.suffix in SOURCE_SUFFIXES:
                text = path.read_text(encoding='utf-8', errors='replace')
                for name in CORE_SET.findall(text) + CORE_CONST.findall(text):
                    names.setdefault(name, '')  # declared in source; default not recorded
    return names, services


def version_value(text):
    text = text.strip().rstrip('uUlL')
    try:
        return int(text, 0)
    except ValueError:
        return None


def scan(root):
    """Return ({capability: [sites]}, [(service, version_text, site)])."""
    caps, queries = {}, []
    for folder in ('plugins', 'src'):
        for path in (root / folder).rglob('*'):
            if path.suffix not in SOURCE_SUFFIXES or 'third_party' in path.parts:
                continue
            rel = path.relative_to(root).as_posix()
            for number, line in enumerate(path.read_text(encoding='utf-8', errors='replace').splitlines(), 1):
                site = f'{rel}:{number}'
                for name in LOOKUP.findall(line):
                    caps.setdefault(name, []).append(site)
                for name, version in QUERY.findall(line):
                    queries.append((name, version, site))
    return caps, queries


def check(root, core):
    """Return (errors, notes, number of game.* capability names used)."""
    known, services = core_names(core)
    caps, queries = scan(root)
    errors, notes = [], []
    # Only game.* names are checked as capabilities: other dotted strings reach the same
    # call names (settings keys, offline patch names) and are not Core capabilities.
    used = {name: where for name, where in caps.items() if name.startswith('game.')}
    for name, where in sorted(used.items()):
        if name not in known:
            errors.append(f'unknown capability {name}: {", ".join(where)}')
        elif known[name] == 'OFF':
            notes.append(f'capability {name} is OFF by default in Core: {", ".join(where)}')
    for name, version, site in queries:
        if name not in services:
            errors.append(f'unknown service {name}: {site}')
            continue
        value = version_value(version)
        if value is not None:
            major, minor = (services[name] or '0.0').split('.')[:2]
            # A request is served by the same major and an equal or newer minor.
            if (value >> 16) != int(major) or (value & 0xFFFF) > int(minor):
                errors.append(f'service {name} version {version.strip()} is not served by Core '
                              f'(registry {services[name]}): {site}')
    return errors, notes, len(used)


def self_test():
    """Synthetic fixtures only; not a Core build and not a game run."""
    with tempfile.TemporaryDirectory() as tmp:
        base = Path(tmp)
        core, root = base / 'core', base / 'product'
        for folder in (core / 'sdk', core / 'src', core / 'include', root / 'plugins', root / 'src'):
            folder.mkdir(parents=True)
        (core / 'sdk' / 'status_registry.json').write_text(json.dumps({'services': [
            {'service': 'game.entities', 'version': '1.0', 'capabilities': [
                {'name': 'game.entities.spawn', 'default': 'ON'},
                {'name': 'game.entities.watch', 'default': 'OFF'}]},
            {'service': 'game.world.control', 'version': '1.1', 'capabilities': [
                {'name': 'game.world.free_camera', 'default': 'OFF'}]}]}), encoding='utf-8')
        # A service name and an event name that look like capabilities, and a source-declared capability.
        (core / 'include' / 'a.h').write_text('#define SC_WORLD_CONTROL_NAME "game.world.control"\n'
                                              'static const char* e = "game.events.zone_changed";\n', encoding='utf-8')
        (core / 'src' / 'a.cpp').write_text('constexpr const char* kCapX = "game.x.declared";\n'
                                            'void f(){ caps::Set("vfs.pak", true); }\n', encoding='utf-8')

        def run(source):
            (root / 'plugins' / 'p.cpp').write_text(source, encoding='utf-8')
            return check(root, core)

        failures = []

        def expect(label, condition):
            if not condition:
                failures.append(label)

        errors, notes, _ = run('bool a(){return Cap("game.entities.spawn");}\n')
        expect('known name passes', not errors and not notes)
        errors, _, _ = run('bool a(){return Cap("game.prefabs.spawn");}\n')
        expect('invented name fails', len(errors) == 1)
        errors, _, _ = run('auto capability=[&](const char*n){return 1;};\nbool a(){return capability("game.world.typo");}\n')
        expect('lowercase capability lambda is scanned', len(errors) == 1)
        errors, _, _ = run('bool a(){return WorldCapability(api, "game.world.typo");}\n')
        expect('prefixed capability callee with a leading argument is scanned', len(errors) == 1)
        errors, _, _ = run('bool a(){return has("game.world.control");}\n')
        expect('a service name is not a capability', len(errors) == 1)
        errors, _, _ = run('bool a(){return Cap("game.events.zone_changed");}\n')
        expect('an event name is not a capability', len(errors) == 1)
        errors, _, _ = run('bool a(){return Cap("game.x.declared");}\n')
        expect('a capability declared only by caps::Set or kCap is accepted', not errors)
        errors, notes, _ = run('bool a(){return Cap("game.entities.watch") && capability("game.world.free_camera");}\n')
        expect('default OFF names are reported, not failed', len(notes) == 2 and not errors)
        errors, _, _ = run('void a(){ api->query_service("game.worldd", 0x10000u, &t); }\n')
        expect('unknown service name fails', len(errors) == 1)
        errors, _, _ = run('void a(){ api->query_service("game.world.control", 0x10000u, &t); }\n')
        expect('same major, lower minor is served', not errors)
        errors, _, _ = run('void a(){ api->query_service("game.entities", 0x10001u, &t); }\n')
        expect('minor above the registry fails', len(errors) == 1)
        errors, _, _ = run('void a(){ api->query_service("game.entities", 0x20000u, &t); }\n')
        expect('wrong major fails', len(errors) == 1)
        errors, _, _ = run('bool a(){return Has("offline.force_offline");}\n')
        expect('non game.* strings are not treated as capabilities', not errors)
        if failures:
            sys.exit('check_capability_names self-test failed: ' + '; '.join(failures))
    print('check_capability_names self-test: ok (synthetic fixtures)')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--core', type=Path, help='Core source folder (has sdk/status_registry.json)')
    parser.add_argument('--self-test', action='store_true', help='check the checker against a synthetic Core and exit')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    core = args.core or Path(os.environ.get('SCO_HOST_SOURCE_DIR') or ROOT / 'external' / 'sco-core')
    if not (core / 'sdk' / 'status_registry.json').is_file():
        sys.exit(f'check_capability_names: no Core source at {core} (pass --core or set SCO_HOST_SOURCE_DIR)')
    errors, notes, count = check(ROOT, core)
    for line in notes:
        print(f'note: {line}')
    for line in errors:
        print(line)
    if errors:
        sys.exit(f'check_capability_names: {len(errors)} unknown capability or service name(s) at {core}')
    print(f'check_capability_names: {count} game.* capability names, all known to Core at {core}')


if __name__ == '__main__':
    main()
