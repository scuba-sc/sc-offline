// SPDX-License-Identifier: GPL-3.0-only
#include "store.h"
#include <array>
#include <charconv>
#include <limits>

namespace sco::features::gear_presets {
namespace {
constexpr const char* prefix = "preset.v1.item.";
bool ParseId(std::string_view text, uint64_t& id) {
    if (text.empty() || text.size() > 20 || (text.size() > 1 && text[0] == '0')) return false;
    auto result = std::from_chars(text.data(), text.data() + text.size(), id);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && id != 0;
}
std::string Key(uint64_t id) { return std::string(prefix) + std::to_string(id); }
}
sco_result PresetStore::Open(const sco_api* api, sco_plugin* self) noexcept {
    self_ = nullptr;
    const auto result = storage_.Open(api, self);
    if (result == SCO_OK) self_ = self;
    return result;
}
sco_result PresetStore::Read(const char* key, std::string& bytes, uint32_t limit) noexcept {
    const auto* table = storage_.Table();
    if (!self_ || !sco::sdk::detail::CanCall(table, &sco_storage_v1::get)) return SCO_UNAVAILABLE;
    try {
        std::string copied(limit, '\0'); uint32_t size = limit;
        const auto result = table->get(self_, key, copied.data(), &size);
        if (result != SCO_OK) return result;
        if (size > limit) return SCO_FAILED;
        copied.resize(size); bytes = std::move(copied); return SCO_OK;
    } catch (...) { return SCO_TOO_MANY; }
}
sco_result PresetStore::Keys(std::vector<uint64_t>& ids) noexcept {
    const auto* table = storage_.Table();
    if (!self_ || !sco::sdk::detail::CanCall(table, &sco_storage_v1::next_key)) return SCO_UNAVAILABLE;
    try {
        std::vector<uint64_t> next; std::string previous;
        for (;;) {
            std::array<char, SCO_STORAGE_MAX_KEY+1> buf{}; uint32_t size = static_cast<uint32_t>(buf.size());
            const auto result = table->next_key(self_, prefix, previous.empty() ? nullptr : previous.c_str(), buf.data(), &size);
            if (result == SCO_NOT_FOUND) break;
            if (result != SCO_OK) return result;
            if (!size || size > buf.size() || buf[size-1] != '\0' || next.size() >= 32) return SCO_FAILED;
            std::string key(buf.data(), size-1);
            if (key.compare(0, std::char_traits<char>::length(prefix), prefix) != 0 ||
                (!previous.empty() && key <= previous)) return SCO_FAILED;
            uint64_t id = 0;
            if (!ParseId(std::string_view(key).substr(std::char_traits<char>::length(prefix)), id)) return SCO_FAILED;
            next.push_back(id); previous = std::move(key);
        }
        ids = std::move(next); return SCO_OK;
    } catch (...) { return SCO_TOO_MANY; }
}
sco_result PresetStore::Load(uint64_t id, Preset& out) noexcept {
    if (!id) return SCO_BAD_ARG;
    try {
        std::string bytes; const auto key = Key(id); const auto result = Read(key.c_str(), bytes, 16384);
        if (result != SCO_OK) return result;
        Preset decoded; if (!Decode(bytes, decoded)) return SCO_FAILED;
        out = std::move(decoded); return SCO_OK;
    } catch (...) { return SCO_TOO_MANY; }
}
sco_result PresetStore::List(std::vector<SavedPreset>& out) noexcept {
    try {
        std::vector<uint64_t> ids; auto result = Keys(ids); if (result != SCO_OK) return result;
        std::vector<SavedPreset> next;
        for (auto id : ids) { Preset preset; result = Load(id, preset); if (result != SCO_OK) return result; next.push_back({id, std::move(preset.name)}); }
        out = std::move(next); return SCO_OK;
    } catch (...) { return SCO_TOO_MANY; }
}
sco_result PresetStore::Save(const Preset& preset, uint64_t& id) noexcept {
    try {
        std::string bytes; if (!Encode(preset, bytes)) return SCO_BAD_ARG;
        sco::sdk::StorageTransaction tx(storage_); auto result = tx.Result(); if (result != SCO_OK) return result;
        std::vector<uint64_t> ids; result = Keys(ids); if (result != SCO_OK) return result;
        if (ids.size() >= 32) return SCO_TOO_MANY;
        std::string counter; result = Read("preset.v1.next_id", counter, 20);
        uint64_t next = 1;
        if (result == SCO_OK) { if (!ParseId(counter, next)) return SCO_FAILED; }
        else if (result != SCO_NOT_FOUND) return result;
        if (next == std::numeric_limits<uint64_t>::max()) return SCO_TOO_MANY;
        const auto key = Key(next); std::string existing;
        result = Read(key.c_str(), existing, 16384);
        if (result == SCO_OK) return SCO_FAILED;
        if (result != SCO_NOT_FOUND) return result;
        result = storage_.Put(key.c_str(), std::string_view(bytes)); if (result != SCO_OK) return result;
        result = storage_.Put("preset.v1.next_id", std::string_view(std::to_string(next+1))); if (result != SCO_OK) return result;
        result = tx.Commit(); if (result == SCO_OK) id = next;
        return result;
    } catch (...) { return SCO_TOO_MANY; }
}
}
