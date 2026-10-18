// SPDX-License-Identifier: GPL-3.0-only
// Runs the real module against an external, archive-built synthetic host.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { copyFile, mkdir, mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { createInterface } from 'node:readline';
import { connect } from 'node:net';

const [hostArg, moduleArg, manifestArg] = process.argv.slice(2);
assert(hostArg && moduleArg && manifestArg, 'usage: inspector_hostd_e2e.mjs HOSTD MODULE MANIFEST');
const work = await mkdtemp(join(tmpdir(), 'inspector-hostd-'));
const pluginRoot = join(work, 'plugins');
await mkdir(join(pluginRoot, 'inspector'), { recursive: true });
await copyFile(resolve(moduleArg), join(pluginRoot, 'inspector', 'inspector.dll'));
await copyFile(resolve(manifestArg), join(pluginRoot, 'inspector', 'plugin.ini'));

async function runScenario(mock, cycle) {
  const child = spawn(resolve(hostArg), [
    '--port', '0', '--bind', '127.0.0.1', '--plugins', pluginRoot,
    '--data', join(work, `data-${cycle}`), '--test-shutdown',
    ...(!mock ? ['--no-mock-game'] : []),
  ], { cwd: work, stdio: ['ignore', 'pipe', 'pipe'] });
  const signal = AbortSignal.timeout(45_000);
  const exited = once(child, 'exit', { signal });
  const lines = createInterface({ input: child.stdout });
  const pending = new Map();
  let socket;
  let sequence = 0;
  let log = '';
  child.stderr.on('data', chunk => { log += chunk; });
  const ready = new Promise((accept, reject) => {
    lines.on('line', line => {
      log += `${line}\n`;
      const match = /scuba-hostd: listening port=(\d+)/.exec(line);
      if (match) accept(Number(match[1]));
    });
    child.once('error', reject);
    child.once('exit', code => reject(new Error(`host exited ${code} before readiness`)));
    signal.addEventListener('abort', () => reject(signal.reason), { once: true });
  });
  function call(method, params = {}) {
    const id = ++sequence;
    return new Promise((accept, reject) => {
      const timer = setTimeout(() => {
        pending.delete(id);
        reject(new Error(`${method} timed out`));
      }, 10_000);
      pending.set(id, { accept, reject, timer });
      socket.send(JSON.stringify({ id, method, params }));
    });
  }
  async function invoke(name, args = [], result = 'OK') {
    const value = await call('plugins.invoke', { name, args });
    assert.equal(value.result, result, `${name}: ${JSON.stringify(value)}`);
    return value.reply;
  }
  try {
    const port = await ready;
    socket = new WebSocket(`ws://127.0.0.1:${port}/ws`);
    socket.addEventListener('message', message => {
      const value = JSON.parse(message.data);
      const entry = pending.get(value.id);
      if (!entry) return;
      pending.delete(value.id);
      clearTimeout(entry.timer);
      if (value.error) entry.reject(Object.assign(new Error(value.error.message), value.error));
      else entry.accept(value.result);
    });
    await once(socket, 'open', { signal });
    const status = await call('system.status');
    assert(status.kernel.plugins.some(p => p.id === 'inspector' && p.state === 'loaded'));
    assert.match(await invoke('inspector.status'), /generation=0 sample=never/);
    assert.equal((await call('widgets.get', { id: 'inspector.main' })).id, 'inspector.main');
    assert.match(await invoke('inspector.refresh'), /generation=1/);
    if (mock) {
      assert.match(await invoke('inspector.status'), /stats=available/);
      assert.match(await invoke('inspector.status'), /build=available/);
      const pages = [];
      for (let page = 0; page < 3; page++) pages.push(await invoke('inspector.inventory', [page]));
      assert(pages.every((text, page) => text.includes(`total=3 page=${page} next=${page < 2 ? 1 : 0}`)));
      assert.equal(new Set(pages.map(text => /entity=(\d+)/.exec(text)?.[1])).size, 3);
      assert.match(await invoke('inspector.stats', [0]), /stat=hunger value=20/);
      assert.match(await invoke('inspector.action', ['attack1']), /attack1/);
      assert.match(await invoke('inspector.action_count'), /1267/);
      assert.match(await invoke('inspector.params', [0]), /source=definition/);
      assert.equal((await call('widgets.event', {
        panel: 'inspector.main', node_id: 42, kind: 'button',
      })).result, 'OK');
      assert.match(JSON.stringify(await call('widgets.get', { id: 'inspector.main' })),
        /capturableDebug/);
      await call('mock_game.services.set', { name: 'game.actor_stats', published: false });
      // Historical data remains unchanged until the user explicitly refreshes.
      assert.match(await invoke('inspector.status'), /generation=1.*stats=available/);
      await call('widgets.get', { id: 'inspector.main' });
      assert.match(await invoke('inspector.status'), /generation=1.*stats=available/);
      assert.match(await invoke('inspector.refresh'), /generation=2.*stats=unavailable/);
      await invoke('inspector.stats', [0], 'UNAVAILABLE');
      const panel = await call('widgets.get', { id: 'inspector.main' });
      assert.match(JSON.stringify(panel), /unavailable/);
      await assert.rejects(call('plugins.invoke', { name: 'inspector.inventory', args: ['0'] }),
        error => error.code === -32602);
      await invoke('inspector.inventory', [-1], 'BAD_ARG');
    } else {
      assert.match(await invoke('inspector.status'), /build=unavailable.*stats=unavailable/);
      await invoke('inspector.inventory', [0], 'UNAVAILABLE');
      assert.match(await invoke('inspector.read'), /actor=unavailable/);
    }
    await call('system.shutdown');
    const [code] = await exited;
    assert.equal(code, 0);
    await new Promise((accept, reject) => {
      const probe = connect({ host: '127.0.0.1', port });
      probe.once('connect', () => { probe.destroy(); reject(new Error(`port ${port} remained open`)); });
      probe.once('error', error => error.code === 'ECONNREFUSED' ? accept() : reject(error));
    });
    console.log(`INSPECTOR_HOSTD_PASS cycle=${cycle} synthetic=${mock} shutdown=0 port_closed=true`);
  } catch (error) {
    console.error(log);
    throw error;
  } finally {
    socket?.close();
    for (const entry of pending.values()) clearTimeout(entry.timer);
    if (child.exitCode === null) child.kill();
    await exited;
    lines.close();
  }
}

try {
  await runScenario(true, 1);
  await runScenario(true, 2);
  await runScenario(false, 3);
} finally {
  await rm(work, { recursive: true });
}
