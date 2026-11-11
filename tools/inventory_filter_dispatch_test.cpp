// SPDX-License-Identifier: GPL-3.0-only
// Exercises the same dispatch template used by Hook_ValidateFilter. Synthetic
// inputs only: no game, native hook, engine object, or provider is installed.
#include "../src/inventory_filter_dispatch.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#ifdef SCO_OFFLINE_G1_HOST
#include <chrono>
#include <future>
#include <thread>
#endif

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)
namespace {
std::uint64_t originalResult = 1;
std::uintptr_t expectedParent = 2;
int originalCalls = 0, postCalls = 0;
#ifdef SCO_OFFLINE_G1_HOST
sco::OwnedShutdown* owner = nullptr;
bool closeInOriginal = false;
int readAttempts = 0;
#endif

std::uint64_t Original(std::uintptr_t self, std::uintptr_t parent, std::uintptr_t state,
                       std::uintptr_t filter, std::uintptr_t a5, std::uintptr_t a6,
                       std::uintptr_t a7, std::uintptr_t a8) {
    ++originalCalls;
    CHECK(self == 1 && parent == expectedParent && state == 3 && filter == 4);
    CHECK(a5 == 5 && a6 == 6 && a7 == 7 && a8 == 8);
#ifdef SCO_OFFLINE_G1_HOST
    if (closeInOriginal) owner->request_stop();
#endif
    return originalResult;
}

std::uint64_t Post(std::uint64_t result, std::uintptr_t self, std::uintptr_t parent,
                   std::uintptr_t filter) {
    CHECK(originalCalls == 1 && self == 1 && parent == expectedParent && filter == 4);
    ++postCalls;
    // Model the existing admitted change for result 1 with a parent; the actual
    // native property lookup remains in hooks.cpp, inside the Dispatch lease.
    return result == 1 && parent ? 4 : result;
}

template <typename PostFn>
std::uint64_t Call(PostFn post, std::uintptr_t parent = 2) {
    expectedParent = parent;
    originalCalls = postCalls = 0;
    return sc_offline::inventory_filter::Dispatch(&Original, post, 1, parent, 3, 4, 5, 6, 7, 8);
}
}

#ifdef SCO_OFFLINE_G1_HOST
namespace offline_host {
sco::OwnedShutdown::Read TryRead() noexcept {
    ++readAttempts;
    if (!owner) return {};
    try { return owner->try_read(); } catch (...) { return {}; }
}
}
#endif

int main() {
#ifdef SCO_OFFLINE_G1_HOST
    {
        sco::OwnedShutdown current; owner = &current;
        originalResult = 1; closeInOriginal = true; readAttempts = 0;
        CHECK(Call(Post) == 1 && originalCalls == 1 && postCalls == 0 && readAttempts == 1);
        closeInOriginal = false;
    }
    {
        sco::OwnedShutdown current; owner = &current; current.request_stop();
        originalResult = 3; readAttempts = 0;
        CHECK(Call(Post) == 3 && originalCalls == 1 && postCalls == 0 && readAttempts == 1);
    }
    {
        sco::OwnedShutdown current; owner = &current; readAttempts = 0;
        originalResult = 1;
        CHECK(Call(Post) == 4 && postCalls == 1 && readAttempts == 1);
        CHECK(Call(Post, 0) == 1 && postCalls == 1 && readAttempts == 2);
        originalResult = 3;
        CHECK(Call(Post) == 3 && postCalls == 1 && readAttempts == 3);
        originalResult = 2;
        CHECK(Call(Post) == 2 && postCalls == 0 && readAttempts == 3);
    }
    {
        sco::OwnedShutdown current; owner = &current; current.fault();
        originalResult = 1; readAttempts = 0;
        CHECK(Call(Post) == 1 && postCalls == 0 && readAttempts == 1);
        owner = nullptr;
        CHECK(Call(Post) == 1 && postCalls == 0 && readAttempts == 2);
    }
    {
        using namespace std::chrono_literals;
        sco::OwnedShutdown current; owner = &current; originalResult = 1;
        std::thread drain;
        std::promise<void> entered;
        auto ready = entered.get_future();
        bool drained = false;
        const auto result = Call([&](std::uint64_t r, std::uintptr_t self,
                                     std::uintptr_t parent, std::uintptr_t filter) {
            CHECK(r == 1 && self == 1 && parent == 2 && filter == 4);
            ++postCalls;
            current.request_stop();
            drain = std::thread([&] {
                entered.set_value();
                drained = current.drain_until(std::chrono::steady_clock::now() + 5s);
            });
            ready.wait();
            const auto limit = std::chrono::steady_clock::now() + 2s;
            while (current.drain_state() == sco::OwnedShutdown::DrainState::Requested &&
                   std::chrono::steady_clock::now() < limit) std::this_thread::yield();
            CHECK(current.drain_state() == sco::OwnedShutdown::DrainState::InProgress);
            // The borrowed text is consumed here, after closure, before Dispatch
            // releases the lease. This is a synthetic stand-in for final Log use.
            const char* borrowed = "synthetic property";
            CHECK(borrowed[0] == 's' && borrowed[10] == 'p');
            return std::uint64_t{4};
        });
        drain.join();
        CHECK(result == 4 && originalCalls == 1 && postCalls == 1 && drained);
    }
    owner = nullptr;
    std::puts("inventory filter: 5 G1 ordering/lease groups passed (synthetic)");
#else
    originalResult = 1;
    CHECK(Call(Post) == 1 && originalCalls == 1 && postCalls == 0);
    originalResult = 3;
    CHECK(Call(Post) == 3 && originalCalls == 1 && postCalls == 0);
    originalResult = 2;
    CHECK(Call(Post) == 2 && originalCalls == 1 && postCalls == 0);
    std::puts("inventory filter: no-G1 original-only cases passed (synthetic)");
#endif
}
