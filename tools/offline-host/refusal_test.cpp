// SPDX-License-Identifier: GPL-3.0-only
// Test the actual product composition seam with an always-refusing core stub.
// No native reader gate, game image, Windows hook or success callback is modeled.
#include "../../src/offline_host.h"
#include "../../src/inventory_filter_dispatch.h"
#include <cstdio>

namespace gh = sco::game::host;
namespace {
int prepareCalls = 0, startCalls = 0, tickCalls = 0, statusCalls = 0, stopCalls = 0;
gh::Admission Inspect() { return {}; }
}
namespace sco::plugins {
ModuleOps PlatformModuleOps() { return {}; }
}
namespace sco::game::host {
Result Prepare(const Options& options) {
    (void)options;
    ++prepareCalls;
    return Result::Unavailable;
}
Result Start() { ++startCalls; return Result::Unavailable; }
Result Tick(uint32_t) { ++tickCalls; return Result::Unavailable; }
Result Stop() { ++stopCalls; return Result::Failed; }
Snapshot Status() { ++statusCalls; return {}; }
}
int main() {
    int originalCalls = 0, postCalls = 0;
    const auto original = [&](auto, auto, auto, auto, auto, auto, auto, auto) {
        ++originalCalls; return std::uint64_t{1};
    };
    const auto post = [&](auto, auto, auto, auto) { ++postCalls; return std::uint64_t{4}; };
    const auto filter = sc_offline::inventory_filter::Dispatch(original, post, 1, 2, 3, 4, 5, 6, 7, 8);
    const bool filterRefused = filter == 1 && originalCalls == 1 && postCalls == 0;
    gh::Options options;
    options.inspect = Inspect;
    options.quit = gh::QuitPolicy::External;
    options.dataCoreHook = gh::DataCoreHook::External;
    options.platform.gameServices = true;
    const auto result = offline_host::Prepare(options);
    options.platform.gameServices = false;
    const auto second = offline_host::Prepare(options);
    const bool before = result == sco::Result::Unavailable && second == sco::Result::Unavailable &&
        offline_host::Start() == sco::Result::Unavailable &&
        offline_host::Tick(1) == sco::Result::Unavailable && !offline_host::Running() &&
        !offline_host::RestartRequired() && prepareCalls == 0 && startCalls == 0 && tickCalls == 0 && statusCalls == 0;
    // Native policy refuses before any owner request or synchronous Core Stop.
    const bool after = offline_host::Stop() == sco::Result::Unavailable && !offline_host::RestartRequired() &&
        offline_host::Prepare(options) == sco::Result::Unavailable && offline_host::Start() == sco::Result::Unavailable &&
        offline_host::Stop() == sco::Result::Unavailable && stopCalls == 0 && prepareCalls == 0 && startCalls == 0;
    const bool ok = before && after && filterRefused;
    std::printf("offline host native activation refusal: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
