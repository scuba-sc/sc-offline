// SPDX-License-Identifier: GPL-3.0-only
#include "product_lifetime.h"
#include <atomic>
#include <cstdio>
#include <thread>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)
int main() {
    {
        offline_host::ProductLifetime clean;
        CHECK(clean.Close());
        CHECK(clean.Close());
        CHECK(!clean.TryBeginMutation());
    }
    {
        offline_host::ProductLifetime retained;
        CHECK(retained.TryBeginMutation());
        CHECK(!retained.Close());
        CHECK(!retained.Close());
        CHECK(!retained.TryBeginMutation());
    }
    // A concurrent start either wins first and forces retention, or retirement
    // closes the gate first and the mutation must not begin.
    for (int i = 0; i < 1000; ++i) {
        offline_host::ProductLifetime lifetime;
        std::atomic<bool> go{false};
        bool began = false;
        std::thread starter([&] { while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
                                  began = lifetime.TryBeginMutation(); });
        go.store(true, std::memory_order_release);
        const bool noMutationAdmitted = lifetime.Close();
        starter.join();
        CHECK(noMutationAdmitted != began);
        CHECK(!lifetime.TryBeginMutation());
        CHECK(lifetime.Close() == noMutationAdmitted);
    }
    std::printf("product lifetime gate: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
