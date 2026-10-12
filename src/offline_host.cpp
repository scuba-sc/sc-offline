// SPDX-License-Identifier: GPL-3.0-only
#include "offline_host.h"

namespace gh = sco::game::host;
namespace offline_host {
sco::Result Prepare(gh::Options options) {
    // Required game-pack hooks, never a shell WM_QUIT-only or private-loader fallback.
    options.quit = gh::QuitPolicy::Required;
    options.dataCoreHook = gh::DataCoreHook::Required;
    return gh::Prepare(options);
}
sco::Result Start() { return gh::Start(); }
sco::Result Tick(uint32_t now) { return gh::Tick(now); }
sco::Result Stop() { return gh::Stop(); }
bool Running() { return gh::Status().phase == gh::Phase::Running; }
} // namespace offline_host
