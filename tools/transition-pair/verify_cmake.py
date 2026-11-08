#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify CMake's configured source selection and required target graph."""
import json
import pathlib
import sys

if len(sys.argv) != 5 or sys.argv[4] not in ('fixture', 'product'):
    raise SystemExit('usage: verify_cmake.py BUILD CORE OFFLINE fixture|product')
build, core, offline = (pathlib.Path(x).resolve() for x in sys.argv[1:4])
mode = sys.argv[4]
cache = (build / 'CMakeCache.txt').read_text(encoding='utf-8')
values = [line.split('=', 1)[1] for line in cache.splitlines()
          if line.startswith('SCO_HOST_SOURCE_DIR:PATH=')]
if len(values) != 1 or pathlib.Path(values[0]).resolve() != core:
    raise SystemExit('CMake cache source override mismatch')
reply = build / '.cmake/api/v1/reply'
indexes = sorted(reply.glob('index-*.json'))
if len(indexes) != 1:
    raise SystemExit('missing or ambiguous CMake file API index')
index = json.loads(indexes[0].read_text(encoding='utf-8'))
models = [o for o in index['objects'] if o['kind'] == 'codemodel']
if len(models) != 1:
    raise SystemExit('missing or ambiguous CMake codemodel')
model = json.loads((reply / models[0]['jsonFile']).read_text(encoding='utf-8'))
source_root = pathlib.Path(model['paths']['source']).resolve()
configs = model['configurations']
if not configs:
    raise SystemExit('empty CMake configurations')
for config in configs:
    targets = {}
    for ref in config['targets']:
        target = json.loads((reply / ref['jsonFile']).read_text(encoding='utf-8'))
        targets[target['name']] = target
    required = {
        'sco_app': [core / 'src/app/sco_app.cpp', core / 'src/app/plugin_startup_snapshot.cpp'],
        'sco_game_sc': [core / 'src/game/exact_build_profile.cpp'],
        'sco_game_host': [core / 'src/game/host.cpp'],
        'sc_offline_host_composition' if mode == 'fixture' else 'dinput8':
            [offline / 'src/offline_host.cpp'],
    }
    for name, paths in required.items():
        if name not in targets:
            raise SystemExit(f'missing target: {name}')
        sources = {(source_root / x['path']).resolve() for x in targets[name].get('sources', [])}
        if any(p.resolve() not in sources for p in paths):
            raise SystemExit(f'source mismatch in target: {name}')
    def depends(target, dependency):
        return any(x['id'].split('::', 1)[0] == dependency
                   for x in targets[target].get('dependencies', []))
    seam = 'sc_offline_host_composition' if mode == 'fixture' else 'dinput8'
    for target, dep in ((seam, 'sco_game_host'), ('sco_game_host', 'sco_app'),
                        ('sco_game_host', 'sco_game_sc')):
        if not depends(target, dep):
            raise SystemExit(f'missing dependency: {target} -> {dep}')
    if mode == 'fixture' and not depends('registry_refusal_test', 'sc_offline_host_composition'):
        raise SystemExit('registry refusal fixture not linked to product seam')
print(f'CMake {mode} source override and target graph: PASS')
