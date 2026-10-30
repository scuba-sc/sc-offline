#include <windows.h>
#include <tlhelp32.h>
#include <shlwapi.h>
#include "common.h"
#include "patches.h"
#include "hooks.h"
#include "teleport.h"
#include "spawner.h"
#include "npc.h"
#include "build.h"
#include "cvars.h"
#include "missions.h"
#include "contracts.h"
#include "travel.h"
#include "version.h"
#ifdef SCO_ASOP_HOST_COMPOSITION
#include "asop_shell.h"
#else
#include "services.h"
#include "fleet.h"
#endif
#include "menu.h"
#include "hotkeys.h"
#include "builtins/builtins.h"
#include "sco/app.h"
#include "sco/caps.h"
#include "sco/log.h"
#include "sco/scan.h"
#include "sco/signatures.h"
#include "sco/hook.h"
#ifdef SCO_OFFLINE_G1_HOST
#include "offline_host.h"
#include "admission_probe.h"
#include "start_retry.h"
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

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")

#pragma comment(linker, "/export:DirectInput8Create=C:\\Windows\\System32\\dinput8.DirectInput8Create,@1")

static const wchar_t* kTargetModule = L"StarCitizen.exe";

// Startup gate. The offline patches must land before the game's own startup reaches its EAC
// handshake (an unpatched game exits a fraction of a second after it starts). G1's Prepare may not
// run under the loader lock, so it runs on ModThread; the game's main thread is held at the exe's
// entry point until ModThread has finished StartOffline (admitted or refused), or kGateTimeoutMs.
// The entry starts with a call rel32, which InstallDetour can't relocate, so the gate writes its own
// 12-byte absolute jump and puts the original bytes back before the entry runs. Only the main thread
// exists at the entry, so the restore can't race another thread.
static HANDLE g_startupDone = nullptr;
static uint8_t* g_entry = nullptr;
static uint8_t g_entrySaved[12];
static constexpr DWORD kGateTimeoutMs = 60000;

static void ReleaseStartupGate() {
    if (g_startupDone) SetEvent(g_startupDone);
}

extern "C" void StartupGateWait() {
    const DWORD waited = WaitForSingleObject(g_startupDone, kGateTimeoutMs);
    if (!sco::hook::WriteCode(g_entry, g_entrySaved, sizeof(g_entrySaved)))
        TerminateProcess(GetCurrentProcess(), 0xC0DE0001);   // the entry would run our jump forever
    if (waited != WAIT_OBJECT_0) Log("[!] startup gate: released after %lu ms without the mod finishing startup", kGateTimeoutMs);
}

static void InstallStartupGate() {
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    wchar_t path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!base || !length || length >= MAX_PATH || _wcsicmp(PathFindFileNameW(path), kTargetModule) != 0) return;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || !nt->OptionalHeader.AddressOfEntryPoint) return;
    uint8_t* entry = base + nt->OptionalHeader.AddressOfEntryPoint;
    uint8_t* stub = sco::hook::AllocateNear(entry, 64);
    g_startupDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stub || !g_startupDone) return;
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
    FlushInstructionCache(GetCurrentProcess(), stub, 34);
    uint8_t jump[12] = { 0x48, 0xB8 };   // mov rax, stub; jmp rax
    const uint64_t stubAddr = reinterpret_cast<uint64_t>(stub);
    std::memcpy(jump + 2, &stubAddr, 8);
    jump[10] = 0xFF; jump[11] = 0xE0;
    std::memcpy(g_entrySaved, entry, sizeof(g_entrySaved));
    g_entry = entry;
    if (!sco::hook::WriteCode(entry, jump, sizeof(jump))) { g_entry = nullptr; CloseHandle(g_startupDone); g_startupDone = nullptr; }
}

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

#ifdef SCO_OFFLINE_G1_HOST
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

static sco::game::host::Admission InspectOfflineEnvironment() {
    const admission::Evidence ev = g_evidence.Get(InspectTargetProcess()); // target read fresh every call
    LogEvidenceIfChanged(ev);
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
    // sco-core's signature rows first. They match the game's original bytes (sco-sigcheck checks
    // them against StarCitizen.exe on disk), and the ASOP rows cover bytes that the offline patches
    // and hooks below change (the shard gate, the fleet manager's retrieve). The report goes out
    // with LogStartup.
    if (!PrepareHostKit()) return; // fresh fail-closed admission BEFORE any engine mutation
#ifdef SCO_OFFLINE_G1_HOST
    g_signaturesResolved = sco::game::host::Status().signaturesResolved;
#endif
    g_offline = ApplyOfflinePatches();
    if (!g_offline) return;
    InstallHooks();
    if (ResolveTeleportApi()) {
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
}

static void LogStartup() {
    LogOfflinePatches();
    LogHooks();
    if (g_signaturesResolved) sco::LogSignatureReport(false);
    if (g_quitHookStatus) Log("[app] game quit hook: %s", g_quitHookStatus);
    Log("[!] teleport: G2 REFUSE; F7/F8 consumer unavailable; read services are separate");
    if (SpawnerReady()) Log("[+] ship spawner: ready (M = menu)");
    else                Log("[!] ship spawner: unavailable (see above)");
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

static void SetCap(const char* name, bool ready) {
    const sco::Result r = sco::caps::Set(name, ready, ready ? nullptr : "unavailable on this game build (see mod.log)");
    if (r != sco::Result::Ok) Log("[!] capability %s: %s", name, sco::ResultName(r));
}

// One capability per feature, from the same readiness LogStartup prints.
static void SetFeatureCaps() {
    SetCap("offline", g_offline);
    SetCap("teleport", false); // G2 mutation not approved; read tables remain independently available
    SetCap("npc", SpawnerReady());
    SetCap("quantum", false); // movement remains REFUSE; external read commands have their own availability
    SetCap("contracts", ContractsReady());
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
    if ((!bn || (bn < sizeof(bind) && !std::strcmp(bind, "127.0.0.1"))) && !pn) return; // exact loopback only
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
    options.platform = std::move(pf);
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
    return result == sco::Result::Ok;
#else
    Log("[app] offline startup unavailable: published host pin lacks G1; no private fallback");
    return false;
#endif
}

static void StopHostKit(const char* trigger) {
#ifdef SCO_ASOP_HOST_COMPOSITION
    offline::asop::Stop(); // Untaken callers wake before app/provider teardown.
#endif
#ifdef SCO_OFFLINE_G1_HOST
    const sco::Result result = offline_host::Stop();
    InvalidateEvidence();
    if (result != sco::Result::Ok) Log("[app] stop (%s) refused: %s", trigger, sco::ResultName(result));
#else
    (void)trigger;
#endif
}

#ifdef SCO_ASOP_HOST_COMPOSITION
static const char kAsopExitOwner = 0;
static void StopAsopBeforeProviders(const char*, const void*, void*) {
    offline::asop::Stop();
}
#endif

// Contracts run on the retained host engine (contracts.cpp), as the v0.7.7 built-in did, until a
// published missions/economy provider lets the external plugin take over. Its mission accounting
// needs every actor death.
static const char kContractsOwner = 0;
static void OnContractActorDied(const char*, const void* data, void*) {
    const auto* d = static_cast<const sc_game_actor_died*>(data);
    if (d && d->size >= sizeof(*d)) RecordContractDeath(d->entity_id);
}

static void StartHostKit() {
#ifdef SCO_OFFLINE_G1_HOST
    InvalidateEvidence(); // Start re-inspects: no positive reading carried over from Prepare
    const sco::Result result = offline_host::Start();
    g_hostKitStarted = result == sco::Result::Ok;
    if (!g_hostKitStarted) Log("[app] start refused: %s", sco::ResultName(result));
    if (g_hostKitStarted) {
        const auto deaths = sco::Subscribe(&kContractsOwner, SC_GAME_EVENT_ACTOR_DIED, OnContractActorDied, nullptr);
        if (deaths != sco::Result::Ok)
            Log("[contracts] actor-death subscription refused: %s; kill objectives may not complete", sco::ResultName(deaths));
    }
#ifdef SCO_ASOP_HOST_COMPOSITION
    if (g_hostKitStarted) {
        // G1 owns engine quit and admission-loss teardown. Its app::Stop dispatches
        // game.exit before withdrawing providers, including the direct quit path.
        const auto subscribed = sco::Subscribe(&kAsopExitOwner, "game.exit", StopAsopBeforeProviders, nullptr);
        if (subscribed != sco::Result::Ok) {
            Log("[asop] shutdown subscription refused: %s; ASOP stays inactive", sco::ResultName(subscribed));
        } else {
            char ships[MAX_PATH]{};
            ShipsFilePath(ships, sizeof(ships));
            const auto r = offline::asop::StartFromEnvironment(g_offline, InspectAntiCheat() == AcCheck::Clear, ships);
            Log("[asop] host composition result=%u; no product fallback", static_cast<unsigned>(r));
        }
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
#ifdef SCO_OFFLINE_G1_HOST
    // Terminal phases never restart; each attempt re-inspects before provider publication.
    // A persistent admission refusal must not inspect and log on every window message.
    static sc_offline::StartRetry retry;
    retry.Tick(sco::game::host::Status().phase == sco::game::host::Phase::Prepared,
               GetTickCount(), [] { StartHostKit(); });
    if (!offline_host::Running()) return;
    const DWORD now = GetTickCount();
    // Re-inspect BEFORE cadence, OS callbacks or retained legacy consumers. Lost
    // admission cancels pending app tasks through G1 without executing them.
    if (offline_host::Tick(now) != sco::Result::Ok || !offline_host::Running()) { InvalidateEvidence(); return; }
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

static LRESULT CALLBACK GetMsgProc(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0) {
        // WM_QUIT is terminal BEFORE queued work/render/input; a non-removing peek
        // also must not execute a tick while quit is pending. Game-pack quit owns the
        // synchronous pre-engine teardown; this is only the secondary OS trigger.
        if (reinterpret_cast<const MSG*>(lp)->message == WM_QUIT) {
            if (wp & PM_REMOVE) StopHostKit("WM_QUIT");
        } else OnMainThreadTick();
    }
    return CallNextHookEx(g_msgHook, code, wp, lp);
}

static BOOL CALLBACK FindGameWindow(HWND hwnd, LPARAM out) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
    wchar_t title[128];
    if (GetWindowTextW(hwnd, title, 128) && wcsstr(title, L"Star Citizen")) { *reinterpret_cast<HWND*>(out) = hwnd; return FALSE; }
    return TRUE;
}

// The message hook is the bootstrap's, not teleport's: it starts the host kit (and with it the
// built-in plugins and plugins) on its first tick whether or not a feature resolved. It stays off
// only when the game is running online.
static void RunMainThreadService() {
    if (!g_offline) return;
    if (g_tp.ok) LoadSavedSpot(StartingOverDaymar());
    HWND hwnd = nullptr;
    while (!hwnd) { EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&hwnd)); if (!hwnd) Sleep(1000); }
    // The game's window exists, so its own crash filter is set: ours goes in front of it.
    InstallCrashLine();
    Log("[app] crash line: installed (a crash writes its module and offset here as [crash] lines)");
    g_msgHook = SetWindowsHookExW(WH_GETMESSAGE, GetMsgProc, nullptr, GetWindowThreadProcessId(hwnd, nullptr));
    if (!g_msgHook) { Log("[app] could not hook the game's message loop (%lu); hotkeys, menu and plugins disabled", GetLastError()); return; }
    if (SpawnerReady()) Menu_Start(hwnd);
#ifdef SCO_OFFLINE_G1_HOST
    for (;;) {
        const auto phase = sco::game::host::Status().phase;
        if (phase == sco::game::host::Phase::Stopped || phase == sco::game::host::Phase::Failed) break;
        PostMessageW(hwnd, WM_NULL, 0, 0);
        Sleep(200);
    }
    UnhookWindowsHookEx(g_msgHook); // hook handle stays immutable for any in-flight callback
#endif
}

static DWORD WINAPI ModThread(LPVOID param) {
    HMODULE self = static_cast<HMODULE>(param);
    OpenConsole();
    Log(SCO_TITLE " (" SCO_BASED_ON ")");
    Log("Bug reports: https://github.com/scuba-sc/sc-offline/issues");

    HMODULE game = GetModuleHandleW(kTargetModule);
    if (!game) game = GetModuleHandleW(nullptr);
    Log("[+] game module base: 0x%p", reinterpret_cast<void*>(game));

    if (InspectAntiCheat() != AcCheck::Clear) {
        Log("[AC] anti-cheat present or inspection unknown; offline admission refused");
        if (g_hooksInstalled) {
            Log("[!] Hooks are already installed, so staying loaded (idle). Close the game.");
            for (;;) Sleep(1000);
        }
        Log("[!] Launch Star Citizen offline with no EAC, then load this. Unloading in 5s.");
        ReleaseStartupGate();
        Sleep(5000);
        FreeLibraryAndExitThread(self, 0);
    }
    Log("[app] anti-cheat inspection clear; offline/network admission still required");
    if (ProcessElevated())
        Log("[!] the game is running as administrator. Windows then ignores the switches the game uses to turn off "
            "overlay Vulkan layers (OBS, Steam), and one of those can crash the game while it loads. Start sc-offline.exe "
            "normally, not \"Run as administrator\", and untick \"Run this program as an administrator\" on StarCitizen.exe.");

    ReadStartOptions();
    StartOffline();
    ReleaseStartupGate();
    LogStartup();
    RunMainThreadService();

    for (;;) Sleep(1000);
}

// sco-core's lines ([core], [plugin], [app], [status]) go to mod.log and the console like ours.
static void ForwardCoreLog(const char* line) { Log("%s", line); }

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        InitLog();
        sco::SetLogSink(ForwardCoreLog);
        InstallStartupGate();
        if (!CreateThread(nullptr, 0, ModThread, hModule, 0, nullptr)) ReleaseStartupGate();
    }
    return TRUE;
}
