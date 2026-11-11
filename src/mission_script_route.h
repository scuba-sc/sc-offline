// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#ifdef SCO_OFFLINE_G1_HOST
#include "offline_host.h"
#endif

namespace sc_offline::mission_script {
// One process-retained route. The caller owns every item and its bytes through
// process exit, and must not mutate them after successful preparation. A failed
// preparation is terminal; no partially loaded snapshot is published.
template <typename Script, typename Original>
class Route {
public:
    struct Snapshot { const Script* scripts; std::size_t count; Original original; };
    template <typename Reader>
    bool Prepare(Script* scripts, std::size_t count, Original original, Reader reader) noexcept {
        if (started_.test_and_set(std::memory_order_acq_rel)) return false;
        if (!scripts || !count || !original) return false;
        try {
            for (std::size_t i = 0; i < count; ++i)
                if (!reader(scripts[i]) || !scripts[i].data || !scripts[i].size) return false;
        } catch (...) { return false; }
        retained_ = {scripts, count, original};
        snapshot_.store(&retained_, std::memory_order_release);
        return true;
    }
    const Snapshot* Get() const noexcept { return snapshot_.load(std::memory_order_acquire); }
private:
    std::atomic_flag started_ = ATOMIC_FLAG_INIT;
    Snapshot retained_{};
    std::atomic<const Snapshot*> snapshot_{nullptr};
};

// Installation is an owned product obligation, not an engine quiescence proof.
// A close that wins before admission prevents the write; one racing after
// admission must wait for the write to return before retiring product uses.
template <typename Write>
bool Install(Write write) {
#ifdef SCO_OFFLINE_G1_HOST
    auto read = offline_host::TryRead();
    if (!read) return false;
    return write();
#else
    (void)write;
    return false;
#endif
}

// Runs synchronously with original arguments. On refusal, the original is
// called after the read lease leaves scope. A selected replacement runs under
// the read through its return. This census is NOT an engine lifetime barrier.
template <typename Snapshot, typename Match, typename LoadBuffer>
void* Dispatch(const Snapshot* snapshot, Match match, LoadBuffer loadBuffer,
               std::uintptr_t system, void* out, const char* path, bool reuseStrings) {
    if (!snapshot || !snapshot->original) return nullptr;
#ifdef SCO_OFFLINE_G1_HOST
    {
        auto read = offline_host::TryRead();
        if (read && path) {
            for (std::size_t i = 0; i < snapshot->count; ++i) {
                const auto& script = snapshot->scripts[i];
                if (!match(path, script.game)) continue;
                if (script.data && script.size)
                    return loadBuffer(system, out, static_cast<const char*>(script.data), script.size, reuseStrings);
                break; // first case-insensitive match wins, including failure
            }
        }
    }
#else
    (void)match;
    (void)loadBuffer;
#endif
    return snapshot->original(system, out, path, reuseStrings);
}
} // namespace sc_offline::mission_script
