// SPDX-License-Identifier: GPL-3.0-only
// Restricted synthetic game-host composition. No native barrier or product activation.
#include "sco/game/host.h"
#include "src/game/host_testing.h"
#ifdef SCO_HAS_REQUEST_API
#include "../../src/stop_request_protocol.h"
#include "../../src/capsule_lifecycle.h"
#endif
#ifdef SCO_HAS_REAP_TEST_ACCESS
#include "tests/owned_shutdown_test_access.h"
#endif
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <memory>
#include <new>
#include <string>
#include <thread>

namespace gh = sco::game::host;
using R = sco::Result;
using D = gh::DependencyDisposition;
using namespace std::chrono_literals;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); std::abort(); } } while (0)

// Instrumented host.cpp arms this only during the actual Options assignment.
static thread_local bool allocationFired = false;
#if defined(_MSC_VER)
#define HOST_REPORT_NOINLINE __declspec(noinline)
#else
#define HOST_REPORT_NOINLINE __attribute__((noinline))
#endif
HOST_REPORT_NOINLINE void* operator new(std::size_t n) {
    if (gh::testing::ConsumeOptionsAllocationFault()) { allocationFired = true; throw std::bad_alloc(); }
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
HOST_REPORT_NOINLINE void* operator new[](std::size_t n) { return ::operator new(n); }
HOST_REPORT_NOINLINE void operator delete(void* p) noexcept { std::free(p); }
HOST_REPORT_NOINLINE void operator delete[](void* p) noexcept { ::operator delete(p); }
HOST_REPORT_NOINLINE void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
HOST_REPORT_NOINLINE void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

static std::atomic<int> images{0}, resolvers{0}, backings{0}, inspections{0};
struct ImageBacking {
    std::array<uint8_t, 8192> bytes{};
    sco::Image image{bytes.data(), {bytes.data(), 4096}, {bytes.data() + 4096, 4096}, {}, 0, 8192};
    ~ImageBacking() { ++images; }
};
struct ResolverBacking {
    // Concrete test data stays owned by this object throughout the copy.
    // Core resolver code itself is linked into this process, not modeled here.
    std::string output = "complete copied value";
    ~ResolverBacking() { ++resolvers; }
};
struct Backing {
    std::shared_ptr<ImageBacking> image = std::make_shared<ImageBacking>();
    std::shared_ptr<ResolverBacking> resolver = std::make_shared<ResolverBacking>();
    std::shared_ptr<sco::OwnedShutdown> owner = std::make_shared<sco::OwnedShutdown>();
    std::string version = "synthetic host";
    ~Backing() { ++backings; }
};
static bool reenter = false;
#ifdef SCO_HAS_REQUEST_API
static offline_host::detail::CapsuleLifecycle* busyCandidate = nullptr;
static gh::HostReport busyReport;
#endif
static gh::HostReport nestedStart, nestedStop;
static gh::Admission Inspect() {
    ++inspections;
#ifdef SCO_HAS_REQUEST_API
    if (busyCandidate) {
        auto* candidate = busyCandidate; busyCandidate = nullptr;
        // Adversarial inspector reentry holds the actual Core control gate.
        busyReport = candidate->Prepare();
        CHECK(!candidate->Associated());
        CHECK(candidate->RequestStop().result == R::Unavailable);
        CHECK(candidate->Drain(0) == offline_host::OwnerDrain::Unavailable);
    }
#endif
    if (reenter) {
        reenter = false;
        nestedStart = gh::StartDetailed();
        nestedStop = gh::StopDetailed();
    }
    return {gh::Session::Offline, gh::Check::Clear, gh::Check::Clear, gh::Check::Clear};
}
static gh::Options Configuration(const std::shared_ptr<Backing>& b) {
    gh::Options o;
    o.inspect = Inspect; o.image = &b->image->image;
    o.imageOwner = b->image; o.resolverOwner = b->resolver; o.shutdownOwner = b->owner;
    o.platform.hostVersion = b->version.c_str();
    o.quit = gh::QuitPolicy::External; o.dataCoreHook = gh::DataCoreHook::External;
    o.platform.pluginsEnabled = false; o.platform.privateSession.enabled = false;
    return o;
}
static void Retained(const gh::HostReport& r) {
    static uint64_t session = 0;
    CHECK(r.sessionId != 0 && r.dependencies == D::ProcessRetained);
    if (!session) session = r.sessionId;
    CHECK(r.sessionId == session);
}
#ifdef SCO_HAS_RETAINED_REAP
using Reap = sco::OwnedShutdown::ReapResult;
struct WorkerSignals {
    std::promise<void> entered, releaseWork, tlsEntered, releaseTls;
    std::promise<Reap> selfReap;
    std::atomic<bool> callableDestroyed{false}, tlsDestroyed{false};
};
struct CallableMarker {
    std::shared_ptr<WorkerSignals> signals;
    ~CallableMarker() { signals->callableDestroyed.store(true, std::memory_order_release); }
};
struct TlsTail {
    std::shared_ptr<WorkerSignals> signals;
    ~TlsTail() {
        signals->tlsEntered.set_value();
        signals->releaseTls.get_future().wait();
        signals->tlsDestroyed.store(true, std::memory_order_release);
    }
};
#endif
#ifdef SCO_HAS_REQUEST_API
static int BoundScenario(const std::string& mode) {
    using Adapter = offline_host::detail::CapsuleLifecycle;
    auto backing = std::make_shared<Backing>();
    auto options = Configuration(backing);
    if (mode == "bound-refusal") options.platform.gameServices = true;
    auto capsule = std::make_shared<const gh::HostCapsule>(options, backing);
    auto adapter = std::make_unique<Adapter>(capsule);
    CHECK(!adapter->Associated() && !adapter->TryRead());
    CHECK(adapter->RequestStop().result == R::Unavailable);
    CHECK(adapter->Drain(0) == offline_host::OwnerDrain::Unavailable);
    auto foreign = std::make_shared<Backing>();
    auto foreignCapsule = std::make_shared<const gh::HostCapsule>(Configuration(foreign), foreign);
    if (mode == "bound-private") {
        std::weak_ptr<Backing> weakBacking = backing;
        std::weak_ptr<const gh::HostCapsule> weakCapsule = capsule;
        options = {}; backing.reset(); capsule.reset(); adapter.reset();
        CHECK(weakBacking.expired() && weakCapsule.expired());
        CHECK(gh::StatusDetailed().dependencies == D::NeverExposed);
        return 0;
    }
    gh::HostReport prepared;
    if (mode == "bound-foreign") {
        CHECK(gh::PrepareDetailed(foreignCapsule).result == R::Ok);
        prepared = adapter->Prepare();
    } else if (mode == "bound-busy") {
        busyCandidate = adapter.get();
        CHECK(gh::PrepareDetailed(foreignCapsule).result == R::Ok);
        prepared = busyReport;
    } else {
        if (mode == "bound-failed-prepare")
            gh::testing::SetReportFault(gh::testing::ReportFault::BeforeOptionsCopy);
        if (mode == "bound-root-allocation")
            gh::testing::SetReportFault(gh::testing::ReportFault::BeforeRootAllocation);
        prepared = adapter->Prepare();
        gh::testing::SetReportFault(gh::testing::ReportFault::None);
    }
    if (mode == "bound-foreign" || mode == "bound-busy" ||
        mode == "bound-refusal" || mode == "bound-failed-prepare" || mode == "bound-root-allocation") {
        CHECK(prepared.result != R::Ok && !adapter->Associated());
        if (mode == "bound-root-allocation") {
            CHECK(prepared.result == R::Failed && prepared.sessionId == 0);
            CHECK(prepared.dependencies == D::NeverExposed && !backing->owner->closed());
        }
        CHECK(adapter->RestartRequired() && !adapter->TryRead());
        CHECK(adapter->Start().result == R::Unavailable);
        CHECK(adapter->RequestStop().result == R::Unavailable);
        CHECK(adapter->Drain(0) == offline_host::OwnerDrain::Unavailable);
        CHECK(adapter->Prepare().result == R::Unavailable);
        if (mode == "bound-foreign" || mode == "bound-busy") {
            CHECK(prepared.sessionId != 0 && !foreign->owner->closed());
            CHECK(!backing->owner->closed());
        }
    } else {
        CHECK(prepared.result == R::Ok && adapter->Associated());
        CHECK(!adapter->TryRead()); // startup admission remains shut
        if (mode == "bound-failed-start") gh::testing::FailAfterAppStart(true);
        const auto started = adapter->Start();
        if (mode == "bound-failed-start") {
            CHECK(started.result == R::Failed && backing->owner->closed());
            CHECK(adapter->RestartRequired() && !adapter->TryRead());
            CHECK(adapter->Start().result == R::Unavailable);
            CHECK(adapter->RequestStop().result == R::Ok);
        } else {
            CHECK(started.result == R::Ok);
            if (mode == "bound-repeat-start") {
                CHECK(adapter->Start().result != R::Ok);
                CHECK(adapter->Associated() && adapter->RestartRequired() && !adapter->TryRead());
                CHECK(adapter->Start().result == R::Unavailable);
                CHECK(adapter->RequestStop().result == R::Ok);
                CHECK(adapter->Drain(1000) == offline_host::OwnerDrain::DrainedRetained);
            } else if (mode == "bound-destroy-read") {
                std::weak_ptr<Backing> weakBacking = backing;
                std::weak_ptr<const gh::HostCapsule> weakCapsule = capsule;
                auto read = adapter->TryRead(); CHECK(read);
                auto* owner = backing->owner.get();
                options = {}; backing.reset(); capsule.reset(); adapter.reset();
                CHECK(!weakBacking.expired() && !weakCapsule.expired());
                CHECK(owner->open());
                CHECK(gh::RequestStopDetailed().result == R::Ok);
                CHECK(owner->closed() && !owner->ready());
                // The Read destructor below still has a live process-rooted owner.
                return 0;
            }
            if (mode != "bound-repeat-start") {
                std::promise<gh::HostReport> requested;
                std::promise<void> release;
                auto releaseRead = release.get_future();
                std::string output;
                std::thread callback([&] {
                    auto read = adapter->TryRead(); CHECK(read);
                    output = backing->resolver->output.substr(0, 1);
                    requested.set_value(adapter->RequestStop());
                    releaseRead.wait();
                    output += backing->resolver->output.substr(1);
                });
                auto result = requested.get_future();
                CHECK(result.wait_for(2s) == std::future_status::ready);
                const auto report = result.get();
                CHECK(report.result == R::Ok && report.sessionId == prepared.sessionId);
                CHECK(adapter->RestartRequired() && !adapter->TryRead());
                if (mode == "bound-timeout") {
                    auto drain = std::async(std::launch::async, [&] { return adapter->Drain(0); });
                    CHECK(drain.get() == offline_host::OwnerDrain::IncompleteRetained);
                    release.set_value(); callback.join();
                    CHECK(adapter->Drain(100) == offline_host::OwnerDrain::IncompleteRetained);
                    CHECK(!backing->owner->ready());
                } else {
                    auto drain = std::async(std::launch::async, [&] { return adapter->Drain(5000); });
                    CHECK(drain.wait_for(20ms) == std::future_status::timeout);
                    release.set_value(); callback.join();
                    CHECK(drain.get() == offline_host::OwnerDrain::DrainedRetained);
                    CHECK(backing->owner->ready());
                }
                CHECK(output == "complete copied value");
                CHECK(adapter->RequestStop().result == R::Failed);
                CHECK(adapter->Start().result == R::Unavailable);
            }
        }
    }
    std::weak_ptr<Backing> weakBacking = backing;
    std::weak_ptr<const gh::HostCapsule> weakCapsule = capsule;
    options = {}; backing.reset(); capsule.reset(); adapter.reset();
    CHECK(!weakBacking.expired() && !weakCapsule.expired());
    std::puts("capsule-bound synthetic lifecycle: retained; no native activation");
    return 0;
}
#endif
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string mode = argv[1];
    CHECK(gh::StatusDetailed().dependencies == D::NeverExposed);
#ifdef SCO_HAS_REQUEST_API
    if (mode.starts_with("bound-")) return BoundScenario(mode);
#endif
    auto b = std::make_shared<Backing>();
    auto o = Configuration(b);
#ifdef SCO_HAS_RETAINED_REAP
    if (mode == "retained-reap") {
        CHECK(b->owner->reap_retained_until(std::chrono::steady_clock::now() + 1s) == Reap::Refused);
        o.drainTimeoutMs = 10;
    }
#endif
    if (mode == "refusal") o.platform.gameServices = true;
    if (mode == "options-copy") o.platform.pluginRoot = std::string(8192, 'x');
    auto capsule = std::make_shared<const gh::HostCapsule>(o, b);
    std::weak_ptr<Backing> wb = b;
    std::weak_ptr<const gh::HostCapsule> wc = capsule;
    CHECK(capsule->configuration().image != &b->image->image);
    CHECK(capsule->configuration().platform.hostVersion != b->version.c_str());
    b->image->image = {}; b->version = "changed";
    CHECK(capsule->configuration().image->base == b->image->bytes.data());
    CHECK(std::strcmp(capsule->configuration().platform.hostVersion, "synthetic host") == 0);
    if (mode == "options-copy") gh::testing::SetReportFault(gh::testing::ReportFault::OptionsAllocation);
    reenter = mode == "reentry";
    const auto prepared = gh::PrepareDetailed(capsule);
    gh::testing::SetReportFault(gh::testing::ReportFault::None);
    if (mode == "refusal") {
        CHECK(prepared.result == R::BadArg && prepared.phase == gh::Phase::Idle);
        CHECK(prepared.sessionId == 0 && prepared.dependencies == D::NeverExposed && inspections == 0);
        o = gh::Options{}; capsule.reset(); b.reset();
        CHECK(wc.expired() && wb.expired() && backings == 1 && images == 1 && resolvers == 1);
        return 0;
    }
    Retained(prepared);
    if (mode == "reentry") {
        CHECK(nestedStart.result == R::Unavailable && nestedStop.result == R::Unavailable);
        Retained(nestedStart); Retained(nestedStop);
    }
    // All caller roots are discarded; lifetime must follow core's retained capsule.
    o = gh::Options{}; capsule.reset(); b.reset();
    CHECK(!wc.expired() && !wb.expired() && backings == 0 && images == 0 && resolvers == 0);
    auto retained = wb.lock(); CHECK(retained);
    if (mode == "options-copy") {
        CHECK(allocationFired && prepared.result == R::Failed && prepared.phase == gh::Phase::Failed);
        CHECK(retained->owner->closed() && inspections == 0);
        // In addition to the backing and capsule Options, the interrupted core
        // assignment copied both owners before allocating pluginRoot.
        CHECK(retained->image.use_count() == 3 && retained->resolver.use_count() == 3);
    } else {
        CHECK(prepared.result == R::Ok && prepared.phase == gh::Phase::Prepared);
        if (mode == "failed-start") gh::testing::FailAfterAppStart(true);
        const auto started = gh::StartDetailed(); Retained(started);
        if (mode == "failed-start") {
            CHECK(started.result == R::Failed && started.phase == gh::Phase::Failed && retained->owner->closed());
        } else {
            CHECK(started.result == R::Ok && started.phase == gh::Phase::Running);
            CHECK(!gh::Status().quitHookInstalled && !gh::Status().dataCoreActive);
#ifdef SCO_HAS_REQUEST_API
            if (mode == "owner-mismatch") {
                // Negative control: helper alone cannot establish that its
                // supplied owner is the one adopted by the detailed capsule.
                offline_host::detail::StopRequestGate gate;
                auto other = std::make_shared<sco::OwnedShutdown>();
                auto read = retained->owner->try_read(); CHECK(read);
                const auto report = offline_host::detail::RequestOwnedStop(gate, [&] { return other; });
                Retained(report);
                CHECK(report.result == R::Ok && retained->owner->closed() && other->closed());
                CHECK(offline_host::detail::DrainOwnedStop(gate, other, 1000) ==
                      offline_host::OwnerDrain::DrainedRetained);
                CHECK(other->ready() && !retained->owner->ready());
                CHECK(gh::StatusDetailed().drain == gh::DrainDisposition::Requested);
                CHECK(!wc.expired() && !wb.expired());
                std::puts("mismatched owner: helper cannot establish identity (synthetic)");
                return 0;
            }
            if (mode == "cooperative-request") {
                // Synthetic admitted callback, same owner as the adopted capsule.
                // This is not a product hook, native reader or direct quit path.
                offline_host::detail::StopRequestGate gate;
                std::promise<gh::HostReport> requested;
                std::promise<void> release;
                auto releaseRead = release.get_future().share();
                auto* owner = retained->owner.get();
                std::thread callback([&] {
                    auto read = owner->try_read(); CHECK(read);
                    requested.set_value(offline_host::detail::RequestOwnedStop(
                        gate, [&] { return retained->owner; }));
                    releaseRead.wait(); // keep the admitted read through this wait
                });
                auto requestFuture = requested.get_future();
                CHECK(requestFuture.wait_for(2s) == std::future_status::ready);
                const auto report = requestFuture.get(); Retained(report);
                CHECK(report.result == R::Ok && report.phase == gh::Phase::Running &&
                      report.drain == gh::DrainDisposition::Requested);
                CHECK(gate.RestartRequired() && owner->closed() && !owner->try_read());
                std::promise<void> drainStarted;
                auto drain = std::async(std::launch::async, [&] {
                    drainStarted.set_value();
                    return offline_host::detail::DrainOwnedStop(gate, retained->owner, 5000);
                });
                CHECK(drainStarted.get_future().wait_for(2s) == std::future_status::ready);
                CHECK(drain.wait_for(20ms) == std::future_status::timeout && !owner->ready());
                release.set_value(); callback.join();
                CHECK(drain.wait_for(2s) == std::future_status::ready &&
                      drain.get() == offline_host::OwnerDrain::DrainedRetained);
                CHECK(owner->ready() && gate.RestartRequired());
                const auto after = gh::StatusDetailed(); Retained(after);
                CHECK(after.drain == gh::DrainDisposition::Drained && !wb.expired() && !wc.expired());
                std::puts("cooperative request and controller drain: synthetic retention verified");
                return 0;
            }
#endif
            if (mode == "wrong-thread") {
                gh::HostReport other;
                std::thread t([&] { other = gh::StopDetailed(); }); t.join();
                Retained(other); CHECK(other.result == R::WrongThread && other.phase == gh::Phase::Running);
            }
#ifdef SCO_HAS_RETAINED_REAP
            if (mode == "retained-reap") {
                auto signals = std::make_shared<WorkerSignals>();
                auto entered = signals->entered.get_future();
                auto tlsEntered = signals->tlsEntered.get_future();
                auto selfReap = signals->selfReap.get_future();
                auto marker = std::make_shared<CallableMarker>(signals);
                auto* owner = retained->owner.get();
                CHECK(owner->start_worker([signals, marker, owner](const auto&) {
                    thread_local TlsTail tail{signals};
                    (void)tail; (void)marker;
                    signals->entered.set_value();
                    signals->releaseWork.get_future().wait();
                    signals->selfReap.set_value(owner->reap_retained_until(std::chrono::steady_clock::now() + 1s));
                }));
                marker.reset();
                CHECK(entered.wait_for(2s) == std::future_status::ready);
                const auto stopped = gh::StopDetailed(); Retained(stopped);
                CHECK(stopped.result == R::Failed && stopped.phase == gh::Phase::Failed);
                CHECK(owner->closed() && !owner->ready());
#ifdef SCO_HAS_REAP_TEST_ACCESS
                auto overlapping = std::async(std::launch::async, [&] {
                    return owner->reap_retained_until(std::chrono::steady_clock::now() + 2s);
                });
                CHECK(sco::OwnedShutdownTestAccess::wait_for_join_control(*owner, std::chrono::steady_clock::now() + 2s));
                CHECK(owner->reap_retained_until(std::chrono::steady_clock::now() + 1s) == Reap::Refused);
                CHECK(overlapping.get() == Reap::PendingRetained);
#endif
                CHECK(owner->reap_retained_until(std::chrono::steady_clock::now() + 10ms) == Reap::PendingRetained);
                CHECK(!signals->callableDestroyed.load(std::memory_order_acquire));
                CHECK(!signals->tlsDestroyed.load(std::memory_order_acquire));
                signals->releaseWork.set_value();
                CHECK(selfReap.wait_for(2s) == std::future_status::ready && selfReap.get() == Reap::Refused);
                CHECK(tlsEntered.wait_for(2s) == std::future_status::ready);
                CHECK(owner->reap_retained_until(std::chrono::steady_clock::now() + 10ms) == Reap::PendingRetained);
                CHECK(!signals->tlsDestroyed.load(std::memory_order_acquire));
                signals->releaseTls.set_value();
                CHECK(owner->reap_retained_until(std::chrono::steady_clock::now() + 2s) == Reap::ReapedRetained);
                CHECK(signals->callableDestroyed.load(std::memory_order_acquire));
                CHECK(signals->tlsDestroyed.load(std::memory_order_acquire));
#ifdef SCO_HAS_REAP_TEST_ACCESS
                CHECK(sco::OwnedShutdownTestAccess::handles_reaped(*owner));
#endif
                CHECK(owner->reap_retained_until(std::chrono::steady_clock::now() + 2s) == Reap::ReapedRetained);
                CHECK(!owner->ready() && !owner->start_worker([](const auto&) {}));
                Retained(gh::StatusDetailed());
                CHECK(!wc.expired() && !wb.expired() && backings == 0 && images == 0 && resolvers == 0);
            } else
#endif
            if (mode == "copy" || mode == "copy-negative") {
                const bool withLease = mode == "copy";
                std::promise<void> entered, resume;
                auto release = resume.get_future().share();
                std::atomic<bool> stopDone{false}; std::string output;
                std::thread reader([&] {
                    auto read = withLease ? retained->owner->try_read() : sco::OwnedShutdown::Read{};
                    CHECK(!withLease || read);
                    output = retained->resolver->output.substr(0, 1); entered.set_value(); release.wait();
                    output += retained->resolver->output.substr(1);
                });
                CHECK(entered.get_future().wait_for(2s) == std::future_status::ready);
                std::thread releaseReader([&] {
                    CHECK(retained->owner->wait_until_closed(std::chrono::steady_clock::now() + 2s));
                    CHECK(!wb.expired() && backings == 0);
                    if (withLease) CHECK(!stopDone.load() && !retained->owner->ready());
                    else {
                        const auto deadline = std::chrono::steady_clock::now() + 2s;
                        while (!stopDone.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
                        CHECK(stopDone.load() && output.size() == 1);
                    }
                    resume.set_value();
                });
                const auto stopped = gh::StopDetailed(); stopDone.store(true);
                reader.join(); releaseReader.join();
                Retained(stopped); CHECK(stopped.result == R::Failed && stopped.phase == gh::Phase::Failed);
                CHECK(output == retained->resolver->output);
            } else {
                const auto stopped = gh::StopDetailed(); Retained(stopped);
                CHECK(stopped.result == R::Failed && stopped.phase == gh::Phase::Failed);
            }
        }
    }
    const auto again = gh::StopDetailed(); Retained(again); CHECK(again.result != R::Ok);
    const auto retry = gh::StartDetailed(); Retained(retry); CHECK(retry.result != R::Ok);
    const auto status = gh::StatusDetailed(); Retained(status); CHECK(status.phase == gh::Phase::Failed);
    retained.reset(); CHECK(!wc.expired() && !wb.expired() && backings == 0 && images == 0 && resolvers == 0);
    std::puts("detailed host: process retention verified");
}
