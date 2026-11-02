// SPDX-License-Identifier: GPL-3.0-only
// Synthetic policy test: the product native-activation gate must refuse before
// host callbacks, ASOP file access, selection startup, or game-engine work.
#include "offline_host.h"
#include <cstdio>
#include <windows.h>

static unsigned prepareCalls=0, startCalls=0, tickCalls=0, stopCalls=0, statusCalls=0;

bool ShipsFilePath(char*, DWORD) { return false; }
bool DataFilePath(char*, DWORD, const char*) { return false; }
void Log(const char*, ...) {}

namespace sco::game::host {
Result Prepare(const Options&) { ++prepareCalls; return Result::Ok; }
Result Start() { ++startCalls; return Result::Ok; }
Result Tick(uint32_t) { ++tickCalls; return Result::Ok; }
Result Stop() { ++stopCalls; return Result::Ok; }
Snapshot Status() { ++statusCalls; Snapshot value; value.phase=Phase::Running; return value; }
}

int main() {
    sco::game::host::Options options;
    if (offline_host::Prepare(options)!=sco::Result::Unavailable ||
        offline_host::Start()!=sco::Result::Unavailable ||
        offline_host::Tick(1)!=sco::Result::Unavailable ||
        offline_host::Running()) {
        std::puts("ASOP host refusal: FAIL (native gate did not fail closed)");
        return 1;
    }
    if (prepareCalls || startCalls || tickCalls || stopCalls || statusCalls) {
        std::puts("ASOP host refusal: FAIL (host callback ran before native safety proof)");
        return 1;
    }
    std::puts("ASOP host refusal: PASS (native activation remains disabled before host callbacks)");
    return 0;
}
