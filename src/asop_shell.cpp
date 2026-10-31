// SPDX-License-Identifier: GPL-3.0-only
#include "asop_shell.h"
#include <algorithm>
#include <sstream>
namespace offline::asop {
namespace host = sco::game::asop_host;
bool ParseShips(const std::string& bytes, std::vector<std::string>& out) {
    if (bytes.size() > host::kMaxShips * 128) return false;
    std::istringstream input(bytes);
    std::vector<std::string> next;
    std::string line;
    while (std::getline(input, line)) {
        if (line.size() > 127) return false;
        line.erase(line.find_first_of("\r#") == std::string::npos ? line.size() : line.find_first_of("\r#"));
        const auto begin = line.find_first_not_of(" \t");
        if (begin == std::string::npos) continue;
        line = line.substr(begin, line.find_last_not_of(" \t") - begin + 1);
        if (line.size() >= 64 || next.size() == host::kMaxShips) return false;
        for (unsigned char c : line)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
        if (std::find(next.begin(), next.end(), line) != next.end()) return false;
        next.push_back(line);
    }
    out = std::move(next);
    return true;
}
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
    // Unavailable can follow partial installation. Close custom admission even
    // when the provider reported failure; Stop retains installed-hook backing.
    // AlreadyStarted describes an earlier attempt, not this installation.
    host::Result result;
    try {
        result = host::Start(config); // Provider copies the catalogue before returning.
    } catch (...) {
        host::Stop();
        return host::Result::Unavailable;
    }
    if (result == host::Result::Unavailable) host::Stop();
    return result;
}
host::Status Status() { return host::Snapshot(); }
void Stop() { host::Stop(); } // Admission closes before app/provider teardown.
void Pump(uint32_t now) { host::Pump(now); } // Provider retains fleet cadence and lift ownership.
bool Ready(uint32_t required) {
    const auto status = Status();
    return required && status.admitted && !status.stopped && (status.ready & required) == required;
}
}
