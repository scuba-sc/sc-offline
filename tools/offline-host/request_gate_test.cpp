// SPDX-License-Identifier: GPL-3.0-only
// Tests the product's actual atomic request gate with synthetic work, not the
// Core owner, a DLL, native callbacks, or the product's hard-false policy.
#include "../../src/stop_request_gate.h"
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>
using offline_host::detail::StopRequestGate;

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    StopRequestGate gate;
    if (!gate.Begin() || !gate.RestartRequired() || gate.DrainMayStart()) return 1;
    bool result = false;
    if (!std::strcmp(argv[1], "normal")) {
        result = gate.Finish() && gate.DrainMayStart() && !gate.Begin();
    } else if (!std::strcmp(argv[1], "reentrant")) {
        result = !gate.Begin() && !gate.DrainMayStart() && !gate.Finish() &&
                 gate.DrainMayStart() && !gate.Begin();
    } else if (!std::strcmp(argv[1], "failure")) {
        try { throw std::runtime_error("synthetic request failure"); }
        catch (...) { result = gate.Finish(); }
        result = result && gate.RestartRequired() && gate.DrainMayStart() && !gate.Begin();
    } else if (!std::strcmp(argv[1], "competing")) {
        std::mutex mutex;
        std::condition_variable cv;
        bool completed = false;
        bool admitted = true;
        std::thread competitor([&] {
            admitted = gate.Begin();
            { std::lock_guard lock(mutex); completed = true; }
            cv.notify_one();
        });
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return completed; });
        }
        // A contender has completed but the first request has not. Drain must
        // still wait for the first request to finish and publish retention.
        const bool pending = !gate.DrainMayStart();
        competitor.join();
        result = !admitted && pending && !gate.Finish() && gate.DrainMayStart() && !gate.Begin();
    } else {
        return 2;
    }
    std::printf("product stop request gate %s: %s\n", argv[1], result ? "PASS" : "FAIL");
    return result ? 0 : 1;
}
