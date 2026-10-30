// SPDX-License-Identifier: GPL-3.0-only
#include "offline_host.h"
#include "product_lifetime.h"
#include "sco/log.h"
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
namespace { ProductLifetime productLifetime; }
bool BeginPersistentMutation() noexcept { return productLifetime.TryBeginMutation(); }
bool CloseProductMutationGate() noexcept { return productLifetime.Close(); }
gh::ConsumerRetirement RetireProductConsumer(void*) noexcept {
    CloseProductMutationGate();
    // This gate neither tracks the startup entry callback nor drains installed
    // product detours or direct writes. Retain backing until process exit.
    try { sco::Log("[app] product callbacks or patches may remain; close the game before restarting the host"); }
    catch (...) {} // A diagnostic failure must never change the retained result.
    return gh::ConsumerRetirement::Retained;
}
sco::Result Prepare(gh::Options options) {
    // Game-service startup needs a barrier that excludes native target execution
    // for the full startup phase. The executable-entry gate has already been
    // released by then and cannot supply that proof. Refuse before Prepare can
    // install host detours and before the shell applies product patches.
    if (options.platform.gameServices && !options.platform.gameReadersQuiesced) {
        try { sco::Log("[app] offline startup refused: game services require a native reader barrier; product features remain inactive"); }
        catch (...) {}
        return sco::Result::Failed;
    }
    // Required game-pack hooks, never a shell WM_QUIT-only or private-loader fallback.
    options.quit = gh::QuitPolicy::Required;
    options.dataCoreHook = gh::DataCoreHook::Required;
    options.retireConsumer = RetireProductConsumer;
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
