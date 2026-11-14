#pragma once
// SPDX-License-Identifier: GPL-3.0-only
// CMake rejects these compositions until their direct-quit notification and
// control-thread drain ordering are implemented. Keep this source guard too:
// raw compiler definitions must not bypass the configure-time policy.
#if defined(SCO_ASOP_HOST_COMPOSITION) || defined(SCO_ASOP_SELECTION_COMPOSITION)
#error "ASOP composition lacks a reviewed nonblocking quit and stop/drain contract"
#endif
// Native activation is available only on a Core that provides the opt-in
// process-retained composition (and the non-draining stop API), as detected by
// CMake. The product then operates under retain-until-exit: its module and every
// dependency stay mapped until the process ends (nothing is unloaded), code
// patches are written at the executable's entry gate, and stop is closure only
// (admission closed, game.exit dispatched once, nothing torn down, no restart).
// This is NOT a reader/worker drain or unload guarantee, and it has not been
// verified in game. Any other Core keeps the refusal.
namespace offline_host {
inline bool NativeActivationAvailable() noexcept {
#if defined(SCO_CORE_PROCESS_RETAINED_COMPOSITION) && defined(SCO_CORE_NON_DRAINING_STOP_API)
    return true;
#else
    return false;
#endif
}
// New native startup also requires the selected Core to support non-draining
// stop plus same-owner control-thread drain. Missing headers fail closed.
inline bool ProductControlStopContractAvailable() noexcept {
#if defined(SCO_CORE_NON_DRAINING_STOP_API)
    return true;
#else
    return false;
#endif
}
}
