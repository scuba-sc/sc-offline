// SPDX-License-Identifier: GPL-3.0-only
#include "creative.h"
#include <cstddef>
extern "C" SCO_EXPORT const sco_plugin_info* sco_plugin_query() {
    static const sco_plugin_info info = {sizeof(sco_plugin_info), SCO_API_MAJOR,
        SCO_API_MINOR, "creative", "1.1.0", "sc-offline"};
    return &info;
}
extern "C" SCO_EXPORT sco_result sco_plugin_load(const sco_api* api, sco_plugin* self) {
    if (!api || !self) return SCO_BAD_ARG;
    // Full query_service extent dominates every earlier member we use. Check it
    // before reading major/minor/callbacks or entering the pinned SDK helper.
    if (api->size < offsetof(sco_api, query_service) + sizeof(api->query_service)
        || api->major != SCO_API_MAJOR || api->minor < SCO_API_MINOR
        || !api->has || !api->log || !api->register_command || !api->query_service)
        return SCO_UNAVAILABLE;
    return creative_plugin::Load(api, self);
}
extern "C" SCO_EXPORT void sco_plugin_unload() { creative_plugin::Unload(); }
