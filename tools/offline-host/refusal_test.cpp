// SPDX-License-Identifier: GPL-3.0-only
// Test the actual product composition seam with an always-refusing core stub.
// No native reader gate, game image, Windows hook or success callback is modeled.
#include "../../src/offline_host.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace gh = sco::game::host;
namespace {
int prepareCalls = 0, startCalls = 0, tickCalls = 0, statusCalls = 0, stopCalls = 0;
bool throwOnStop = false;
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
Result Stop() { ++stopCalls; if (throwOnStop) throw std::runtime_error("synthetic stop failure"); return Result::Failed; }
Snapshot Status() { ++statusCalls; return {}; }
}
int main(int argc, char** argv) {
    if (argc > 2 || (argc == 2 && std::strcmp(argv[1], "exception") != 0)) return 2;
    throwOnStop = argc == 2;
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
    const bool after = offline_host::Stop() == sco::Result::Failed && offline_host::RestartRequired() &&
        offline_host::Prepare(options) == sco::Result::Unavailable && offline_host::Start() == sco::Result::Unavailable &&
        offline_host::Stop() == sco::Result::Failed && stopCalls == 1 && prepareCalls == 0 && startCalls == 0;
    const bool ok = before && after;
    std::printf("offline host native activation refusal: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
