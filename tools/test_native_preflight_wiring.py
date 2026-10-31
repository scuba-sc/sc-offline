#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check Windows-only call-site wiring; portable host behavior has its own test.

This source-level check does not run Windows or establish a native barrier.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'src/dllmain.cpp').read_text()
guard = 'if (!offline_host::NativeActivationAvailable()) return;'

gate = source.split('static void InstallStartupGate() {', 1)[1].split('\n}', 1)[0]
assert gate.strip().startswith(guard), 'entry hook must be guarded before native access'
start = source.split('static void StartOffline() {', 1)[1].split('\n}', 1)[0]
assert start.strip().startswith(guard), 'product mutations must be guarded before Prepare'
worker = source.split('static DWORD WINAPI ModThread(LPVOID param) {', 1)[1].split('\n}', 1)[0]
assert worker.index('if (!offline_host::NativeActivationAvailable())') < worker.index('GetModuleHandleW(')
assert worker.index('return 0;', worker.index('if (!offline_host::NativeActivationAvailable())')) < worker.index('StartOffline();')
assert source.count('InstallStartupGate();') == 1
assert source.count('StartOffline();') == 1
print('Windows native preflight call-site wiring: PASS (static)')
