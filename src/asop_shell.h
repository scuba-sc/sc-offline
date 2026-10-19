// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Product policy only. All engine hooks, fleet state and continuations belong to sco-core.
#include <sco/game/asop_host.h>
#include <string>
#include <vector>
namespace offline::asop {
struct Options {
    bool enabled = true;
    bool diagnostics = false;
    sco::game::asop_host::CatalogMode catalog = sco::game::asop_host::CatalogMode::Game;
    std::vector<std::string> ships;
};
// Atomic bounded parsing of user-supplied class-list bytes; no game records or pointers.
bool ParseShips(const std::string& bytes, std::vector<std::string>& out);
// Optional plugin raw snapshot: validated saved classes, applied only at startup.
bool ApplySelection(const std::string& bytes, Options& options);
sco::game::asop_host::Result Start(const Options& options, bool offlinePrepared, bool eacAbsent);
sco::game::asop_host::Status Status();
sco::game::asop_host::Result StartFromEnvironment(bool offlinePrepared, bool eacAbsent, const char* shipsPath);
// Query individual installation groups, not an all-or-nothing promise.
bool Ready(uint32_t required);
void Pump(uint32_t now);
void Stop();
}
