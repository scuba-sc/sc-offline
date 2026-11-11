// SPDX-License-Identifier: GPL-3.0-only
// Synthetic exercise of the exact route and dispatcher used by missions.cpp.
// No game image, native hook, real provider, or game data is involved.
#include "../src/mission_script_route.h"
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)
namespace {
struct Script { const char* game; const char* data; std::size_t size; };
using Original = void* (*)(std::uintptr_t, void*, const char*, bool);
using Route = sc_offline::mission_script::Route<Script, Original>;
int originalCalls = 0, matchCalls = 0, bufferCalls = 0, readCalls = 0;
int originalResult, bufferResult, output;
const char* expectedPath = nullptr;
bool expectedReuse = false;
#ifdef SCO_OFFLINE_G1_HOST
sco::OwnedShutdown* owner = nullptr;
#endif

void* OriginalCall(std::uintptr_t system, void* out, const char* path, bool reuse) {
    ++originalCalls;
    CHECK(system == 19 && out == &output && path == expectedPath && reuse == expectedReuse);
    return &originalResult;
}
bool Match(const char* a, const char* b) {
    ++matchCalls;
    while (*a && *b) {
        if (std::tolower(static_cast<unsigned char>(*a++)) !=
            std::tolower(static_cast<unsigned char>(*b++))) return false;
    }
    return *a == *b;
}
void* Buffer(std::uintptr_t system, void* out, const char* bytes, std::size_t size, bool reuse) {
    ++bufferCalls;
    CHECK(system == 19 && out == &output && reuse == expectedReuse);
    CHECK(bytes && size == 3 && bytes[0] == 'o' && bytes[1] == 'n' && bytes[2] == 'e' && bytes[3] == '\0');
    return &bufferResult;
}
bool Read(Script& script) { ++readCalls; return script.data && script.size; }
void Reset(const char* path, bool reuse) {
    expectedPath = path; expectedReuse = reuse; originalCalls = matchCalls = bufferCalls = readCalls = 0;
}
template <typename BufferFn>
void* Call(const Route::Snapshot* snapshot, const char* path, bool reuse, BufferFn buffer) {
    return sc_offline::mission_script::Dispatch(snapshot, Match, buffer, 19, &output, path, reuse);
}
}

#ifdef SCO_OFFLINE_G1_HOST
namespace offline_host {
sco::OwnedShutdown::Read TryRead() noexcept {
    if (!owner) return {};
    try { return owner->try_read(); } catch (...) { return {}; }
}
}
#endif

int main() {
    {
        Route route; Script scripts[] = {{"A.xml", "one", 3}, {"bad.xml", nullptr, 0}};
        readCalls = 0;
        CHECK(!route.Prepare(scripts, 2, &OriginalCall, Read));
        CHECK(readCalls == 2 && !route.Get());
        scripts[1] = {"bad.xml", "one", 3};
        CHECK(!route.Prepare(scripts, 2, &OriginalCall, Read) && !route.Get());
    }
    {
        Route empty; Script scripts[] = {{"empty.xml", "", 0}};
        CHECK(!empty.Prepare(scripts, 1, &OriginalCall, Read) && !empty.Get());
        Route liar; Script invalid[] = {{"empty.xml", nullptr, 0}};
        CHECK(!liar.Prepare(invalid, 1, &OriginalCall, [](Script&) { return true; }) && !liar.Get());
        Route throws; Script good[] = {{"a", "one", 3}};
        CHECK(!throws.Prepare(good, 1, &OriginalCall, [](Script&) -> bool { throw std::runtime_error("read"); }));
        CHECK(!throws.Get());
    }
    {
        using namespace std::chrono_literals;
        Route route; Script scripts[] = {{"A.xml", "one", 3}, {"B.xml", "one", 3}};
        std::promise<void> entered, release;
        auto started = entered.get_future(); auto go = release.get_future().share();
        bool prepared = false;
        std::thread builder([&] {
            int n = 0;
            prepared = route.Prepare(scripts, 2, &OriginalCall, [&](Script& s) {
                if (++n == 1) { entered.set_value(); go.wait(); }
                return s.data && s.size;
            });
        });
        CHECK(started.wait_for(2s) == std::future_status::ready);
        CHECK(!route.Get()); // neither incomplete data nor the original is published
        int competingReads = 0;
        CHECK(!route.Prepare(scripts, 2, &OriginalCall, [&](Script&) { ++competingReads; return true; }));
        CHECK(competingReads == 0 && !route.Get());
        release.set_value(); builder.join();
        CHECK(prepared && route.Get() && route.Get()->count == 2 && route.Get()->scripts[1].size == 3);
    }
    {
        Route route;
        // Case-insensitive duplicates must keep the first item, not pick later data.
        Script scripts[] = {{"Case.XML", "one", 3}, {"case.xml", "two", 3}};
        CHECK(route.Prepare(scripts, 2, &OriginalCall, Read));
        const char path[] = "CASE.xml";
        Reset(path, true);
#ifdef SCO_OFFLINE_G1_HOST
        sco::OwnedShutdown current; owner = &current;
        CHECK(Call(route.Get(), path, true, Buffer) == &bufferResult);
        CHECK(originalCalls == 0 && bufferCalls == 1 && matchCalls == 1);
        const char other[] = "unknown.xml";
        Reset(other, false);
        CHECK(Call(route.Get(), other, false, Buffer) == &originalResult);
        CHECK(originalCalls == 1 && bufferCalls == 0 && matchCalls == 2);
        Reset(nullptr, false);
        CHECK(Call(route.Get(), nullptr, false, Buffer) == &originalResult);
        CHECK(originalCalls == 1 && bufferCalls == 0 && matchCalls == 0);
#else
        CHECK(Call(route.Get(), path, true, Buffer) == &originalResult);
        CHECK(originalCalls == 1 && bufferCalls == 0 && matchCalls == 0);
#endif
    }
#ifdef SCO_OFFLINE_G1_HOST
    {
        using namespace std::chrono_literals;
        Route route; Script scripts[] = {{"A.xml", "one", 3}};
        sco::OwnedShutdown current; owner = &current;
        std::promise<void> entered, release;
        auto started = entered.get_future(); auto go = release.get_future().share();
        bool prepared = false;
        std::thread builder([&] {
            auto read = current.try_read(); CHECK(read);
            prepared = route.Prepare(scripts, 1, &OriginalCall, [&](Script& s) {
                entered.set_value(); go.wait(); return s.data && s.size;
            });
        });
        CHECK(started.wait_for(2s) == std::future_status::ready);
        current.request_stop(); release.set_value(); builder.join();
        CHECK(prepared && route.Get()); // retained snapshot, not an installed hook
        int writes = 0;
        CHECK(!sc_offline::mission_script::Install([&] { ++writes; return true; }));
        CHECK(writes == 0); // closure won before install admission
    }
    {
        using namespace std::chrono_literals;
        sco::OwnedShutdown current; owner = &current;
        std::promise<void> entered, release;
        auto started = entered.get_future(); auto go = release.get_future().share();
        bool installed = false, drained = false;
        std::thread writer([&] {
            installed = sc_offline::mission_script::Install([&] {
                entered.set_value(); go.wait(); return true;
            });
        });
        CHECK(started.wait_for(2s) == std::future_status::ready);
        current.request_stop();
        std::thread drain([&] { drained = current.drain_until(std::chrono::steady_clock::now() + 5s); });
        const auto limit = std::chrono::steady_clock::now() + 2s;
        while (current.drain_state() == sco::OwnedShutdown::DrainState::Requested &&
               std::chrono::steady_clock::now() < limit) std::this_thread::yield();
        CHECK(current.drain_state() == sco::OwnedShutdown::DrainState::InProgress);
        int writes = 0;
        CHECK(!sc_offline::mission_script::Install([&] { ++writes; return true; }));
        CHECK(writes == 0);
        release.set_value(); writer.join(); drain.join();
        CHECK(installed && drained); // admitted writer was counted until return
    }
    {
        Route route; Script scripts[] = {{"A.xml", "one", 3}};
        CHECK(route.Prepare(scripts, 1, &OriginalCall, Read));
        const char path[] = "A.xml";
        sco::OwnedShutdown current; owner = &current; current.request_stop();
        Reset(path, false);
        CHECK(Call(route.Get(), path, false, Buffer) == &originalResult);
        CHECK(originalCalls == 1 && matchCalls == 0 && bufferCalls == 0);
        owner = nullptr; Reset(path, false);
        CHECK(Call(route.Get(), path, false, Buffer) == &originalResult);
        CHECK(originalCalls == 1 && matchCalls == 0 && bufferCalls == 0);
    }
    {
        Route route; Script scripts[] = {{"A.xml", "one", 3}};
        CHECK(route.Prepare(scripts, 1, &OriginalCall, Read));
        const char path[] = "A.xml";
        sco::OwnedShutdown current; owner = &current; current.fault(); Reset(path, false);
        CHECK(Call(route.Get(), path, false, Buffer) == &originalResult);
        CHECK(originalCalls == 1 && matchCalls == 0 && bufferCalls == 0);
    }
    {
        Route route; Script scripts[] = {{"A.xml", "one", 3}};
        CHECK(route.Prepare(scripts, 1, &OriginalCall, Read));
        const char path[] = "A.xml";
        sco::OwnedShutdown current; owner = &current; Reset(path, false);
        auto closeDuringSelection = [&](const char*, const char*) {
            ++matchCalls; current.request_stop(); return true;
        };
        // Already admitted work continues and is counted after closure; it is
        // NOT proof that a reentrant engine quit waits before object teardown.
        CHECK(sc_offline::mission_script::Dispatch(route.Get(), closeDuringSelection, Buffer,
              19, &output, path, false) == &bufferResult);
        CHECK(originalCalls == 0 && bufferCalls == 1 && matchCalls == 1);
    }
    {
        using namespace std::chrono_literals;
        Route route; Script scripts[] = {{"A.xml", "one", 3}};
        CHECK(route.Prepare(scripts, 1, &OriginalCall, Read));
        const char path[] = "A.xml";
        sco::OwnedShutdown current; owner = &current; Reset(path, false);
        std::thread drain; std::promise<void> started; auto ready = started.get_future();
        bool drained = false;
        auto replacement = [&](std::uintptr_t system, void* out, const char* bytes, std::size_t size, bool reuse) {
            CHECK(system == 19 && out == &output && size == 3 && bytes[0] == 'o' && !reuse);
            ++bufferCalls;
            current.request_stop(); // synthetic nested stop, not native quit proof
            CHECK(Call(route.Get(), path, false, Buffer) == &originalResult);
            CHECK(originalCalls == 1 && bufferCalls == 1);
            drain = std::thread([&] { started.set_value(); drained = current.drain_until(std::chrono::steady_clock::now() + 5s); });
            ready.wait();
            const auto limit = std::chrono::steady_clock::now() + 2s;
            while (current.drain_state() == sco::OwnedShutdown::DrainState::Requested &&
                   std::chrono::steady_clock::now() < limit) std::this_thread::yield();
            CHECK(current.drain_state() == sco::OwnedShutdown::DrainState::InProgress);
            CHECK(bytes[2] == 'e'); // selected bytes remain usable through the call
            return static_cast<void*>(&bufferResult);
        };
        CHECK(Call(route.Get(), path, false, replacement) == &bufferResult);
        drain.join();
        CHECK(drained && originalCalls == 1 && bufferCalls == 1);
        owner = nullptr;
    }
    std::puts("mission script route: 10 G1 synthetic groups passed");
#else
    {
        int writes = 0;
        CHECK(!sc_offline::mission_script::Install([&] { ++writes; return true; }));
        CHECK(writes == 0);
    }
    std::puts("mission script route: 5 no-G1 synthetic groups passed");
#endif
}
