#pragma once
// SPDX-License-Identifier: GPL-3.0-only
// One owner-counted worker serializes flight-ready input requests until stop.
#include "sco/owned_shutdown.h"
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

namespace offline_host {
class FlightReadyWorker {
public:
    using Stop = std::function<bool()>;
    using Work = std::function<void(Stop)>;
    using Launcher = std::function<bool(sco::OwnedShutdown&, Work)>;
    struct Callbacks {
        std::function<bool()> keyDown;
        std::function<void()> keyUp;
        std::function<void()> hold;
    };

    FlightReadyWorker(std::shared_ptr<sco::OwnedShutdown> owner, Callbacks callbacks,
                      Launcher launcher = {})
        : owner_(std::move(owner)), state_(std::make_shared<State>(std::move(callbacks))),
          launcher_(std::move(launcher)) {}

    // An accepted request means queued, not that input was delivered. Request
    // admission remains counted through publication and refuses after closure.
    bool request() noexcept {
        try {
            if (!owner_ || !state_->callbacks.keyDown || !state_->callbacks.keyUp ||
                !state_->callbacks.hold) return false;
            auto read = owner_->try_read();
            if (!read) return false;
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (state_->pending || state_->active || state_->finished) return false;
            if (!state_->started) {
                auto state = state_;
                Work work = [state](Stop stop) { run(state, std::move(stop)); };
                const bool launched = launcher_ ? launcher_(*owner_, std::move(work)) :
                    owner_->start_worker([work = std::move(work)](auto stop) mutable { work(stop); });
                if (!launched) return false;
                state_->started = true;
            }
            state_->pending = true;
            state_->cv.notify_one();
            return true;
        } catch (...) { return false; }
    }
private:
    struct State {
        explicit State(Callbacks c) : callbacks(std::move(c)) {}
        Callbacks callbacks;
        std::mutex mutex;
        std::condition_variable cv;
        bool started = false;
        bool pending = false;
        bool active = false;
        bool finished = false;
    };
    static void run(const std::shared_ptr<State>& state, Stop stop) {
        std::unique_lock<std::mutex> lock(state->mutex);
        try {
            while (!stop()) {
                state->cv.wait_for(lock, std::chrono::milliseconds(20), [&] { return state->pending || stop(); });
                if (stop()) break;
                if (!state->pending) continue;
                state->pending = false;
                state->active = true;
                lock.unlock();
                bool down = false;
                try {
                    if (!stop()) down = state->callbacks.keyDown();
                    if (down) state->callbacks.hold();
                } catch (...) {
                    if (down) state->callbacks.keyUp();
                    throw;
                }
                if (down) state->callbacks.keyUp();
                lock.lock();
                state->active = false;
            }
            state->finished = true;
        } catch (...) {
            if (!lock.owns_lock()) lock.lock();
            state->active = false;
            state->finished = true;
            throw; // owner records a fault and retains the dependency closure
        }
    }
    std::shared_ptr<sco::OwnedShutdown> owner_;
    std::shared_ptr<State> state_;
    Launcher launcher_;
};
} // namespace offline_host
