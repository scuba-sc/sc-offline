#pragma once
// SPDX-License-Identifier: GPL-3.0-only
#include "stop_request_protocol.h"
#include <atomic>
#include <memory>
#include <utility>

namespace offline_host::detail {
// Restricted non-native composition only; not wired into product startup.
// Construction owns the immutable capsule but invokes no Core entry. Prepare
// deliberately roots it before any exposure, including failed/busy attempts.
// That conservative process retention is not native ownership or cleanup proof.
// Public calls must finish before wrapper destruction; already returned Read
// leases may outlive it because the exposed root is never released.
class CapsuleLifecycle final {
    enum class State { Fresh, Preparing, Bound, Refused };
    struct Root {
        explicit Root(std::shared_ptr<const sco::game::host::HostCapsule> value)
            : capsule(std::move(value)) {}
        const std::shared_ptr<const sco::game::host::HostCapsule> capsule;
        StopRequestGate request;
    };
    std::unique_ptr<Root> privateRoot_;
    Root* const root_;
    std::atomic<State> state_{State::Fresh};
    std::atomic<bool> retained_{false};
    bool Bound() const noexcept { return state_.load(std::memory_order_acquire) == State::Bound; }
    std::shared_ptr<sco::OwnedShutdown> Owner() const noexcept {
        return root_->capsule ? root_->capsule->configuration().shutdownOwner : nullptr;
    }
public:
    explicit CapsuleLifecycle(std::shared_ptr<const sco::game::host::HostCapsule> capsule)
        : privateRoot_(std::make_unique<Root>(std::move(capsule))), root_(privateRoot_.get()) {}
    CapsuleLifecycle(const CapsuleLifecycle&) = delete;
    CapsuleLifecycle& operator=(const CapsuleLifecycle&) = delete;
    sco::game::host::HostReport Prepare() noexcept {
        auto expected = State::Fresh;
        if (!state_.compare_exchange_strong(expected, State::Preparing, std::memory_order_acq_rel))
            return RequestReport(sco::Result::Unavailable);
        // Intentionally never destroyed after a preparation attempt. This also
        // retains inputs on uncertain outcomes, not only successful adoption.
        (void)privateRoot_.release();
        const auto report = sco::game::host::PrepareDetailed(root_->capsule);
        // Only this completed successful call establishes association. A busy,
        // failed or preexisting-host report can name somebody else's session.
        const bool adopted = report.result == sco::Result::Ok && report.sessionId != 0 &&
            report.phase == sco::game::host::Phase::Prepared &&
            report.dependencies == sco::game::host::DependencyDisposition::ProcessRetained;
        state_.store(adopted ? State::Bound : State::Refused, std::memory_order_release);
        return report; // report describes Core, not proof that this input was adopted
    }
    bool Associated() const noexcept { return Bound(); }
    sco::game::host::HostReport Start() noexcept {
        if (!Bound() || RestartRequired()) return RequestReport(sco::Result::Unavailable);
        const auto report = sco::game::host::StartDetailed();
        if (report.result != sco::Result::Ok) retained_.store(true, std::memory_order_release);
        return report;
    }
    sco::OwnedShutdown::Read TryRead() noexcept {
        if (!Bound() || RestartRequired()) return {};
        try { return Owner()->try_read(); } catch (...) { return {}; }
    }
    // Before successful Prepare association, request/drain refuse without
    // touching Core or any owner. In particular they cannot stop a foreign host.
    sco::game::host::HostReport RequestStop() noexcept {
        if (!Bound()) return RequestReport(sco::Result::Unavailable);
        return RequestOwnedStop(root_->request, [this] { return Owner(); });
    }
    OwnerDrain Drain(uint32_t timeoutMs) noexcept {
        if (!Bound()) return OwnerDrain::Unavailable;
        return DrainOwnedStop(root_->request, Owner(), timeoutMs);
    }
    bool RestartRequired() const noexcept {
        const auto state = state_.load(std::memory_order_acquire);
        if (state == State::Preparing || state == State::Refused ||
            retained_.load(std::memory_order_acquire) || root_->request.RestartRequired()) return true;
        if (state == State::Bound) {
            try { return Owner()->closed(); } catch (...) { return true; }
        }
        return false;
    }
};
}
