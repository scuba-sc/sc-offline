#include <windows.h>
#include <tlhelp32.h>
#include <shlwapi.h>
#include "common.h"
#include "patches.h"
#include "hooks.h"
#include "teleport.h"
#include "spawner.h"
#include "feature_caps.h"
#include "npc.h"
#include "build.h"
#include "cvars.h"
#include "missions.h"
#include "contracts.h"
#include "travel.h"
#include "version.h"
#include "native_activation_policy.h"
#include "startup_gate_wait.h"
#include "startup_gate_load_mode.h"
#ifdef SCO_ASOP_HOST_COMPOSITION
#include "asop_shell.h"
#else
#include "services.h"
#include "fleet.h"
#endif
#include "menu.h"
#include "menu_worker.h"
#include "menu_callback_admission.h"
#include "hotkeys.h"
#include "builtins/builtins.h"
#include "sco/app.h"
#include "sco/caps.h"
#include "sco/log.h"
#include "sco/scan.h"
#include "sco/signatures.h"
#include "sco/signature_registry.h"
#include "sco/hook.h"
#ifdef SCO_OFFLINE_G1_HOST
#include "offline_host.h"
#include "admission_probe.h"
#include "start_retry.h"
#include "stop_channel.h"
#include <mutex>
#include <cstdio>
#endif
#include "sco/runtime.h"
#ifdef SCO_OFFLINE_RELOCATION_SWITCH
#include "sco/game/services.h"
#endif
#include <sc_game_events.h>
#include "sco_lua.h"
#include "sco_lua_game.h"   // sco.datacore, sco.game.* and the game.* payloads for Lua scripts
#include <filesystem>
#include <cstring>
#include <iterator>
#include <atomic>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")

#pragma comment(linker, "/export:DirectInput8Create=C:\\Windows\\System32\\dinput8.DirectInput8Create,@1")

static const wchar_t* kTargetModule = L"StarCitizen.exe";

// Startup gate. The offline patches must land before the game's own startup reaches its EAC
// handshake (an unpatched game exits a fraction of a second after it starts). G1's Prepare may not
// run under the loader lock, so it runs on ModThread; the game's main thread is held at the exe's
// entry point until ModThread has finished StartOffline (admitted or refused). If completion is not
// confirmed, or restoring the original entry bytes fails, the waiting thread fail-stops the process.
// The entry starts with a call rel32, which InstallDetour can't relocate, so the gate writes its own
// 12-byte absolute jump and puts the original bytes back before the entry runs. Only the main thread
// exists at the entry, so the restore can't race another thread.
static HANDLE g_startupDone = nullptr;
// ModThread pins this module before any refusal path, so the gate and reporter
// code remain backed even if its ordinary reference is dropped. Nothing ever
// unloads the module: it stays mapped until the process exits.
static HMODULE g_startupGateModulePin = nullptr;
static std::atomic<bool> g_startupGateInstalled{false};
// Set by ReleaseStartupGate() before the event is signalled: the game's entry point is no longer parked.
static std::atomic<bool> g_startupGateReleased{false};
// Thread id of the game's main thread, recorded when it enters the gate at the entry point (0 until then).
static std::atomic<DWORD> g_gateThreadId{0};
// Thread id inside the product's own StartHostKit() -> offline_host::Start() window, else 0.
static std::atomic<DWORD> g_hostStartThreadId{0};
static std::atomic<unsigned> g_quiesceGateHeld{0}, g_quiesceAdmittedStart{0}, g_quiesceRefused{0};
static uint8_t* g_entry = nullptr;
static uint8_t g_entrySaved[12];
static std::atomic<uint64_t> g_startupGateProgressMs{0};
static constexpr DWORD kGateTimeoutMs = 180000;
static constexpr DWORD kGateProgressIntervalMs = 5000;

static void ReleaseStartupGate() {
    g_startupGateReleased.store(true, std::memory_order_release);
    if (g_startupDone) SetEvent(g_startupDone);
}

#ifdef SCO_CORE_PROCESS_RETAINED_COMPOSITION
// Core's install-quiescence attestation (Platform::gameReadersQuiesced): it calls this immediately
// before each native mutation (quit hook and pak hook in Prepare; game-service hooks in Start) and
// refuses on false. Only atomics are read; it never blocks, allocates or throws. True in exactly two
// situations, anything else is false:
//
// (i) Gate held. The startup gate is installed and not yet released, so the game's main thread is
//     still parked at the executable's entry point. This is real quiescence of the executable's
//     code: no game code of the executable has run yet. It says nothing about threads the loader or
//     other modules started before the entry point (ModThread logs the thread count at the gate).
//
// (ii) Admitted start. The caller is the thread on which the product itself opened the start window,
//     inside its own StartHostKit() -> offline_host::Start() call (the message-hook thread). It is
//     not required to equal the gate thread: nothing proves they are the same thread, and the first
//     Start attempt logs whether they are. This is NOT quiescence: other engine threads keep running
//     while the game services install their hooks on this thread. It matches how the last shipped
//     versions started their services and is a known, documented deviation from the
//     install-quiescence rule, to be revisited after in-game evidence. It admits no other thread or
//     time: the window is opened and closed (RAII) only around that one call.
static bool InstallQuiescenceAttested(void*) noexcept {
    if (g_startupGateInstalled.load(std::memory_order_acquire) &&
        !g_startupGateReleased.load(std::memory_order_acquire)) {
        g_quiesceGateHeld.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    const DWORD self = GetCurrentThreadId();
    if (g_hostStartThreadId.load(std::memory_order_acquire) == self) {
        g_quiesceAdmittedStart.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    g_quiesceRefused.fetch_add(1, std::memory_order_relaxed);
    return false;
}
#endif

[[noreturn]] static void StartupGateFailStop(sc_offline::startup_gate_wait::Outcome outcome) {
    DWORD exitCode = 0xC0DE0004;
    switch (outcome) {
    case sc_offline::startup_gate_wait::Outcome::TimedOut:
        exitCode = 0xC0DE0001; break;
    case sc_offline::startup_gate_wait::Outcome::WaitFailed:
        exitCode = 0xC0DE0002; break;
    case sc_offline::startup_gate_wait::Outcome::RestoreFailed:
        exitCode = 0xC0DE0003; break;
    case sc_offline::startup_gate_wait::Outcome::SetupFailed:
    case sc_offline::startup_gate_wait::Outcome::Completed:
        break;
    }
    TerminateProcess(GetCurrentProcess(), exitCode);
    // TerminateProcess normally does not return on success. If it fails or returns,
    // keep this thread parked at the entry stub so game startup cannot continue.
    for (;;) Sleep(INFINITE);
}

extern "C" void StartupGateWait() {
    g_gateThreadId.store(GetCurrentThreadId(), std::memory_order_release); // game main thread, for the attestation
    struct Operations {
        uint64_t NowMs() const { return GetTickCount64(); }
        sc_offline::startup_gate_wait::WaitResult Wait(uint64_t timeoutMs) const {
            const DWORD timeout = timeoutMs > MAXDWORD ? MAXDWORD : static_cast<DWORD>(timeoutMs);
            const DWORD result = WaitForSingleObject(g_startupDone, timeout);
            if (result == WAIT_OBJECT_0) return sc_offline::startup_gate_wait::WaitResult::Signalled;
            if (result == WAIT_TIMEOUT) return sc_offline::startup_gate_wait::WaitResult::TimedOut;
            return sc_offline::startup_gate_wait::WaitResult::Failed;
        }
        bool Restore() const { return sco::hook::WriteCode(g_entry, g_entrySaved, sizeof(g_entrySaved)); }
        void Progress(uint64_t elapsedMs, uint64_t) const {
            g_startupGateProgressMs.store(elapsedMs, std::memory_order_relaxed);
        }
        void FailStop(sc_offline::startup_gate_wait::Outcome outcome) const { StartupGateFailStop(outcome); }
    } operations;

    const auto outcome = sc_offline::startup_gate_wait::WaitAndRestore(
        operations, kGateTimeoutMs, kGateProgressIntervalMs);
    if (outcome != sc_offline::startup_gate_wait::Outcome::Completed)
        StartupGateFailStop(outcome);
}

static DWORD WINAPI StartupGateProgressThread(LPVOID) {
    for (;;) {
        const DWORD waited = WaitForSingleObject(g_startupDone, kGateProgressIntervalMs);
        if (waited == WAIT_OBJECT_0) return 0;
        if (waited != WAIT_TIMEOUT) return 0;
        const auto elapsedMs = g_startupGateProgressMs.load(std::memory_order_relaxed);
        Log("[app] startup gate: waiting for initialization (%llu/%lu ms)",
            static_cast<unsigned long long>(elapsedMs), kGateTimeoutMs);
    }
}

enum class StartupGateInstallResult { Refused, RefusedLoadMode, NotTarget, Installed };

// Runs under the loader lock: no allocation, logging or thread creation. Reports whether the
// process executable's in-memory import directory names this module's file.
static bool ProcessImageImportsModule(HMODULE self) {
    wchar_t modulePath[MAX_PATH];
    const DWORD length = GetModuleFileNameW(self, modulePath, MAX_PATH);
    if (!length || length >= MAX_PATH) return false;
    const wchar_t* baseName = PathFindFileNameW(modulePath);
    char narrow[MAX_PATH];
    size_t count = 0;
    for (; baseName[count]; ++count) {
        if (baseName[count] > 0x7F) return false;
        narrow[count] = static_cast<char>(baseName[count]);
    }
    narrow[count] = '\0';

    auto* image = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    if (!image) return false;
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const DWORD imageSize = nt->OptionalHeader.SizeOfImage;
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    return sc_offline::startup_gate_load_mode::ImportTableNames(
        image, imageSize, directory.VirtualAddress, directory.Size, narrow);
}

static StartupGateInstallResult InstallStartupGate(HMODULE self, LPVOID attachReserved) {
    if (!offline_host::NativeActivationAvailable()) return StartupGateInstallResult::Refused;
#ifndef SCO_OFFLINE_G1_HOST
    return StartupGateInstallResult::Refused;
#endif
    if (!offline_host::ProductControlStopContractAvailable()) return StartupGateInstallResult::Refused;
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    wchar_t path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!base || !length || length >= MAX_PATH || _wcsicmp(PathFindFileNameW(path), kTargetModule) != 0)
        return StartupGateInstallResult::NotTarget;
    // Only a static import is retained by the loader before DllMain; refuse anything else
    // before any patch, event, thread or callback is published.
    struct LoadModeOperations {
        HMODULE self;
        bool ProcessImageImportsModule() { return ::ProcessImageImportsModule(self); }
    } loadModeOperations{self};
    if (sc_offline::startup_gate_load_mode::Evaluate(
            sc_offline::startup_gate_load_mode::FromAttachReserved(attachReserved), loadModeOperations) !=
        sc_offline::startup_gate_load_mode::Decision::Retained)
        return StartupGateInstallResult::RefusedLoadMode;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) StartupGateFailStop(sc_offline::startup_gate_wait::Outcome::SetupFailed);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || !nt->OptionalHeader.AddressOfEntryPoint)
        StartupGateFailStop(sc_offline::startup_gate_wait::Outcome::SetupFailed);
    uint8_t* entry = base + nt->OptionalHeader.AddressOfEntryPoint;
    struct Operations {
        uint8_t* entry;
        uint8_t* stub = nullptr;
        bool AllocateStub() {
            stub = sco::hook::AllocateNear(entry, 64);
            return stub != nullptr;
        }
        bool CreateCompletionEvent() {
            g_startupDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            return g_startupDone != nullptr;
        }
        bool WriteEntryJump() {
            // push rcx; sub rsp,0x20; mov rax,StartupGateWait; call rax; add rsp,0x20; pop rcx; mov rax,entry; jmp rax
            // (rsp is 8 mod 16 at the entry; push makes it 16-aligned for the call; rcx carries the PEB argument.)
            const uint64_t waitFn = reinterpret_cast<uint64_t>(&StartupGateWait), entryAddr = reinterpret_cast<uint64_t>(entry);
            uint8_t code[36] = { 0x51, 0x48, 0x83, 0xEC, 0x20, 0x48, 0xB8 };
            std::memcpy(code + 7, &waitFn, 8);
            const uint8_t mid[] = { 0xFF, 0xD0, 0x48, 0x83, 0xC4, 0x20, 0x59, 0x48, 0xB8 };
            std::memcpy(code + 15, mid, sizeof(mid));
            std::memcpy(code + 24, &entryAddr, 8);
            code[32] = 0xFF; code[33] = 0xE0;
            std::memcpy(stub, code, 34);
            if (!FlushInstructionCache(GetCurrentProcess(), stub, 34)) return false;
            uint8_t jump[12] = { 0x48, 0xB8 };   // mov rax, stub; jmp rax
            const uint64_t stubAddr = reinterpret_cast<uint64_t>(stub);
            std::memcpy(jump + 2, &stubAddr, 8);
            jump[10] = 0xFF; jump[11] = 0xE0;
            std::memcpy(g_entrySaved, entry, sizeof(g_entrySaved));
            g_entry = entry;
            return sco::hook::WriteCode(entry, jump, sizeof(jump));
        }
        void FailStop(sc_offline::startup_gate_wait::Outcome outcome) {
            StartupGateFailStop(outcome);
        }
    } operations{entry};

    if (sc_offline::startup_gate_wait::InstallGate(operations) !=
        sc_offline::startup_gate_wait::InstallResult::Installed)
        StartupGateFailStop(sc_offline::startup_gate_wait::Outcome::SetupFailed);
    return StartupGateInstallResult::Installed;
}

static bool RetainStartupGateModule() {
    if (!g_startupGateInstalled.load(std::memory_order_acquire)) return true;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&StartupGateWait), &g_startupGateModulePin) != FALSE;
}

// The gate is only installed for a static-import load, whose loader retention
// precedes DllMain, so nothing DLL-resident is published while the module could be
// released. A dynamically loaded copy is refused before publication. The
// process-lifetime pin acquired at ModThread entry (avoiding a loader-lock call from
// DllMain) is kept as defense in depth.

// Read-only anti-cheat inspection; no service/process/module state is modified.
// Unknown is a refusal, including access-denied, partial enumeration and query errors.
enum class AcCheck { Unknown, Clear, Present };
static AcCheck InspectAntiCheat() {
    bool unknown = false;
    HANDLE modules = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (modules == INVALID_HANDLE_VALUE) unknown = true;
    else {
        MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
        if (!Module32FirstW(modules, &entry)) unknown = true;
        else do {
            if (StrStrIW(entry.szModule, L"EasyAntiCheat")) { CloseHandle(modules); return AcCheck::Present; }
        } while (Module32NextW(modules, &entry));
        if (GetLastError() != ERROR_NO_MORE_FILES) unknown = true;
        CloseHandle(modules);
    }
    HANDLE processes = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (processes == INVALID_HANDLE_VALUE) unknown = true;
    else {
        PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
        if (!Process32FirstW(processes, &entry)) unknown = true;
        else do {
            if (StrStrIW(entry.szExeFile, L"EACLauncher") || StrStrIW(entry.szExeFile, L"EasyAntiCheat") ||
                StrStrIW(entry.szExeFile, L"start_protected_game")) {
                CloseHandle(processes); return AcCheck::Present;
            }
        } while (Process32NextW(processes, &entry));
        if (GetLastError() != ERROR_NO_MORE_FILES) unknown = true;
        CloseHandle(processes);
    }
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) unknown = true;
    else {
        const wchar_t* names[] = {L"EasyAntiCheat", L"EasyAntiCheat_EOS", L"EasyAntiCheatEOS"};
        for (const wchar_t* name : names) {
            SC_HANDLE service = OpenServiceW(manager, name, SERVICE_QUERY_STATUS);
            if (!service) { if (GetLastError() != ERROR_SERVICE_DOES_NOT_EXIST) unknown = true; continue; }
            SERVICE_STATUS status{};
            const bool ok = QueryServiceStatus(service, &status) != FALSE;
            CloseServiceHandle(service);
            if (!ok) unknown = true;
            else if (status.dwCurrentState != SERVICE_STOPPED) { CloseServiceHandle(manager); return AcCheck::Present; }
        }
        CloseServiceHandle(manager);
    }
    return unknown ? AcCheck::Unknown : AcCheck::Clear;
}

// Startup/shutdown instrumentation for mod.log. Counts and names only, never addresses. Called from
// ModThread or the game thread, never from DllMain or the gate wait.
static ULONGLONG g_modThreadStartMs = 0;

// Threads of this process, by a Toolhelp32 snapshot; -1 when the snapshot is unavailable.
static long ProcessThreadCount() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return -1;
    THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
    const DWORD pid = GetCurrentProcessId();
    long count = 0;
    if (Thread32First(snapshot, &entry)) {
        do { if (entry.th32OwnerProcessID == pid) ++count; } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return count;
}

#ifdef SCO_OFFLINE_G1_HOST
static const char* PhaseName(sco::game::host::Phase phase) {
    using P = sco::game::host::Phase;
    switch (phase) {
    case P::Idle: return "Idle";
    case P::Preparing: return "Preparing";
    case P::Prepared: return "Prepared";
    case P::Activating: return "Activating";
    case P::Activated: return "Activated";
    case P::Starting: return "Starting";
    case P::Running: return "Running";
    case P::Stopping: return "Stopping";
    case P::Stopped: return "Stopped";
    case P::Failed: return "Failed";
    }
    return "?";
}

// session/officialNetwork come from the live OS reading in admission_evidence.h / admission_probe.cpp
// (firewall rule store + this process's TCP table). Launcher flags, IsOnline patch success and EAC
// absence are NOT evidence. Every decision lives there; this file only maps Tri onto the host's enums.
using admission::Tri;

static Tri InspectTargetProcess() {
    wchar_t path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return Tri::Unknown;
    return _wcsicmp(PathFindFileNameW(path), kTargetModule) == 0 ? Tri::Clear : Tri::Present;
}

// What the last probe saw, for the [g1] evidence line only.
struct ProbeDetail {
    long profiles = 0;
    unsigned long long rules = 0, ms = 0;
    bool exeOk = false;
    long fwHr = 0, tcpError = 0;
    const char* fwStage = "";
    const char* tcpStage = "";
};
static std::mutex g_probeDetailLock;
static ProbeDetail g_probeDetail;

static admission::Evidence ProbeEvidence() {
    const ULONGLONG start = GetTickCount64();
    const AcCheck ac = InspectAntiCheat();
    const Tri antiCheat = ac == AcCheck::Clear ? Tri::Clear : ac == AcCheck::Present ? Tri::Present : Tri::Unknown;
    std::wstring exe;
    const bool exeOk = admission::CurrentExePath(exe);
    if (!exeOk) exe.clear(); // empty path: OfficialNetwork() answers Unknown
    admission::FwSnapshot fw;
    admission::ProbeFirewall(exe, fw);
    admission::TcpSnapshot tcp;
    admission::ProbeTcpTable(tcp);
    // Launcher intent narrows acceptable firewall gaps, never replaces live proof.
    wchar_t allow[256]{};
    SetLastError(ERROR_SUCCESS);
    const DWORD n = GetEnvironmentVariableW(L"SC_OFFLINE_MULTIPLAYER_ALLOW", allow, 256);
    const DWORD error = GetLastError();
    const bool readable = n < 256 && (n != 0 || error == ERROR_SUCCESS || error == ERROR_ENVVAR_NOT_FOUND);
    const std::wstring configuredAllow = readable ? std::wstring(allow, n) : L"invalid";
    const admission::Evidence ev =
        admission::Decide(antiCheat, InspectTargetProcess(), fw, exe, tcp, GetCurrentProcessId(), nullptr,
                          configuredAllow);
    std::lock_guard<std::mutex> lock(g_probeDetailLock);
    g_probeDetail = ProbeDetail{fw.activeProfiles, fw.rulesSeen, GetTickCount64() - start, exeOk,
                                fw.hr, tcp.error, fw.stage, tcp.stage};
    return ev;
}

// D5: positive-only, 1000 ms, monotonic clock. Refusals are never cached.
static admission::EvidenceCache g_evidence([] { return static_cast<uint64_t>(GetTickCount64()); }, ProbeEvidence);

static void InvalidateEvidence() { g_evidence.Invalidate(); }

static sco::game::host::Check ToCheck(Tri t) {
    namespace gh = sco::game::host;
    return t == Tri::Clear ? gh::Check::Clear : t == Tri::Present ? gh::Check::Present : gh::Check::Unknown;
}
static sco::game::host::Session ToSession(Tri t) {
    namespace gh = sco::game::host;
    return t == Tri::Clear ? gh::Session::Offline : t == Tri::Present ? gh::Session::Online : gh::Session::Unknown;
}
static const char* CheckName(Tri t) { return t == Tri::Clear ? "Clear" : t == Tri::Present ? "Present" : "Unknown"; }
static const char* SessionName(Tri t) { return t == Tri::Clear ? "Offline" : t == Tri::Present ? "Online" : "Unknown"; }

// One line per change of the evidence tuple, never per call.
static void LogEvidenceIfChanged(const admission::Evidence& ev) {
    static std::mutex lock;
    static bool logged = false;
    static admission::Evidence last;
    std::lock_guard<std::mutex> guard(lock);
    if (logged && last.session == ev.session && last.officialNetwork == ev.officialNetwork &&
        last.antiCheat == ev.antiCheat && last.targetProcess == ev.targetProcess)
        return;
    logged = true;
    last = ev;
    ProbeDetail d;
    {
        std::lock_guard<std::mutex> detail(g_probeDetailLock);
        d = g_probeDetail;
    }
    char failure[192] = "";
    int used = 0;
    if (d.fwHr && used >= 0 && used < static_cast<int>(sizeof(failure)))
        used += std::snprintf(failure + used, sizeof(failure) - used, " firewall_hr=0x%08lX(%s)",
                              static_cast<unsigned long>(d.fwHr), d.fwStage);
    if (d.tcpError && used >= 0 && used < static_cast<int>(sizeof(failure)))
        used += std::snprintf(failure + used, sizeof(failure) - used, " tcp_error=%ld(%s)", d.tcpError, d.tcpStage);
    if (!d.exeOk && used >= 0 && used < static_cast<int>(sizeof(failure)))
        std::snprintf(failure + used, sizeof(failure) - used, " exe_path=unavailable");
    Log("[g1] evidence: session=%s officialNetwork=%s antiCheat=%s target=%s profiles=%ld rules=%llu probe_ms=%llu%s",
        SessionName(ev.session), CheckName(ev.officialNetwork), CheckName(ev.antiCheat), CheckName(ev.targetProcess),
        d.profiles, d.rules, d.ms, failure);
}

// [startup] 1/8, once: which readings were not Clear, by name only (later changes: the [g1] line).
static void LogAdmissionMilestone(const admission::Evidence& ev) {
    static std::atomic<bool> logged{false};
    if (logged.exchange(true)) return;
    char names[64] = "";
    size_t used = 0;
    const auto add = [&](Tri t, const char* name) {
        if (t == Tri::Clear) return;
        used += static_cast<size_t>(std::snprintf(names + used, sizeof(names) - used, "%s%s", used ? ", " : "", name));
    };
    add(ev.session, "session");
    add(ev.antiCheat, "anti-cheat");
    add(ev.officialNetwork, "network");
    add(ev.targetProcess, "target");
    if (used) Log("[startup] 1/8 admission inspection: not clear: %s", names);
    else Log("[startup] 1/8 admission inspection: all clear");
}

static sco::game::host::Admission InspectOfflineEnvironment() {
    const admission::Evidence ev = g_evidence.Get(InspectTargetProcess()); // target read fresh every call
    LogEvidenceIfChanged(ev);
    LogAdmissionMilestone(ev);
    sco::game::host::Admission evidence;
    evidence.session = ToSession(ev.session);
    evidence.officialNetwork = ToCheck(ev.officialNetwork);
    evidence.antiCheat = ToCheck(ev.antiCheat);
    evidence.targetProcess = ToCheck(ev.targetProcess);
    return evidence;
}
#endif

static bool g_offline = false;
static bool g_signaturesResolved = false;
static const char* g_quitHookStatus = nullptr;   // "[app] game quit hook: ..." in LogStartup

static bool PrepareHostKit();

static void StartOffline() {
    if (!offline_host::NativeActivationAvailable()) return;
    if (!offline_host::ProductControlStopContractAvailable()) return;
    // sco-core's signature rows first. They match the game's original bytes (sco-sigcheck checks
    // them against StarCitizen.exe on disk), and the ASOP rows cover bytes that the offline patches
    // and hooks below change (the shard gate, the fleet manager's retrieve). The report goes out
    // with LogStartup.
    if (!PrepareHostKit()) return; // fresh fail-closed admission BEFORE any engine mutation
#ifdef SCO_OFFLINE_G1_HOST
    g_signaturesResolved = sco::game::host::Status().signaturesResolved;
#endif
    g_offline = ApplyOfflinePatches();
    // Per-patch ok/failed lines follow in LogStartup; the patch results are private to patches.cpp.
    Log("[startup] 3/8 offline patches: %s", g_offline ? "applied (per-patch results follow)" : "refused or failed");
    if (!g_offline) {
        Log("[startup] host stays prepared: no plugins or menu will load; the game continues with whatever patches the per-patch lines list; close the game");
        return;
    }
    InstallHooks();
    Log("[startup] 4/8 hooks installed: %s", g_hooksInstalled ? "yes" : "none");
    const bool teleportApi = ResolveTeleportApi();
    if (teleportApi) {
        ResolveSpawnApi(g_text, g_rdata);
        ResolveNpcApi(g_text);
        ResolveLegacyPlacementApi(g_text, g_rdata); // unrelated legacy NPC/contracts helpers only
        ResolveCVarsApi(g_text, g_rdata);
        ResolveMissionsApi(g_text, g_rdata);
        ResolveContractsApi(g_text, g_rdata);
#ifndef SCO_ASOP_HOST_COMPOSITION
        ResolveFleetApi();
        ResolveHangarsApi();
#endif
    }
    Log("[startup] 5/8 API resolution: teleport=%s spawner=%s contracts=%s", teleportApi ? "ok" : "no",
        SpawnerReady() ? "ok" : "no", ContractsReady() ? "ok" : "no");
}

static void LogStartup() {
    LogOfflinePatches();
    LogHooks();
    if (g_signaturesResolved) {
        const auto snapshot = sco::AcquireHostSignatureSnapshot();
        if (snapshot) sco::LogSignatureReport(*snapshot, false);
        else Log("[app] signature snapshot unavailable; no host registry is published");
    }
    if (g_quitHookStatus) Log("[app] game quit hook: %s", g_quitHookStatus);
    Log("[app] teleport: moves use game.relocation and report their own result");
    if (SpawnerReady()) Log("[+] ship spawner: ready (M = menu)");
    else                Log("[!] ship spawner: unavailable (see above); the menu still starts");
#ifndef SCO_ASOP_HOST_COMPOSITION
    LogFleet();
    LogHangars();
#endif
    if (g_offline)
        Log("[i] check Game.log: \"Process sc-client started\" line should show bOnline[0].");
    else
        Log("[app] offline bootstrap unavailable/refused; session state is not certified");
}

// --- sco-core's host kit (sco/app.h), on the game thread -----------------------------------------

static void LogCapRefusal(const char* name, sco::Result result) {
    Log("[!] capability %s: %s", name, sco::ResultName(result));
}

// One capability per feature, from the same readiness LogStartup prints. The table, the reasons a
// plugin sees and the one capability held off by decision live in feature_caps.h.
static void SetFeatureCaps() {
    sc_offline::feature_caps::Readiness ready;
    ready.offline = g_offline;
    ready.teleportRows = g_tp.ok;   // the teleport.* rows resolved: Core's registry default is ON
    ready.spawner = SpawnerReady();
    ready.contracts = ContractsReady();
    sc_offline::feature_caps::Apply(ready, LogCapRefusal);
#ifndef SCO_ASOP_HOST_COMPOSITION
    SetFleetCaps();
    SetHangarCaps();
#endif
}

static const sco::plugins::ScriptRuntime kLua{ sco_lua_load, sco_lua_unload };

static bool g_hostKitStarted = false;   // sco::app::Start ran (on the game thread)

#ifdef SCO_OFFLINE_G13_HOST
static void ConfigurePrivateSession(sco::app::Platform& pf) {
    // The host (never a plugin) owns bind/scope. No legacy CIDR expansion.
    // G1 supplies positive live admission; do not substitute g_offline or flags.
    char enabled[16]{};
    const DWORD on = GetEnvironmentVariableA("SC_OFFLINE_MULTIPLAYER", enabled, sizeof(enabled));
    if (on >= sizeof(enabled)) { pf.privateSession.enabled = false; return; }
    pf.privateSession.enabled = !(on && _stricmp(enabled, "off") == 0);
    char bind[32]{}, peers[272]{};
    const DWORD bn = GetEnvironmentVariableA("SC_OFFLINE_MULTIPLAYER_BIND", bind, sizeof(bind));
    const DWORD pn = GetEnvironmentVariableA("SC_OFFLINE_MULTIPLAYER_PEERS", peers, sizeof(peers));
    if ((!bn || (bn < sizeof(bind) && !std::strcmp(bind, "127.0.0.1"))) && !pn) {   // exact loopback only
        // multiplayer_allow never widens this: it only narrows which firewall gaps count as proof.
        // A LAN needs BOTH variables below, set in the process environment (no launcher key sets them).
        Log("[G13] private session: loopback only; a LAN needs SC_OFFLINE_MULTIPLAYER_BIND (this machine's private address) "
            "and SC_OFFLINE_MULTIPLAYER_PEERS (exact peer addresses, comma separated)");
        return;
    }
    if (!bn || !pn || bn >= sizeof(bind) || pn >= sizeof(peers)) {
        pf.privateSession.enabled = false;
        Log("[G13] invalid/incomplete host bind + exact peer configuration; disabled");
        return;
    }
    pf.privateSession.bindAddress = bind;
    pf.privateSession.allowedAddresses.clear();
    std::string value(peers);
    size_t first = 0;
    while (first < value.size() && pf.privateSession.allowedAddresses.size() < 16) {
        const size_t end = value.find(',', first);
        std::string address = value.substr(first, end == std::string::npos ? end : end-first);
        const size_t begin = address.find_first_not_of(" ");
        const size_t last = address.find_last_not_of(" ");
        if (begin == std::string::npos) { pf.privateSession.enabled = false; return; }
        pf.privateSession.allowedAddresses.push_back(address.substr(begin,last-begin+1));
        if (end == std::string::npos) { first = value.size(); break; }
        first = end+1;
        if (first == value.size()) { pf.privateSession.enabled = false; return; }
    }
    if (first < value.size()) pf.privateSession.enabled = false;
    // sco-core Configure validates private numeric bind/exact addresses and fails
    // closed on public/wildcard/DNS/CIDR/duplicate/mismatched LAN input.
}
#endif

// sc-offline.ini relocation = on|off (SC_OFFLINE_RELOCATION; unset = on): the host switch for sco-core's
// game.relocation. Off is a hard off. On only lets sco-core's own checks decide (fresh offline admission,
// verified engine rows, live player, known star system); it never bypasses them.
static void ApplyRelocationSwitch() {
    char value[8]{};
    const DWORD n = GetEnvironmentVariableA("SC_OFFLINE_RELOCATION", value, sizeof(value));
    const bool on = n == 0 || (n < sizeof(value) && _stricmp(value, "on") == 0);
#ifdef SCO_OFFLINE_RELOCATION_SWITCH
    sco::game::services::SetRelocationEnabled(on);
    Log("[app] relocation = %s (game.relocation still requires sco-core's offline admission)", on ? "on" : "off");
#else
    Log("[app] relocation = %s: the pinned sco-core has no relocation switch; game.relocation stays as the pin ships it", on ? "on" : "off");
#endif
}

static bool PrepareHostKit() {
    ApplyRelocationSwitch();
    sco::app::Platform pf;
    pf.hostVersion = SCO_TITLE;
    char dir[MAX_PATH];
    if (DataFilePath(dir, sizeof(dir), "plugins")) {   // data\plugins, beside ships.txt
        std::error_code ec;
        pf.pluginRoot = std::filesystem::absolute(dir, ec);
        if (ec) pf.pluginRoot = dir;
        // sco.storage: every plugin's database, the built-ins' included, in data\storage\<id>.db.
        pf.dataRoot = pf.pluginRoot.parent_path();
    } else {
        Log("[app] SC_OFFLINE_SHIPS_FILE is unset, so there is no data folder to load plugins from or keep storage in");
    }
    pf.pluginsEnabled = PluginsEnabled();
    pf.scripts = &kLua;
    // sco-lua is game-neutral since sco-core c133484: the Star Citizen tables (sco.datacore,
    // the read-only sco.game.*, the game.* event payloads) are an adapter the product adds, once,
    // before any script loads. Same tables and permissions as before.
    if (sco_lua_add_extension(&sco_lua_game_extension) != SCO_OK)
        Log("[app] sco-lua: the Star Citizen adapter wasn't added; scripts get no sco.game or sco.datacore");
    // G1 owns original-image signature resolution in the worker's Prepare, outside
    // loader lock. The Platform image/registerSignatures inputs must remain null.
    pf.setCapabilities = SetFeatureCaps;
    // The keys sc-offline handles itself (hotkeys.h); no plugin can bind them. The built-ins bind
    // F6, F7 and F8 to their commands through sco.ui like any plugin.
    pf.reservedChords = kReservedChords;
    pf.nReservedChords = kReservedChordCount;
    // sco-core's game pack publishes teleport.spatial (and, later, the game.* services) under the
    // owner "game" before the built-ins load; the teleport built-in no longer provides it.
    pf.gameServices = true;
#ifdef SCO_OFFLINE_G13_HOST
    ConfigurePrivateSession(pf);
#endif
    // G1 owns early manifest discovery, OPEN/LOADED timing and producer drain.
    // Its current source contract refuses mixed builtin + early DataCore input;
    // do not disable DataCore or resurrect the old private pack loader to bypass it.
    pf.dataCore = true;
#ifdef SCO_OFFLINE_G1_HOST
    static const sco::Image original = sco::ModuleImage();
    sco::game::host::Options options;
    options.inspect = InspectOfflineEnvironment;
    options.image = &original;
#ifdef SCO_CORE_PROCESS_RETAINED_COMPOSITION
    pf.gameReadersQuiesced = InstallQuiescenceAttested;
    pf.gameReadersContext = nullptr;
#endif
    options.platform = std::move(pf);
    if (!offline_host::AttachShutdownOwner(options)) {
        Log("[app] product owner unavailable or core stop contract missing; host remains refused");
        return false;
    }
    // D6: an admission refusal at load can be transient (firewall service not answering yet). Retry
    // only that case: Unavailable with the host back at Idle. Each attempt is a full fresh inspection
    // (refusals are never cached); Options are copied per attempt.
    constexpr int kPrepareAttempts = 3;
    constexpr DWORD kPrepareRetryMs = 500;
    sco::Result result = sco::Result::Unavailable;
    for (int attempt = 1;; ++attempt) {
        result = offline_host::Prepare(options);
        if (result != sco::Result::Unavailable || attempt == kPrepareAttempts ||
            sco::game::host::Status().phase != sco::game::host::Phase::Idle)
            break;
        Log("[app] offline admission refused (attempt %d/%d); retrying in %lu ms", attempt, kPrepareAttempts,
            static_cast<unsigned long>(kPrepareRetryMs));
        Sleep(kPrepareRetryMs);
    }
    g_quitHookStatus = sco::game::host::Status().quitHookInstalled ? "installed by game pack" : "unavailable";
    if (result != sco::Result::Ok) Log("[app] offline admission/prepare refused: %s", sco::ResultName(result));
    Log("[startup] 2/8 host Prepare: %s (phase %s; quiescence attested gate-held=%u refused=%u; %llu ms since ModThread start)",
        sco::ResultName(result), PhaseName(sco::game::host::Status().phase), g_quiesceGateHeld.load(std::memory_order_relaxed),
        g_quiesceRefused.load(std::memory_order_relaxed),
        static_cast<unsigned long long>(GetTickCount64() - g_modThreadStartMs));
    return result == sco::Result::Ok;
#else
    Log("[app] offline startup unavailable: published host pin lacks G1; no private fallback");
    return false;
#endif
}

static void RequestLifecycleStop(const char* trigger);

static void StopHostKit(const char* trigger) {
    // Runs inside the game hook. Request closure and wake ModThread, but return
    // before either the core owner or the menu worker is drained.
    RequestLifecycleStop(trigger);
#ifdef SCO_OFFLINE_G1_HOST
    InvalidateEvidence();
    // Closure only (admission closed, game.exit once, nothing unloaded). Core's quit hook runs the
    // same closure; a repeat is Ok. Nothing waits here.
    Log("[shutdown] game-thread closure (%s): %s", trigger, sco::ResultName(offline_host::Stop()));
#else
    (void)trigger;
#endif
}

// The launcher's Stop (src/stop_channel.h). WM_CLOSE alone ends the game without the quit path Core hooks,
// so the launcher first signals Local\sc-offline-stop-<pid>; the game thread then runs the same closure a
// normal quit runs (Core's Stop: admission closed, game.exit once, nothing unloaded) and acknowledges.
// Every part is latched (Core's closure, the wallet's exit save), so a later real quit repeats nothing.
// Nothing here quits the game or unloads anything: Core keeps its quit function private, and the launcher
// closes the window afterwards.
static sc_offline::stop_channel::HostChannel g_stopChannel;

static void RunLauncherStopWork() {
    struct Ops {
        void Log(const char* line) { ::Log("%s", line); }
        void CloseHost() { StopHostKit("launcher stop"); }
        bool HostClosed() {
#ifdef SCO_OFFLINE_G1_HOST
            return sco::game::host::Status().phase == sco::game::host::Phase::Stopped;
#else
            return false;
#endif
        }
        void SaveWallet() { SaveContractsWalletAtExit(); }
    } ops;
    sc_offline::stop_channel::RunExitWork(ops);
}

// Contracts run on the retained host engine (contracts.cpp), as the v0.7.7 built-in did, until a
// published missions/economy provider lets the external plugin take over. Its mission accounting
// needs every actor death.
static const char kContractsOwner = 0;
static void OnContractActorDied(const char*, const void* data, void*) {
    const auto* d = static_cast<const sc_game_actor_died*>(data);
    if (d && d->size >= sizeof(*d)) RecordContractDeath(d->entity_id);
}
// The wallet is polled every 5 s; this writes the last change before the game closes.
static void OnContractsGameExit(const char*, const void*, void*) { SaveContractsWalletAtExit(); }

static void StartHostKit() {
#ifdef SCO_OFFLINE_G1_HOST
    InvalidateEvidence(); // Start re-inspects: no positive reading carried over from Prepare
    // Attestation case (ii): the window is open only around this one call, on this thread, and is
    // closed on every exit path (including an exception out of Start).
    struct StartWindow {
        StartWindow() noexcept { g_hostStartThreadId.store(GetCurrentThreadId(), std::memory_order_release); }
        ~StartWindow() { g_hostStartThreadId.store(0, std::memory_order_release); }
    };
    const sco::Result result = [] { StartWindow window; return offline_host::Start(); }();
    g_hostKitStarted = result == sco::Result::Ok;
    {   // [startup] 7/8: the first attempt, then only a changed result (retries run until admitted).
        static unsigned attempts = 0;
        static int lastLogged = -1;
        ++attempts;
        if (attempts == 1 || static_cast<int>(result) != lastLogged) {
            lastLogged = static_cast<int>(result);
            char threads[48] = "";
            if (attempts == 1)
                std::snprintf(threads, sizeof(threads), "; start-thread==gate-thread: %s",
                    GetCurrentThreadId() == g_gateThreadId.load(std::memory_order_acquire) ? "yes" : "no");
            Log("[startup] 7/8 host Start: attempt %u: %s (phase %s; attested admitted-start=%u refused=%u%s)",
                attempts, sco::ResultName(result), PhaseName(sco::game::host::Status().phase),
                g_quiesceAdmittedStart.load(std::memory_order_relaxed), g_quiesceRefused.load(std::memory_order_relaxed),
                threads);
        }
    }
    if (!g_hostKitStarted && sco::game::host::Status().phase == sco::game::host::Phase::Failed) {
        static bool failureLogged = false; // Failed is terminal: no further retry follows
        if (!failureLogged) {
            failureLogged = true;
            Log("[startup] host start failed permanently: the game continues with the offline patches but without plugins, services or menu; close the game");
        }
    }
    static sc_offline::StartDiagnostics diagnostics;
    const auto diagnostic = diagnostics.Observe(g_hostKitStarted, static_cast<int32_t>(result));
    if (diagnostic == sc_offline::StartDiagnosticAction::Refused)
        Log("[app] start refused: %s", sco::ResultName(result));
    else if (diagnostic == sc_offline::StartDiagnosticAction::Recovered)
        Log("[app] start recovered after refusal");
    if (g_hostKitStarted) {
        const auto deaths = sco::Subscribe(&kContractsOwner, SC_GAME_EVENT_ACTOR_DIED, OnContractActorDied, nullptr);
        if (deaths != sco::Result::Ok)
            Log("[contracts] actor-death subscription refused: %s; kill objectives may not complete", sco::ResultName(deaths));
        const auto exits = sco::Subscribe(&kContractsOwner, "game.exit", OnContractsGameExit, nullptr);
        if (exits != sco::Result::Ok)
            Log("[contracts] game.exit subscription refused: %s; the wallet saves every 5 s only", sco::ResultName(exits));
    }
#ifdef SCO_ASOP_HOST_COMPOSITION
    if (g_hostKitStarted) {
        // CMake currently rejects this composition until it has a nonblocking
        // stop request and same-owner drain that preserve provider ordering.
        const auto r = offline::asop::StartFromProductEnvironment(g_offline, InspectAntiCheat() == AcCheck::Clear,
            [](void*, char* path, std::size_t capacity, const char* file) {
                return DataFilePath(path, static_cast<DWORD>(capacity), file);
            });
        Log("[asop] host composition result=%u; no product fallback", static_cast<unsigned>(r));
    }
#endif
#endif
}

// Retained legacy consumers pending their own game-pack migrations. Contracts is the one retained
// engine ticked here on purpose (maintainer decision, v0.7.8.2); never revive other
// failed/disabled external features here.
static void RunLegacyTicks(DWORD now) {
    ProcessNpcs(); // unrelated legacy callers; external NPC owns its public requests
    ProcessCVars();
    ProcessMissions();
    ProcessContracts(); // mobiGlas listing, running missions, rewards; wallet in wallet.txt
    // Build, quantum, teleport and loadout are external; no fallback ticks.
#ifndef SCO_ASOP_HOST_COMPOSITION
    ProcessFleet(now);
#endif
}

static void OnMainThreadTick() {
    // Before anything else: the launcher's Stop must be answered even when the host never started.
    if (g_stopChannel.Poll(RunLauncherStopWork)) return;
#ifdef SCO_OFFLINE_G1_HOST
    // Terminal phases never restart; each attempt re-inspects before provider publication.
    // A persistent admission refusal must not inspect and log on every window message.
    static sc_offline::StartRetry retry;
    retry.Tick(sco::game::host::Status().phase == sco::game::host::Phase::Prepared,
               GetTickCount(), [] { StartHostKit(); });
    if (!offline_host::Running()) return;
    // Hold the exact OwnedShutdown carried in host Options through every
    // product consumer invoked by this admitted callback.
    auto ownerRead = offline_host::TryRead();
    if (!ownerRead) { StopHostKit("owner admission closed"); return; }
    const DWORD now = GetTickCount();
    // Re-inspect BEFORE cadence, OS callbacks or retained legacy consumers. Lost
    // admission cancels pending app tasks through G1 without executing them.
    const sco::Result tick = offline_host::Tick(now);
    if (!offline_host::Running()) {
        InvalidateEvidence();
        StopHostKit("tick admission lost"); // game thread: request, then Core's closure
        return;
    }
    if (tick != sco::Result::Ok) {
        // Still Running: a transient refusal (re-entrant callback in a plugin callout, Core's
        // control gate busy). Skip this tick entirely and try again on the next message.
        static bool skipLogged = false;
        if (!skipLogged) {
            skipLogged = true;
            Log("[app] tick skipped: %s while the host is still Running; retrying on the next message (logged once)",
                sco::ResultName(tick));
        }
        return;
    }
    static bool firstTickLogged = false;
    if (!firstTickLogged) {
        firstTickLogged = true;
        Log("[startup] 8/8 first game-thread tick: ok");
    }
#ifdef SCO_ASOP_HOST_COMPOSITION
    offline::asop::Pump(now); // EVERY admitted message, never behind tick cadence.
#else
    ProcessHangars(now); // synchronous message handoff remains unthrottled
#endif
    Menu_GameThreadFrame();
    Hotkeys_Poll();
    static DWORD last = 0;
    if (now - last < 100) return;
    last = now;
    if (g_tp.ok) RunLegacyTicks(now);
    // Opt-in test probe (registry_probe = 1); a no-op unless the launcher set the switch.
    ProcessRegistryProbe();
#endif
}

static HHOOK g_msgHook = nullptr;
static HANDLE g_controlWakeEvent = nullptr;
static HANDLE g_hookCallbacksDrainedEvent = nullptr;
static sc_offline::menu_lifetime::CallbackAdmission g_hookCallbackAdmission;
static std::atomic<bool> g_stopRequestIssued{false};

class HookCallbackLease final {
public:
    HookCallbackLease() noexcept : admitted_(g_hookCallbackAdmission.Enter()) {}
    ~HookCallbackLease() {
        if (!admitted_) return;
        g_hookCallbackAdmission.Leave();
        if (g_hookCallbackAdmission.ClosedAndDrained() && g_hookCallbacksDrainedEvent)
            SetEvent(g_hookCallbacksDrainedEvent);
    }
    explicit operator bool() const noexcept { return admitted_; }
private:
    bool admitted_;
};

static void RequestLifecycleStop(const char* trigger) {
    g_stopChannel.Retire();   // a later launcher Stop has nothing to wait for (a stop running right now answers at its end)
    if (!g_stopRequestIssued.load(std::memory_order_acquire)) Log("[shutdown] 1/3 stop requested: %s", trigger);
    Menu_RequestStop();
    g_hookCallbackAdmission.Close();
    if (g_hookCallbackAdmission.ClosedAndDrained() && g_hookCallbacksDrainedEvent)
        SetEvent(g_hookCallbacksDrainedEvent);
    if (!g_stopRequestIssued.exchange(true, std::memory_order_acq_rel)) {
#ifdef SCO_OFFLINE_G1_HOST
        const auto request = offline_host::RequestStop();
        Log("[app] lifecycle stop request (%s): result=%s phase=%u dependency=%u; closure only, backing stays retained",
            trigger, sco::ResultName(request.result), static_cast<unsigned>(request.phase),
            static_cast<unsigned>(request.dependencies));
#else
        Log("[app] lifecycle stop request (%s): host unavailable; no Core request made", trigger);
#endif
    }
    if (g_controlWakeEvent && !SetEvent(g_controlWakeEvent))
        Log("[app] controller wake signal failed (%lu); retained phase check remains active", GetLastError());
}

static LRESULT CALLBACK GetMsgProc(int code, WPARAM wp, LPARAM lp) {
    // This admission operation is deliberately the first product access in the
    // callback. A callback that loses the close race only forwards to the next
    // hook; it never touches the core, menu, or product state.
    HookCallbackLease callback;
    if (!callback) return CallNextHookEx(nullptr, code, wp, lp);
    if (code >= 0) {
        // WM_QUIT is terminal BEFORE queued work/render/input; a non-removing peek
        // also must not execute a tick while quit is pending. Game-pack quit owns the
        // synchronous pre-engine teardown; this is only the secondary OS trigger.
        if (reinterpret_cast<const MSG*>(lp)->message == WM_QUIT) {
            if (wp & PM_REMOVE) StopHostKit("WM_QUIT");
        } else OnMainThreadTick();
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

static BOOL CALLBACK FindGameWindow(HWND hwnd, LPARAM out) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
    wchar_t title[128];
    if (GetWindowTextW(hwnd, title, 128) && wcsstr(title, L"Star Citizen")) { *reinterpret_cast<HWND*>(out) = hwnd; return FALSE; }
    return TRUE;
}

// Set only once ModThread has passed the fail-closed gate and the early refusal
// paths. The sink is an atomic borrowed callback in sco-core; core has no in-flight
// sink drain, so do not publish it on paths that may return early. The module is
// pinned and never unloaded, but a refused start should not leave a callback behind.
static void ForwardCoreLog(const char* line) { Log("%s", line); }

// The message hook is the bootstrap's, not teleport's: it starts the host kit (and with it the
// built-in plugins and plugins) on its first tick whether or not a feature resolved. It stays off
// only when the game is running online.
static void RunMainThreadService() {
    if (!g_offline) return;
    if (!g_controlWakeEvent || !g_hookCallbacksDrainedEvent) {
        Log("[app] lifecycle controller unavailable; keep host inactive");
        return;
    }
    if (g_tp.ok) LoadSavedSpot(StartingOverDaymar());
    HWND hwnd = nullptr;
    while (!hwnd) { EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&hwnd)); if (!hwnd) Sleep(1000); }
    // The game's window exists, so its own crash filter is set: ours goes in front of it.
    InstallCrashLine();
    Log("[app] crash line: installed (a crash writes its module and offset here as [crash] lines)");
    g_msgHook = SetWindowsHookExW(WH_GETMESSAGE, GetMsgProc, nullptr, GetWindowThreadProcessId(hwnd, nullptr));
    if (!g_msgHook) { Log("[app] could not hook the game's message loop (%lu); hotkeys, menu and plugins disabled", GetLastError()); return; }
    {   // Only once the hook is in: the launcher finds these events only where a tick can answer them.
        DWORD error = 0;
        if (g_stopChannel.Create(GetCurrentProcessId(), &error)) Log("[app] launcher stop channel: ready");
        else Log("[app] launcher stop channel unavailable (%lu); the launcher closes the window directly", error);
    }
    // The menu is the one place every plugin page and the M key live, so it starts whether or not the
    // legacy ship spawner resolved. The spawn plugin has its own gate (the spawn.ship capability).
    Menu_Start(hwnd);
    if (!SpawnerReady()) Log("[menu] started without the ship spawner: only the ship pages are unavailable");
#ifdef SCO_OFFLINE_G1_HOST
    for (;;) {
        const auto phase = sco::game::host::Status().phase;
        if (phase == sco::game::host::Phase::Stopped || phase == sco::game::host::Phase::Failed) {
            RequestLifecycleStop("core terminal phase");
            break;
        }
        if (g_stopRequestIssued.load(std::memory_order_acquire)) break;
        const DWORD wake = WaitForSingleObject(g_controlWakeEvent, 200);
        if (wake == WAIT_OBJECT_0) break; // explicit stop request, independent of phase
        if (wake == WAIT_FAILED) {
            Log("[app] lifecycle controller wait failed (%lu); retaining hook and backing", GetLastError());
            return;
        }
        PostMessageW(hwnd, WM_NULL, 0, 0);
    }
    // An explicit request can originate inside this hook. Wait, bounded, for admitted callbacks to
    // return before the independent thread drains their owner. Timing out changes nothing else:
    // the hook, the module and every dependency stay retained, and the drains below are bounded too.
    constexpr DWORD kHookDrainWaitMs = 5000;
    const ULONGLONG waitStartMs = GetTickCount64();
    bool reportedPending = false;
    bool callbacksDrained = true;
    while (!g_hookCallbackAdmission.ClosedAndDrained()) {
        const ULONGLONG elapsedMs = GetTickCount64() - waitStartMs;
        if (elapsedMs >= kHookDrainWaitMs) { callbacksDrained = false; break; }
        const DWORD slice = static_cast<DWORD>(kHookDrainWaitMs - elapsedMs < 1000 ? kHookDrainWaitMs - elapsedMs : 1000);
        const DWORD drained = WaitForSingleObject(g_hookCallbacksDrainedEvent, slice);
        if (drained == WAIT_OBJECT_0) continue;
        if (!reportedPending) {
            Log("[app] admitted hook callback is still active; waiting up to %lu ms, then continuing retained",
                static_cast<unsigned long>(kHookDrainWaitMs));
            reportedPending = true;
        }
        if (drained == WAIT_FAILED) Sleep(slice); // never spin; the bound above still applies
    }
    if (callbacksDrained)
        Log("[shutdown] 2/3 hook callbacks drained after %llu ms", static_cast<unsigned long long>(GetTickCount64() - waitStartMs));
    else
        Log("[shutdown] 2/3 hook callback wait timed out after %lu ms; continuing, everything stays retained",
            static_cast<unsigned long>(kHookDrainWaitMs));
    // This registration path has no entry fence proving no thread has entered
    // GetMsgProc but not yet executed HookCallbackLease::Enter. Keep the hook
    // registered and its HHOOK stable. After Close, later entries take only the
    // forwarding path above; the product module remains pinned.
    Log("[app] message hook remains registered; closed admission forwards callbacks and backing stays retained");
    const auto coreDrain = offline_host::DrainRequestedStop(1000);
    Log("[app] core owner drain=%u; dependency backing remains process-retained",
        static_cast<unsigned>(coreDrain));
    // Menu status is a separate product-local callback/worker census.
    const auto menu = Menu_Drain(1000);
    Log("[menu] worker drain: state=%u status=%u backing=%u; product backing remains process-retained",
        static_cast<unsigned>(menu.worker), static_cast<unsigned>(menu.status), static_cast<unsigned>(menu.backing));
    // Closure itself runs on the game thread (Core's quit hook, a tick, or StopHostKit); this only reads it.
    const auto closurePhase = sco::game::host::Status().phase;
    Log("[shutdown] 3/3 closure: core phase %s (%s)", PhaseName(closurePhase),
        closurePhase == sco::game::host::Phase::Stopped ? "closed, nothing unloaded" :
        closurePhase == sco::game::host::Phase::Failed ? "closure failed, backing retained" :
        "closure not yet run on the game thread, backing retained");
#endif
}

static DWORD WINAPI ModThread(LPVOID param) {
    (void)param; // the module is pinned and never unloaded, so no path needs its handle
#ifdef SCO_OFFLINE_G1_HOST
    if (!RetainStartupGateModule())
        StartupGateFailStop(sc_offline::startup_gate_wait::Outcome::SetupFailed);
    g_modThreadStartMs = GetTickCount64();
#endif
    OpenConsole();
    Log(SCO_TITLE " (" SCO_BASED_ON ")");
    Log("Bug reports: https://github.com/scuba-sc/sc-offline/issues");
#ifdef SCO_OFFLINE_G1_HOST
    Log("[startup] threads in process at the gate: %ld (gate installed: %s)", ProcessThreadCount(),
        g_startupGateInstalled.load(std::memory_order_acquire) ? "yes" : "no");
#endif

    if (!offline_host::NativeActivationAvailable()) {
        Log("[app] offline startup unavailable: native reader and worker exclusion and drain are not proven; close the game and await a validated build");
        return 0;
    }
#ifndef SCO_OFFLINE_G1_HOST
    // A missing host composition is a refusal even if another build flag
    // claims a stop contract; do not publish callbacks without a module owner.
    Log("[app] offline startup unavailable: game host composition missing");
    ReleaseStartupGate();
    return 0;
#else
    if (!offline_host::ProductControlStopContractAvailable()) {
        Log("[app] offline startup unavailable: Core lacks the non-draining stop and owner-drain contract");
        ReleaseStartupGate();
        return 0;
    }

    HMODULE game = GetModuleHandleW(kTargetModule);
    if (!game) game = GetModuleHandleW(nullptr);
    Log("[+] game module base: 0x%p", reinterpret_cast<void*>(game));

    if (InspectAntiCheat() != AcCheck::Clear) {
        Log("[AC] anti-cheat present or inspection unknown; offline admission refused");
        if (g_hooksInstalled) {
            Log("[!] Hooks are already installed, so staying loaded (idle). Close the game.");
            for (;;) Sleep(1000);
        }
        Log("[!] Launch Star Citizen offline with no EAC, then load this. Offline startup is refused; close the game.");
        ReleaseStartupGate();
        // The module stays loaded (pinned, retained until exit); only this worker returns.
        return 0;
    }
    Log("[app] anti-cheat inspection clear; offline/network admission still required");
    if (ProcessElevated())
        Log("[!] the game is running as administrator. Windows then ignores the switches the game uses to turn off "
            "overlay Vulkan layers (OBS, Steam), and one of those can crash the game while it loads. Start sc-offline.exe "
            "normally, not \"Run as administrator\", and untick \"Run this program as an administrator\" on StarCitizen.exe.");

    // Controller events are created outside DllMain and before any host hook or
    // callback is published. Refuse this path if either allocation fails.
    g_controlWakeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_hookCallbacksDrainedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_controlWakeEvent || !g_hookCallbacksDrainedEvent) {
        Log("[app] lifecycle controller unavailable (%lu); startup remains refused", GetLastError());
        if (g_controlWakeEvent) { CloseHandle(g_controlWakeEvent); g_controlWakeEvent = nullptr; }
        if (g_hookCallbacksDrainedEvent) { CloseHandle(g_hookCallbacksDrainedEvent); g_hookCallbacksDrainedEvent = nullptr; }
        ReleaseStartupGate();
        return 0;
    }
    if (!offline_host::RetainProductModule()) {
        Log("[app] product module owner unavailable; host callbacks remain refused");
        ReleaseStartupGate();
        return 0;
    }

    // No core host work occurs before this point. Keep the borrowed sink out of
    // the fail-closed and early-refusal paths above.
    sco::SetLogSink(ForwardCoreLog);
    ReadStartOptions();
    StartOffline();
    ReleaseStartupGate();
    Log("[startup] 6/8 gate released after %llu ms", static_cast<unsigned long long>(GetTickCount64() - g_modThreadStartMs));
    LogStartup();
    RunMainThreadService();

    for (;;) Sleep(1000);
#endif // SCO_OFFLINE_G1_HOST
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        InitLog();
        const auto gate = InstallStartupGate(hModule, reserved);
        g_startupGateInstalled.store(gate == StartupGateInstallResult::Installed, std::memory_order_release);
        switch (gate) {
        case StartupGateInstallResult::Refused:
            // DllMain runs under the loader lock. Keep refusal reporting synchronous
            // and minimal; do not enter the logger or start DLL-owned code that could
            // outlive an unpinned module.
            OutputDebugStringA("[sc-offline] offline startup unavailable: native reader and worker exclusion and drain are not proven; no worker started\r\n");
            return TRUE;
        case StartupGateInstallResult::RefusedLoadMode:
            OutputDebugStringA("[sc-offline] offline startup unavailable: module was not loaded through the game executable's import table; no gate, worker or callback was published\r\n");
            return TRUE;
        case StartupGateInstallResult::NotTarget:
            return TRUE;
        case StartupGateInstallResult::Installed:
            break;
        }

        HANDLE progress = CreateThread(nullptr, 0, StartupGateProgressThread, nullptr, 0, nullptr);
        if (!progress) StartupGateFailStop(sc_offline::startup_gate_wait::Outcome::SetupFailed);
        CloseHandle(progress);
        HANDLE worker = CreateThread(nullptr, 0, ModThread, hModule, 0, nullptr);
        // Do not signal completion if worker creation fails. An installed gate
        // remains closed and its bounded wait will fail-stop.
        if (worker) CloseHandle(worker);
    }
    return TRUE;
}
