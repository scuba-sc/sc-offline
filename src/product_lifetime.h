#pragma once
// SPDX-License-Identifier: GPL-3.0-only
#include <atomic>

namespace offline_host {
// The legacy product's code and slots are process-retained. This records an
// attempted mutation before any write, even if a later step fails. Closing this
// gate prevents a new mutation from beginning; it does not drain an already
// entered mutation or any native detour and must not be presented as doing so.
class ProductLifetime {
public:
    bool TryBeginMutation() noexcept {
        unsigned state = state_.load(std::memory_order_acquire);
        for (;;) {
            if (state & kClosed) return false;
            if (state_.compare_exchange_weak(state, state | kRetained,
                                             std::memory_order_acq_rel, std::memory_order_acquire)) return true;
        }
    }
    // True only when this gate never admitted a mutation. This does not prove
    // quiescence of startup gates, other hooks, callers or provider state.
    bool Close() noexcept {
        return !(state_.fetch_or(kClosed, std::memory_order_acq_rel) & kRetained);
    }
private:
    static constexpr unsigned kClosed = 1, kRetained = 2;
    std::atomic<unsigned> state_{0};
};
} // namespace offline_host
