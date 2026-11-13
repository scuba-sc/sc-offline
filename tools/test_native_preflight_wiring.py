#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check Windows-only call-site wiring; portable host behavior has its own test.

This source-level check does not run Windows or establish a native barrier.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'src/dllmain.cpp').read_text()
guard = 'if (!offline_host::NativeActivationAvailable()) return;'

startup_wait = source.split('extern "C" void StartupGateWait() {', 1)[1].split('\n}', 1)[0]
fail_stop = source.split('[[noreturn]] static void StartupGateFailStop(', 1)[1].split('\n}', 1)[0]
gate = source.split('static StartupGateInstallResult InstallStartupGate() {', 1)[1].split('\n}', 1)[0]
progress_thread = source.split('static DWORD WINAPI StartupGateProgressThread(LPVOID) {', 1)[1].split('\n}', 1)[0]
module_retain = source.split('static bool RetainStartupGateModule() {', 1)[1].split('\n}', 1)[0]
assert 'startup_gate_wait::WaitAndRestore(' in startup_wait
assert startup_wait.count('WaitForSingleObject(') == 1, 'only the injectable OS wait adapter may call the Windows wait API'
assert 'bool Restore() const { return sco::hook::WriteCode(g_entry, g_entrySaved, sizeof(g_entrySaved)); }' in startup_wait
completion_check = startup_wait.split('if (outcome !=', 1)[1]
assert startup_wait.index('WaitAndRestore(') < startup_wait.index('if (outcome !=')
assert completion_check.index('StartupGateFailStop(outcome)') >= 0
assert 'GetTickCount64()' in startup_wait and 'kGateProgressIntervalMs' in startup_wait
assert 'g_startupGateProgressMs.store(elapsedMs, std::memory_order_relaxed)' in startup_wait
assert 'Log(' not in startup_wait, 'the deadline thread must not call a potentially blocking logger'
assert 'Log(' in progress_thread and 'WaitForSingleObject(g_startupDone, kGateProgressIntervalMs)' in progress_thread
assert 'startup_gate_wait::InstallGate(operations)' in gate
assert 'StartupGateFailStop(sc_offline::startup_gate_wait::Outcome::SetupFailed)' in gate
assert 'GET_MODULE_HANDLE_EX_FLAG_PIN' in module_retain and 'g_startupGateModulePin' in module_retain
assert 'independent external unload before that pin is acquired' in source
assert 'TerminateProcess(GetCurrentProcess(), exitCode);' in fail_stop
assert fail_stop.index('TerminateProcess(') < fail_stop.index('for (;;) Sleep(INFINITE);'), \
    'fail-stop must remain non-returning if termination returns'
assert 'Log(' not in fail_stop, 'fail-stop must terminate without waiting on synchronous logging'
assert 'Outcome WaitAndRestore(' in (root / 'src/startup_gate_wait.h').read_text()
assert 'InstallResult InstallGate(' in (root / 'src/startup_gate_wait.h').read_text()
assert 'startup_gate_wait_policy' in (root / 'tools/offline-host/CMakeLists.txt').read_text()
assert gate.strip().startswith('if (!offline_host::NativeActivationAvailable()) return StartupGateInstallResult::Refused;'), \
    'entry hook must be refused before native access'
start = source.split('static void StartOffline() {', 1)[1].split('\n}', 1)[0]
assert start.strip().startswith(guard), 'product mutations must be guarded before Prepare'
worker = source.split('static DWORD WINAPI ModThread(LPVOID param) {', 1)[1].split('\n}', 1)[0]
assert 'if (!RetainStartupGateModule())' in worker
assert worker.index('RetainStartupGateModule()') < worker.index('OpenConsole();'), \
    'pin installed gate code before any worker refusal or unload path'
assert worker.index('RetainStartupGateModule()') < worker.index('FreeLibraryAndExitThread')
assert 'Unloading in 5s.' not in worker
assert 'Offline startup is refused; close the game.' in worker
assert worker.index('if (!offline_host::NativeActivationAvailable())') < worker.index('GetModuleHandleW(')
assert worker.index('return 0;', worker.index('if (!offline_host::NativeActivationAvailable())')) < worker.index('StartOffline();')
# StartOffline can return after either admission or refusal; either completed
# attempt releases the held game entry point.
assert 'StartOffline();\n    ReleaseStartupGate();' in worker
dllmain = source.split('BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {', 1)[1]
assert 'ReleaseStartupGate();' not in dllmain, 'worker creation failure must leave an installed gate unsignalled'
assert 'gate != StartupGateInstallResult::NotTarget' in dllmain
assert 'g_startupGateInstalled.store(gate == StartupGateInstallResult::Installed' in dllmain
assert 'CloseHandle(progress)' in dllmain and 'StartupGateProgressThread' in dllmain
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
assert source.count('const auto gate = InstallStartupGate();') == 1
assert source.count('StartOffline();') == 1
print('Windows native preflight call-site wiring: PASS (static)')
