// SPDX-License-Identifier: GPL-3.0-only
// The real product wrapper is linked with a controllable core Stop stub.
// This does not model native readers, workers, hooks, or module ownership.
#include "../../src/offline_host.h"
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace {
std::atomic<int> calls{0};
std::mutex mutex;
std::condition_variable changed;
bool entered = false, releaseStop = false, reenter = false;
int mode = 0;
sco::Result nested = sco::Result::Ok;
}
namespace sco::plugins { ModuleOps PlatformModuleOps() { return {}; } }
namespace sco::game::host {
Result Prepare(const Options&) { return Result::Unavailable; }
Result Start() { return Result::Unavailable; }
Result Tick(uint32_t) { return Result::Unavailable; }
Snapshot Status() { return {}; }
Result Stop() {
    // Baseline permits a second entry. Return immediately there so the
    // negative control reports failure rather than deadlocking or recursing.
    const int count = ++calls;
    if (mode == 1) return Result::WrongThread;
    if (mode == 2 || count != 1) return Result::Ok;
    if (reenter) {
        nested = offline_host::Stop();
    } else {
        std::unique_lock lock(mutex);
        entered = true;
        changed.notify_all();
        changed.wait(lock, [] { return releaseStop; });
    }
    return Result::Ok;
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    reenter = std::strcmp(argv[1], "reentrant") == 0;
    if (std::strcmp(argv[1], "wrong-thread") == 0) mode = 1;
    if (std::strcmp(argv[1], "repeat-success") == 0) mode = 2;
    if (!reenter && mode == 0 && std::strcmp(argv[1], "overlap") != 0) return 2;
    if (mode == 1) {
        const bool ok = offline_host::Stop() == sco::Result::WrongThread &&
            offline_host::RestartRequired() && offline_host::Stop() == sco::Result::Failed && calls == 1;
        std::printf("wrong-thread result retention: %s\n", ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }
    if (mode == 2) {
        const bool ok = offline_host::Stop() == sco::Result::Ok &&
            offline_host::Stop() == sco::Result::Ok && !offline_host::RestartRequired() && calls == 2;
        std::printf("sequential successful stops: %s\n", ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }
    sco::Result outer = sco::Result::Ok, other = sco::Result::Ok;
    if (reenter) {
        outer = offline_host::Stop();
        other = nested;
    } else {
        std::thread worker([&] { outer = offline_host::Stop(); });
        {
            std::unique_lock lock(mutex);
            changed.wait(lock, [] { return entered; });
        }
        other = offline_host::Stop();
        {
            std::lock_guard lock(mutex);
            releaseStop = true;
        }
        changed.notify_all();
        worker.join();
    }
    const bool ok = outer == sco::Result::Failed && other == sco::Result::Failed &&
        calls == 1 && offline_host::RestartRequired() &&
        offline_host::Stop() == sco::Result::Failed && calls == 1 &&
        offline_host::Start() == sco::Result::Unavailable &&
        offline_host::Tick(0) == sco::Result::Unavailable && !offline_host::Running();
    std::printf("offline host %s stop refusal: %s\n", argv[1], ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
