// SPDX-License-Identifier: GPL-3.0-only
// Test the actual product composition seam with an always-refusing core stub.
// No native reader gate, game image, Windows hook or success callback is modeled.
#include "../../src/offline_host.h"
#include <cstdio>

namespace gh = sco::game::host;
namespace {
int prepareCalls = 0;
gh::Options received;
gh::Admission Inspect() { return {}; }
}
namespace sco::plugins {
ModuleOps PlatformModuleOps() { return {}; }
}
namespace sco::game::host {
Result Prepare(const Options& options) {
    ++prepareCalls;
    received = options;
    return Result::Unavailable;
}
Result Start() { return Result::Unavailable; }
Result Tick(uint32_t) { return Result::Unavailable; }
Result Stop() { return Result::Unavailable; }
Snapshot Status() { return {}; }
}
int main() {
    gh::Options options;
    options.inspect = Inspect;
    options.quit = gh::QuitPolicy::External;
    options.dataCoreHook = gh::DataCoreHook::External;
    options.platform.gameServices = true;
    const auto result = offline_host::Prepare(options);
    const bool ok = result == sco::Result::Unavailable && prepareCalls == 1 &&
        received.inspect == Inspect && received.quit == gh::QuitPolicy::Required &&
        received.dataCoreHook == gh::DataCoreHook::Required && received.platform.gameServices &&
        !received.nativeDataCoreReads && !received.preEngineQuiesced &&
        options.quit == gh::QuitPolicy::External && options.dataCoreHook == gh::DataCoreHook::External;
    std::printf("offline host refusal forwarding: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
