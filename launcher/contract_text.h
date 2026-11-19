// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Pure text and list builders for the launcher-to-game contract: the key=value records the launcher
// writes (install marker, pc-changes.txt) and the SC_OFFLINE_* environment block for `play`.
// They were inline in launcher.cpp and are moved here unchanged so that the contract dump tool
// (tools/launcher-contract) and the launcher run the same code. No Windows calls in this file.
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace sco::launcher::contract {

// Value of the first `key=` line of a key=value record, without trailing CR or spaces; "" when absent.
inline std::string RecordField(const std::string& text, const char* key) {
    const std::string k = std::string(key) + "=";
    for (size_t pos = 0; pos < text.size();) {
        size_t nl = text.find('\n', pos); if (nl == std::string::npos) nl = text.size();
        if (!text.compare(pos, k.size(), k)) {
            std::string v = text.substr(pos + k.size(), nl - pos - k.size());
            while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
            return v;
        }
        pos = nl + 1;
    }
    return "";
}

// Appends `key=value\n` unless the record already holds that exact value for the key.
// True when the record changed.
inline bool RecordAdd(std::string& rec, const char* key, const char* value) {
    if (RecordField(rec, key) == value) return false;
    rec += std::string(key) + "=" + value + "\n";
    return true;
}

// "2026-11-17 09:05:03", the marker's time= value.
inline std::string MarkerTime(unsigned year, unsigned month, unsigned day, unsigned hour, unsigned minute, unsigned second) {
    char when[32];
    std::snprintf(when, sizeof(when), "%04u-%02u-%02u %02u:%02u:%02u", year, month, day, hour, minute, second);
    return when;
}

// "2026-11-17 09:05", the pc-changes.txt time= value.
inline std::string PcTime(unsigned year, unsigned month, unsigned day, unsigned hour, unsigned minute) {
    char ts[32];
    std::snprintf(ts, sizeof(ts), "%04u-%02u-%02u %02u:%02u", year, month, day, hour, minute);
    return ts;
}

// The Bin64\sc-offline.installed text. `mode` is play or install; `xml` is backup, none or skip.
inline std::string MarkerText(const std::string& version, const std::string& sha256, const char* mode,
                              const std::string& when, const std::string& xml) {
    return std::string("version=") + version + "\nsha256=" + sha256 + "\nmode=" + mode + "\ntime=" + when +
           "\nxml=" + xml + "\n";
}

// What the launcher sets for the game process on `play`, in this order.
struct PlayEnvInput {
    std::wstring bootMap, start, startShip, multiplayerAllow, asopFleetList, registryProbe;
    std::wstring menu = L"imgui";   // imgui or rust: which in-game menu the mod shows
    std::wstring data;      // <launcher folder>\data
    std::wstring userDir;   // <channel>\user\client\0
    bool plugins = true, relocation = true, multiplayer = true, asop = true;
};

inline std::vector<std::pair<std::wstring, std::wstring>> PlayEnv(const PlayEnvInput& c) {
    return {
        { L"SC_OFFLINE_BOOT_MAP", c.bootMap },
        { L"SC_OFFLINE_MOD_LOG", c.data + L"\\mod.log" },
        { L"SC_OFFLINE_SPAWN_FILE", c.data + L"\\spawn.txt" },
        { L"SC_OFFLINE_SHIPS_FILE", c.data + L"\\ships.txt" },
        { L"SC_OFFLINE_START", c.start },
        { L"SC_OFFLINE_START_SHIP", c.startShip },
        { L"SC_OFFLINE_PLUGINS", c.plugins ? L"on" : L"off" },
        { L"SC_OFFLINE_RELOCATION", c.relocation ? L"on" : L"off" },
        { L"SC_OFFLINE_MULTIPLAYER", c.multiplayer ? L"on" : L"off" },
        { L"SC_OFFLINE_MULTIPLAYER_ALLOW", c.multiplayerAllow },
        { L"SC_OFFLINE_ASOP", c.asop ? L"on" : L"off" },
        { L"SC_OFFLINE_ASOP_FLEET_LIST", c.asopFleetList },
        { L"SC_USER", c.userDir },
        { L"SC_OFFLINE_REGISTRY_PROBE", c.registryProbe },
        { L"SC_OFFLINE_MENU", c.menu },
    };
}

}  // namespace sco::launcher::contract
