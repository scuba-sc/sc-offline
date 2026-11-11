// SPDX-License-Identifier: GPL-3.0-only
#include "asop_shell.h"
namespace offline::asop {
namespace host = sco::game::asop_host;
#ifdef SCO_ASOP_SELECTION_COMPOSITION
namespace { host::CatalogMode g_startedCatalog = host::CatalogMode::Game; }
#endif
host::Result Start(const Options& options, bool offlinePrepared, bool eacAbsent) {
    host::Config config;
    config.enabled = options.enabled;
    config.offlinePrepared = offlinePrepared;
    config.eacAbsent = eacAbsent;
    config.diagnostics = options.diagnostics;
    config.catalog = options.catalog;
    std::vector<const char*> names;
    for (const auto& name : options.ships) names.push_back(name.c_str());
    config.shipClasses = names.data();
    config.shipCount = names.size();
    // The composition is compile-time disabled until the provider offers a
    // reviewed nonblocking stop/drain contract. An exception or refusal is not
    // repaired by calling synchronous Stop from this game-thread callout.
    // AlreadyStarted describes an earlier attempt, not this installation.
    host::Result result;
    try {
        result = host::Start(config); // Provider copies the catalogue before returning.
    } catch (...) {
        return host::Result::Unavailable;
    }
#ifdef SCO_ASOP_SELECTION_COMPOSITION
    if (result == host::Result::Ok) g_startedCatalog = options.catalog;
#endif
    return result;
}
#ifdef SCO_ASOP_SELECTION_COMPOSITION
host::Result Reconfigure(const std::vector<std::string>& ships) {
    host::Config config;
    config.enabled = true;
    config.catalog = g_startedCatalog;
    std::vector<const char*> names;
    for (const auto& name : ships) names.push_back(name.c_str());
    config.shipClasses = names.data();
    config.shipCount = names.size();
    return host::Reconfigure(config);
}
#endif
host::Status Status() { return host::Snapshot(); }
void Stop() { host::Stop(); } // Synchronous; called only by a future ordered stop coordinator.
void Pump(uint32_t now) { host::Pump(now); } // Provider retains fleet cadence and lift ownership.
bool Ready(uint32_t required) {
    const auto status = Status();
    return required && status.admitted && !status.stopped && (status.ready & required) == required;
}
}
