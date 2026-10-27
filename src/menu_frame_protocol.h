// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
namespace sc_offline::menu_frame {
enum class State { Idle, Wanted, Building, Built, Cancelled };
class Protocol {
public:
    bool Request() noexcept {
        State expected=State::Idle;
        return state_.compare_exchange_strong(expected,State::Wanted);
    }
    bool CancelUnstarted() noexcept {
        State expected=State::Wanted;
        return state_.compare_exchange_strong(expected,State::Idle);
    }
    bool Acquire() noexcept {
        State expected=State::Wanted;
        return state_.compare_exchange_strong(expected,State::Building);
    }
    void Finish(bool success) noexcept { state_.store(success ? State::Built : State::Cancelled); }
    bool TakeBuilt() noexcept {
        State expected=State::Built;
        return state_.compare_exchange_strong(expected,State::Idle);
    }
    bool TakeCancelled() noexcept {
        State expected=State::Cancelled;
        return state_.compare_exchange_strong(expected,State::Idle);
    }
    State Current() const noexcept { return state_.load(); }
private:
    std::atomic<State> state_{State::Idle};
};
// The platform guard is supplied by the host. Completion and wake stay outside it.
template<class Invoke, class Abort, class Wake>
bool Run(Protocol& protocol,Invoke&& invoke,Abort&& abort,Wake&& wake) noexcept {
    if (!protocol.Acquire()) return false;
    bool built=false;
    try { built=invoke(); } catch (...) {}
    if (!built) { try { abort(); } catch (...) {} }
    protocol.Finish(built);
    try { wake(); } catch (...) {}
    return true;
}
} // namespace sc_offline::menu_frame
