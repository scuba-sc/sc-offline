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
// A failed or uncertain stop refuses restart; backing must remain live until exit.
bool RestartRequired() noexcept;
} // namespace offline_host
