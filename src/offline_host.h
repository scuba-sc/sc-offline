#pragma once
// SPDX-License-Identifier: GPL-3.0-only
// Product composition seam. No engine offsets, patches, game pointers or OS policy.
#include "sco/game/host.h"
#include <cstdint>

namespace offline_host {
sco::Result Prepare(sco::game::host::Options options);
sco::Result Start();
sco::Result Tick(uint32_t now);
sco::Result Stop();
bool Running();
// Attach the process-retained product owner before Prepare copies Options.
// Refuses when the selected Core pin lacks the non-draining stop contract.
bool AttachShutdownOwner(sco::game::host::Options& options) noexcept;
// Product callbacks hold this same owner's read for their entire use.
sco::OwnedShutdown::Read TryRead() noexcept;
// Admitted callback or controller terminal observer: reserves the request,
// closes this owner, then asks Core for its report. Competing requests latch
// retention; this does not wait for or perform a drain.
sco::game::host::HostReport RequestStop() noexcept;
enum class OwnerDrain { Unavailable, DrainedRetained, IncompleteRetained };
// Independent control thread only, after request completion. Drains the exact
// owner passed in Options; never a callback or loader-lock operation.
OwnerDrain DrainRequestedStop(uint32_t timeoutMs) noexcept;
// A failed or uncertain stop refuses restart; backing must remain live until exit.
bool RestartRequired() noexcept;
// Pin product code before publishing the log sink or any callback.
bool RetainProductModule() noexcept;
} // namespace offline_host
