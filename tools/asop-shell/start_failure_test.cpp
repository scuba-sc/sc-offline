// SPDX-License-Identifier: GPL-3.0-only
// Product result propagation with a synthetic partial provider; no native hooks.
#include "../../src/asop_shell.h"
#include <cstdio>
#include <stdexcept>

namespace host = sco::game::asop_host;
namespace {
host::Result nextResult = host::Result::Unavailable;
bool backingLive = false, throwAfterInstall = false;
int starts = 0, stops = 0;

void Reset(host::Result result, bool throws = false, bool existingBacking = false) {
    nextResult = result;
    throwAfterInstall = throws;
    backingLive = existingBacking;
    starts = 0;
    stops = 0;
}

int Check(const char* scenario, host::Result expected) {
    offline::asop::Options options;
    const auto result = offline::asop::Start(options, true, true);
    if (result == expected && starts == 1 && stops == 0 && backingLive) return 0;
    std::fprintf(stderr, "%s: result=%u starts=%d stops=%d backing=%d\n", scenario,
                 static_cast<unsigned>(result), starts, stops, backingLive);
    return 1;
}
}
namespace sco::game::asop_host {
Result Start(const Config&) {
    ++starts;
    // AlreadyStarted reports an existing installation, not a new partial start.
    if (nextResult != Result::AlreadyStarted) backingLive = true;
    if (throwAfterInstall) throw std::runtime_error("synthetic installation failure");
    return nextResult;
}
// Simulate unsafe teardown so the fixture catches any synchronous Stop call.
void Stop() { ++stops; backingLive = false; }
Status Snapshot() { return {}; }
void Pump(uint32_t) {}
}
int main() {
    int failures = 0;
    Reset(host::Result::Unavailable);
    failures += Check("partial start refused", host::Result::Unavailable);
    Reset(host::Result::AdmissionRefused);
    failures += Check("admission refused", host::Result::AdmissionRefused);
    Reset(host::Result::InvalidConfig);
    failures += Check("invalid configuration", host::Result::InvalidConfig);
    Reset(host::Result::Disabled);
    failures += Check("disabled", host::Result::Disabled);
    Reset(host::Result::Ok);
    failures += Check("started", host::Result::Ok);
    Reset(host::Result::AlreadyStarted, false, true);
    failures += Check("already started", host::Result::AlreadyStarted);
    Reset(host::Result::Ok, true);
    failures += Check("partial start threw", host::Result::Unavailable);
    if (failures) return 1;
    std::puts("ASOP start failure propagation: PASS (synthetic)");
    return 0;
}
