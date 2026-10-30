// SPDX-License-Identifier: GPL-3.0-only
// Real host and product retirement callback on synthetic images, no game hooks.
#include "offline_host.h"
#include "sco/caps.h"
#include "sco/signature_registry.h"
#include "sco/runtime.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace gh = sco::game::host;
static int failures = 0, exits = 0, tasks = 0;
static bool partial = false;
static gh::Admission evidence{gh::Session::Offline, gh::Check::Clear, gh::Check::Clear, gh::Check::Clear};
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)
static gh::Admission Inspect() { return evidence; }
static void SetCapabilities() {
    CHECK(sco::caps::Set("fixture.product", true) == sco::Result::Ok);
    if (partial) throw std::runtime_error("synthetic partial startup");
}
static void OnExit(const char*, const void*, void*) { ++exits; }
static void Task(void*) { ++tasks; }

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const char* mode = argv[1];
    partial = std::strcmp(mode, "partial") == 0;
    if (!partial && std::strcmp(mode, "stop") && std::strcmp(mode, "loss") && std::strcmp(mode, "quit")) return 2;
    unsigned char bytes[8192]{};
    sco::Image image{bytes, {bytes, 4096}, {bytes + 4096, 4096}, {}, 0, sizeof(bytes)};
    gh::Options options;
    options.inspect = Inspect; options.image = &image; options.quit = gh::QuitPolicy::External;
    options.dataCoreHook = gh::DataCoreHook::External; // synthetic test installs no hooks
    options.retireConsumer = offline_host::RetireProductConsumer;
    options.platform.setCapabilities = SetCapabilities;
    CHECK(gh::Prepare(options) == sco::Result::Ok);
    const auto owner = sco::AcquireHostSignatureRegistry();
    CHECK(owner && offline_host::BeginPersistentMutation());
    const sco::Result start = gh::Start();
    if (partial) CHECK(start == sco::Result::Failed);
    else {
        CHECK(start == sco::Result::Ok);
        CHECK(sco::caps::Has("fixture.product"));
        CHECK(sco::Subscribe(&exits, "game.exit", OnExit, nullptr) == sco::Result::Ok);
        CHECK(sco::Post(Task, nullptr) == sco::Result::Ok);
        if (!std::strcmp(mode, "loss")) {
            evidence.session = gh::Session::Unknown;
            CHECK(gh::Tick(1) == sco::Result::Unavailable);
        } else if (!std::strcmp(mode, "quit")) CHECK(gh::BeforeQuit() == sco::Result::Failed);
        else CHECK(gh::Stop() == sco::Result::Failed);
        CHECK(exits == 0 && tasks == 0 && sco::QueuedTasks() == 1);
    }
    CHECK(gh::Status().phase == gh::Phase::Failed);
    CHECK(!gh::FreshAdmissionAllowed() && !sco::caps::Has("fixture.product"));
    CHECK(sco::AcquireHostSignatureRegistry() == owner);
    CHECK(!offline_host::BeginPersistentMutation());
    CHECK(gh::Start() == sco::Result::BadArg && gh::Stop() == sco::Result::Unavailable);
    std::printf("product retirement %s: %d failure(s)\n", mode, failures);
    return failures ? 1 : 0;
}
