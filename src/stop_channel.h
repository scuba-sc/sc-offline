#pragma once
// SPDX-License-Identifier: GPL-3.0-only
// The launcher's Stop request channel. The mod (dinput8.dll) creates two named events scoped to its own
// process; the launcher opens them by the game's process id. Both live in the session-local namespace
// (Local\) with the default security descriptor, so only the same user can open them; nothing is global.
//   Local\sc-offline-stop-<pid>     launcher -> mod: run the exit work now (manual-reset, stays signalled)
//   Local\sc-offline-stopped-<pid>  mod -> launcher: the exit work has run (manual-reset)
// The stop event exists only in a process where the mod is loaded and offline, so its absence tells the
// launcher to close the window directly. No game internals, no unloading: the mod only runs the closure
// a normal quit would have run, and the launcher still closes the game.
#include <windows.h>
#include <cstdio>

namespace sc_offline::stop_channel {

inline void EventName(wchar_t (&out)[64], const wchar_t* kind, DWORD pid) {
    swprintf_s(out, L"Local\\sc-offline-%ls-%lu", kind, static_cast<unsigned long>(pid));
}

// Mod side. Create once, early; Poll on the game thread. Handles are kept until the process exits.
class HostChannel {
public:
    // False when an event of that name already exists (someone else made it) or creation fails.
    bool Create(DWORD pid, DWORD* error = nullptr) {
        if (stop_) return true;
        wchar_t name[64];
        EventName(name, L"stopped", pid);
        stopped_ = CreateEventW(nullptr, TRUE, FALSE, name);
        if (!stopped_ || GetLastError() == ERROR_ALREADY_EXISTS) return Fail(error);
        EventName(name, L"stop", pid);
        stop_ = CreateEventW(nullptr, TRUE, FALSE, name);
        if (!stop_ || GetLastError() == ERROR_ALREADY_EXISTS) return Fail(error);
        return true;
    }
    bool Created() const { return stop_ != nullptr; }
    bool Requested() const { return stop_ && WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0; }
    bool Done() const { return done_; }

    // Game thread. When the launcher has signalled, runs `work` once and then signals the stopped event.
    // Returns true only on the call that ran the work; a repeated signal or a re-entrant call (a plugin
    // pumping messages inside the work) does nothing.
    template <class Work>
    bool Poll(Work&& work) {
        if (!stop_ || done_ || running_ || !Requested()) return false;
        running_ = true;
        work();
        done_ = true;
        running_ = false;
        SetEvent(stopped_);
        return true;
    }

private:
    bool Fail(DWORD* error) {
        if (error) *error = GetLastError();
        if (stopped_) { CloseHandle(stopped_); stopped_ = nullptr; }
        if (stop_) { CloseHandle(stop_); stop_ = nullptr; }
        return false;
    }
    HANDLE stop_ = nullptr, stopped_ = nullptr;
    bool done_ = false, running_ = false;
};

// Launcher side.
enum class Outcome { NotPresent, Denied, Acknowledged, TimedOut };
struct Request {
    Outcome outcome = Outcome::NotPresent;
    DWORD elapsedMs = 0;
    DWORD error = 0;   // Denied: the Win32 error from opening the event
};

// Signals the stop event of `pid` if it exists and waits up to `timeoutMs` for the stopped event.
inline Request RequestStop(DWORD pid, DWORD timeoutMs) {
    Request r;
    wchar_t name[64];
    EventName(name, L"stop", pid);
    HANDLE stop = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
    if (!stop) {
        r.error = GetLastError();
        r.outcome = r.error == ERROR_FILE_NOT_FOUND ? Outcome::NotPresent : Outcome::Denied;
        return r;
    }
    EventName(name, L"stopped", pid);
    HANDLE stopped = OpenEventW(SYNCHRONIZE, FALSE, name);
    if (!stopped) {
        r.error = GetLastError();
        r.outcome = Outcome::Denied;
        CloseHandle(stop);
        return r;
    }
    const ULONGLONG start = GetTickCount64();
    SetEvent(stop);
    const DWORD wait = WaitForSingleObject(stopped, timeoutMs);
    r.elapsedMs = static_cast<DWORD>(GetTickCount64() - start);
    r.outcome = wait == WAIT_OBJECT_0 ? Outcome::Acknowledged : Outcome::TimedOut;
    CloseHandle(stopped);
    CloseHandle(stop);
    return r;
}

}  // namespace sc_offline::stop_channel
