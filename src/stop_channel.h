#pragma once
// SPDX-License-Identifier: GPL-3.0-only
// The launcher's Stop request channel. The mod (dinput8.dll) creates two named events scoped to its own
// process; the launcher opens them by the game's process id. Both live in the session-local namespace
// (Local\) with the default security descriptor, so only the same user can open them; nothing is global.
//   Local\sc-offline-stop-<pid>     launcher -> mod: run the exit work now (manual-reset, stays signalled)
//   Local\sc-offline-stopped-<pid>  mod -> launcher: the exit work has run, or there is nothing left to run
// The stop event exists only in a process where the mod is loaded and offline, so its absence tells the
// launcher to close the window directly. No game internals, no unloading: the mod only runs the closure
// a normal quit would have run, and the launcher still closes the game. Handles are kept until exit.
#include <windows.h>
#include <atomic>
#include <cstdio>

namespace sc_offline::stop_channel {

inline void EventName(wchar_t (&out)[64], const wchar_t* kind, DWORD pid) {
    swprintf_s(out, L"Local\\sc-offline-%ls-%lu", kind, static_cast<unsigned long>(pid));
}

// Mod side. Create once, from any thread, before or after the game thread starts polling; Poll and Retire are
// safe against that (the stop handle is published last). Poll runs on the game thread.
class HostChannel {
public:
    // False when an event of that name already exists (someone else made it) or creation fails.
    bool Create(DWORD pid, DWORD* error = nullptr) {
        if (stop_.load(std::memory_order_acquire)) return true;
        wchar_t name[64];
        EventName(name, L"stopped", pid);
        HANDLE stopped = CreateEventW(nullptr, TRUE, FALSE, name);
        if (!stopped || GetLastError() == ERROR_ALREADY_EXISTS) return Fail(error, stopped, nullptr);
        EventName(name, L"stop", pid);
        HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, name);
        if (!stop || GetLastError() == ERROR_ALREADY_EXISTS) return Fail(error, stopped, stop);
        stopped_ = stopped;
        stop_.store(stop, std::memory_order_release);   // published last
        // Retired before the channel existed (the closure was reached early): nothing to wait for, answer at once.
        if (retired_.load()) SetEvent(stopped_);
        return true;
    }
    bool Created() const { return stop_.load(std::memory_order_acquire) != nullptr; }
    bool Requested() const {
        HANDLE stop = stop_.load(std::memory_order_acquire);
        return stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0;
    }
    bool Done() const { return done_.load(std::memory_order_acquire); }

    // Game thread. When the launcher has signalled, runs `work` once and then signals the stopped event.
    // Returns true only on the call that ran the work; a repeated signal, a re-entrant call (a plugin pumping
    // messages inside the work) or a call after Retire does nothing.
    template <class Work>
    bool Poll(Work&& work) {
        if (!Created() || done_.load() || retired_.load() || !Requested()) return false;
        if (running_.exchange(true)) return false;   // claimed by a running call (a re-entrant one)
        if (retired_.load()) {
            // Retired between the check and the claim: Retire may have seen the claim and left the answer to us.
            running_.store(false);
            SetEvent(stopped_);
            return false;
        }
        work();
        done_.store(true);
        running_.store(false);
        SetEvent(stopped_);
        return true;
    }

    // Any thread. The mod's closure was reached some other way (WM_QUIT, lost admission, an earlier stop):
    // there is no exit work left for the launcher to wait for, so a Stop answers at once instead of timing out.
    // While the launcher's own work is running the answer is left to its end.
    void Retire() {
        retired_.store(true);
        if (Created() && !running_.load()) SetEvent(stopped_);
    }

private:
    bool Fail(DWORD* error, HANDLE a, HANDLE b) {
        if (error) *error = GetLastError();
        if (a) CloseHandle(a);
        if (b) CloseHandle(b);
        return false;
    }
    std::atomic<HANDLE> stop_{nullptr};
    HANDLE stopped_ = nullptr;   // written before stop_ is published
    std::atomic<bool> done_{false}, running_{false}, retired_{false};
};

// The decision sequence of the launcher-stop exit work, with the product's pieces behind `ops` so a test can
// drive it with fakes. ops: Log(const char*), CloseHost() (Core's closure, on this stack), HostClosed(),
// SaveWallet() (latched by the caller's side).
template <class Ops>
void RunExitWork(Ops& ops) {
    ops.Log("[app] stop requested by the launcher: running the exit work");
    ops.CloseHost();
    const bool closed = ops.HostClosed();
    // Closure refused on this stack (a callout holds Core's gate): the wallet is still saved here, as
    // game.exit would have. The save is latched, so a later quit does not repeat it.
    if (!closed) ops.SaveWallet();
    ops.Log(closed ? "[app] stop requested by the launcher: exit work done"
                   : "[app] stop requested by the launcher: exit work done except Core's closure "
                     "(refused on this stack; wallet saved directly)");
}

// Launcher side.
enum class Outcome { NotPresent, Denied, Acknowledged, TimedOut, ProcessExited };
struct Request {
    Outcome outcome = Outcome::NotPresent;
    DWORD elapsedMs = 0;
    DWORD error = 0;   // Denied: the Win32 error from opening an event
};

// One target's wait. Begin signals without waiting; Poll is non-blocking when sliceMs is 0.
struct Waiter {
    DWORD pid = 0;
    HANDLE stopped = nullptr;   // owned
    HANDLE process = nullptr;   // owned, optional: a game that dies ends the wait
    ULONGLONG begun = 0;
    Request result;
    bool finished = false;
};

// Opens the events of `pid`; when the stop event exists, signals it. Finished at once when the mod is not there.
inline void Begin(Waiter& w, DWORD pid, HANDLE process) {
    w = Waiter{};
    w.pid = pid;
    w.process = process;
    wchar_t name[64];
    EventName(name, L"stop", pid);
    HANDLE stop = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
    if (!stop) {
        w.result.error = GetLastError();
        w.result.outcome = w.result.error == ERROR_FILE_NOT_FOUND ? Outcome::NotPresent : Outcome::Denied;
        w.finished = true;
        return;
    }
    EventName(name, L"stopped", pid);
    w.stopped = OpenEventW(SYNCHRONIZE, FALSE, name);
    if (!w.stopped) {
        w.result.error = GetLastError();
        w.result.outcome = Outcome::Denied;
        w.finished = true;
        CloseHandle(stop);
        return;
    }
    w.begun = GetTickCount64();
    SetEvent(stop);
    CloseHandle(stop);
}

// True once the wait is over (answered, process gone, or timed out after timeoutMs).
inline bool Poll(Waiter& w, DWORD timeoutMs, DWORD sliceMs = 0) {
    if (w.finished) return true;
    HANDLE h[2] = { w.stopped, w.process };
    const DWORD n = w.process ? 2 : 1;
    const DWORD r = WaitForMultipleObjects(n, h, FALSE, sliceMs);
    w.result.elapsedMs = static_cast<DWORD>(GetTickCount64() - w.begun);
    if (r == WAIT_OBJECT_0) w.result.outcome = Outcome::Acknowledged;
    else if (r == WAIT_OBJECT_0 + 1) w.result.outcome = Outcome::ProcessExited;
    else if (w.result.elapsedMs >= timeoutMs) w.result.outcome = Outcome::TimedOut;
    else return false;
    w.finished = true;
    return true;
}

inline void Release(Waiter& w) {
    if (w.stopped) CloseHandle(w.stopped);
    if (w.process) CloseHandle(w.process);
    w.stopped = w.process = nullptr;
}

// Blocking form: signals the stop event of `pid` if it exists and waits up to `timeoutMs` for the answer.
inline Request RequestStop(DWORD pid, DWORD timeoutMs) {
    Waiter w;
    Begin(w, pid, nullptr);
    while (!Poll(w, timeoutMs, 20)) {}
    Release(w);
    return w.result;
}

}  // namespace sc_offline::stop_channel
