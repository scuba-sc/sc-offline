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
    std::puts("Vehicles/ASOP grouping and standalone fallback passed");
}
