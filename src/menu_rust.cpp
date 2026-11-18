// SPDX-License-Identifier: GPL-3.0-only
// See menu_rust.h. Loads sc_offline_menu.dll and gives it the widgets view; pumps dispatch_events on
// the game thread. Never runs in the ImGui default.
#include "menu_rust.h"
#ifdef SCO_MENU_RUST
#include "common.h"
#include "sco/plugins.h"
#include <windows.h>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

namespace sc_offline::menu_rust {
namespace {

// Product-private start block: the mirror of StartV1 in scol-menu/src/lib.rs. Not an SDK ABI.
struct StartV1 {
    uint32_t size, abi;
    void* gameWindow;
    const sco_widgets_view_v1* view;
    sco_view_client* client;
    void (*log)(const char*);
    void (*nudgeGame)();
    const char* shotDir;
    uint32_t flags, hotkey;
};
constexpr uint32_t kAbi = 1;
constexpr uint32_t kFlagVisible = 1;

struct Module {
    int32_t (*start)(const StartV1*) = nullptr;
    void (*requestStop)() = nullptr;
    uint32_t (*abi)() = nullptr;
};

struct State {
    sco::app::HostMenuClient inner{};
    Module module{};
    const sco_widgets_view_v1* view = nullptr;
    sco_view_client* client = nullptr;
    DWORD gameThread = 0;
    HHOOK hook = nullptr;
    std::atomic<HWND> gameWindow{nullptr};
    std::atomic<bool> running{false};
    std::atomic<bool> nudged{false};
    std::atomic<bool> inPump{false};
    bool loadedOnce = false;
};
// Process lifetime on purpose: the Rust thread may still be finishing when stop returns, and the
// DLL is never unloaded (see Stop).
State& S() { static State* s = new State(); return *s; }

void RustLog(const char* line) { if (line) Log("%s", line); }

void Nudge() {
    // One WM_NULL in flight at most: the hook clears the flag before it dispatches.
    State& s = S();
    HWND w = s.gameWindow.load();
    if (w && !s.nudged.exchange(true)) PostMessageW(w, WM_NULL, 0, 0);
}

struct PumpArgs { sco_result result; };
void PumpThunk(void* context) {
    State& s = S();
    uint32_t dispatched = 0, dropped = 0;
    static_cast<PumpArgs*>(context)->result = s.view->dispatch_events(s.client, &dispatched, &dropped);
}

// Runs on the game thread, from the game's own message loop (the way the host's message hook
// calls Menu_GameThreadFrame). Only dispatch_events: the Rust menu thread does everything else.
LRESULT CALLBACK GameHook(int code, WPARAM wParam, LPARAM lParam) {
    State& s = S();
    if (code >= 0 && s.running.load(std::memory_order_acquire) && GetCurrentThreadId() == s.gameThread &&
        !s.inPump.exchange(true)) {
        s.nudged.store(false);
        if (!s.gameWindow.load()) {
            GUITHREADINFO info{sizeof(info)};
            if (GetGUIThreadInfo(s.gameThread, &info)) s.gameWindow.store(info.hwndActive ? info.hwndActive : info.hwndFocus);
        }
        PumpArgs args{SCO_OK};
        if (sco::plugins::Guarded(PumpThunk, &args) != 0) {
            Log("[menu] rust menu: dispatch_events faulted; the Rust menu stops");
            s.running.store(false);
            if (s.module.requestStop) s.module.requestStop();
        } else if (args.result == SCO_UNAVAILABLE) {
            s.running.store(false); // Core is stopping
        }
        s.inPump.store(false);
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

std::wstring ModuleDir() {
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&ModuleDir), &self)) return L"";
    wchar_t path[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return L"";
    std::wstring dir(path, n);
    const size_t slash = dir.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : dir.substr(0, slash + 1);
}

// The open key. The ImGui window still answers M in the same process, so the Rust window defaults
// to F8 until the handoff in docs/rust-menu.md (4.1) moves M over. SC_OFFLINE_MENU_KEY=M (or F1..F12,
// or one letter) overrides it.
uint32_t HotKey() {
    char v[16]{};
    if (!GetEnvironmentVariableA("SC_OFFLINE_MENU_KEY", v, sizeof(v)) || !v[0]) return VK_F8;
    if (v[1] == 0 && std::isalnum(static_cast<unsigned char>(v[0]))) return static_cast<uint32_t>(std::toupper(static_cast<unsigned char>(v[0])));
    if ((v[0] == 'F' || v[0] == 'f') && std::atoi(v + 1) >= 1 && std::atoi(v + 1) <= 12) return VK_F1 + static_cast<uint32_t>(std::atoi(v + 1) - 1);
    return VK_F8;
}

void TryStart(const sco::app::HostMenuContext& ctx) {
    State& s = S();
    if (s.loadedOnce) return;
    s.loadedOnce = true;
    if (!sco::app::HostWidgetsView(ctx, &s.view, &s.client) || !s.view || !s.client) {
        Log("[menu] rust menu: the Core in use gives no widgets view; the ImGui menu stays");
        return;
    }
    if (s.view->size < sizeof(sco_widgets_view_v1) || (s.view->version >> 16) != (SCO_WIDGETS_VIEW_VERSION_1_0 >> 16)) {
        Log("[menu] rust menu: widgets view table has an unexpected size or version; the ImGui menu stays");
        return;
    }
    const std::wstring dir = ModuleDir();
    HMODULE dll = dir.empty() ? nullptr : LoadLibraryExW((dir + L"sc_offline_menu.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!dll) { Log("[menu] rust menu: sc_offline_menu.dll not found next to the host (%lu); the ImGui menu stays", GetLastError()); return; }
    // The DLL is never freed: its thread may outlive the client's stop.
    s.module.start = reinterpret_cast<decltype(s.module.start)>(GetProcAddress(dll, "scol_menu_start"));
    s.module.requestStop = reinterpret_cast<decltype(s.module.requestStop)>(GetProcAddress(dll, "scol_menu_request_stop"));
    s.module.abi = reinterpret_cast<decltype(s.module.abi)>(GetProcAddress(dll, "scol_menu_abi"));
    if (!s.module.start || !s.module.requestStop || !s.module.abi || s.module.abi() != kAbi) {
        Log("[menu] rust menu: sc_offline_menu.dll has the wrong exports or ABI; the ImGui menu stays");
        return;
    }
    s.gameThread = GetCurrentThreadId();
    StartV1 start{};
    start.size = sizeof(start); start.abi = kAbi; start.view = s.view; start.client = s.client;
    start.log = RustLog; start.nudgeGame = Nudge; start.flags = 0; start.hotkey = HotKey();
    s.running.store(true, std::memory_order_release);
    s.hook = SetWindowsHookExW(WH_GETMESSAGE, GameHook, nullptr, s.gameThread);
    if (!s.hook) {
        Log("[menu] rust menu: could not hook the game thread (%lu); the ImGui menu stays", GetLastError());
        s.running.store(false);
        return;
    }
    const int rc = s.module.start(&start);
    if (rc != 0) {
        Log("[menu] rust menu: start answered %d; the ImGui menu stays", rc);
        s.running.store(false);
        UnhookWindowsHookEx(s.hook); s.hook = nullptr;
        return;
    }
    Log("[menu] rust menu started (SC_OFFLINE_MENU=rust); open key 0x%02X; the ImGui menu stays as the fallback; not run in game", start.hotkey);
}

sco_result Load(const sco::app::HostMenuContext& ctx, void*) {
    State& s = S();
    const sco_result r = s.inner.load ? s.inner.load(ctx, s.inner.context) : SCO_UNAVAILABLE;
    if (r != SCO_OK) return r;
    TryStart(ctx);
    return SCO_OK;
}

void Frame(const sco::app::HostMenuContext& ctx, void*, void* request) {
    State& s = S();
    if (s.inner.frame) s.inner.frame(ctx, s.inner.context, request);
}

void Stop(const sco::app::HostMenuContext& ctx, void*) {
    State& s = S();
    // Never waits (the client's stop may run while the game thread is blocked). The Rust thread
    // sees SCO_UNAVAILABLE from the view, closes its window and leaves the view by itself.
    s.running.store(false, std::memory_order_release);
    if (s.module.requestStop) s.module.requestStop();
    if (s.inner.stop) s.inner.stop(ctx, s.inner.context);
}

} // namespace

bool Requested() {
    char v[16]{};
    return GetEnvironmentVariableA("SC_OFFLINE_MENU", v, sizeof(v)) && _stricmp(v, "rust") == 0;
}

sco::app::HostMenuClient Wrap(const sco::app::HostMenuClient& inner) {
    if (!Requested()) return inner;
    State& s = S();
    s.inner = inner;
    return sco::app::HostMenuClient{nullptr, Load, Frame, Stop};
}

bool Active() { return S().running.load(std::memory_order_acquire); }

} // namespace sc_offline::menu_rust
#endif
