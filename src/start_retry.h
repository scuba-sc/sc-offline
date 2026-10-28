// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

namespace sc_offline {
// Limit fresh admission inspections while the host remains Prepared. Unsigned elapsed
// arithmetic permits GetTickCount to wrap; leaving Prepared resets the next attempt.
class StartRetry {
public:
    static constexpr uint32_t kIntervalMs = 500;
    template<class Start>
    void Tick(bool prepared, uint32_t now, Start start) {
        // A nested message during Start may see the transient Starting phase.
        if (in_flight_) return;
        if (!prepared) { attempted_ = false; return; }
        if (attempted_ && now - last_ < kIntervalMs) return;
        attempted_ = true;
        last_ = now;
        struct Guard {
            bool& active;
            explicit Guard(bool& value) : active(value) { active = true; }
            ~Guard() { active = false; }
        } guard(in_flight_);
        start();
    }
private:
    uint32_t last_ = 0;
    bool attempted_ = false;
    bool in_flight_ = false;
};
} // namespace sc_offline
