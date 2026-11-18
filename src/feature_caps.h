// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The product's own capabilities ("teleport", "spawn.ship" and the rest), decided in one place so a
// test can read the table without the game: readiness in, name + ready + the reason a plugin sees
// out. dllmain.cpp's SetFeatureCaps applies it to sco::caps and adds nothing of its own.
//
// Core's status registry (sdk/status_registry.json) lists two of these as product capabilities,
// "teleport" and "spawn.ship", both default ON: a plugin that follows the SDK docs gates its use
// of the service on has("teleport") / has("spawn.ship"). "spawn.ship" follows the readiness of the
// rows behind it; "teleport" is held off by product decision (see below). A capability the product holds off on purpose is named in kHeldOff and carries a reason
// that says so; the reason is never "unavailable on this game build" for a decision of the product.
#include "ship_caps.h"
#include "sco/caps.h"
#include <cstddef>
#include <cstdio>

namespace sc_offline::feature_caps {

struct Readiness {
    bool offline = false;       // the offline bootstrap took
    bool teleportRows = false;  // the teleport.* rows resolved (g_tp.ok)
    bool spawner = false;       // the ship spawner resolved
    bool contracts = false;     // the contracts API resolved
};

struct State {
    const char* name;
    bool ready;
    const char* reason;         // null when ready
};

// Capabilities the product keeps off by decision, whatever the rows say. Core's registry has no
// entry for these; a new name here is a product decision a reviewer should see.
inline constexpr const char* kHeldOff[] = { "teleport", "quantum" };

inline constexpr const char* kTeleportHeldReason = "held off by the product: moves go through game.relocation, and this older teleport route is not approved for moves";
inline constexpr const char* kQuantumHeldReason = "held off by the product: the Travel page and its quantum drive section have their own controls, and this old switch is not used";

inline constexpr size_t kMaxStates = 8;

// Fills out[0..n) and returns n. The order is the order the host sets them in.
inline size_t States(const Readiness& r, State (&out)[kMaxStates]) {
    size_t n = 0;
    out[n++] = { "offline", r.offline, "offline start-up did not complete (see [app] lines in mod.log)" };
    // "teleport" is held off whatever the rows say: the product's own save and go routes refuse
    // (src/teleport.cpp) and moves go through game.relocation. Core's registry lists it default ON, so
    // turning it on is an owner decision, not a wiring fix. The reason says it is a product decision.
    (void)r.teleportRows;
    out[n++] = { "teleport", false, kTeleportHeldReason };
    out[n++] = { "npc", r.spawner, kShipSpawnUnavailable };
    out[n++] = { "spawn.ship", r.spawner, kShipSpawnUnavailable };
    out[n++] = { "quantum", false, kQuantumHeldReason };
    out[n++] = { "contracts", r.contracts, "the contracts API did not resolve on this game build (see [contracts] lines in mod.log)" };
    for (size_t i = 0; i < n; ++i)
        if (out[i].ready) out[i].reason = nullptr;
    return n;
}

// Sets every state through sco::caps; `log` (may be null) gets one line per refused write.
inline void Apply(const Readiness& r, void (*log)(const char* name, sco::Result result)) {
    State states[kMaxStates];
    const size_t n = States(r, states);
    for (size_t i = 0; i < n; ++i) {
        const sco::Result result = sco::caps::Set(states[i].name, states[i].ready, states[i].reason);
        if (result != sco::Result::Ok && log) log(states[i].name, result);
    }
}

}  // namespace sc_offline::feature_caps
