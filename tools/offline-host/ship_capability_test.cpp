// SPDX-License-Identifier: GPL-3.0-only
// The host publishes `spawn.ship` exactly when the ship spawner is ready, with a reason when not.
#include "ship_caps.h"
#include <cstdio>
#include <cstring>

namespace {
int sets = 0;
char lastName[64]{};
bool lastReady = false;
const char* lastReason = nullptr;
}
namespace sco::caps {
Result Set(const char* name, bool ready, const char* reason) {
    ++sets;
    std::snprintf(lastName, sizeof(lastName), "%s", name);
    lastReady = ready;
    lastReason = reason;
    return Result::Ok;
}
}
int main() {
    if (SetShipSpawnCapability(true) != sco::Result::Ok || sets != 1) return 1;
    if (std::strcmp(lastName, "spawn.ship") != 0 || !lastReady || lastReason) return 2;
    if (SetShipSpawnCapability(false) != sco::Result::Ok || sets != 2) return 3;
    if (std::strcmp(lastName, "spawn.ship") != 0 || lastReady || !lastReason || !*lastReason) return 4;
    std::puts("spawn.ship capability follows spawner readiness: PASS (synthetic)");
}
