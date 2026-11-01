// SPDX-License-Identifier: GPL-3.0-only
// Restricted synthetic game-host composition. No native barrier or product activation.
#include "sco/game/host.h"
#include "src/game/host_testing.h"
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
void* operator new(std::size_t n) {
    if (gh::testing::ConsumeOptionsAllocationFault()) { allocationFired = true; throw std::bad_alloc(); }
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

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
static gh::HostReport nestedStart, nestedStop;
static gh::Admission Inspect() {
    ++inspections;
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
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string mode = argv[1];
    CHECK(gh::StatusDetailed().dependencies == D::NeverExposed);
    auto b = std::make_shared<Backing>();
    auto o = Configuration(b);
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
            if (mode == "wrong-thread") {
                gh::HostReport other;
                std::thread t([&] { other = gh::StopDetailed(); }); t.join();
                Retained(other); CHECK(other.result == R::WrongThread && other.phase == gh::Phase::Running);
            }
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
