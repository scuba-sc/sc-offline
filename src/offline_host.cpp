// SPDX-License-Identifier: GPL-3.0-only
#include "offline_host.h"
#include "native_activation_policy.h"
#include "stop_request_gate.h"
#include <chrono>
#include <memory>
#include <new>
#include <utility>
#ifdef SCO_CORE_NON_DRAINING_STOP_API
#include "stop_request_protocol.h"
#include <windows.h>
#endif
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
        sco::game::asop_host::Result reconfigure_result=sco::game::asop_host::Result::Unavailable;
        std::vector<std::string> ships;
        const auto applied=offline::asop_selection::g_committedSelection.ApplyNext([&](const std::string& committed) {
            try {
                if (!offline::asop::ParseShips(committed, ships)) { parse_failed=true; return false; }
            } catch (...) { parse_failed=true; return false; }
            try { reconfigure_result=offline::asop::Reconfigure(ships); }
            catch (...) { apply_threw=true; return false; }
            return reconfigure_result==sco::game::asop_host::Result::Ok;
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
namespace { detail::StopRequestGate stopRequest; }
bool RestartRequired() noexcept { return stopRequest.RestartRequired(); }

namespace {
gh::HostReport StopReport(sco::Result result, gh::Phase phase,
                          gh::DependencyDisposition dependencies) noexcept {
    gh::HostReport report{};
    report.result = result;
    report.phase = phase;
    report.dependencies = dependencies;
    return report;
}
}

#ifdef SCO_CORE_NON_DRAINING_STOP_API
namespace {
struct ModulePin {
    HMODULE module;
    ~ModulePin() { if (module) FreeLibrary(module); }
};

std::shared_ptr<const void> PinModule(const void* address) noexcept {
    HMODULE module = nullptr;
    if (!address || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(address), &module)) return {};
    auto* pin = new (std::nothrow) ModulePin{module};
    if (!pin) { FreeLibrary(module); return {}; }
    try { return std::shared_ptr<const void>(pin); }
    catch (...) { return {}; } // shared_ptr deletes pin if control-block allocation fails
}

struct ProductRoot {
    std::shared_ptr<sco::OwnedShutdown> shutdown;
    std::shared_ptr<const void> module;
};

ProductRoot* GetProductRoot() noexcept {
    static ProductRoot* const root = []() noexcept -> ProductRoot* {
        try {
            auto created = std::make_unique<ProductRoot>();
            created->shutdown = std::make_shared<sco::OwnedShutdown>();
            created->module = PinModule(reinterpret_cast<const void*>(&offline_host::RetainProductModule));
            if (!created->shutdown || !created->module) return nullptr;
            // Intentionally never destroyed; OwnedShutdown joins in its destructor.
            return created.release();
        } catch (...) { return nullptr; }
    }();
    return root;
}

// Intentionally process-retained. Its destructor joins workers, so never let
// module-static teardown destroy the owner's last reference.
std::shared_ptr<sco::OwnedShutdown> ProductOwner() noexcept {
    auto* root = GetProductRoot();
    return root ? root->shutdown : std::shared_ptr<sco::OwnedShutdown>{};
}
}
#endif

bool RetainProductModule() noexcept {
    if (RestartRequired() || !NativeActivationAvailable()) return false;
#ifdef SCO_CORE_NON_DRAINING_STOP_API
    const auto* root = GetProductRoot();
    return root && root->module && root->shutdown;
#else
    return false;
#endif
}

bool AttachShutdownOwner(gh::Options& options) noexcept {
    if (RestartRequired() || !NativeActivationAvailable()) return false;
#ifdef SCO_CORE_NON_DRAINING_STOP_API
    auto* root = GetProductRoot();
    auto imageOwner = options.image ? PinModule(options.image->base) : std::shared_ptr<const void>{};
    if (!root || !root->shutdown || !root->module || !imageOwner) return false;
    options.imageOwner = std::move(imageOwner);
    options.resolverOwner = root->module;
    options.shutdownOwner = root->shutdown;
    return true;
#else
    (void)options;
    return false;
#endif
}

sco::OwnedShutdown::Read TryRead() noexcept {
#ifdef SCO_CORE_NON_DRAINING_STOP_API
    auto owner = ProductOwner();
    if (owner) {
        try { return owner->try_read(); } catch (...) {}
    }
#endif
    return {};
}

sco::game::host::HostReport RequestStop() noexcept {
#ifdef SCO_CORE_NON_DRAINING_STOP_API
    if (!NativeActivationAvailable())
        return StopReport(sco::Result::Unavailable, gh::Phase::Idle, gh::DependencyDisposition::NeverExposed);
    // Legacy Prepare does not adopt a detailed HostCapsule, so the Core report
    // is Unavailable here. Owner closure is independent and remains retained.
    return detail::RequestOwnedStop(stopRequest, [] { return ProductOwner(); });
#else
    return StopReport(sco::Result::Unavailable, gh::Phase::Idle, gh::DependencyDisposition::NeverExposed);
#endif
}

OwnerDrain DrainRequestedStop(uint32_t timeoutMs) noexcept {
#ifdef SCO_CORE_NON_DRAINING_STOP_API
    if (!stopRequest.DrainMayStart()) return OwnerDrain::Unavailable;
    return detail::DrainOwnedStop(stopRequest, ProductOwner(), timeoutMs);
#else
    (void)timeoutMs;
    return OwnerDrain::Unavailable;
#endif
}

sco::Result Prepare(gh::Options options) {
    // Core Prepare owns a private registry candidate, but this product cannot
    // yet prove the image, resolver, reader and worker lifetimes it requires.
    if (RestartRequired() || !NativeActivationAvailable()) return sco::Result::Unavailable;
#ifndef SCO_CORE_NON_DRAINING_STOP_API
    (void)options;
    return sco::Result::Unavailable;
#else
    if (!options.shutdownOwner || options.shutdownOwner != ProductOwner()) return sco::Result::BadArg;
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
#endif
}
sco::Result Start() {
    if (RestartRequired() || !NativeActivationAvailable()) return sco::Result::Unavailable;
#ifndef SCO_CORE_NON_DRAINING_STOP_API
    return sco::Result::Unavailable;
#else
    return gh::Start();
#endif
}
sco::Result Tick(uint32_t now) {
    if (RestartRequired() || !NativeActivationAvailable()) return sco::Result::Unavailable;
#ifndef SCO_CORE_NON_DRAINING_STOP_API
    (void)now;
    return sco::Result::Unavailable;
#else
    return gh::Tick(now);
#endif
}
sco::Result Stop() {
    // Legacy game-thread Stop can drain synchronously. Product shutdown must
    // close owner admission and let the independent controller drain it.
    return RequestStop().result;
}
bool Running() { return !RestartRequired() && NativeActivationAvailable() && gh::Status().phase == gh::Phase::Running; }
} // namespace offline_host
