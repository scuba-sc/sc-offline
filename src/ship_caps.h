#pragma once
// The product's `spawn.ship` capability (the `spawn` plugin's gate, backed by the spawn.helpers
// rows): ready exactly when the ship spawner resolved, the same readiness as "ship spawner: ready".
#include "sco/caps.h"

// Returns the result of the capability write so the caller can log a refusal.
inline constexpr const char* kShipSpawnUnavailable = "ship spawner unavailable (see [ship] lines in mod.log)";

inline sco::Result SetShipSpawnCapability(bool spawnerReady) {
    return sco::caps::Set("spawn.ship", spawnerReady, spawnerReady ? nullptr : kShipSpawnUnavailable);
}
