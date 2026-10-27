// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "model.h"
#include <scosdk/storage.hpp>
namespace sco::features::gear_presets {
struct SavedPreset { uint64_t id; std::string name; };
class PresetStore {
public:
    sco_result Open(const sco_api* api, sco_plugin* self) noexcept;
    sco_result Save(const Preset& preset, uint64_t& id) noexcept;
    sco_result List(std::vector<SavedPreset>& out) noexcept;
    sco_result Load(uint64_t id, Preset& out) noexcept;
private:
    sco_result Read(const char* key, std::string& bytes, uint32_t limit) noexcept;
    sco_result Keys(std::vector<uint64_t>& ids) noexcept;
    sco::sdk::Storage storage_;
    sco_plugin* self_ = nullptr;
};
}
