// SPDX-License-Identifier: GPL-3.0-only
// Standalone synthetic ownership exercise. No product activation or native barrier.
#include "sco/game/host.h"
#include "sco/scan.h"
#ifdef SCO_CAPSULE_PRIVATE_SEAM
#include "src/game/host_testing.h"
#endif
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace gh = sco::game::host;
using namespace std::chrono_literals;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)
static std::atomic<int> images{0}, resolvers{0}, capsules{0};
struct ImageBacking {
    std::array<uint8_t, 8192> bytes{};
    sco::Image image{bytes.data(), {bytes.data(), 4096}, {bytes.data() + 4096, 4096}, {}, 0, 8192};
    ~ImageBacking() { ++images; }
};
struct ResolverBacking { ~ResolverBacking() { ++resolvers; } };
struct Capsule {
    std::shared_ptr<ImageBacking> image = std::make_shared<ImageBacking>();
    std::shared_ptr<ResolverBacking> resolver = std::make_shared<ResolverBacking>();
    std::shared_ptr<sco::OwnedShutdown> shutdown = std::make_shared<sco::OwnedShutdown>();
    std::vector<std::string> output{"first", "second", "third"};
    ~Capsule() { ++capsules; }
};
// This fixture's process root is established before exposure, without allocating
// on failure. Intentionally process-retained on uncertain/terminal host paths.
static std::shared_ptr<Capsule>* const root = new std::shared_ptr<Capsule>();
static bool admit = true, reenter = false, faultOnInspect = false;
static sco::Result nested = sco::Result::Ok;
static gh::Admission Inspect() {
    if (reenter) { reenter = false; nested = gh::Stop(); }
    if (faultOnInspect) { faultOnInspect = false; (*root)->shutdown->fault(); }
    return {admit ? gh::Session::Offline : gh::Session::Unknown,
            gh::Check::Clear, gh::Check::Clear, gh::Check::Clear};
}
static gh::Options Options(const std::shared_ptr<Capsule>& c) {
    gh::Options o;
    o.inspect = Inspect;
    o.image = &c->image->image;
    o.imageOwner = c->image;
    o.resolverOwner = c->resolver;
    o.shutdownOwner = c->shutdown;
    o.quit = gh::QuitPolicy::External;
    o.dataCoreHook = gh::DataCoreHook::External;
    o.platform.pluginsEnabled = false;
    o.platform.privateSession.enabled = false;
    return o;
}
static void Host(const std::string& mode) {
    *root = std::make_shared<Capsule>();
    auto opts = Options(*root);
    std::weak_ptr<Capsule> weak = *root;
    if (mode == "badarg") {
        opts.size = 0;
        CHECK(gh::Prepare(opts) == sco::Result::BadArg);
        CHECK(gh::Status().phase == gh::Phase::Idle);
        opts = gh::Options{};
        root->reset();
        CHECK(weak.expired() && capsules == 1 && images == 1 && resolvers == 1);
        return;
    }
    if (mode == "admission" || mode == "budget") {
        if (mode == "admission") admit = false;
        else opts.signatureRegistryBudgetBytes = 1;
        const auto result = gh::Prepare(opts);
        CHECK(result == (mode == "admission" ? sco::Result::Unavailable : sco::Result::Failed));
        CHECK(gh::Status().phase == gh::Phase::Failed);
        CHECK((*root)->shutdown->closed());
        CHECK(gh::Start() == sco::Result::BadArg);
        opts = gh::Options{};
        CHECK(!weak.expired() && capsules == 0 && images == 0 && resolvers == 0);
        return;
    }
    CHECK(gh::Prepare(opts) == sco::Result::Ok);
    CHECK(gh::Status().phase == gh::Phase::Prepared);
    if (mode == "start-fault") {
        CHECK(gh::Activate() == sco::Result::Ok);
        faultOnInspect = true;
        CHECK(gh::Start() == sco::Result::Failed);
        CHECK(gh::Status().phase == gh::Phase::Failed && (*root)->shutdown->closed());
        CHECK(gh::Start() == sco::Result::BadArg);
        opts = gh::Options{};
        CHECK(!weak.expired() && capsules == 0 && images == 0 && resolvers == 0);
        return;
    }
#ifdef SCO_CAPSULE_PRIVATE_SEAM
    if (mode == "after-app-start") {
        gh::testing::FailAfterAppStart(true);
        CHECK(gh::Start() == sco::Result::Failed);
        CHECK(gh::Status().phase == gh::Phase::Failed && (*root)->shutdown->closed());
        CHECK(gh::Start() == sco::Result::BadArg);
        opts = gh::Options{};
        CHECK(!weak.expired() && capsules == 0 && images == 0 && resolvers == 0);
        return;
    }
#endif
    CHECK(gh::Start() == sco::Result::Ok);
    CHECK(gh::Status().phase == gh::Phase::Running);
    CHECK(!gh::Status().quitHookInstalled && !gh::Status().dataCoreActive);
    sco::Result offThread = sco::Result::Ok;
    std::thread wrong([&] { offThread = gh::Stop(); }); wrong.join();
    CHECK(offThread == sco::Result::WrongThread && gh::Status().phase == gh::Phase::Running);
    reenter = true;
    CHECK(gh::Tick(1) == sco::Result::Ok);
    CHECK(nested == sco::Result::Unavailable && gh::Status().phase == gh::Phase::Running);
    CHECK(gh::Stop() == sco::Result::Failed);
    CHECK(gh::Status().phase == gh::Phase::Failed && (*root)->shutdown->closed());
    CHECK(gh::Stop() == sco::Result::Unavailable && gh::Start() == sco::Result::BadArg);
    opts = gh::Options{};
    CHECK(!weak.expired() && capsules == 0 && images == 0 && resolvers == 0);
}
static void Copy(bool withLease) {
    // Separate synthetic operation: no game-host callback or native reader is implied.
    *root = std::make_shared<Capsule>();
    auto c = *root;
    std::weak_ptr<Capsule> weak = c;
    std::promise<void> copying, resume;
    auto release = resume.get_future().share();
    std::vector<std::string> copied;
    std::atomic<bool> leaseOk{false};
    std::thread reader([&] {
        auto lease = withLease ? c->shutdown->try_read() : sco::OwnedShutdown::Read{};
        leaseOk = !withLease || static_cast<bool>(lease);
        if (!leaseOk) { copying.set_value(); return; }
        copied.push_back(c->output[0]);
        copying.set_value();
        release.wait();
        copied.insert(copied.end(), c->output.begin() + 1, c->output.end());
    });
    copying.get_future().wait();
    CHECK(leaseOk);
    std::atomic<bool> drained{false};
    std::thread stopper([&] { drained = c->shutdown->drain_until(std::chrono::steady_clock::now() + 5s); });
    CHECK(c->shutdown->wait_until_closed(std::chrono::steady_clock::now() + 2s));
    CHECK(!weak.expired() && capsules == 0 && images == 0 && resolvers == 0);
    if (withLease) {
        // Closure occurs within drain; with this one held read, it cannot finish yet.
        CHECK(!drained && !c->shutdown->ready());
        resume.set_value(); reader.join(); stopper.join();
    } else {
        // Negative control: no registered worker and no Read. Keep all backing
        // alive even though drain completes before the final copy.
        stopper.join();
        CHECK(drained && c->shutdown->ready() && copied.size() == 1);
        resume.set_value(); reader.join();
    }
    CHECK(drained && copied == c->output && c->shutdown->ready());
    root->reset(); c.reset();
    CHECK(weak.expired() && capsules == 1 && images == 1 && resolvers == 1);
    CHECK((copied == std::vector<std::string>{"first", "second", "third"}));
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string mode = argv[1];
    if (mode == "copy" || mode == "copy-negative") Copy(mode == "copy");
    else if (mode == "badarg" || mode == "admission" || mode == "budget" || mode == "host" || mode == "start-fault") Host(mode);
#ifdef SCO_CAPSULE_PRIVATE_SEAM
    else if (mode == "after-app-start") Host(mode);
#endif
    else return 2;
    std::printf("owned capsule %s: %s\n", mode.c_str(), failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
