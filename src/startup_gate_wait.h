#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace sc_offline::startup_gate_wait {

enum class WaitResult {
    Signalled,
    TimedOut,
    Failed,
};

enum class Outcome {
    Completed,
    TimedOut,
    WaitFailed,
    RestoreFailed,
    SetupFailed,
};

enum class InstallResult {
    Installed,
    Failed,
};

constexpr std::uint64_t SaturatingAdd(std::uint64_t left, std::uint64_t right) noexcept {
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    return right > maximum - left ? maximum : left + right;
}

// Operations supplies AllocateStub(), CreateCompletionEvent(), WriteEntryJump(),
// and FailStop(outcome). Setup stops at the first failure. A production FailStop
// never returns; the Failed result keeps test seams and any unexpectedly returning
// termination call from treating an incomplete gate as installed.
template <class Operations>
InstallResult InstallGate(Operations& operations) {
    if (!operations.AllocateStub()) {
        operations.FailStop(Outcome::SetupFailed);
        return InstallResult::Failed;
    }
    if (!operations.CreateCompletionEvent()) {
        operations.FailStop(Outcome::SetupFailed);
        return InstallResult::Failed;
    }
    if (!operations.WriteEntryJump()) {
        operations.FailStop(Outcome::SetupFailed);
        return InstallResult::Failed;
    }
    return InstallResult::Installed;
}

// Operations supplies NowMs(), Wait(milliseconds), Restore(), Progress(elapsed, budget),
// and FailStop(outcome). The wait is always bounded by the earlier of the next progress
// report and the absolute monotonic deadline. A timed-out wait can be spurious; it is
// retried until the deadline, while a signalled wait is the only path allowed to restore.
template <class Operations>
Outcome WaitAndRestore(Operations& operations, std::uint64_t timeoutMs,
                       std::uint64_t progressIntervalMs) {
    const std::uint64_t startedAt = operations.NowMs();
    const std::uint64_t deadline = SaturatingAdd(startedAt, timeoutMs);
    const std::uint64_t progressInterval = progressIntervalMs == 0 ? 1 : progressIntervalMs;
    std::uint64_t nextProgress = SaturatingAdd(startedAt, progressInterval);

    operations.Progress(0, timeoutMs);

    for (;;) {
        const std::uint64_t now = operations.NowMs();
        if (now >= deadline) {
            operations.FailStop(Outcome::TimedOut);
            return Outcome::TimedOut;
        }

        if (now >= nextProgress) {
            operations.Progress(now - startedAt, timeoutMs);
            nextProgress = SaturatingAdd(now, progressInterval);
        }

        std::uint64_t waitMs = deadline - now;
        if (nextProgress > now)
            waitMs = (std::min)(waitMs, nextProgress - now);
        if (waitMs == 0) waitMs = 1;

        switch (operations.Wait(waitMs)) {
        case WaitResult::Signalled:
            if (!operations.Restore()) {
                operations.FailStop(Outcome::RestoreFailed);
                return Outcome::RestoreFailed;
            }
            return Outcome::Completed;
        case WaitResult::TimedOut:
            // Recheck the monotonic clock. A timeout before the deadline can be a
            // spurious wake; the absolute deadline, not the number of waits, governs.
            break;
        case WaitResult::Failed:
        default:
            operations.FailStop(Outcome::WaitFailed);
            return Outcome::WaitFailed;
        }
    }
}

} // namespace sc_offline::startup_gate_wait
