// SPDX-License-Identifier: GPL-3.0-only
#include "controls.h"
#define TRAVEL_QUANTUM_CONTROLS
#include "../spatial/travel_plugin.h"
namespace sco::features::quantum {
inline void RefreshPanel() { sco::features::travel::implementation::Panel(); }
}
extern "C" SCO_EXPORT const sco_plugin_info* sco_plugin_query() { return sco::features::travel::Query(); }
extern "C" SCO_EXPORT sco_result sco_plugin_load(const sco_api* api,sco_plugin* self) {
    const auto travel=sco::features::travel::Load(api,self);
    if (travel!=SCO_OK) return travel;
    try {
        const auto control=sco::features::quantum::Load(api,self);
        if (control!=SCO_OK) { sco::features::travel::Unload(); return control; }
        sco::features::quantum::RefreshPanel(); return SCO_OK;
    } catch (...) { sco::features::quantum::Unload(); sco::features::travel::Unload(); return SCO_FAILED; }
}
extern "C" SCO_EXPORT void sco_plugin_unload() {
    sco::features::quantum::Unload(); sco::features::travel::Unload();
}
