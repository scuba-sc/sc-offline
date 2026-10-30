// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Product policy only. All engine hooks, fleet state and continuations belong to sco-core.
#include <sco/game/asop_host.h>
#include <cstddef>
#include <string>
#include <vector>
namespace offline::asop {
// Contract cap for the selection file (sco-core docs/game/asop-selection.md).
constexpr std::size_t kMaxSelectionBytes = 262144;
struct Options {
    bool enabled = true;
    bool diagnostics = false;
    sco::game::asop_host::CatalogMode catalog = sco::game::asop_host::CatalogMode::Game;
    std::vector<std::string> ships;
};
// Strict and atomic: one bad line rejects the whole file. Duplicates collapse case-insensitively, first spelling kept.
bool ParseShips(const std::string& bytes, std::vector<std::string>& out);
sco::game::asop_host::Result Start(const Options& options, bool offlinePrepared, bool eacAbsent);
// Rebuild the owned-ship list from a committed selection (game thread). Refuses unless
// the host is already configured, admitted and not stopped, and the catalog mode matches.
sco::game::asop_host::Result Reconfigure(const std::vector<std::string>& ships);
sco::game::asop_host::Status Status();
sco::game::asop_host::Result StartFromEnvironment(bool offlinePrepared, bool eacAbsent, const char* selectionPath);
// Query individual installation groups, not an all-or-nothing promise.
bool Ready(uint32_t required);
void Pump(uint32_t now);
void Stop();
}
