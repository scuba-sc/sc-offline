#pragma once
// SPDX-License-Identifier: GPL-3.0-only
// Product composition seam. No engine offsets, patches, game pointers or OS policy.
#include "sco/game/host.h"
#include <cstdint>
#include <memory>

namespace offline_host {
sco::Result Prepare(sco::game::host::Options options);
sco::Result Start();
sco::Result Tick(uint32_t now);
// Game thread. Request plus, under the process-retained composition, Core's closure-only Stop
// (admission closed, game.exit dispatched once; nothing is drained, unloaded or restarted).
sco::Result Stop();
bool Running();
// Attach the process-retained product owner before Prepare copies Options.
// Refuses when the selected Core lacks the non-draining stop contract or the process-retained
// composition (Prepare then selects it; native startup is otherwise refused).
bool AttachShutdownOwner(sco::game::host::Options& options) noexcept;
// Product callbacks hold this same owner's read for their entire use.
sco::OwnedShutdown::Read TryRead() noexcept;
// Shared process-retained owner; unavailable while native activation is refused.
std::shared_ptr<sco::OwnedShutdown> FlightReadyOwner() noexcept;
// Admitted callback or controller terminal observer: reserves the request,
// closes this owner, then asks Core for its report. Competing requests latch
// retention; this does not wait for or perform a drain.
sco::game::host::HostReport RequestStop() noexcept;
enum class OwnerDrain { Unavailable, DrainedRetained, IncompleteRetained };
// Independent control thread only, after owner/Core request work completes and
// retention is published. This can precede RequestStop's return instruction.
// Drains the exact owner passed in Options; never a callback or loader lock.
OwnerDrain DrainRequestedStop(uint32_t timeoutMs) noexcept;
// Any stop request, closure included, is final: no restart. Backing must remain live until exit.
bool RestartRequired() noexcept;
// Pin product code before publishing the log sink or any callback.
bool RetainProductModule() noexcept;
} // namespace offline_host
