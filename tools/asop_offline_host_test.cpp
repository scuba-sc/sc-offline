// SPDX-License-Identifier: GPL-3.0-only
// Windows selection composition must not bypass the native activation refusal.
#include "offline_host.h"
#include <windows.h>
#include <cstdio>

static int prepareCalls = 0, startCalls = 0;
bool ShipsFilePath(char*, DWORD) { return false; }
bool DataFilePath(char*, DWORD, const char*) { return false; }
namespace sco::game::host {
Result Prepare(const Options&) { ++prepareCalls; return Result::Unavailable; }
Result Start() { ++startCalls; return Result::Unavailable; }
Result Tick(uint32_t) { return Result::Unavailable; }
Result Stop() { return Result::Unavailable; }
Snapshot Status() { return {}; }
}
int main() {
    sco::game::host::Options options;
    options.platform.gameServices = true;
    if (offline_host::Prepare(options) != sco::Result::Unavailable ||
        offline_host::Start() != sco::Result::Unavailable || prepareCalls || startCalls) return 1;
    std::puts("ASOP selection composition refuses native activation: PASS");
    return 0;
}
