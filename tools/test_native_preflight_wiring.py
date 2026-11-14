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
gate = source.split('static StartupGateInstallResult InstallStartupGate(HMODULE self, LPVOID attachReserved) {', 1)[1].split('\n}', 1)[0]
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
assert 'only installed for a static-import load' in source and 'defense in depth' in source
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
# The module is never unloaded: the anti-cheat refusal returns after releasing the gate.
refusal = worker.split('if (InspectAntiCheat() != AcCheck::Clear) {', 1)[1].split('Log("[app] anti-cheat inspection clear', 1)[0]
assert refusal.index('ReleaseStartupGate();') < refusal.index('return 0;')
assert 'Sleep(5000)' not in refusal and 'FreeLibrary' not in refusal
assert worker.index('RetainStartupGateModule()') < worker.index('if (InspectAntiCheat() != AcCheck::Clear)')
import re
for src_file in sorted((root / 'src').rglob('*')):
    if src_file.suffix in ('.cpp', '.h') and 'third_party' not in src_file.parts:
        assert not re.search(r'\bFreeLibrary(AndExitThread)?\s*\(', src_file.read_text(errors='replace')), \
            f'{src_file.name}: the product module must never be unloaded'
assert 'Unloading in 5s.' not in worker
assert 'Offline startup is refused; close the game.' in worker
assert worker.index('if (!offline_host::NativeActivationAvailable())') < worker.index('GetModuleHandleW(')
assert worker.index('return 0;', worker.index('if (!offline_host::NativeActivationAvailable())')) < worker.index('StartOffline();')
# StartOffline can return after either admission or refusal; either completed
# attempt releases the held game entry point.
assert 'StartOffline();\n    ReleaseStartupGate();' in worker
dllmain = source.split('BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {', 1)[1]
assert 'ReleaseStartupGate();' not in dllmain, 'worker creation failure must leave an installed gate unsignalled'
assert 'g_startupGateInstalled.store(gate == StartupGateInstallResult::Installed' in dllmain
dispatch = dllmain.split('switch (gate) {', 1)[1].split('\n        }', 1)[0]
refused = dispatch.split('case StartupGateInstallResult::Refused:', 1)[1].split(
    'case StartupGateInstallResult::RefusedLoadMode:', 1)[0]
load_mode_refused = dispatch.split('case StartupGateInstallResult::RefusedLoadMode:', 1)[1].split(
    'case StartupGateInstallResult::NotTarget:', 1)[0]
not_target = dispatch.split('case StartupGateInstallResult::NotTarget:', 1)[1].split(
    'case StartupGateInstallResult::Installed:', 1)[0]
installed = dispatch.split('case StartupGateInstallResult::Installed:', 1)[1]
assert 'OutputDebugStringA(' in refused and 'no worker started' in refused
assert 'CreateThread(' not in refused and 'Log(' not in refused, \
    'refusal diagnostics under the loader lock must not schedule DLL code or enter the logger'
assert 'return TRUE;' in refused and 'return TRUE;' in not_target
assert 'OutputDebugStringA(' in load_mode_refused and 'return TRUE;' in load_mode_refused
assert 'CreateThread(' not in load_mode_refused and 'Log(' not in load_mode_refused,     'a refused load mode must publish nothing and must not enter the logger under the loader lock'
# A dynamic or non-imported load is refused before any entry patch, stub, event or thread.
assert 'startup_gate_load_mode::Evaluate(' in gate
load_mode_at = gate.index('startup_gate_load_mode::Evaluate(')
assert 'StartupGateInstallResult::RefusedLoadMode' in gate[load_mode_at:]
assert gate.index('StartupGateInstallResult::NotTarget') < load_mode_at
assert load_mode_at < gate.index('startup_gate_wait::InstallGate(operations)')
assert load_mode_at < gate.index('AllocateNear(')
assert load_mode_at < gate.index('WriteCode(entry')
assert 'Log(' not in gate and 'CreateThread(' not in gate
imports_helper = source.split('static bool ProcessImageImportsModule(HMODULE self) {', 1)[1].split('\n}', 1)[0]
for forbidden in ('Log(', 'CreateThread(', 'CreateEvent', '= new ', 'make_unique', 'malloc(', 'std::string', 'std::vector', 'LoadLibrary'):
    assert forbidden not in imports_helper, f'loader-lock import check must not use {forbidden}'
assert 'startup_gate_load_mode::ImportTableNames(' in imports_helper
load_mode_header = (root / 'src/startup_gate_load_mode.h').read_text()
assert 'Decision Evaluate(' in load_mode_header
assert 'startup_gate_load_mode_policy' in (root / 'tools/offline-host/CMakeLists.txt').read_text()
assert 'module_lifetime_static_import' in (root / 'tools/offline-host/CMakeLists.txt').read_text()
assert 'module_lifetime_dynamic_load' in (root / 'tools/offline-host/CMakeLists.txt').read_text()
assert 'CreateThread(' not in not_target and 'CreateThread(' not in installed
assert 'break;' in installed, 'only the installed gate may continue to thread creation'
after_dispatch = dllmain.split('\n        }', 1)[1]
assert 'CloseHandle(progress)' in after_dispatch and 'StartupGateProgressThread' in after_dispatch
assert after_dispatch.index('StartupGateProgressThread') < after_dispatch.index('CreateThread(nullptr, 0, ModThread')
assert 'if (gate != StartupGateInstallResult::NotTarget)' not in dllmain, \
    'only an installed gate may reach asynchronous worker creation'
# The core logger stores an atomic borrowed function pointer and has no in-flight
# callback drain. Do not publish the DLL callback in DllMain or before ModThread's
# fail-closed and early-unload paths.
dllmain = source.split('BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {', 1)[1]
assert 'SetLogSink(ForwardCoreLog)' not in dllmain, 'do not publish a callback from DllMain'
sink = worker.index('sco::SetLogSink(ForwardCoreLog)')
assert sink > worker.index('if (!offline_host::NativeActivationAvailable())')
assert sink > worker.index('if (InspectAntiCheat() != AcCheck::Clear)'), 'early refusal path must precede sink registration'
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
assert source.count('const auto gate = InstallStartupGate(hModule, reserved);') == 1
assert source.count('StartOffline();') == 1
# Process-retained activation rule: available only when BOTH macros are defined (retain until exit,
# stop is closure). Any other Core keeps the refusal.
policy = (root / 'src/native_activation_policy.h').read_text()
available = policy.split('inline bool NativeActivationAvailable() noexcept {', 1)[1].split('\n}', 1)[0]
assert '#if defined(SCO_CORE_PROCESS_RETAINED_COMPOSITION) && defined(SCO_CORE_NON_DRAINING_STOP_API)' in available
assert available.index('return true;') < available.index('#else') < available.index('return false;')
assert 'return false; }' not in policy, 'activation is no longer unconditionally false'
assert '#error "ASOP composition lacks a reviewed nonblocking quit and stop/drain contract"' in policy
host_cpp = (root / 'src/offline_host.cpp').read_text()
assert 'options.composition = gh::Composition::ProcessRetained;' in host_cpp
assert 'if (!options.platform.gameReadersQuiesced) return sco::Result::BadArg;' in host_cpp
# Install-quiescence attestation: gate-held, or the admitted game-thread start window; nothing else.
attest = source.split('static bool InstallQuiescenceAttested(void*) noexcept {', 1)[1].split('\n}', 1)[0]
assert 'g_startupGateInstalled.load' in attest and 'g_startupGateReleased.load' in attest
# Case (ii): the thread that opened the product's own start window; no equality with the gate thread.
assert 'g_hostStartThreadId.load' in attest and 'GetCurrentThreadId()' in attest
assert 'g_gateThreadId' not in attest, 'case (ii) must not require the gate thread'
assert attest.rstrip().endswith('return false;')
assert 'pf.gameReadersQuiesced = InstallQuiescenceAttested;' in source
assert source.index('pf.gameReadersQuiesced = InstallQuiescenceAttested;') < source.index('options.platform = std::move(pf);')
release = source.split('static void ReleaseStartupGate() {', 1)[1].split('\n}', 1)[0]
assert release.index('g_startupGateReleased.store(true') < release.index('SetEvent(g_startupDone)')
assert 'g_gateThreadId.store(GetCurrentThreadId()' in startup_wait
start_host = source.split('static void StartHostKit() {', 1)[1].split('\n}', 1)[0]
# RAII window: opened by the guard's constructor, closed by its destructor on every exit path.
guard = start_host.split('struct StartWindow {', 1)[1].split('};', 1)[0]
assert 'g_hostStartThreadId.store(GetCurrentThreadId()' in guard and '~StartWindow()' in guard
assert 'g_hostStartThreadId.store(0' in guard.split('~StartWindow()', 1)[1]
assert start_host.index('StartWindow window;') < start_host.index('offline_host::Start()')
assert start_host.count('g_hostStartThreadId.store') == 2, 'only the guard touches the window'
assert source.count('g_hostStartThreadId.store(GetCurrentThreadId()') == 1, 'only StartHostKit opens the window'
assert 'start-thread==gate-thread' in start_host
assert '[startup] host start failed permanently: the game continues with the offline patches but without plugins, services or menu; close the game' in start_host
# Game-thread stop paths run Core's closure through StopHostKit; ModThread and DllMain never do.
tick = source.split('static void OnMainThreadTick() {', 1)[1].split('\n}', 1)[0]
assert 'StopHostKit("owner admission closed")' in tick and 'StopHostKit("tick admission lost")' in tick
assert 'RequestLifecycleStop(' not in tick
stop_host = source.split('static void StopHostKit(const char* trigger) {', 1)[1].split('\n}', 1)[0]
assert stop_host.index('RequestLifecycleStop(trigger)') < stop_host.index('offline_host::Stop()')
assert source.count('offline_host::Stop()') == 1
assert 'offline_host::Stop()' not in worker and 'offline_host::Stop()' not in dllmain
# Tick result: stop only when no longer Running; a non-Ok tick while Running skips the rest of the tick.
assert 'const sco::Result tick = offline_host::Tick(now);' in tick
assert tick.index('if (!offline_host::Running())') < tick.index('if (tick != sco::Result::Ok)')
skip = tick.split('if (tick != sco::Result::Ok) {', 1)[1].split('\n    }', 1)[0]
assert 'return;' in skip and 'StopHostKit' not in skip and 'skipLogged' in skip
assert tick.index('if (tick != sco::Result::Ok)') < tick.index('Menu_GameThreadFrame();')
assert tick.index('if (tick != sco::Result::Ok)') < tick.index('RunLegacyTicks(now)')
start_offline = source.split('static void StartOffline() {', 1)[1].split('\n}', 1)[0]
assert 'host stays prepared: no plugins or menu will load' in start_offline
# The post-stop wait for hook callbacks is bounded; the existing bounded drains still follow it.
assert 'constexpr DWORD kHookDrainWaitMs = 5000;' in controller
assert controller.index('kHookDrainWaitMs') < controller.index('DrainRequestedStop(')
assert 'WaitForSingleObject(g_hookCallbacksDrainedEvent, slice)' in controller
assert 'WaitForSingleObject(g_hookCallbacksDrainedEvent, 1000)' not in controller
print('Windows native preflight call-site wiring: PASS (static)')
