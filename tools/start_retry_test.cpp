// SPDX-License-Identifier: GPL-3.0-only
#include "start_retry.h"
#include <cassert>
#include <cstdio>
#include <cstdint>

int main() {
    sc_offline::StartRetry retry;
    int attempts = 0;
    auto start = [&] { ++attempts; };
    retry.Tick(true, 100, start);   // initial attempt is immediate
    for (uint32_t now = 101; now < 600; ++now) retry.Tick(true, now, start);
    assert(attempts == 1);
    retry.Tick(true, 600, start);
    assert(attempts == 2);
    retry.Tick(false, 601, start); // running or terminal phases never start
    retry.Tick(false, 1200, start);
    assert(attempts == 2);
    retry.Tick(true, UINT32_MAX - 100, start); // a new Prepared phase starts immediately
    retry.Tick(true, 398, start);             // elapsed 499 across the clock wrap
    assert(attempts == 3);
    retry.Tick(true, 399, start);             // elapsed 500 across the clock wrap
    assert(attempts == 4);
    retry.Tick(false, 400, start);
    retry.Tick(true, 401, [&] {
        ++attempts;
        retry.Tick(false, 402, start); // Starting must not reset the outer attempt
        retry.Tick(true, 403, start);  // nor recursively start
    });
    retry.Tick(true, 404, start);
    assert(attempts == 5);
    retry.Tick(true, 901, start);
    assert(attempts == 6);

    // Model the host boundary: every attempt observes current admission; a refusal
    // remains Prepared, and success moves to Running. A stopped host is terminal.
    for (int session = 0; session != 3; ++session) {
        sc_offline::StartRetry session_retry;
        enum class Phase { Prepared, Running, Stopped } phase = Phase::Prepared;
        bool admitted = false;
        int inspections = 0, starts = 0;
        auto inspect_and_start = [&] {
            ++inspections;
            if (admitted) { ++starts; phase = Phase::Running; }
        };
        session_retry.Tick(phase == Phase::Prepared, 0, inspect_and_start);
        assert(inspections == 1 && starts == 0 && phase == Phase::Prepared);
        admitted = true;
        session_retry.Tick(phase == Phase::Prepared, 499, inspect_and_start);
        assert(inspections == 1 && starts == 0); // no cached or premature admission
        session_retry.Tick(phase == Phase::Prepared, 500, inspect_and_start);
        assert(inspections == 2 && starts == 1 && phase == Phase::Running);
        session_retry.Tick(phase == Phase::Prepared, 501, inspect_and_start);
        phase = Phase::Stopped;
        session_retry.Tick(phase == Phase::Prepared, 1000, inspect_and_start);
        session_retry.Tick(phase == Phase::Prepared, 1500, inspect_and_start);
        assert(inspections == 2 && starts == 1); // repeated stop/messages do not restart
    }
    std::puts("host start retry: cadence, phase reset and clock wrap passed");
}
