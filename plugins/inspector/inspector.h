// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "sco_api.h"

namespace sco::features::inspector {

const sco_plugin_info* Query();
sco_result Load(const sco_api* api, sco_plugin* self);
void Unload();

}  // namespace sco::features::inspector
