#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the published RC2 discovery server and its complete workflow index.

Run against the verified, extracted combined kit. The optional output is a JSONL
transcript of requests and replies, including generated source; it is local evidence.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

SOURCE = '844e136ccb280ad8cb43f73bc43c9447e8f6799d'
IDS = ['sco-plan-sdk-work', 'sco-build-check-package', 'sco-debug-plugin',
       'sco-package-plugin', 'sco-upgrade-plugin', 'sco-report-sdk-gap']
TOOLS = ['search_api', 'get_symbol', 'list_services', 'get_example',
         'get_capability_status', 'check_compatibility', 'list_engine_natives',
         'get_engine_native', 'validate_manifest_and_widgets', 'generate_plugin_scaffold',
         'search_guides', 'get_guide_section', 'list_workflows', 'get_workflow']


def digest(data):
    return hashlib.sha256(data).hexdigest()


def run(server, messages, extra=()):
    result = subprocess.run([sys.executable, *server, *extra],
                            input=''.join(json.dumps(m) + '\n' for m in messages),
                            capture_output=True, text=True, timeout=30)
    return result, [json.loads(line) for line in result.stdout.splitlines()]


def request(i, method, params=None):
    result = {'jsonrpc': '2.0', 'id': i, 'method': method}
    if params is not None:
        result['params'] = params
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True, help='Verified extracted combined SDK root')
    parser.add_argument('--transcript', type=Path, help='New local JSONL evidence file')
    parser.add_argument('--scaffold', type=Path, help='New empty directory for the generated C plugin')
    args = parser.parse_args()
    sdk = args.sdk.resolve(strict=True)
    index = json.loads((sdk / 'sdk-workflows.json').read_text())
    catalogue = json.loads((sdk / 'sdk-catalogue.json').read_text())
    compat = json.loads((sdk / 'COMPATIBILITY.json').read_text())
    assert (index['source_sha'], catalogue['source_sha'], index['sdk_version'],
            index['bundle_version'], compat['profile']) == (SOURCE, SOURCE, '2.4.0', '0.2.2', 'legacy')
    assert compat['source_commits'] == {'core': SOURCE, 'game-sc': SOURCE}
    server = [str(sdk / 'tools/sco-core-mcp/server.py'), '--catalogue',
              str(sdk / 'sdk-catalogue.json'), '--repo-root', str(sdk)]
    calls = [('list_workflows', {}),
             ('check_compatibility', {'sdk_version': '2.4.0', 'game_pack_version': '0.8.0'}),
             ('check_compatibility', {'sdk_version': '9.9.9'}),
             ('search_api', {'query': 'set_panel', 'service': 'sco.widgets', 'language': 'c', 'limit': 5}),
             ('get_symbol', {'symbol_or_id': 'sco_widgets_v1.set_panel'}),
             ('get_capability_status', {'symbol_or_id': 'sco_widgets_v1.set_panel'}),
             ('get_example', {'symbol_or_id': 'sco_widgets_v1.set_panel', 'language': 'c'}),
             ('generate_plugin_scaffold', {'id': 'rc2_probe', 'lang': 'c', 'with_menu': True,
                                           'services': ['sco.storage@1.0']}),
             ('generate_plugin_scaffold', {'id': 'rc2_probe', 'lang': 'lua',
                                           'services': ['game.inventory@1.0']}),
             ('get_workflow', {'id': 'missing'}),
             ('get_workflow', {'id': IDS[0], 'path': '../../secret'}),
             ('get_capability_status', {'symbol_or_id': 'sc_inventory_v1_2.snapshot_layout'})]
    for item in index['workflows']:
        calls.append(('get_workflow', {'id': item['id']}))
        calls.extend(('get_workflow', {'id': item['id'], 'path': f['path']}) for f in item['files'])
    messages = [request(1, 'initialize', {'protocolVersion': '2025-11-25', 'capabilities': {},
                'clientInfo': {'name': 'rc2-validation', 'version': '1'}}),
                {'jsonrpc': '2.0', 'method': 'notifications/initialized'}, request(2, 'tools/list')]
    messages += [request(i, 'tools/call', {'name': name, 'arguments': arguments})
                 for i, (name, arguments) in enumerate(calls, 3)]
    process, replies = run(server, messages)
    assert process.returncode == 0, process.stderr
    assert len(replies) == len(messages) - 1
    assert replies[0]['result']['protocolVersion'] == '2025-11-25'
    assert [t['name'] for t in replies[1]['result']['tools']] == TOOLS
    assert all(t['annotations']['readOnlyHint'] for t in replies[1]['result']['tools'])
    results = [r['result'] for r in replies[2:]]
    data = [r['structuredContent'] for r in results]
    assert [w['id'] for w in data[0]['workflows']] == IDS
    assert data[0]['workflow_provenance']['source_sha'] == SOURCE
    assert data[1]['coordinate_matches'] == {'sdk_version': True, 'game_pack_version': True}
    assert data[2]['coordinate_matches']['sdk_version'] is False
    assert any(r['symbol'] == 'sco_widgets_v1.set_panel' for r in data[3]['records'])
    assert data[4]['record']['member_extent'] == 16
    assert data[5]['evidence_state']['default_runtime_state']['state'] == 'unknown'
    assert not results[6]['isError']
    assert data[7]['state'] == 'available' and not data[7]['written']
    assert results[8]['isError'] and 'no query_service' in data[8]['error']
    assert results[9]['isError'] and results[10]['isError']
    layout = data[11]['evidence_state']['service_status']['rows']
    assert any(r['name'] == 'game.inventory.layout' and r['default'] == 'OFF'
               and r['registry_evidence_level'] == 'synthetic_only' for r in layout)
    assert data[11]['evidence_state']['gameplay_receipt']['state'] == 'unknown'
    offset = 12
    for item in index['workflows']:
        entry = data[offset]; offset += 1
        assert entry['file']['path'] == item['entry']
        for expected in item['files']:
            actual = data[offset]['file']; offset += 1
            assert actual == expected
            assert digest(actual['text'].encode()) == actual['sha256']
            assert (sdk / actual['path']).read_bytes() == actual['text'].encode()
    assert offset == len(data)
    minimal, minimal_replies = run(server, messages[:3], ['--minimal-tools'])
    assert minimal.returncode == 0 and [t['name'] for t in minimal_replies[1]['result']['tools']] == TOOLS[:6]
    pre, pre_replies = run(server, [request(1, 'tools/list')])
    assert pre.returncode == 0 and 'error' in pre_replies[0]
    with tempfile.TemporaryDirectory() as tmp:
        bad = dict(index, source_sha='0' * 40)
        path = Path(tmp) / 'stale.json'
        path.write_text(json.dumps(bad))
        refusal, _ = run(server, [], ['--workflows', str(path)])
        assert refusal.returncode != 0 and 'workflows refused' in refusal.stderr
    if args.scaffold:
        args.scaffold.mkdir(parents=True, exist_ok=False)
        for name, content in data[7]['files'].items():
            path = args.scaffold / name
            assert path.parent == args.scaffold and not path.exists()
            path.write_text(content)
    if args.transcript:
        with args.transcript.open('x') as output:
            for msg in messages:
                output.write(json.dumps({'request': msg}) + '\n')
            for reply in replies:
                output.write(json.dumps({'reply': reply}) + '\n')
    print('RC2 MCP: 14 default and 6 minimal tools; six workflows and all indexed files verified')
    print('Version match/mismatch, unsupported Lua service, stale index and session refusals: PASS')
    print('Discovery is static; host, Windows and game behavior NOT VERIFIED')


if __name__ == '__main__':
    main()
