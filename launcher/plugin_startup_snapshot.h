// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Read-only launcher contract for the future host-owned data/plugin-startup.json file.
// This parser intentionally accepts only the small ASCII v1 schema documented in
// docs/launcher.md. It does not read mod.log and does not establish live plugin state.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sco::launcher::plugin_startup {

constexpr size_t kMaxFileBytes = 64u * 1024u;
constexpr size_t kMaxPlugins = 256;

enum class Status { Complete, NoCompletedSnapshot, Unknown, Unsupported };

struct PluginRecord {
    std::string id;
    bool manifestValid = false;
    std::optional<std::string> version;
    std::string kind;
    std::string state;
    std::optional<std::string> reasonCode;
    bool restartRequired = false;
};

struct Snapshot {
    std::string sessionId;
    std::string startedAtUtc;
    std::string completedAtUtc;
    bool pluginsEnabledAtStart = false;
    size_t pluginCount = 0;
    size_t loadedCount = 0;
    std::vector<PluginRecord> plugins;
};

struct ParseResult {
    Status status = Status::Unknown;
    Snapshot snapshot;
};

inline bool IsSafeId(std::string_view id) {
    if (id.empty() || id.size() > 31) return false;
    for (const unsigned char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

inline const PluginRecord* FindPlugin(const Snapshot& snapshot, std::string_view id) {
    if (!IsSafeId(id)) return nullptr;
    for (const PluginRecord& plugin : snapshot.plugins)
        if (plugin.manifestValid && plugin.id == id) return &plugin;
    return nullptr;
}

namespace detail {

class JsonReader {
public:
    explicit JsonReader(std::string_view bytes) : bytes_(bytes) {}

    ParseResult Read() {
        ParseResult result;
        if (bytes_.empty() || bytes_.size() > kMaxFileBytes) return result;

        SkipSpace();
        if (!Take('{')) return result;

        unsigned fields = 0;
        unsigned schema = 0;
        std::string recordState;
        std::optional<size_t> pluginCount, loadedCount;
        bool pluginsNull = false;
        bool havePlugins = false;
        size_t pluginCountParsed = 0;
        SkipSpace();
        if (!Peek('}')) {
            for (;;) {
                std::string key;
                if (!ReadString(key, 32)) return result;
                SkipSpace();
                if (!Take(':')) return result;
                SkipSpace();

                unsigned bit = 0;
                if (key == "schema_version") {
                    bit = 1u << 0;
                    if (!ReadUnsigned(schema, 1000)) return result;
                } else if (key == "record_state") {
                    bit = 1u << 1;
                    if (!ReadString(recordState, 16)) return result;
                    if (recordState != "complete" && recordState != "starting" &&
                        recordState != "failed" && recordState != "incomplete") return result;
                } else if (key == "session_id") {
                    bit = 1u << 2;
                    if (!ReadString(result.snapshot.sessionId, 32)) return result;
                } else if (key == "started_at_utc") {
                    bit = 1u << 3;
                    if (!ReadString(result.snapshot.startedAtUtc, 20)) return result;
                } else if (key == "completed_at_utc") {
                    bit = 1u << 4;
                    if (TakeNull()) result.snapshot.completedAtUtc.clear();
                    else if (!ReadString(result.snapshot.completedAtUtc, 20)) return result;
                } else if (key == "plugins_enabled_at_start") {
                    bit = 1u << 5;
                    if (!ReadBool(result.snapshot.pluginsEnabledAtStart)) return result;
                } else if (key == "plugin_count") {
                    bit = 1u << 6;
                    if (TakeNull()) pluginCount.reset();
                    else {
                        size_t value = 0;
                        if (!ReadUnsigned(value, kMaxPlugins)) return result;
                        pluginCount = value;
                    }
                } else if (key == "loaded_count") {
                    bit = 1u << 7;
                    if (TakeNull()) loadedCount.reset();
                    else {
                        size_t value = 0;
                        if (!ReadUnsigned(value, kMaxPlugins)) return result;
                        loadedCount = value;
                    }
                } else if (key == "plugins") {
                    bit = 1u << 8;
                    havePlugins = true;
                    if (TakeNull()) pluginsNull = true;
                    else if (!ReadPlugins(result.snapshot.plugins, pluginCountParsed)) return result;
                } else {
                    return result; // v1 is closed: unrecognized fields are ambiguous to this reader.
                }
                if (fields & bit) return result;
                fields |= bit;

                SkipSpace();
                if (Take('}')) break;
                if (!Take(',')) return result;
                SkipSpace();
            }
        } else {
            return result;
        }

        SkipSpace();
        if (at_ != bytes_.size() || fields != 0x1ffu) return result;
        if (schema != 1) { result.status = Status::Unsupported; return result; }
        if (!ValidSessionId(result.snapshot.sessionId) || !ValidTimestamp(result.snapshot.startedAtUtc)) return result;
        if (!result.snapshot.completedAtUtc.empty() &&
            (!ValidTimestamp(result.snapshot.completedAtUtc) ||
             result.snapshot.startedAtUtc > result.snapshot.completedAtUtc)) return result;

        if (recordState != "complete") {
            const bool failed = recordState == "failed" || recordState == "incomplete";
            if ((failed && !ValidTimestamp(result.snapshot.completedAtUtc)) ||
                (!failed && !result.snapshot.completedAtUtc.empty()) || pluginCount || loadedCount || !havePlugins || !pluginsNull)
                return result;
            result.status = Status::NoCompletedSnapshot;
            return result;
        }

        if (result.snapshot.completedAtUtc.empty() ||
            !pluginCount || !loadedCount || !havePlugins || pluginsNull ||
            *pluginCount != pluginCountParsed || *loadedCount > *pluginCount) return result;

        size_t loaded = 0;
        std::set<std::string> ids;
        for (const PluginRecord& plugin : result.snapshot.plugins) {
            if (!ValidatePlugin(plugin) || !ids.insert(plugin.id).second) return result;
            loaded += plugin.state == "loaded";
        }
        if (loaded != *loadedCount) return result;
        result.snapshot.pluginCount = *pluginCount;
        result.snapshot.loadedCount = *loadedCount;
        result.status = Status::Complete;
        return result;
    }

private:
    void SkipSpace() {
        while (at_ < bytes_.size() && (bytes_[at_] == ' ' || bytes_[at_] == '\t' ||
               bytes_[at_] == '\r' || bytes_[at_] == '\n')) ++at_;
    }
    bool Peek(char c) const { return at_ < bytes_.size() && bytes_[at_] == c; }
    bool Take(char c) {
        if (!Peek(c)) return false;
        ++at_;
        return true;
    }
    bool TakeNull() {
        if (bytes_.substr(at_, 4) != "null") return false;
        at_ += 4;
        return true;
    }
    bool ReadBool(bool& out) {
        if (bytes_.substr(at_, 4) == "true") { at_ += 4; out = true; return true; }
        if (bytes_.substr(at_, 5) == "false") { at_ += 5; out = false; return true; }
        return false;
    }
    bool ReadUnsigned(size_t& out, size_t maximum) {
        if (at_ >= bytes_.size() || bytes_[at_] < '0' || bytes_[at_] > '9') return false;
        if (bytes_[at_] == '0' && at_ + 1 < bytes_.size() && bytes_[at_ + 1] >= '0' && bytes_[at_ + 1] <= '9')
            return false;
        size_t value = 0;
        while (at_ < bytes_.size() && bytes_[at_] >= '0' && bytes_[at_] <= '9') {
            const size_t digit = static_cast<size_t>(bytes_[at_] - '0');
            if (value > (maximum - digit) / 10) return false;
            value = value * 10 + digit;
            ++at_;
        }
        out = value;
        return true;
    }
    bool ReadUnsigned(unsigned& out, unsigned maximum) {
        size_t value = 0;
        if (!ReadUnsigned(value, maximum)) return false;
        out = static_cast<unsigned>(value);
        return true;
    }
    bool AppendAscii(std::string& out, unsigned value, size_t maximum) {
        if (value < 0x20 || value > 0x7e || out.size() >= maximum) return false;
        out.push_back(static_cast<char>(value));
        return true;
    }
    bool Hex4(unsigned& out) {
        if (bytes_.size() - at_ < 4) return false;
        unsigned value = 0;
        for (unsigned i = 0; i < 4; ++i) {
            const char c = bytes_[at_++];
            unsigned digit;
            if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A' + 10);
            else return false;
            value = (value << 4) | digit;
        }
        out = value;
        return true;
    }
    bool ReadString(std::string& out, size_t maximum) {
        if (!Take('"')) return false;
        out.clear();
        while (at_ < bytes_.size()) {
            const unsigned char c = static_cast<unsigned char>(bytes_[at_++]);
            if (c == '"') return true;
            if (c == '\\') {
                if (at_ >= bytes_.size()) return false;
                const char escaped = bytes_[at_++];
                if (escaped == '"' || escaped == '\\' || escaped == '/') {
                    if (!AppendAscii(out, static_cast<unsigned char>(escaped), maximum)) return false;
                } else if (escaped == 'u') {
                    unsigned codepoint = 0;
                    if (!Hex4(codepoint) || (codepoint >= 0xd800u && codepoint <= 0xdfffu) ||
                        !AppendAscii(out, codepoint, maximum)) return false;
                } else {
                    // Escaped controls are not part of this ASCII display schema.
                    return false;
                }
            } else {
                if (c < 0x20 || c > 0x7e || out.size() >= maximum) return false;
                out.push_back(static_cast<char>(c));
            }
        }
        return false;
    }
    bool ReadPlugins(std::vector<PluginRecord>& out, size_t& count) {
        if (!Take('[')) return false;
        SkipSpace();
        if (Take(']')) return true;
        for (;;) {
            if (out.size() >= kMaxPlugins) return false;
            PluginRecord plugin;
            if (!ReadPlugin(plugin)) return false;
            out.push_back(std::move(plugin));
            ++count;
            SkipSpace();
            if (Take(']')) return true;
            if (!Take(',')) return false;
            SkipSpace();
        }
    }
    bool ReadPlugin(PluginRecord& plugin) {
        if (!Take('{')) return false;
        unsigned fields = 0;
        SkipSpace();
        if (Peek('}')) return false;
        for (;;) {
            std::string key;
            if (!ReadString(key, 24)) return false;
            SkipSpace();
            if (!Take(':')) return false;
            SkipSpace();
            unsigned bit = 0;
            if (key == "id") {
                bit = 1u << 0;
                if (!ReadString(plugin.id, 31)) return false;
            } else if (key == "manifest_valid") {
                bit = 1u << 1;
                if (!ReadBool(plugin.manifestValid)) return false;
            } else if (key == "version") {
                bit = 1u << 2;
                if (!TakeNull()) {
                    std::string value;
                    if (!ReadString(value, 31)) return false;
                    plugin.version = std::move(value);
                }
            } else if (key == "kind") {
                bit = 1u << 3;
                if (!ReadString(plugin.kind, 8)) return false;
            } else if (key == "state") {
                bit = 1u << 4;
                if (!ReadString(plugin.state, 16)) return false;
            } else if (key == "reason_code") {
                bit = 1u << 5;
                if (!TakeNull()) {
                    std::string value;
                    if (!ReadString(value, 40)) return false;
                    plugin.reasonCode = std::move(value);
                }
            } else if (key == "restart_required") {
                bit = 1u << 6;
                if (!ReadBool(plugin.restartRequired)) return false;
            } else {
                return false;
            }
            if (fields & bit) return false;
            fields |= bit;
            SkipSpace();
            if (Take('}')) break;
            if (!Take(',')) return false;
            SkipSpace();
        }
        return fields == 0x7fu;
    }
    static bool ValidSessionId(std::string_view value) {
        if (value.size() != 32) return false;
        for (char c : value) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        return true;
    }
    static bool ValidTimestamp(std::string_view s) {
        if (s.size() != 20 || s[4] != '-' || s[7] != '-' || s[10] != 'T' ||
            s[13] != ':' || s[16] != ':' || s[19] != 'Z') return false;
        const auto digits = [&](size_t from, size_t count, unsigned& value) {
            value = 0;
            for (size_t i = from; i < from + count; ++i) {
                if (s[i] < '0' || s[i] > '9') return false;
                value = value * 10 + static_cast<unsigned>(s[i] - '0');
            }
            return true;
        };
        unsigned year, month, day, hour, minute, second;
        if (!digits(0, 4, year) || !digits(5, 2, month) || !digits(8, 2, day) ||
            !digits(11, 2, hour) || !digits(14, 2, minute) || !digits(17, 2, second) ||
            !year || month < 1 || month > 12 || hour > 23 || minute > 59 || second > 59) return false;
        static constexpr unsigned days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        unsigned maxDay = days[month - 1];
        if (month == 2 && (year % 400 == 0 || (year % 4 == 0 && year % 100 != 0))) ++maxDay;
        return day >= 1 && day <= maxDay;
    }
    static bool ValidVersion(std::string_view value) {
        if (value.empty() || value.size() > 31) return false;
        for (char c : value)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '+' || c == '-')) return false;
        return true;
    }
    static bool InvalidManifestAlias(std::string_view id) {
        constexpr std::string_view prefix = "unknown_";
        if (id.size() <= prefix.size() || id.substr(0, prefix.size()) != prefix) return false;
        for (char c : id.substr(prefix.size())) if (c < '0' || c > '9') return false;
        return true;
    }
    static bool ValidReason(std::string_view value) {
        return value == "plugins_off" || value == "plugin_disabled" || value == "invalid_manifest" ||
               value == "plugin_refused" || value == "plugin_crashed" ||
               value == "provider_fault_restart_required";
    }
    static bool ReasonMatchesState(const PluginRecord& plugin) {
        if (!plugin.reasonCode) return true;
        const std::string& reason = *plugin.reasonCode;
        if (reason == "plugins_off") return plugin.state == "off";
        if (reason == "plugin_disabled") return plugin.state == "disabled";
        if (reason == "invalid_manifest") return plugin.state == "refused" && !plugin.manifestValid;
        if (reason == "plugin_refused") return plugin.state == "refused";
        if (reason == "plugin_crashed") return plugin.state == "crashed";
        if (reason == "provider_fault_restart_required") return plugin.state == "faulted";
        return false;
    }
    static bool ValidatePlugin(const PluginRecord& plugin) {
        if (!IsSafeId(plugin.id)) return false;
        const bool kindKnown = plugin.kind == "native" || plugin.kind == "lua" ||
                               plugin.kind == "data" || plugin.kind == "builtin";
        const bool stateKnown = plugin.state == "off" || plugin.state == "disabled" ||
                                plugin.state == "refused" || plugin.state == "ready" ||
                                plugin.state == "loaded" || plugin.state == "crashed" ||
                                plugin.state == "unloaded" || plugin.state == "faulted";
        if (!stateKnown || (plugin.reasonCode && !ValidReason(*plugin.reasonCode)) ||
            !ReasonMatchesState(plugin) ||
            plugin.restartRequired != (plugin.state == "faulted")) return false;
        if (plugin.manifestValid)
            return kindKnown && plugin.version && ValidVersion(*plugin.version);
        return InvalidManifestAlias(plugin.id) && plugin.kind == "unknown" && !plugin.version;
    }

    std::string_view bytes_;
    size_t at_ = 0;
};

} // namespace detail

inline ParseResult Parse(std::string_view bytes) {
    return detail::JsonReader(bytes).Read();
}

} // namespace sco::launcher::plugin_startup
