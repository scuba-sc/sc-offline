#pragma once
// SPDX-License-Identifier: GPL-3.0-only
#include <atomic>

namespace offline_host::detail {
// Serializes non-draining stop requests. It does not count readers, close an
// owner, drain workers, or authorize cleanup. The successful begin is the
// linearization point that closes this product seam to further requests.
class StopRequestGate {
    enum class State { Idle, Requesting, Contended, Retained };
    std::atomic<State> state_{State::Idle};
public:
    bool Begin() noexcept {
        auto state = state_.load(std::memory_order_acquire);
        for (;;) {
            if (state == State::Retained || state == State::Contended) return false;
            auto next = state == State::Idle ? State::Requesting : State::Contended;
            if (state_.compare_exchange_weak(state, next, std::memory_order_acq_rel))
                return next == State::Requesting;
        }
    }
    // Call once on every path after a successful Begin, after all request work.
    // False means a competing request was observed before this completion.
    bool Finish() noexcept {
        auto expected = State::Requesting;
        if (state_.compare_exchange_strong(expected, State::Retained, std::memory_order_acq_rel))
            return true;
        state_.store(State::Retained, std::memory_order_release);
        return false;
    }
    bool RestartRequired() const noexcept {
        return state_.load(std::memory_order_acquire) != State::Idle;
    }
    bool DrainMayStart() const noexcept {
        return state_.load(std::memory_order_acquire) == State::Retained;
    }
};
}
