// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <cstdint>

namespace sc_offline::menu_lifetime {
// Product-local census of Menu_GameThreadFrame calls, including their final
// event wake. Closing is permanent. This is not a core/native reader barrier.
class CallbackAdmission final {
public:
    bool Enter() noexcept {
        auto value = state_.load(std::memory_order_acquire);
        for (;;) {
            if ((value & kClosed) || (value & kCount) == kCount) return false;
            if (state_.compare_exchange_weak(value, value + 1,
                    std::memory_order_acq_rel, std::memory_order_acquire)) return true;
        }
    }
    void Leave() noexcept { state_.fetch_sub(1, std::memory_order_release); }
    void Close() noexcept { state_.fetch_or(kClosed, std::memory_order_acq_rel); }
    bool ClosedAndDrained() const noexcept {
        return state_.load(std::memory_order_acquire) == kClosed;
    }
private:
    static constexpr uint32_t kClosed = uint32_t{1} << 31;
    static constexpr uint32_t kCount = kClosed - 1;
    std::atomic<uint32_t> state_{0};
};

class CallbackLease final {
public:
    explicit CallbackLease(CallbackAdmission& admission) noexcept
        : admission_(admission.Enter() ? &admission : nullptr) {}
    ~CallbackLease() { if (admission_) admission_->Leave(); }
    CallbackLease(const CallbackLease&) = delete;
    CallbackLease& operator=(const CallbackLease&) = delete;
    explicit operator bool() const noexcept { return admission_ != nullptr; }
private:
    CallbackAdmission* admission_;
};
} // namespace sc_offline::menu_lifetime
