// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#ifdef SCO_OFFLINE_G1_HOST
#include "offline_host.h"
#endif

namespace sc_offline::inventory_filter {
// Invoke the original synchronously with all eight arguments, without a lease.
// Borrowed arguments are not deferred beyond this detour; only the product's
// post-original native reads are gated. Their engine lifetime remains unproven.
template <typename Original, typename Post>
std::uint64_t Dispatch(Original original, Post post, std::uintptr_t self, std::uintptr_t parent,
                       std::uintptr_t state, std::uintptr_t filter, std::uintptr_t a5,
                       std::uintptr_t a6, std::uintptr_t a7, std::uintptr_t a8) {
    const std::uint64_t result = original(self, parent, state, filter, a5, a6, a7, a8);
    if (result != 1 && result != 3) return result;
#ifdef SCO_OFFLINE_G1_HOST
    auto read = offline_host::TryRead();
    if (!read) return result;
    return post(result, self, parent, filter); // read remains held through the final output/log use
#else
    (void)post;
    return result; // no owner is linked in a no-G1 build
#endif
}
} // namespace sc_offline::inventory_filter
