// SPDX-License-Identifier: GPL-3.0-only
// Synthetic process-isolated proof of the hook API's shutdown limits.
#include "sco/hook.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#if (defined(__x86_64__) || defined(_M_X64)) && !defined(__APPLE__)
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace hook = sco::hook;
static std::atomic<bool> entered{false}, releaseCallback{false}, callbackDone{false};
static int DetourWait() {
    entered.store(true, std::memory_order_release);
    while (!releaseCallback.load(std::memory_order_acquire)) std::this_thread::yield();
    callbackDone.store(true, std::memory_order_release);
    return 7;
}
static int DetourOther() { return 8; }
template <class T> static void* Address(T f) {
    void* p; static_assert(sizeof p == sizeof f); std::memcpy(&p, &f, sizeof p); return p;
}
#if defined(__clang__)
__attribute__((no_sanitize("function")))
#endif
static int Call(void* p) {
    int (*f)(); std::memcpy(&f, &p, sizeof f); return f();
}

int main() {
#if defined(_WIN32)
    auto* page = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!page) return 1;
#else
    auto* page = static_cast<unsigned char*>(mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                                                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (page == MAP_FAILED) return 1;
#endif
    const unsigned char code[]{0xb8, 1, 0, 0, 0, 0xc3};
    auto* product = page + 64; auto* host = page + 128; auto* patched = page + 192;
    std::memcpy(product, code, sizeof code); std::memcpy(host, code, sizeof code); std::memcpy(patched, code, sizeof code);
#if defined(_WIN32)
    if (!FlushInstructionCache(GetCurrentProcess(), page, 4096)) return 1;
#endif
    void* productOriginal = nullptr; void* hostOriginal = nullptr;
    if (hook::InstallDetour(product, 5, Address(&DetourWait), &productOriginal) != hook::Error::None ||
        hook::InstallDetour(host, 5, Address(&DetourOther), &hostOriginal) != hook::Error::None) return 2;
    const unsigned char replacement = 9;
    if (!hook::WriteCode(patched + 1, &replacement, 1)) return 3;
    std::thread worker([&] { Call(product); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!entered.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    const bool callbackEntered = entered.load(std::memory_order_acquire);
    if (!callbackEntered) {
        releaseCallback.store(true, std::memory_order_release);
        worker.join();
        std::puts("detour did not enter before deadline");
        return 4;
    }
    const auto removed = hook::RemoveAll();
    const bool stillRunning = !callbackDone.load(std::memory_order_acquire);
    const bool bothRemoved = removed == 2 && !hook::IsHooked(product) && !hook::IsHooked(host) && Call(host) == 1;
    const bool directWriteSurvives = Call(patched) == 9;
    releaseCallback.store(true, std::memory_order_release);
    worker.join();
    const bool finished = callbackDone.load(std::memory_order_acquire);
    std::printf("RemoveAll removes both owners: %s; direct write survives: %s; callback outlives removal: %s\n",
                bothRemoved ? "yes" : "NO", directWriteSurvives ? "yes" : "NO", stillRunning && finished ? "yes" : "NO");
    return bothRemoved && directWriteSurvives && stillRunning && finished ? 0 : 5;
}
#else
int main() { std::puts("synthetic executable detours require x86-64 Windows or Linux"); return 77; }
#endif
