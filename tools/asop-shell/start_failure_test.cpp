// SPDX-License-Identifier: GPL-3.0-only
// Product result propagation with a synthetic partial provider; no native hooks.
#include "../../src/asop_shell.h"
#include <cstdio>
#include <stdexcept>

namespace host = sco::game::asop_host;
namespace {
host::Result nextResult = host::Result::Unavailable;
bool admitted = false, throwAfterInstall = false;
int starts = 0, stops = 0;
}
namespace sco::game::asop_host {
Result Start(const Config&) {
    ++starts; admitted = true;
    if (throwAfterInstall) throw std::runtime_error("synthetic installation failure");
    return nextResult;
}
void Stop() { ++stops; admitted = false; } // synthetic provider retains its backing
Status Snapshot() { return {}; }
void Pump(uint32_t) {}
}
int main() {
    offline::asop::Options options;
    if (offline::asop::Start(options, true, true) != host::Result::Unavailable ||
        starts != 1 || stops != 1 || admitted) return 1;
    nextResult = host::Result::Ok;
    if (offline::asop::Start(options, true, true) != host::Result::Ok ||
        starts != 2 || stops != 1 || !admitted) return 2;
    nextResult = host::Result::AlreadyStarted;
    if (offline::asop::Start(options, true, true) != host::Result::AlreadyStarted ||
        starts != 3 || stops != 1 || !admitted) return 3;
    throwAfterInstall = true;
    if (offline::asop::Start(options, true, true) != host::Result::Unavailable ||
        starts != 4 || stops != 2 || admitted) return 4;
    std::puts("ASOP start failure propagation: PASS (synthetic)");
    return 0;
}
