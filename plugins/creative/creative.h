// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sco_api.h"
namespace creative_plugin {
sco_result Load(const sco_api* api, sco_plugin* self) noexcept;
void Unload() noexcept;
}
