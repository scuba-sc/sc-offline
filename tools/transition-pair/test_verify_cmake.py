#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check cache and codemodel refusal behavior with a synthetic file-API reply."""
import json
import pathlib
import subprocess
import sys
import tempfile

script = pathlib.Path(__file__).with_name('verify_cmake.py')
with tempfile.TemporaryDirectory(prefix='pair-cmake-check-') as tmp:
    base = pathlib.Path(tmp)
    build, core, offline = (base / x for x in ('build', 'core', 'offline'))
    reply = build / '.cmake/api/v1/reply'
    reply.mkdir(parents=True)
    core.mkdir(); offline.mkdir()
    cache = build / 'CMakeCache.txt'
    cache.write_text(f'SCO_HOST_SOURCE_DIR:PATH={core}\n')
    index = {'objects': [{'kind': 'codemodel', 'jsonFile': 'model.json'}]}
    (reply / 'index-test.json').write_text(json.dumps(index))
    targets = {
        'sco_app': ([core / 'src/app/sco_app.cpp', core / 'src/app/plugin_startup_snapshot.cpp'], []),
        'sco_game_sc': ([core / 'src/game/exact_build_profile.cpp'], []),
        'sco_game_host': ([core / 'src/game/host.cpp'], ['sco_app', 'sco_game_sc']),
        'sc_offline_host_composition': ([offline / 'src/offline_host.cpp'], ['sco_game_host']),
        'registry_refusal_test': ([], ['sc_offline_host_composition']),
    }
    refs = []
    for name, (sources, deps) in targets.items():
        file = name + '.json'
        refs.append({'jsonFile': file})
        data = {'name': name, 'sources': [{'path': str(x)} for x in sources],
                'dependencies': [{'id': x + '::@test'} for x in deps]}
        (reply / file).write_text(json.dumps(data))
    (reply / 'model.json').write_text(json.dumps({'paths': {'source': str(offline)},
                                                   'configurations': [{'targets': refs}]}))
    def verify(ok, reason=''):
        result = subprocess.run([sys.executable, str(script), str(build), str(core), str(offline), 'fixture'],
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if (result.returncode == 0) != ok or (reason and reason not in result.stdout):
            raise AssertionError(result.stdout)
    verify(True)
    cache.write_text(f'SCO_HOST_SOURCE_DIR:PATH={offline}\n')
    verify(False, 'cache source override mismatch')
    cache.write_text(f'SCO_HOST_SOURCE_DIR:PATH={core}\n')
    target_file = reply / 'sco_app.json'
    target = json.loads(target_file.read_text()); target['sources'].pop()
    target_file.write_text(json.dumps(target))
    verify(False, 'source mismatch in target: sco_app')
    print('paired CMake source checks (synthetic codemodel): PASS')
