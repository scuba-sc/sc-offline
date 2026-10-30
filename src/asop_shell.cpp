// SPDX-License-Identifier: GPL-3.0-only
#include "asop_shell.h"
namespace offline::asop {
namespace host = sco::game::asop_host;
namespace { host::CatalogMode g_startedCatalog = host::CatalogMode::Game; }
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
    const auto result = host::Start(config); // Provider copies the catalogue before returning.
    g_startedCatalog = options.catalog;
    return result;
}
host::Result Reconfigure(const std::vector<std::string>& ships) {
    host::Config config;
    config.enabled = true;
    config.catalog = g_startedCatalog; // Reconfigure refuses a catalog-mode change.
    std::vector<const char*> names;
    for (const auto& name : ships) names.push_back(name.c_str());
    config.shipClasses = names.data();
    config.shipCount = names.size();
    return host::Reconfigure(config);
}
host::Status Status() { return host::Snapshot(); }
void Stop() { host::Stop(); } // Admission closes before app/provider teardown.
void Pump(uint32_t now) { host::Pump(now); } // Provider retains fleet cadence and lift ownership.
bool Ready(uint32_t required) {
    const auto status = Status();
    return required && status.admitted && !status.stopped && (status.ready & required) == required;
}
}
