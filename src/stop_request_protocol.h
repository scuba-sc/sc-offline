#pragma once
// SPDX-License-Identifier: GPL-3.0-only
// Shared product request/control ordering. Never grants native admission or
// unload; callers must own and retain the complete backing independently.
// A caller using a detailed capsule must independently prove that getOwner()
// returns that exact capsule's owner. This helper cannot establish identity.
#include "offline_host.h"
#include "stop_request_gate.h"
#include <chrono>
#include <memory>

namespace offline_host::detail {
inline sco::game::host::HostReport RequestReport(sco::Result result) noexcept {
    sco::game::host::HostReport report{};
    report.result = result;
    report.phase = sco::game::host::Phase::Failed;
    report.dependencies = sco::game::host::DependencyDisposition::ProcessRetained;
    return report;
}
template <typename OwnerProvider>
inline sco::game::host::HostReport RequestOwnedStop(
        StopRequestGate& gate, OwnerProvider&& getOwner) noexcept {
    if (!gate.Begin()) return RequestReport(sco::Result::Failed);
    std::shared_ptr<sco::OwnedShutdown> owner;
    try {
        owner = getOwner();
        if (!owner) {
            gate.Finish();
            return RequestReport(sco::Result::Failed);
        }
        owner->request_stop();
        const auto report = sco::game::host::RequestStopDetailed();
        if (!gate.Finish()) return RequestReport(sco::Result::Failed);
        return report;
    } catch (...) {
        if (owner) { try { owner->fault(); } catch (...) {} }
        gate.Finish();
        return RequestReport(sco::Result::Failed);
    }
}

inline OwnerDrain DrainOwnedStop(StopRequestGate& gate,
        const std::shared_ptr<sco::OwnedShutdown>& owner, uint32_t timeoutMs) noexcept {
    if (!owner || !gate.DrainMayStart()) return OwnerDrain::Unavailable;
    const auto bounded = timeoutMs > 60000 ? 60000 : timeoutMs;
    try {
        const bool drained = owner->drain_until(std::chrono::steady_clock::now() +
            std::chrono::milliseconds(bounded));
        return drained ? OwnerDrain::DrainedRetained : OwnerDrain::IncompleteRetained;
    } catch (...) {
        try { owner->fault(); } catch (...) {}
        return OwnerDrain::IncompleteRetained;
    }
}
}
