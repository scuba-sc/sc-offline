#pragma once
// SPDX-License-Identifier: GPL-3.0-only
// Product composition seam. No engine offsets, patches, game pointers or OS policy.
#include "sco/game/host.h"

namespace offline_host {
sco::Result Prepare(sco::game::host::Options options);
sco::Result Start();
sco::Result Tick(uint32_t now);
sco::Result Stop();
bool Running();
} // namespace offline_host
