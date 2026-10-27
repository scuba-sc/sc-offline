// SPDX-License-Identifier: GPL-3.0-only
// Real Windows SEH guard and event wake over the menu's frame protocol; no window or game.
#include "menu_frame_protocol.h"
#include "sco/plugins.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <thread>
static constexpr DWORD kFault = 0xe0001234u;
static void Fault(void*) { RaiseException(kFault, 0, 0, nullptr); }
static void Succeed(void*) {}
int main() {
    using namespace sc_offline::menu_frame;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event) return 1;
    Protocol p; std::atomic<DWORD> waitResult{WAIT_FAILED};
    std::thread waiter([&] { waitResult.store(WaitForSingleObject(event, 3000)); });
    unsigned aborts = 0; DWORD code = 0;
    const bool acquired = p.Request() && Run(p,
        [&] { code = sco::plugins::Guarded(Fault, nullptr); return code == 0; },
        [&] { ++aborts; }, [&] { SetEvent(event); });
    waiter.join();
    const bool cancelled = acquired && code == kFault && aborts == 1 && waitResult.load() == WAIT_OBJECT_0 && p.TakeCancelled();
    waitResult.store(WAIT_FAILED);
    std::thread next([&] { waitResult.store(WaitForSingleObject(event, 3000)); });
    const bool finished = p.Request() && Run(p,
        [] { return sco::plugins::Guarded(Succeed, nullptr) == 0; },
        [&] { ++aborts; }, [&] { SetEvent(event); });
    next.join();
    const bool success = finished && aborts == 1 && waitResult.load() == WAIT_OBJECT_0 && p.TakeBuilt();
    CloseHandle(event);
    if (!cancelled || !success) { std::printf("SEH frame failure/wake: cancelled=%d success=%d code=%08lx\n", cancelled, success, code); return 2; }
    std::puts("Windows SEH frame cancellation and event wake passed");
    return 0;
}
