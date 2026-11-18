// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sco/widgets.h"
#include <vector>
namespace sc_offline::menu_panels {
// Exact owner and IDs prevent a similarly titled third-party panel from being grouped.
// `asopAvailable` is whether the game's own ship list can be read right now (Core's
// game.asop.selection.file capability). The shipped host never composes that provider, so the
// section "Your own ships" would only ever say it is unavailable: it is not grouped or shown then.
inline bool GroupAsop(const std::vector<sco::widgets::Panel>& panels, bool asopAvailable = true) {
    if (!asopAvailable) return false;
    bool vehicles = false, asop = false;
    for (const auto& panel : panels) {
        if (panel.owner != "spawn") continue;
        vehicles |= panel.id == "spawn.vehicles";
        asop |= panel.id == "spawn.asop";
    }
    return vehicles && asop;
}
inline bool IsAsop(const sco::widgets::Panel& panel) {
    return panel.owner == "spawn" && panel.id == "spawn.asop";
}
inline bool IsVehicles(const sco::widgets::Panel& panel) {
    return panel.owner == "spawn" && panel.id == "spawn.vehicles";
}
// True when the sidebar must not list this panel as a page of its own: the ASOP panel is either
// drawn under the vehicles page (grouped) or hidden because its provider is not there.
inline bool SkipAsopPage(const sco::widgets::Panel& panel, bool grouped, bool asopAvailable) {
    return IsAsop(panel) && (grouped || !asopAvailable);
}
}
