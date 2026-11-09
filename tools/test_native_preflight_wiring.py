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
# The core logger stores an atomic borrowed function pointer and has no in-flight
# callback drain. Do not publish the DLL callback in DllMain or before ModThread's
# fail-closed and early-unload paths.
dllmain = source.split('BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {', 1)[1]
assert 'SetLogSink(ForwardCoreLog)' not in dllmain, 'do not publish a callback from DllMain'
sink = worker.index('sco::SetLogSink(ForwardCoreLog)')
assert sink > worker.index('if (!offline_host::NativeActivationAvailable())')
assert sink > worker.index('FreeLibraryAndExitThread'), 'early unload path must precede sink registration'
assert sink > worker.index('if (InspectAntiCheat() != AcCheck::Clear)')
assert sink < worker.index('StartOffline();'), 'install before the first core host operation'
# The product request may run in a hook callback, so it must only request.
request = (root / 'src/offline_host.cpp').read_text().split('sco::game::host::HostReport RequestStop() noexcept {', 1)[1].split('\nOwnerDrain DrainRequestedStop', 1)[0]
assert 'drain_until' not in request and 'gh::Stop(' not in request
callback = source.split('static LRESULT CALLBACK GetMsgProc(int code, WPARAM wp, LPARAM lp) {', 1)[1].split('\n}', 1)[0]
assert callback.count('CallNextHookEx(nullptr, code, wp, lp)') == 2
assert 'CallNextHookEx(g_msgHook' not in callback
hook_request = source.split('static void RequestLifecycleStop(const char* trigger) {', 1)[1].split('\n}', 1)[0]
assert 'DrainRequestedStop' not in hook_request and 'Menu_Drain' not in hook_request
controller = source.split('static void RunMainThreadService() {', 1)[1].split('\n}', 1)[0]
assert controller.index('ClosedAndDrained()') < controller.index('DrainRequestedStop(')
assert source.count('InstallStartupGate();') == 1
assert source.count('StartOffline();') == 1
print('Windows native preflight call-site wiring: PASS (static)')
