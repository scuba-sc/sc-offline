// SPDX-License-Identifier: GPL-3.0-only
#include "offline_host.h"
#ifdef SCO_HOST_MENU_CLIENT
#include "menu.h"
#endif

#ifdef SCO_ASOP_SELECTION_COMPOSITION
#include "asop_catalogue.h"
#include "asop_selection_file.h"
#include "asop_shell.h"
#include "common.h"
#include <fstream>
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
// A committed selection save rebuilds the owned-ship list mid-session (stable slots).
sco::Result TickSelection(void*) {
    std::string committed;
    while (offline::asop_selection::TakeCommittedSelection(committed)) {
        std::vector<std::string> ships;
        if (!offline::asop::ParseShips(committed, ships)) break; // Commit already verified bytes; retry next pump.
        (void)offline::asop::Reconfigure(ships);
    }
    return selection::Pump();
}
sco::Result StopSelection(void*) { return selection::Stop(); }
}
#endif

namespace gh = sco::game::host;
namespace offline_host {
sco::Result Prepare(gh::Options options) {
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
sco::Result Start() { return gh::Start(); }
sco::Result Tick(uint32_t now) { return gh::Tick(now); }
sco::Result Stop() { return gh::Stop(); }
bool Running() { return gh::Status().phase == gh::Phase::Running; }
} // namespace offline_host
