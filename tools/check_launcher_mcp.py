#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the read-only MCP server (`sc-offline.exe mcp`): portable protocol tests, and with --exe a stdio session
against a built launcher in a scratch folder that proves the server writes nothing."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent


def check_portable(work):
    work.mkdir(parents=True, exist_ok=False)
    build = work / 'build'
    subprocess.run(['cmake', '-S', str(ROOT / 'tools/launcher-mcp'), '-B', str(build)], check=True)
    subprocess.run(['cmake', '--build', str(build), '--config', 'Release', '--parallel', '2'], check=True)
    subprocess.run(['ctest', '--test-dir', str(build), '-C', 'Release', '--output-on-failure'], check=True)


def snapshot(folder):
    """Every path under folder with its size, mtime and SHA-256 (None for a directory)."""
    tree = {}
    for path in sorted(folder.rglob('*')):
        rel = path.relative_to(folder).as_posix()
        if path.is_dir():
            tree[rel] = None
        else:
            stat = path.stat()
            tree[rel] = (stat.st_size, stat.st_mtime_ns, hashlib.sha256(path.read_bytes()).hexdigest())
    return tree


def expect(condition, message):
    if not condition:
        raise SystemExit('FAIL: ' + message[:400])


class Session:
    """One `sc-offline.exe mcp` process: a request line out, a response line in."""

    def __init__(self, exe, env):
        self.proc = subprocess.Popen([str(exe), 'mcp'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, env=env, cwd=str(exe.parent))
        self.stderr = []
        threading.Thread(target=lambda: self.stderr.append(self.proc.stderr.read()), daemon=True).start()
        self.next_id = 0

    def send_raw(self, data):
        self.proc.stdin.write(data)
        self.proc.stdin.flush()

    def read_line(self):
        line = self.proc.stdout.readline()
        expect(line.endswith(b'\n'), 'the server closed stdout or sent an unterminated line: %r' % line)
        expect(b'\r' not in line.rstrip(b'\r\n') and line.count(b'\n') == 1, 'a reply must be one line: %r' % line)
        return json.loads(line.decode('utf-8'))

    def request(self, method, params=None):
        self.next_id += 1
        message = {'jsonrpc': '2.0', 'id': self.next_id, 'method': method}
        if params is not None:
            message['params'] = params
        self.send_raw((json.dumps(message) + '\n').encode())
        reply = self.read_line()
        expect(reply.get('id') == self.next_id and reply.get('jsonrpc') == '2.0', 'reply id mismatch: %r' % reply)
        return reply

    def notify(self, method):
        self.send_raw((json.dumps({'jsonrpc': '2.0', 'method': method}) + '\n').encode())

    def call(self, name, arguments=None):
        reply = self.request('tools/call', {'name': name, 'arguments': arguments or {}})
        expect('result' in reply, '%s: %r' % (name, reply))
        return reply['result']

    def close(self):
        self.proc.stdin.close()
        code = self.proc.wait(timeout=30)
        self.proc.stdout.close()
        return code


def make_install(folder):
    folder.mkdir(parents=True)
    (folder / 'sc-offline.ini').write_bytes(
        b'# synthetic settings\r\nchannel = LIVE\r\nplugins = on\r\nboot_map = PU_All\r\n')
    for name, version, disabled in (('alpha', '1.0.0', False), ('beta', '2.0.0', True)):
        plugin = folder / 'data' / 'plugins' / name
        plugin.mkdir(parents=True)
        (plugin / 'plugin.ini').write_bytes(
            ('id = %s\r\nname = Plugin %s\r\nversion = %s\r\nauthor = tests\r\napi = 1.1\r\nkind = native\r\n'
             'entry = %s.dll\r\n' % (name, name, version, name)).encode())
        if disabled:
            (plugin / 'disabled').write_bytes(b'')
    (folder / 'data' / 'mod.log').write_bytes(
        b'[mod] started\r\n[mod] data dir C:\\Users\\SomeName\\Games\\sc-offline\\data\r\n[mod] ready\r\n')


def check_exe(exe, work):
    exe = exe.resolve()
    expect(exe.is_file(), 'no such file: %s' % exe)
    install = work / 'install'
    make_install(install)
    shutil.copy2(exe, install / 'sc-offline.exe')
    before = snapshot(install)
    env = dict(os.environ)
    env['USERPROFILE'] = r'C:\Users\SomeName'
    env.pop('SC_OFFLINE_GUI', None)

    session = Session(install / 'sc-offline.exe', env)
    try:
        run_session(session, install, before)
    finally:
        if session.proc.poll() is None:
            session.proc.kill()
            session.proc.wait()


def run_session(session, install, before):
    reply = session.request('initialize', {'protocolVersion': '2025-06-18', 'capabilities': {},
                                           'clientInfo': {'name': 'check', 'version': '1'}})
    result = reply['result']
    expect(result['protocolVersion'] == '2025-06-18', 'version negotiation: %r' % result)
    expect(result['serverInfo']['name'] == 'sc-offline' and result['serverInfo']['version'], 'serverInfo: %r' % result)
    expect(result['capabilities'] == {'tools': {'listChanged': False}}, 'capabilities: %r' % result)
    session.notify('notifications/initialized')
    expect(session.request('ping')['result'] == {}, 'ping')

    tools = session.request('tools/list')['result']['tools']
    names = [t['name'] for t in tools]
    expect(names == ['get_status', 'list_plugins', 'read_log', 'get_settings'], 'tool list: %r' % names)
    for tool in tools:
        expect(tool['annotations']['readOnlyHint'] is True and 'outputSchema' in tool, 'tool shape: %s' % tool['name'])

    status = session.call('get_status')
    expect(status['isError'] is False, 'get_status: %r' % status)
    body = status['structuredContent']
    expect(json.loads(status['content'][0]['text']) == body, 'get_status text differs from structuredContent')
    schema = next(t for t in tools if t['name'] == 'get_status')['outputSchema']
    expect(set(body) == set(schema['required']), 'get_status fields: %r' % sorted(body))
    expect(body['plugins_total'] == 2 and body['plugins_enabled'] == 1, 'plugin counts: %r' % body)
    expect(body['plugins_setting_on'] is True and body['eac'] in ('active', 'disabled', 'not_installed'), 'status: %r' % body)
    expect(isinstance(body['game_running'], bool) and isinstance(body['wine'], bool), 'status types: %r' % body)

    listing = session.call('list_plugins')['structuredContent']
    plugins = listing['plugins']
    expect([p['folder'] for p in plugins] == ['alpha', 'beta'] and listing['count'] == 2, 'plugins: %r' % listing)
    expect(plugins[0]['enabled'] is True and plugins[1]['enabled'] is False, 'enabled flags: %r' % plugins)
    expect(plugins[0]['id'] == 'alpha' and plugins[0]['version'] == '1.0.0' and plugins[0]['entry'] == 'alpha.dll', 'fields')
    expect(plugins[0]['warnings'] == [] and plugins[1]['warnings'] == [], 'warnings: %r' % plugins)

    for log in ('mod', 'launcher', 'launcher_previous', 'game'):
        read = session.call('read_log', {'log': log})
        expect(read['isError'] is False, '%s: %r' % (log, read))
        out = read['structuredContent']
        expect(out['log'] == log and 'lines' in out, 'read_log %s: %r' % (log, out))
        if log in ('launcher', 'launcher_previous'):   # the scratch install has no launcher logs; a real game's Game.log may exist
            expect(out['exists'] is False and out['lines'] == [], 'a missing log must read as empty: %r' % out)
    mod = session.call('read_log', {'log': 'mod', 'lines': 2})['structuredContent']
    expect(mod['exists'] is True and mod['matched_lines'] == 3 and mod['truncated'] is True, 'mod log: %r' % mod)
    expect(mod['lines'][-1] == '[mod] ready' and 'SomeName' not in json.dumps(mod), 'mod log lines: %r' % mod)
    found = session.call('read_log', {'log': 'mod', 'contains': 'DATA DIR'})['structuredContent']
    expect(found['lines'] == ['[mod] data dir %USERPROFILE%\\Games\\sc-offline\\data'], 'redaction: %r' % found)
    expect(session.request('tools/call', {'name': 'read_log', 'arguments': {'log': 'mod', 'lines': 501}})['error']['code'] == -32602,
           'lines 501 must be refused')
    expect(session.request('tools/call', {'name': 'nope'})['error']['code'] == -32602, 'unknown tool')
    expect(session.request('nope/method')['error']['code'] == -32601, 'unknown method')

    settings = session.call('get_settings')['structuredContent']
    expect(settings['exists'] is True and settings['path'].endswith('sc-offline.ini'), 'settings: %r' % settings)
    expect([(s['key'], s['value']) for s in settings['settings']] ==
           [('channel', 'LIVE'), ('plugins', 'on'), ('boot_map', 'PU_All')], 'settings: %r' % settings)

    session.send_raw(b'{not json\n\n[1]\n')
    expect(session.read_line()['error']['code'] == -32700, 'parse error')
    expect(session.read_line()['error']['code'] == -32600, 'batch')
    expect(session.close() == 0, 'EOF on stdin must exit 0')
    expect(b'SomeName' not in b''.join(session.stderr), 'stderr leaked the account name')

    after = snapshot(install)
    expect(after == before, 'the server changed the install folder: %r' % sorted(set(before.items()) ^ set(after.items())))
    print('launcher mcp: stdio session passed; %d files and folders unchanged' % len(after))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path)
    parser.add_argument('--exe', type=Path, help='built sc-offline.exe: also run a stdio session against it and prove it writes nothing')
    args = parser.parse_args()
    exe = args.exe or (Path(os.environ['SC_OFFLINE_EXE']) if os.environ.get('SC_OFFLINE_EXE') else None)

    def run(work):
        check_portable(work / 'portable')
        if exe:
            check_exe(exe, work / 'exe')

    if args.work:
        run(args.work.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix='sc-offline-mcp-') as folder:
            run(Path(folder))


if __name__ == '__main__':
    main()
