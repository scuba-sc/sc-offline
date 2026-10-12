// SPDX-License-Identifier: GPL-3.0-only
#include "platform.h"
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <chrono>
#ifdef _WIN32
#include <tlhelp32.h>
#include <cwchar>
#include <vector>
#endif

namespace {
const sco_api* g_api = nullptr;
sco_plugin* g_self = nullptr;
#ifdef _WIN32
uint64_t g_launchDue = 0;
uint64_t g_placedAt = 0;
wchar_t g_game[260] = {};
wchar_t g_extra[256] = {};

BOOL CALLBACK FindHostWindow(HWND wnd, LPARAM out) {
    DWORD pid = 0;
    GetWindowThreadProcessId(wnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(wnd) || GetWindow(wnd, GW_OWNER)) return TRUE;
    wchar_t title[128] = {};
    if (GetWindowTextW(wnd, title, 128) && wcsstr(title, L"Star Citizen")) {
        *reinterpret_cast<HWND*>(out) = wnd;
        return FALSE;
    }
    return TRUE;
}

bool ProcessRunning(const wchar_t* exe) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe))
        found = !_wcsicmp(pe.szExeFile, exe);
    CloseHandle(snap);
    return found;
}

bool IsFile(const wchar_t* path) {
    const DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

enum class EaState { Ready, Started, Failed };
EaState StartEaApp() {
    if (ProcessRunning(L"EADesktop.exe") || ProcessRunning(L"Origin.exe")) return EaState::Ready;
    wchar_t path[260] = L"C:\\Program Files\\Electronic Arts\\EA Desktop\\EA Desktop\\EADesktop.exe";
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Origin", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        wchar_t found[260] = {};
        DWORD bytes = sizeof(found) - sizeof(wchar_t), type = 0;
        if (RegQueryValueExW(key, L"ClientPath", nullptr, &type, reinterpret_cast<LPBYTE>(found), &bytes) == ERROR_SUCCESS &&
            type == REG_SZ) {
            found[259] = 0;
            const size_t n = wcslen(found);
            if (n > 4 && !_wcsicmp(found + n - 4, L".exe") && IsFile(found)) wcscpy_s(path, found);
        }
        RegCloseKey(key);
    }
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWMINNOACTIVE;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(path, nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        TlPlatformLog(SCO_LOG_WARN, "[titanlink] couldn't start the EA app (%lu); open it and sign in, then launch again", GetLastError());
        return EaState::Failed;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    TlPlatformLog(SCO_LOG_INFO, "[titanlink] started the EA app; Northstar starts in 25 seconds");
    return EaState::Started;
}

bool LaunchNorthstar() {
    uint32_t w = 0, h = 0;
    TlPlatformViewSize(w, h);
    if (w < 64 || h < 64 || w > 8192 || h > 8192) { w = 1920; h = 1080; }
    wchar_t exe[320] = {}, cmd[1200] = {};
    if (swprintf_s(exe, L"%ls\\NorthstarLauncher.exe", g_game) < 0 ||
        swprintf_s(cmd, L"\"%ls\" -novid -windowed -noborder -w %u -h %u %ls", exe, w, h, g_extra) < 0) {
        TlPlatformLog(SCO_LOG_WARN, "[titanlink] the Northstar path or arguments are too long");
        return false;
    }
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNOACTIVATE;
    PROCESS_INFORMATION pi = {};
    TlPlatformLog(SCO_LOG_INFO, "[titanlink] starting Titanfall 2: %ls", cmd);
    if (!CreateProcessW(exe, cmd, nullptr, nullptr, FALSE, 0, nullptr, g_game, &si, &pi)) {
        TlPlatformLog(SCO_LOG_WARN, "[titanlink] couldn't start Northstar (%lu); check the configured Titanfall 2 folder", GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

bool Utf8(const char* text, wchar_t* out, int capacity) {
    if (!text) return false;
    // Reject control characters: configuration must not forge log lines or commands.
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p)
        if (*p < 0x20 || *p == 0x7f) return false;
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, out, capacity) != 0;
}

bool SameUser(HANDLE process) {
    HANDLE peer = nullptr, own = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &peer)) return false;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &own)) { CloseHandle(peer); return false; }
    DWORD pn = 0, on = 0;
    GetTokenInformation(peer, TokenUser, nullptr, 0, &pn);
    GetTokenInformation(own, TokenUser, nullptr, 0, &on);
    std::vector<unsigned char> pb(pn), ob(on);
    const bool ok = pn && on && GetTokenInformation(peer, TokenUser, pb.data(), pn, &pn) &&
                    GetTokenInformation(own, TokenUser, ob.data(), on, &on) &&
                    EqualSid(reinterpret_cast<TOKEN_USER*>(pb.data())->User.Sid,
                             reinterpret_cast<TOKEN_USER*>(ob.data())->User.Sid);
    CloseHandle(peer);
    CloseHandle(own);
    return ok;
}

bool IsTitanfallProcess(DWORD pid) {
    if (!pid || pid == GetCurrentProcessId()) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    wchar_t path[32768] = {};
    DWORD n = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &n) && SameUser(process);
    CloseHandle(process);
    if (!ok) return false;
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return !_wcsicmp(name, L"NorthstarLauncher.exe") || !_wcsicmp(name, L"Titanfall2.exe");
}

struct WindowSearch { DWORD pid; HWND wnd; };
BOOL CALLBACK FindPeerWindow(HWND wnd, LPARAM arg) {
    auto* search = reinterpret_cast<WindowSearch*>(arg);
    DWORD pid = 0;
    GetWindowThreadProcessId(wnd, &pid);
    if (pid != search->pid || !IsWindowVisible(wnd) || GetWindow(wnd, GW_OWNER)) return TRUE;
    RECT r = {};
    if (!GetWindowRect(wnd, &r) || r.right - r.left < 200) return TRUE;
    search->wnd = wnd;
    return FALSE;
}
#endif
} // namespace

void TlPlatformLog(sco_log_level level, const char* format, ...) {
    if (!g_api || g_api->size < offsetof(sco_api, log) + sizeof(g_api->log) || !g_api->log) return;
    char line[512] = {};
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    g_api->log(g_self, level, line);
}

void TlPlatformConfigure(const sco_api* api, sco_plugin* self) {
    TlPlatformStop();
    TlOverlayStop(); // Join before replacing the logger's owner or API table.
    g_api = api;
    g_self = self;
}

void TlPlatformStop() {
#ifdef _WIN32
    g_launchDue = 0; // No deferred launch may survive pilot cancellation, unlink, or unload.
    g_placedAt = 0;
    g_game[0] = g_extra[0] = 0;
#endif
    // Pilot cancellation leaves the channel's overlay worker available for the next pilot-on.
    // Unlink/unload must call TlOverlayStop before channel close/service release.
}

uint64_t TlPlatformNow() {
#ifdef _WIN32
    return GetTickCount64();
#else
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}

bool TlPlatformLaunch(const char* game, const char* extra) {
#ifdef _WIN32
    if (g_launchDue) return true;
    if (!game || !*game || !Utf8(game, g_game, 260) || !Utf8(extra ? extra : "", g_extra, 256) || wcschr(g_game, L'"')) {
        TlPlatformLog(SCO_LOG_WARN, "[titanlink] invalid Titanfall 2 folder or launch arguments");
        return false;
    }
    wchar_t exe[320] = {};
    if (swprintf_s(exe, L"%ls\\NorthstarLauncher.exe", g_game) < 0 || !IsFile(exe)) {
        TlPlatformLog(SCO_LOG_WARN, "[titanlink] NorthstarLauncher.exe is missing from the configured folder");
        return false;
    }
    const EaState state = StartEaApp();
    if (state == EaState::Failed) return false;
    if (state == EaState::Started) { g_launchDue = TlPlatformNow() + 25000; return true; }
    return LaunchNorthstar();
#else
    (void)game; (void)extra;
    TlPlatformLog(SCO_LOG_WARN, "[titanlink] launching Northstar and rendering are unavailable on this platform");
    return false;
#endif
}

void TlPlatformTick(uint64_t now, uint32_t peer_pid) {
#ifdef _WIN32
    if (g_launchDue && now >= g_launchDue) { g_launchDue = 0; LaunchNorthstar(); }
    if (g_placedAt && now - g_placedAt < 2000) return;
    g_placedAt = now;
    if (!IsTitanfallProcess(peer_pid)) return;
    WindowSearch search = { peer_pid, nullptr };
    EnumWindows(FindPeerWindow, reinterpret_cast<LPARAM>(&search));
    HWND host = TlPlatformWindow();
    if (!search.wnd || !host) return;
    RECT rc = {}, cur = {};
    POINT origin = {};
    if (!GetClientRect(host, &rc) || !ClientToScreen(host, &origin) || !GetWindowRect(search.wnd, &cur) ||
        rc.right <= 0 || rc.bottom <= 0 || IsIconic(host)) return;
    const bool same = cur.left == origin.x && cur.top == origin.y &&
                      cur.right - cur.left == rc.right && cur.bottom - cur.top == rc.bottom;
    SetWindowPos(search.wnd, HWND_BOTTOM, origin.x, origin.y, rc.right, rc.bottom,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER | (same ? SWP_NOMOVE | SWP_NOSIZE : 0));
#else
    (void)now; (void)peer_pid;
#endif
}

#ifdef _WIN32
HWND TlPlatformWindow() {
    HWND wnd = nullptr;
    EnumWindows(FindHostWindow, reinterpret_cast<LPARAM>(&wnd));
    return wnd;
}
#endif

bool TlPlatformFocused() {
#ifdef _WIN32
    HWND wnd = TlPlatformWindow();
    return wnd && !IsIconic(wnd) && GetForegroundWindow() == wnd;
#else
    return false;
#endif
}

bool TlPlatformKey(int vk) {
#ifdef _WIN32
    return vk > 0 && vk <= 255 && TlPlatformFocused() && (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
    (void)vk;
    return false;
#endif
}

uint32_t TlPlatformButtons(bool titan, bool busy) {
#ifdef _WIN32
    if (busy || !TlPlatformFocused()) return 0;
    uint32_t b = 0;
    if (TlPlatformKey(VK_LBUTTON)) b |= TL_BTN_ATTACK;
    if (TlPlatformKey(VK_RBUTTON)) b |= TL_BTN_ZOOM;
    if (TlPlatformKey('R')) b |= TL_BTN_RELOAD;
    if (TlPlatformKey('F')) b |= TL_BTN_MELEE;
    if (TlPlatformKey('G')) b |= TL_BTN_OFFHAND;
    if (TlPlatformKey('Q')) b |= TL_BTN_ABILITY;
    if (titan && TlPlatformKey('C')) b |= TL_BTN_TITAN_C;
    if (titan && TlPlatformKey('V')) b |= TL_BTN_TITAN_V;
    if (TlPlatformKey('Y')) b |= TL_BTN_CYCLE;
    if (TlPlatformKey('X')) b |= TL_BTN_X;
    return b;
#else
    (void)titan; (void)busy;
    return 0;
#endif
}

void TlPlatformViewSize(uint32_t& w, uint32_t& h) {
    w = h = 0;
#ifdef _WIN32
    RECT rc = {};
    HWND wnd = TlPlatformWindow();
    if (wnd && GetClientRect(wnd, &rc)) {
        w = static_cast<uint32_t>(rc.right > rc.left ? rc.right - rc.left : 0);
        h = static_cast<uint32_t>(rc.bottom > rc.top ? rc.bottom - rc.top : 0);
    }
#endif
}

bool TlPlatformAvailable() {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}
