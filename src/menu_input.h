// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <utility>
namespace offline::menu {
struct InputMessage { uint32_t message; uintptr_t word; intptr_t parameter; };
class DeferredInput {
    std::array<InputMessage, 1024> messages_{};
    uint32_t count_ = 0;
    bool overflow_ = false;
public:
    template<class Handler> void Route(bool building, uint32_t message, uintptr_t word, intptr_t parameter, Handler handler) {
        if (building) Push(message, word, parameter);
        else handler(InputMessage{message, word, parameter});
    }
    void Push(uint32_t message, uintptr_t word, intptr_t parameter) noexcept {
        if (count_ == messages_.size()) { overflow_ = true; return; }
        messages_[count_++] = {message, word, parameter};
    }
    bool TakeOverflow() noexcept { if (!overflow_) return false; count_ = 0; overflow_ = false; return true; }
    template<class Handler> void Drain(Handler handler) {
        if (overflow_) { count_ = 0; return; } // caller resets input state after dropped transitions
        const auto count = std::exchange(count_, 0u);
        for (uint32_t i = 0; i < count; ++i) handler(messages_[i]);
    }
    bool empty() const { return count_ == 0; }
};
}
