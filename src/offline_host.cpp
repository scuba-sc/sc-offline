// SPDX-License-Identifier: GPL-3.0-only
#include "offline_host.h"
#include "native_activation_policy.h"
#include <atomic>
#ifdef SCO_HOST_MENU_CLIENT
#include "menu.h"
#endif

#ifdef SCO_ASOP_SELECTION_COMPOSITION
#include "asop_catalogue.h"
#include "asop_selection_file.h"
#include "common.h"
#include <fstream>
#include <string>
#include <vector>
namespace {
namespace selection = sco::game::asop_selection;
offline::asop_selection::FixedFile g_selectionFile; // retained even if Stop refuses
bool ReadCatalogueFile(const char* path, std::string& out) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    const auto end = in.tellg();
    if (end < 0 || static_cast<unsigned long long>(end) > SC_ASOP_SELECTION_MAX_FILE_BYTES) return false;
    std::string bytes(static_cast<size_t>(end), '\0');
    if (!in.seekg(0)) return false;
    if (!bytes.empty() && !in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return false;
    if (in.peek() != std::char_traits<char>::eof() || in.bad()) return false;
    out = std::move(bytes);
    return true;
}
sco::Result StartSelection(void*) {
    char path[MAX_PATH]{};
    std::string first, second;
    selection::Options options;
    if (!ShipsFilePath(path, sizeof(path)) || !ReadCatalogueFile(path, first) ||
        !ReadCatalogueFile(path, second) || first != second ||
        !offline::asop_selection::ParseCatalogue(first, options.catalogue) ||
        !g_selectionFile.Initialize()) return sco::Result::Unavailable;
    options.file = g_selectionFile.Adapter();
    return selection::Start(options);
}
sco::Result TickSelection(void*) { return selection::Pump(); }
sco::Result StopSelection(void*) { return selection::Stop(); }
}
#endif

namespace gh = sco::game::host;
namespace offline_host {
namespace {
enum class StopState { Idle, Stopping, Retained };
std::atomic<StopState> stopState{StopState::Idle};
}
bool RestartRequired() noexcept { return stopState.load(std::memory_order_acquire) == StopState::Retained; }
sco::Result Prepare(gh::Options options) {
    // Core Prepare owns a private registry candidate, but this product cannot
    // yet prove the image, resolver, reader and worker lifetimes it requires.
    if (RestartRequired() || !NativeActivationAvailable()) return sco::Result::Unavailable;
    // Required game-pack hooks, never a shell WM_QUIT-only or private-loader fallback.
    options.quit = gh::QuitPolicy::Required;
    options.dataCoreHook = gh::DataCoreHook::Required;
#ifdef SCO_HOST_MENU_CLIENT
    options.platform.hostMenu = MenuHostDescriptor();
#endif
#ifdef SCO_ASOP_SELECTION_COMPOSITION
    options.platform.hostService = {nullptr, StartSelection, TickSelection, StopSelection};
#endif
    return gh::Prepare(options);
}
sco::Result Start() {
    if (RestartRequired() || !NativeActivationAvailable()) return sco::Result::Unavailable;
    return gh::Start();
}
sco::Result Tick(uint32_t now) {
    if (RestartRequired() || !NativeActivationAvailable()) return sco::Result::Unavailable;
    return gh::Tick(now);
}
sco::Result Stop() {
    // Reserve this composition seam before entering Core. A concurrent or
    // reentrant call that changes Stopping to Retained refuses a second Core
    // entry. If it latches before the successful Stopping-to-Idle transition,
    // the first call also fails. This is not a native reader or worker gate.
    auto state = stopState.load(std::memory_order_acquire);
    for (;;) {
        if (state == StopState::Retained) return sco::Result::Failed;
        const auto next = state == StopState::Idle ? StopState::Stopping : StopState::Retained;
        if (stopState.compare_exchange_weak(state, next, std::memory_order_acq_rel)) {
            if (next == StopState::Retained) return sco::Result::Failed;
            break;
        }
    }
    try {
        const auto result = gh::Stop();
        if (result != sco::Result::Ok) {
            stopState.store(StopState::Retained, std::memory_order_release);
            return result;
        }
        auto expected = StopState::Stopping;
        if (!stopState.compare_exchange_strong(expected, StopState::Idle, std::memory_order_acq_rel))
            return sco::Result::Failed;
        return result;
    } catch (...) {
        stopState.store(StopState::Retained, std::memory_order_release);
        return sco::Result::Failed;
    }
}
bool Running() { return !RestartRequired() && NativeActivationAvailable() && gh::Status().phase == gh::Phase::Running; }
} // namespace offline_host
