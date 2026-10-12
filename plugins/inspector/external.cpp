// SPDX-License-Identifier: GPL-3.0-only
// External native entry points; no host registration adapter or fallback.
#include "inspector.h"

extern "C" {

SCO_EXPORT const sco_plugin_info* sco_plugin_query(void) {
    return sco::features::inspector::Query();
}

SCO_EXPORT sco_result sco_plugin_load(const sco_api* api, sco_plugin* self) {
    return sco::features::inspector::Load(api, self);
}

SCO_EXPORT void sco_plugin_unload(void) {
    sco::features::inspector::Unload();
}

}  // extern "C"
