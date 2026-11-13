// SPDX-License-Identifier: GPL-3.0-only
#include "startup_gate_wait.h"

#include <cstdint>
#include <deque>
#include <limits>
#include <utility>
#include <vector>

using sc_offline::startup_gate_wait::Outcome;
using sc_offline::startup_gate_wait::WaitResult;

struct WaitStep {
    WaitResult result;
    std::uint64_t elapsedMs;
};

struct FakeOperations {
    std::uint64_t nowMs = 0;
    std::deque<WaitStep> steps;
    std::vector<std::uint64_t> requestedWaits;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> progress;
    unsigned restoreCalls = 0;
    unsigned failStopCalls = 0;
    Outcome failStopReason = Outcome::Completed;
    bool restoreSucceeds = true;

    std::uint64_t NowMs() const { return nowMs; }

    WaitResult Wait(std::uint64_t timeoutMs) {
        requestedWaits.push_back(timeoutMs);
        if (steps.empty()) return WaitResult::Failed;
        const WaitStep step = steps.front();
        steps.pop_front();
        nowMs += step.elapsedMs > timeoutMs ? timeoutMs : step.elapsedMs;
        return step.result;
    }

    bool Restore() {
        ++restoreCalls;
        return restoreSucceeds;
    }

    void Progress(std::uint64_t elapsedMs, std::uint64_t budgetMs) {
        progress.emplace_back(elapsedMs, budgetMs);
    }

    // A synthetic termination operation deliberately returns so the policy can be
    // checked for fail-closed behavior after that boundary.
    void FailStop(Outcome outcome) {
        ++failStopCalls;
        failStopReason = outcome;
    }
};

static Outcome RunAndMaybeResume(FakeOperations& operations, std::uint64_t budgetMs,
                                 std::uint64_t progressIntervalMs, unsigned& resumed) {
    const Outcome outcome = sc_offline::startup_gate_wait::WaitAndRestore(
        operations, budgetMs, progressIntervalMs);
    if (outcome == Outcome::Completed) ++resumed;
    return outcome;
}

static bool DelayedCompletionReportsProgressAndRestoresOnce() {
    FakeOperations operations;
    operations.nowMs = 100;
    operations.steps = {{WaitResult::TimedOut, 5}, {WaitResult::TimedOut, 5},
                        {WaitResult::Signalled, 3}};
    unsigned resumed = 0;
    const Outcome outcome = RunAndMaybeResume(operations, 20, 5, resumed);
    return outcome == Outcome::Completed && resumed == 1 && operations.restoreCalls == 1 &&
           operations.failStopCalls == 0 && operations.requestedWaits == std::vector<std::uint64_t>{5, 5, 5} &&
           operations.progress == std::vector<std::pair<std::uint64_t, std::uint64_t>>{{0, 20}, {5, 20}, {10, 20}};
}

static bool SpuriousTimeoutDoesNotRestoreOrFailStop() {
    FakeOperations operations;
    operations.steps = {{WaitResult::TimedOut, 0}, {WaitResult::Signalled, 4}};
    unsigned resumed = 0;
    const Outcome outcome = RunAndMaybeResume(operations, 10, 5, resumed);
    return outcome == Outcome::Completed && resumed == 1 && operations.restoreCalls == 1 &&
           operations.failStopCalls == 0 && operations.requestedWaits.size() == 2;
}

static bool SignalAtExactDeadlineCompletes() {
    FakeOperations operations;
    operations.steps = {{WaitResult::TimedOut, 5}, {WaitResult::Signalled, 5}};
    unsigned resumed = 0;
    const Outcome outcome = RunAndMaybeResume(operations, 10, 5, resumed);
    return outcome == Outcome::Completed && resumed == 1 && operations.restoreCalls == 1 &&
           operations.failStopCalls == 0 && operations.nowMs == 10;
}

static bool TimeoutAtExactDeadlineFailsClosedEvenWhenFailStopReturns() {
    FakeOperations operations;
    operations.steps = {{WaitResult::TimedOut, 5}, {WaitResult::TimedOut, 5}};
    unsigned resumed = 0;
    const Outcome outcome = RunAndMaybeResume(operations, 10, 5, resumed);
    return outcome == Outcome::TimedOut && resumed == 0 && operations.restoreCalls == 0 &&
           operations.failStopCalls == 1 && operations.failStopReason == Outcome::TimedOut &&
           operations.nowMs == 10;
}

static bool WaitFailureFailsClosed() {
    FakeOperations operations;
    operations.steps = {{WaitResult::Failed, 0}};
    unsigned resumed = 0;
    const Outcome outcome = RunAndMaybeResume(operations, 10, 5, resumed);
    return outcome == Outcome::WaitFailed && resumed == 0 && operations.restoreCalls == 0 &&
           operations.failStopCalls == 1 && operations.failStopReason == Outcome::WaitFailed;
}

static bool RestoreFailureFailsClosedWithoutRetry() {
    FakeOperations operations;
    operations.steps = {{WaitResult::Signalled, 2}};
    operations.restoreSucceeds = false;
    unsigned resumed = 0;
    const Outcome outcome = RunAndMaybeResume(operations, 10, 5, resumed);
    return outcome == Outcome::RestoreFailed && resumed == 0 && operations.restoreCalls == 1 &&
           operations.failStopCalls == 1 && operations.failStopReason == Outcome::RestoreFailed;
}

static bool ZeroDeadlineNeverWaitsOrRestores() {
    FakeOperations operations;
    operations.steps = {{WaitResult::Signalled, 0}};
    unsigned resumed = 0;
    const Outcome outcome = RunAndMaybeResume(operations, 0, 5, resumed);
    return outcome == Outcome::TimedOut && resumed == 0 && operations.requestedWaits.empty() &&
           operations.restoreCalls == 0 && operations.failStopCalls == 1;
}

static bool DeadlineArithmeticSaturates() {
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    return sc_offline::startup_gate_wait::SaturatingAdd(maximum - 1, 2) == maximum;
}

enum class SetupFailurePoint { None, Allocation, Event, EntryWrite };

struct FakeInstallOperations {
    SetupFailurePoint failAt = SetupFailurePoint::None;
    unsigned allocationCalls = 0;
    unsigned eventCalls = 0;
    unsigned entryWriteCalls = 0;
    unsigned failStopCalls = 0;
    Outcome failStopReason = Outcome::Completed;

    bool AllocateStub() { ++allocationCalls; return failAt != SetupFailurePoint::Allocation; }
    bool CreateCompletionEvent() { ++eventCalls; return failAt != SetupFailurePoint::Event; }
    bool WriteEntryJump() { ++entryWriteCalls; return failAt != SetupFailurePoint::EntryWrite; }
    void FailStop(Outcome outcome) { ++failStopCalls; failStopReason = outcome; }
};

static bool GateInstallStopsAtFirstFailedStep() {
    FakeInstallOperations success;
    const auto installed = sc_offline::startup_gate_wait::InstallGate(success);
    if (installed != sc_offline::startup_gate_wait::InstallResult::Installed ||
        success.allocationCalls != 1 || success.eventCalls != 1 || success.entryWriteCalls != 1 ||
        success.failStopCalls != 0) return false;

    FakeInstallOperations allocation;
    allocation.failAt = SetupFailurePoint::Allocation;
    if (sc_offline::startup_gate_wait::InstallGate(allocation) != sc_offline::startup_gate_wait::InstallResult::Failed ||
        allocation.allocationCalls != 1 || allocation.eventCalls != 0 || allocation.entryWriteCalls != 0 ||
        allocation.failStopCalls != 1 || allocation.failStopReason != Outcome::SetupFailed) return false;

    FakeInstallOperations event;
    event.failAt = SetupFailurePoint::Event;
    if (sc_offline::startup_gate_wait::InstallGate(event) != sc_offline::startup_gate_wait::InstallResult::Failed ||
        event.allocationCalls != 1 || event.eventCalls != 1 || event.entryWriteCalls != 0 ||
        event.failStopCalls != 1 || event.failStopReason != Outcome::SetupFailed) return false;

    FakeInstallOperations entryWrite;
    entryWrite.failAt = SetupFailurePoint::EntryWrite;
    return sc_offline::startup_gate_wait::InstallGate(entryWrite) == sc_offline::startup_gate_wait::InstallResult::Failed &&
           entryWrite.allocationCalls == 1 && entryWrite.eventCalls == 1 && entryWrite.entryWriteCalls == 1 &&
           entryWrite.failStopCalls == 1 && entryWrite.failStopReason == Outcome::SetupFailed;
}

int main() {
    if (!DelayedCompletionReportsProgressAndRestoresOnce()) return 1;
    if (!SpuriousTimeoutDoesNotRestoreOrFailStop()) return 2;
    if (!SignalAtExactDeadlineCompletes()) return 3;
    if (!TimeoutAtExactDeadlineFailsClosedEvenWhenFailStopReturns()) return 4;
    if (!WaitFailureFailsClosed()) return 5;
    if (!RestoreFailureFailsClosedWithoutRetry()) return 6;
    if (!ZeroDeadlineNeverWaitsOrRestores()) return 7;
    if (!DeadlineArithmeticSaturates()) return 8;
    if (!GateInstallStopsAtFirstFailedStep()) return 9;
    return 0;
}
