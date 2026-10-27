// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <sc_inventory.h>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sco::features::gear_presets {
struct Port {
    std::string key;
    uint32_t role = 0, state = 0, flags = 0, category = 0;
};
struct Preset {
    std::string name, domain, layout;
    std::vector<Port> ports;
};
enum class Advice { ExactClassUnknown, EquipCategory, RemoveItem, ReplaceCategory, Unknown, Unclassified };
struct Difference { std::string key; Advice advice; uint32_t wanted = 0, current = 0; };
// Captures only a complete copied layout. Entity IDs, epochs and order are never persisted.
bool Capture(std::string_view name, const sc_inventory_layout_snapshot& info,
             std::span<const sc_inventory_layout_entry> rows, Preset& out);
bool Encode(const Preset& preset, std::string& out);
bool Decode(std::string_view bytes, Preset& out);
// Equal domain, layout and the entire key/role manifest are required; storage never yields advice.
bool Compare(const Preset& saved, const Preset& current, std::vector<Difference>& out);
}
