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
// Called before the first product patch or detour attempt. Once retirement
// closes this gate, the shell must not begin its mutation sequence.
bool BeginPersistentMutation() noexcept;
// Closes the product mutation gate. False means backing must be retained;
// true only says this gate admitted no mutation, not that callbacks are drained.
bool CloseProductMutationGate() noexcept;
// Core invokes this before dependent teardown. The legacy product cannot yet
// prove that its native callbacks and writes have drained.
sco::game::host::ConsumerRetirement RetireProductConsumer(void*) noexcept;
} // namespace offline_host
