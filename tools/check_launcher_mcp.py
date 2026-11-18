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
import time

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

    def __init__(self, exe, env, extra=()):
        self.proc = subprocess.Popen([str(exe), 'mcp', *extra], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
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


GAME_LOG = (b'<2026-03-01T12:00:00.000Z> [Notice] User Login Success - Handle[zq9gamehand] - Time[1]\r\n'
            b'<2026-03-01T12:00:01.000Z> [Notice] <Expect Incoming Connection> nickname="zq9gamenick" playerGEID=2000998877 [Team_Network]\r\n'
            b'chat zq9gamehand says hi to zq9gamenick\r\n')


def make_install(folder):
    folder.mkdir(parents=True)
    # a fake game folder (an empty StarCitizen.exe is enough for the launcher to find it) with a Game.log beside Bin64
    bin64 = folder / 'StarCitizen' / 'LIVE' / 'Bin64'
    bin64.mkdir(parents=True)
    (bin64 / 'StarCitizen.exe').write_bytes(b'')
    (folder / 'StarCitizen' / 'LIVE' / 'Game.log').write_bytes(GAME_LOG)
    (folder / 'sc-offline.ini').write_bytes(
        b'# synthetic settings\r\nchannel = LIVE\r\nplugins = on\r\nboot_map = PU_All\r\ngame = ' + str(folder / 'StarCitizen' / 'LIVE').encode() + b'\r\n')
    for name, version, disabled in (('alpha', '1.0.0', False), ('beta', '2.0.0', True)):
        plugin = folder / 'data' / 'plugins' / name
        plugin.mkdir(parents=True)
        (plugin / 'plugin.ini').write_bytes(
            ('id = %s\r\nname = Plugin %s\r\nversion = %s\r\nauthor = tests\r\napi = 1.1\r\nkind = native\r\n'
             'entry = %s.dll\r\n' % (name, name, version, name)).encode())
        if disabled:
            (plugin / 'disabled').write_bytes(b'')
    # A plugin.ini over 64 KiB with its id, name and version at the top: the head is what list_plugins reads.
    big = folder / 'data' / 'plugins' / 'gamma'
    big.mkdir(parents=True)
    (big / 'plugin.ini').write_bytes(
        b'id = gamma\r\nname = Plugin gamma\r\nversion = 3.0.0\r\n' + b'; padding\r\n' * (80 * 1024 // 11))
    (folder / 'data' / 'mod.log').write_bytes(
        b'[mod] started\r\n[mod] account zq9acct username zq9nick nickname="zq9nick2" mail zq9@example.com\r\n[mod] data dir C:\\Users\\SomeName\\Games\\sc-offline\\data\r\n'
        b'[mod] User Login Success - Handle[zq9hand], Nickname[zq9nick3], Account[7654321]\r\n'
        b'[mod] hello ZQ9HAND and Zq9Nick3 (geid 200123456 - accountId 300123456 - name Zq9Char) bye zq9char\r\n'
        b'[mod] json {"dir":"C:\\\\Users\\\\bobsecretdouble\\\\AppData\\\\x"}\r\n[mod] ready\r\n')

    # The launcher log names the Game.log handle in free text: it is hidden only when Game.log was opened and learned from.
    (folder / 'data' / 'launcher.log').write_bytes(b'[launcher] hello zq9gamehand and zq9gamenick\r\n')
    # A previous launcher log longer than the 2 MiB window: the login line is only in the start, the name is also in the tail.
    (folder / 'data' / 'launcher.log.1').write_bytes(
        b'User Login Success - Handle[zq9big], Nickname[Zq9 Bigname], Account[8800123]\r\n'
        + b'[launcher] filler line to push the login line out of the scanned window\r\n' * (3 * 1024 * 1024 // 71)
        + b'[launcher] CActor::Kill: zq9big killed by Zq9Bigname\r\n[launcher] player zq9big says hi\r\n')


def check_exe(exe, work, game_log):
    exe = exe.resolve()
    expect(exe.is_file(), 'no such file: %s' % exe)
    install = work / 'install'
    make_install(install)
    shutil.copy2(exe, install / 'sc-offline.exe')
    before = snapshot(install)
    env = dict(os.environ)
    env['USERPROFILE'] = r'C:\Users\SomeName'
    env['USERNAME'] = 'zq9acct'
    env.pop('SC_OFFLINE_GUI', None)

    # An unknown argument next to `mcp` must not write usage or errors to stdout (it belongs to the protocol).
    for args in (['mcp', 'bogus'], ['mcp', '--game-log', 'bogus'], ['--game-log', 'mcp', 'bogus']):
        bogus = subprocess.run([str(install / 'sc-offline.exe'), *args], stdin=subprocess.DEVNULL,
                               capture_output=True, env=env, cwd=str(install), timeout=60)
        expect(bogus.stdout == b'' and bogus.returncode != 0 and b'unknown argument' in bogus.stderr,
               '%s: stdout %r, exit %d, stderr %r' % (args, bogus.stdout[:200], bogus.returncode, bogus.stderr[:200]))
    # --game-log belongs to `mcp` alone: any other command refuses it (and does not start anything)
    other = subprocess.run([str(install / 'sc-offline.exe'), 'status', '--game-log'], stdin=subprocess.DEVNULL,
                           capture_output=True, env=env, cwd=str(install), timeout=60)
    expect(other.returncode != 0 and b'--game-log' in other.stdout, 'status --game-log must be refused: %r' % other.stdout[:200])
    expect(snapshot(install) == before, 'a refused command changed the install folder')

    session = Session(install / 'sc-offline.exe', env, ['--game-log'] if game_log else [])
    try:
        run_session(session, install, before, game_log)
    finally:
        if session.proc.poll() is None:
            session.proc.kill()
            session.proc.wait()


def run_session(session, install, before, game_log):
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
    expect(body['plugins_total'] == 3 and body['plugins_enabled'] == 2, 'plugin counts: %r' % body)
    expect(body['plugins_setting_on'] is True and body['eac'] in ('active', 'disabled', 'not_installed'), 'status: %r' % body)
    expect(isinstance(body['game_running'], bool) and isinstance(body['wine'], bool), 'status types: %r' % body)
    expect(body['game_folder_found'] is True, 'the fake game folder must be found: %r' % body)

    listing = session.call('list_plugins')['structuredContent']
    plugins = listing['plugins']
    expect([p['folder'] for p in plugins] == ['alpha', 'beta', 'gamma'] and listing['count'] == 3, 'plugins: %r' % listing)
    expect(plugins[0]['enabled'] is True and plugins[1]['enabled'] is False, 'enabled flags: %r' % plugins)
    expect(plugins[0]['id'] == 'alpha' and plugins[0]['version'] == '1.0.0' and plugins[0]['entry'] == 'alpha.dll', 'fields')
    expect(plugins[0]['warnings'] == [] and plugins[1]['warnings'] == [], 'warnings: %r' % plugins)
    expect(plugins[2]['id'] == 'gamma' and plugins[2]['name'] == 'Plugin gamma' and plugins[2]['version'] == '3.0.0' and
           plugins[2]['warnings'] == [], 'a plugin.ini over 64 KiB must be read from its head: %r' % plugins[2])

    for log in ('mod', 'launcher', 'launcher_previous'):
        read = session.call('read_log', {'log': log})
        expect(read['isError'] is False, '%s: %r' % (log, read))
        out = read['structuredContent']
        expect(out['log'] == log and 'lines' in out and out['notice'] == '' and out['identity_scan_complete'] is True,
               'read_log %s: %r' % (log, out))
    # Game.log is opt-in: without --game-log a normal tool error that says how to turn it on; with it, served and redacted
    game = session.call('read_log', {'log': 'game'})
    launcher = session.call('read_log', {'log': 'launcher'})['structuredContent']
    if game_log:
        expect(game['isError'] is False, 'game with --game-log: %r' % game)
        out = game['structuredContent']
        expect(out['exists'] is True and out['path'].endswith('Game.log') and len(out['lines']) == 3, 'Game.log: %r' % out)
        expect('zq9gamehand' not in json.dumps(out).lower() and '2000998877' not in json.dumps(out) and 'zq9gamenick' not in json.dumps(out).lower(),
               'Game.log leaked: %r' % out)
        expect(out['lines'][2] == 'chat <redacted> says hi to <redacted>', 'Game.log lines: %r' % out)
        # Game.log was learned from: its handle is hidden in another log too
        expect(launcher['lines'] == ['[launcher] hello <redacted> and <redacted>'], 'launcher log with --game-log: %r' % launcher)
    else:
        expect(game['isError'] is True and 'structuredContent' not in game, 'game without --game-log: %r' % game)
        text = game['content'][0]['text']
        expect('--game-log' in text and 'account names' in text, 'the error must say how to enable it: %r' % text)
        # and the file was never read: the handle only it announces stays visible in another log
        expect(launcher['lines'] == ['[launcher] hello zq9gamehand and zq9gamenick'], 'launcher log without --game-log: %r' % launcher)
        expect('zq9gamehand' not in json.dumps(game), 'the error leaked Game.log: %r' % game)
    mod = session.call('read_log', {'log': 'mod', 'lines': 2})['structuredContent']
    expect(mod['exists'] is True and mod['matched_lines'] == 7 and mod['truncated'] is True, 'mod log: %r' % mod)
    expect(mod['lines'][-1] == '[mod] ready' and 'SomeName' not in json.dumps(mod), 'mod log lines: %r' % mod)
    acct = session.call('read_log', {'log': 'mod', 'contains': '[mod] account'})['structuredContent']
    expect(acct['lines'] == ['[mod] account <user> username <redacted> nickname="<redacted>" mail <redacted>'], 'identity redaction: %r' % acct)
    for probe in ('zq9acct', 'zq9nick', 'example.com'):
        expect(session.call('read_log', {'log': 'mod', 'contains': probe})['structuredContent']['lines'] == [], 'probe %s' % probe)
    # a name learned from one line (the login line) is hidden on every other line, in free text too
    hello = session.call('read_log', {'log': 'mod', 'contains': '[mod] hello'})['structuredContent']
    expect(hello['lines'] == ['[mod] hello <redacted> and <redacted> (geid <redacted> - accountId <redacted> - name <redacted>) bye <redacted>'],
           'learned identity: %r' % hello)
    login = session.call('read_log', {'log': 'mod', 'contains': 'login success'})['structuredContent']
    expect(login['lines'] == ['[mod] User Login Success - Handle[<redacted>], Nickname[<redacted>], Account[<redacted>]'], 'login line: %r' % login)
    for probe in ('zq9hand', 'zq9nick3', '7654321', '200123456', 'zq9char'):
        expect(session.call('read_log', {'log': 'mod', 'contains': probe})['structuredContent']['lines'] == [], 'probe %s' % probe)
    # the last 2 lines only: the login line is not returned, yet its name is learned
    tail = session.call('read_log', {'log': 'mod', 'lines': 3})['structuredContent']
    expect('zq9' not in json.dumps(tail).lower() and '7654321' not in json.dumps(tail), 'tail: %r' % tail)
    found = session.call('read_log', {'log': 'mod', 'contains': 'DATA DIR'})['structuredContent']
    expect(found['lines'] == ['[mod] data dir %USERPROFILE%\\Games\\sc-offline\\data'], 'redaction: %r' % found)
    # doubled (JSON-escaped) separators: redacted, and the account name cannot be probed with `contains`
    escaped = session.call('read_log', {'log': 'mod', 'contains': 'bobsecretdouble'})['structuredContent']
    expect(escaped['lines'] == [], 'contains must not see an account name: %r' % escaped)
    shown = session.call('read_log', {'log': 'mod', 'contains': '[mod] json'})['structuredContent']
    expect(len(shown['lines']) == 1 and 'bobsecretdouble' not in json.dumps(shown) and '<user>' in shown['lines'][0],
           'escaped path: %r' % shown)
    big = session.call('read_log', {'log': 'launcher_previous', 'lines': 5})['structuredContent']
    expect(big['truncated'] is True and 'zq9big' not in json.dumps(big).lower() and 'bigname' not in json.dumps(big).lower(),
           'a name announced only in the start of a long log: %r' % big)
    for probe in ('zq9big', 'bigname', '8800123'):
        expect(session.call('read_log', {'log': 'launcher_previous', 'contains': probe})['structuredContent']['lines'] == [], 'probe %s' % probe)
    expect(session.request('tools/call', {'name': 'read_log', 'arguments': {'log': 'mod', 'lines': 501}})['error']['code'] == -32602,
           'lines 501 must be refused')
    expect(session.request('tools/call', {'name': 'nope'})['error']['code'] == -32602, 'unknown tool')
    expect(session.request('nope/method')['error']['code'] == -32601, 'unknown method')

    settings = session.call('get_settings')['structuredContent']
    expect(settings['exists'] is True and settings['path'].endswith('sc-offline.ini'), 'settings: %r' % settings)
    expect([(s['key'], s['value']) for s in settings['settings'][:3]] ==
           [('channel', 'LIVE'), ('plugins', 'on'), ('boot_map', 'PU_All')] and settings['settings'][3]['key'] == 'game', 'settings: %r' % settings)

    session.send_raw(b'{not json\n\n[1]\n')
    expect(session.read_line()['error']['code'] == -32700, 'parse error')
    expect(session.read_line()['error']['code'] == -32600, 'batch')
    # ~80k distinct keys in under 1 MiB used to stall the parser for seconds: refused fast, the session goes on
    keys = ','.join('"k%d":0' % i for i in range(80000))
    line = ('{"jsonrpc":"2.0","id":1,"method":"ping",' + keys + '}').encode()
    expect(len(line) < 1 << 20, 'the key flood must fit one line: %d' % len(line))
    started = time.monotonic()
    session.send_raw(line + b'\n')
    flood = session.read_line()
    elapsed = time.monotonic() - started
    expect(flood['error']['code'] == -32700 and 'too many members' in flood['error']['message'], 'key flood: %r' % flood)
    expect(elapsed < 1.0, 'the key flood took %.2f s' % elapsed)
    expect(session.request('ping')['result'] == {}, 'the session must go on after the key flood')
    expect(session.close() == 0, 'EOF on stdin must exit 0')
    expect(b'SomeName' not in b''.join(session.stderr), 'stderr leaked the account name')

    after = snapshot(install)
    expect(after == before, 'the server changed the install folder: %r' % sorted(set(before.items()) ^ set(after.items())))
    print('launcher mcp: stdio session passed (%s, key flood refused in %.3f s); %d files and folders unchanged'
          % ('with --game-log' if game_log else 'Game.log off', elapsed, len(after)))


def check_big(exe, work):
    """Hostile lines and a log past 64 MiB through the real exe: one read_log call must stay under 5 seconds."""
    exe = exe.resolve()
    install = work / 'install'
    (install / 'data').mkdir(parents=True)
    shutil.copy2(exe, install / 'sc-offline.exe')
    filler = b'<2026-03-01T12:00:00.000Z> [Notice] <Physics> frame 12345 ok zone=Stanton port 5100 build 2.4.100\r\n'
    hostile = [b'User Login Success - ' + b'Handle[' * ((1 << 20) // 7),
               b'nickname="x ' * ((1 << 20) // 12), b'a@' * ((1 << 20) // 2), b'geid 1 name x ' * ((1 << 20) // 14),
               b'GEID[' * ((1 << 20) // 5)]
    with open(install / 'data' / 'mod.log', 'wb') as f:
        f.write(b'User Login Success - Handle[zq9bighand], Nickname[Zq9 Bigname]\r\n')
        for _ in range(65 * 1024 * 1024 // len(filler) + 1):
            f.write(filler)
        for line in hostile:
            f.write(line + b'\r\n')
        f.write(b'chat zq9bighand last\r\n')
    size = (install / 'data' / 'mod.log').stat().st_size
    expect(size > 65 * 1024 * 1024, 'the big log must be larger than 64 MiB: %d' % size)
    env = dict(os.environ)
    env['USERPROFILE'] = r'C:\Users\SomeName'
    env['USERNAME'] = 'zq9acct'
    env.pop('SC_OFFLINE_GUI', None)
    session = Session(install / 'sc-offline.exe', env)
    try:
        started = time.monotonic()
        read = session.call('read_log', {'log': 'mod', 'contains': 'chat'})
        elapsed = time.monotonic() - started
        out = read['structuredContent']
        expect(out['lines'] == ['chat <redacted> last'], 'big log: %r' % out)
        expect(out['identity_scan_complete'] is False and '64 MiB' in out['notice'] and 'mod' in out['notice'], 'the 64 MiB cap must be reported: %r' % out)
        expect(elapsed < 5.0, 'a %d MiB log with hostile lines took %.2f s' % (size >> 20, elapsed))
        session.close()
    finally:
        if session.proc.poll() is None:
            session.proc.kill()
            session.proc.wait()
    print('launcher mcp: %d MiB log with five hostile 1 MiB lines read in %.2f s' % (size >> 20, elapsed))


def check_dense(exe, work):
    """Logs of 64 MiB made only of login lines, plain and with a dozen multi-word keys on each, through the real exe:
    two read_log calls (each scans every log again) must stay under 5 seconds, and the cap must be reported."""
    exe = exe.resolve()
    install = work / 'install'
    (install / 'data').mkdir(parents=True)
    shutil.copy2(exe, install / 'sc-offline.exe')
    keys = b''.join(b' nickname=Aa Bb Cc Dd Ee Ff%d' % k for k in range(12))
    plain = b''.join(b'User Login Success - Handle[Zorbax_%d]\r\n' % k for k in range(20000))
    multi = b''.join(b'User Login Success - Handle[Zorbax_%d]%s\r\n' % (k, keys) for k in range(2000))
    for name, block in (('mod.log', plain), ('launcher.log', multi), ('launcher.log.1', plain)):
        with open(install / 'data' / name, 'wb') as f:
            for _ in range(65 * 1024 * 1024 // len(block) + 1):
                f.write(block)
            f.write(b'chat Zorbax_3 last\r\n')
    env = dict(os.environ)
    env['USERPROFILE'] = r'C:\Users\SomeName'
    env['USERNAME'] = 'zq9acct'
    env.pop('SC_OFFLINE_GUI', None)
    session = Session(install / 'sc-offline.exe', env)
    try:
        started = time.monotonic()
        for log in ('mod', 'launcher'):
            out = session.call('read_log', {'log': log, 'contains': 'chat'})['structuredContent']
            expect(out['identity_scan_complete'] is False and 'more login lines' in out['notice'], 'the login-line cap must be reported: %r' % out)
        elapsed = time.monotonic() - started
        expect(elapsed < 5.0, 'two read_log calls over dense 64 MiB login logs took %.2f s' % elapsed)
        session.close()
    finally:
        if session.proc.poll() is None:
            session.proc.kill()
            session.proc.wait()
    print('launcher mcp: two read_log calls over dense 64 MiB login logs in %.2f s' % elapsed)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path)
    parser.add_argument('--exe', type=Path, help='built sc-offline.exe: also run a stdio session against it and prove it writes nothing')
    args = parser.parse_args()
    exe = args.exe or (Path(os.environ['SC_OFFLINE_EXE']) if os.environ.get('SC_OFFLINE_EXE') else None)

    def run(work):
        check_portable(work / 'portable')
        if exe:
            check_exe(exe, work / 'exe', False)
            check_exe(exe, work / 'exe-game-log', True)
            check_big(exe, work / 'big')
            check_dense(exe, work / 'dense')

    if args.work:
        run(args.work.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix='sc-offline-mcp-') as folder:
            run(Path(folder))


if __name__ == '__main__':
    main()
