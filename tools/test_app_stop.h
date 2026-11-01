// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Test-process shutdown: leave all app backing and loaded modules alive unless
// Stop proves cleanup completed. Older host kits return void and cannot prove it.
#include <sco/app.h>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace test_app {
template<class Stop, class LegacyCheck>
void StopOrExit(Stop stop, LegacyCheck legacyCheck) {
    try {
        if constexpr (std::is_void_v<std::invoke_result_t<Stop>>) {
            stop();
            if (!legacyCheck()) {
                std::fputs("legacy Stop test assertion failed; retaining backing until process exit\n", stderr);
                std::fflush(nullptr);
                std::_Exit(EXIT_FAILURE);
            }
            std::fputs("test assertions passed; shutdown unverified (legacy Stop has no result); retaining backing until process exit\n", stderr);
            std::fflush(nullptr);
            std::_Exit(77); // conventional skip: void Stop cannot prove cleanup
        } else {
            if (stop() == sco::Result::Ok) return;
        }
    } catch (...) {
        // An exception also leaves cleanup unproven.
    }
    std::fputs("app Stop did not prove cleanup; retaining backing until process exit\n", stderr);
    std::fflush(nullptr);
    std::_Exit(EXIT_FAILURE);
}
template<class Stop>
void StopOrExit(Stop stop) { StopOrExit(stop, [] { return true; }); }
}
