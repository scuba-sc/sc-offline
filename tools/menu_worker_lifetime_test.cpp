// SPDX-License-Identifier: GPL-3.0-only
// Deferred synthetic checks for the admission used by the actual menu callback.
// No renderer, native host, game process or claim of Windows worker validation.
#include "menu_callback_admission.h"
#include "menu_frame_protocol.h"
#include <atomic>
#include <cstdio>
#include <thread>

using sc_offline::menu_lifetime::CallbackAdmission;
using sc_offline::menu_lifetime::CallbackLease;
using sc_offline::menu_frame::Protocol;

#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #expr); return 1; } } while (false)

int main() {
    // Stop before start/request: repeated close is idempotent and terminal.
    CallbackAdmission neverStarted;
    CHECK(!neverStarted.ClosedAndDrained());
    neverStarted.Close(); neverStarted.Close();
    CHECK(neverStarted.ClosedAndDrained());
    CHECK(!neverStarted.Enter());

    // Finishing a frame is NOT enough: its event wake still borrows backing.
    CallbackAdmission callbacks;
    Protocol frame;
    CHECK(frame.Request());
    {
        CallbackLease lease(callbacks);
        CHECK(lease && frame.Acquire());
        callbacks.Close();
        frame.Finish(true);
        CHECK(!callbacks.ClosedAndDrained());
        CHECK(!callbacks.Enter());
        CHECK(frame.TakeBuilt());
        // This scope models the final event wake, still under the lease.
    }
    CHECK(callbacks.ClosedAndDrained());

    // Unstarted requests can be cancelled after admission closes.
    CallbackAdmission unstarted;
    Protocol requested;
    CHECK(requested.Request());
    unstarted.Close();
    CHECK(!unstarted.Enter() && requested.CancelUnstarted());
    CHECK(unstarted.ClosedAndDrained());

    // A blocked callback keeps backing required until its actual return.
    CallbackAdmission blocked;
    std::atomic<bool> entered{false}, release{false};
    std::thread held([&] {
        CallbackLease lease(blocked);
        entered.store(static_cast<bool>(lease));
        while (!release.load()) std::this_thread::yield();
    });
    while (!entered.load()) std::this_thread::yield();
    blocked.Close();
    const bool retained = !blocked.ClosedAndDrained() && !blocked.Enter();
    release.store(true);
    held.join();
    CHECK(retained && blocked.ClosedAndDrained());

    // Close and first admission race at the same atomic word. No read admitted
    // after close can make the closed-and-drained result revert to active.
    for (int i = 0; i != 1000; ++i) {
        CallbackAdmission race;
        std::atomic<bool> go{false}, attempted{false}, finish{false};
        std::atomic<bool> admitted{false};
        std::thread reader([&] {
            while (!go.load()) std::this_thread::yield();
            CallbackLease lease(race);
            admitted.store(static_cast<bool>(lease));
            attempted.store(true);
            while (!finish.load()) std::this_thread::yield();
        });
        go.store(true);
        race.Close();
        while (!attempted.load()) std::this_thread::yield();
        const bool census = race.ClosedAndDrained() == !admitted.load();
        const bool refused = !race.Enter();
        finish.store(true);
        reader.join();
        CHECK(census && refused && race.ClosedAndDrained());
    }
    std::puts("menu callback admission: synthetic cases passed");
}
