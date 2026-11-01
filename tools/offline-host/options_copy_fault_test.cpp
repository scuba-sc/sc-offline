// SPDX-License-Identifier: GPL-3.0-only
// Linux-only, fresh-process fault injection into the pinned host Options copy.
#include "sco/game/host.h"
#include "sco/scan.h"
#include <array>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <new>
#include <string>

namespace {
thread_local bool failNext = false;
thread_local bool fired = false;
void* Allocate(std::size_t n) {
    if (failNext) { failNext = false; fired = true; throw std::bad_alloc(); }
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
}
void* operator new(std::size_t n) { return Allocate(n); }
void* operator new[](std::size_t n) { return Allocate(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace gh = sco::game::host;
namespace {
struct ImageBacking {
    std::array<uint8_t, 8192> bytes{};
    sco::Image image{bytes.data(), {bytes.data(), 4096}, {bytes.data() + 4096, 4096}, {}, 0, 8192};
};
struct ResolverBacking { int state = 1; };
struct Capsule {
    std::shared_ptr<ImageBacking> image = std::make_shared<ImageBacking>();
    std::shared_ptr<ResolverBacking> resolver = std::make_shared<ResolverBacking>();
    std::shared_ptr<sco::OwnedShutdown> shutdown = std::make_shared<sco::OwnedShutdown>();
};
// Root before Prepare; terminal failure retains it for process lifetime.
std::shared_ptr<Capsule>* const root = new std::shared_ptr<Capsule>();
int inspections = 0;
gh::Admission Inspect() {
    ++inspections;
    return {gh::Session::Offline, gh::Check::Clear, gh::Check::Clear, gh::Check::Clear};
}
}
int main() {
    *root = std::make_shared<Capsule>();
    gh::Options options;
    options.inspect = Inspect;
    options.image = &(*root)->image->image;
    options.imageOwner = (*root)->image;
    options.resolverOwner = (*root)->resolver;
    options.shutdownOwner = (*root)->shutdown;
    options.quit = gh::QuitPolicy::External;
    options.dataCoreHook = gh::DataCoreHook::External;
    options.platform.pluginsEnabled = false;
    options.platform.privateSession.enabled = false;
    // Construct everything before arming: this long single component forces a
    // dynamically backed path copy on the tested GNU standard library.
    options.platform.pluginRoot = std::string(8192, 'x');
    failNext = true;
    const auto result = gh::Prepare(options);
    failNext = false;
    options = gh::Options{}; // discard caller's owner references before measuring
    if (!fired) {
        std::puts("SKIP: the one-shot allocation hook was not reached");
        return 77;
    }
    const auto imageRefs = (*root)->image.use_count();
    const auto resolverRefs = (*root)->resolver.use_count();
    const bool ok = result == sco::Result::Failed && gh::Status().phase == gh::Phase::Failed &&
        (*root)->shutdown->closed() && inspections == 0 && imageRefs == 2 && resolverRefs == 2;
    std::printf("options copy fault: %s (inspections=%d image_refs=%ld resolver_refs=%ld)\n",
                ok ? "PASS" : "FAIL", inspections, imageRefs, resolverRefs);
    return ok ? 0 : 1;
}
