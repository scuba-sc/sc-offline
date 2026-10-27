// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sco/widgets.h"
#include <vector>
namespace sc_offline::menu_panels {
// Exact owner and IDs prevent a similarly titled third-party panel from being grouped.
inline bool GroupAsop(const std::vector<sco::widgets::Panel>& panels) {
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
}
