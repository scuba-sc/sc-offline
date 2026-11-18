// SPDX-License-Identifier: GPL-3.0-only
#include "menu_panel_group.h"
#include <cstdio>
int main() {
    using namespace sc_offline::menu_panels;
    sco::widgets::Panel v; v.id = "spawn.vehicles"; v.owner = "spawn";
    sco::widgets::Panel a; a.id = "spawn.asop"; a.owner = "spawn";
    if (!GroupAsop({v, a}) || GroupAsop({v}) || GroupAsop({a})) return 1;
    a.owner = "other";
    if (GroupAsop({v, a}) || IsAsop(a)) return 2;
    // The game's own ship list has no provider in the shipped host: with it unavailable the section
    // is neither grouped nor listed as a page of its own, so no player sees "isn't available".
    a.owner = "spawn";
    if (GroupAsop({v, a}, false)) return 3;
    if (!GroupAsop({v, a}, true)) return 4;
    if (!SkipAsopPage(a, false, false)) return 5;   // hidden: provider missing
    if (!SkipAsopPage(a, true, true)) return 6;     // drawn under the vehicles page
    if (SkipAsopPage(a, false, true)) return 7;     // provider there, no vehicles page: standalone
    if (SkipAsopPage(v, false, false) || SkipAsopPage(v, true, true)) return 8;
    // A third-party panel with the same id is never hidden by this rule.
    sco::widgets::Panel other; other.id = "spawn.asop"; other.owner = "other";
    if (SkipAsopPage(other, false, false)) return 9;
    std::puts("Vehicles/ASOP grouping, standalone fallback and hiding without a provider passed");
}
