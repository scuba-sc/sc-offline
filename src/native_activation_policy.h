#pragma once
// SPDX-License-Identifier: GPL-3.0-only
// No runtime flag or caller assertion can establish the native reader/worker
// exclusion and drain required by this product. Replacing this refusal requires
// a reviewed, real phase/lifetime protocol and its native evidence.
namespace offline_host {
inline bool NativeActivationAvailable() noexcept { return false; }
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
