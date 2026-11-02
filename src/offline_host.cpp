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
#include "asop_shell.h"
#include "common.h"
#include <fstream>
#include <memory>
#include <string>
#include <vector>
namespace {
namespace selection = sco::game::asop_selection;
offline::asop_selection::FixedFile g_selectionFile; // retained even if Stop refuses
bool ClassBusyThunk(void*, const char* name) { return sco::game::asop_host::FleetClassBusy(name); }
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
    options.class_busy = ClassBusyThunk; // Runs under the selection lock; only fleet/ship locks inside.
    return selection::Start(options);
}
// Keep a committed file notice pending until the host confirms that its runtime
// selection was applied. Current Core main deliberately refuses Reconfigure.
sco::Result TickSelection(void*) {
    static bool apply_deferred_reported=false;
    for (;;) {
        bool parse_failed=false,apply_threw=false;
        sco::Result reconfigure_result=sco::Result::Unavailable;
        std::vector<std::string> ships;
        const auto applied=offline::asop_selection::g_committedSelection.ApplyNext([&](const std::string& committed) {
            try {
                if (!offline::asop::ParseShips(committed, ships)) { parse_failed=true; return false; }
            } catch (...) { parse_failed=true; return false; }
            try { reconfigure_result=offline::asop::Reconfigure(ships); }
            catch (...) { apply_threw=true; return false; }
            return reconfigure_result==sco::Result::Ok;
        });
        if (applied==offline::asop_selection::NoticeApplyResult::None) {
            apply_deferred_reported=false;
            break;
        }
        if (applied==offline::asop_selection::NoticeApplyResult::Deferred) {
            if (parse_failed) {
                if (!apply_deferred_reported) Log("[asop-selection] saved selection could not be parsed; runtime apply remains pending");
            } else if (apply_threw) {
                if (!apply_deferred_reported) Log("[asop-selection] selection file saved; runtime apply threw and remains pending");
            } else if (!apply_deferred_reported) {
                Log("[asop-selection] selection file saved; runtime apply is pending (Reconfigure result %u)",static_cast<unsigned>(reconfigure_result));
            }
            apply_deferred_reported=true;
            break;
        }
        apply_deferred_reported=false;
    }
    return selection::Pump();
}
sco::Result StopSelection(void*) { return selection::Stop(); }
}
#endif

namespace gh = sco::game::host;
namespace offline_host {
namespace { std::atomic<bool> retained{false}; }
bool RestartRequired() noexcept { return retained.load(std::memory_order_acquire); }
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
    if (RestartRequired()) return sco::Result::Failed;
    try {
        const auto result = gh::Stop();
        if (result != sco::Result::Ok) retained.store(true, std::memory_order_release);
        return result;
    } catch (...) {
        retained.store(true, std::memory_order_release);
        return sco::Result::Failed;
    }
}
bool Running() { return !RestartRequired() && NativeActivationAvailable() && gh::Status().phase == gh::Phase::Running; }
} // namespace offline_host
