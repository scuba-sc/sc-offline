// SPDX-License-Identifier: GPL-3.0-only
#include "npc.h"

extern "C" SCO_EXPORT const sco_plugin_info* sco_plugin_query() { return sco::features::npc::Query(); }
extern "C" SCO_EXPORT sco_result sco_plugin_load(const sco_api* api, sco_plugin* self) {
    return sco::features::npc::Load(api, self);
}
extern "C" SCO_EXPORT void sco_plugin_unload() { sco::features::npc::Unload(); }
