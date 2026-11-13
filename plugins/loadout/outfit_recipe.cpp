#include "outfit_recipe.h"
#include <cstring>
#include <utility>

namespace sco::outfit_recipe {
namespace {
constexpr const char* roles[] = {"Body_ItemPort", "Clothing_Feet", "Clothing_Legs", "Clothing_Torso_0", "Clothing_Hands", "Clothing_Torso_1", "Clothing_Torso2", "Armor_Undersuit", "Armor_Helmet", "Armor_Torso", "Armor_Arms", "Armor_Legs", "Head_ItemPort", "Eyes_ItemPort", "Teeth_ItemPort", "Eyebrow_ItemPort", "Hair_ItemPort", "Hair_Color", "Hat_ItemPort", "Eye_Accessories_ItemPort", "Head_Accessory_ItemPort", "Head_Horn_ItemPort", "Jewellery_ItemPort"};
bool equal(const std::string& a, const char* b) {
    std::size_t i = 0;
    for (; i < a.size() && b[i]; ++i) {
        auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
        if (lower(static_cast<unsigned char>(a[i])) != lower(static_cast<unsigned char>(b[i]))) return false;
    }
    return i == a.size() && b[i] == 0;
}
bool valid(const std::string& s) {
    if (s.empty() || s.size() > max_token) return false;
    for (unsigned char c : s) if (c <= 32 || c == 127) return false;
    return true;
}
}
static Result plan_impl(const Input* in, std::size_t count, bool sq42, LookupFn lookup, void* context, Recipe& output, bool accepted) {
    if (count > max_inputs || (count && !in)) return Result::invalid;
    Recipe r; r.input_count = count;
    int selected[sizeof(roles) / sizeof(*roles)]; for (int& i : selected) i = -1;
    for (std::size_t i = 0; i < count; ++i) {
        if (!valid(in[i].role) || !valid(in[i].value)) return Result::invalid;
        int role = -1;
        for (std::size_t j = 0; j < sizeof(roles) / sizeof(*roles); ++j) if (equal(in[i].role, roles[j])) { role = static_cast<int>(j); break; }
        // Historical loader checks all roles, including unsupported and duplicate roles.
        if (!accepted && role != 17) {
            if (!lookup) return Result::lookup_unavailable;
            Lookup found;
            try { found = lookup(in[i].value, context); }
            catch (...) { return Result::lookup_unavailable; }
            if (found == Lookup::unavailable) return Result::lookup_unavailable;
            if (found == Lookup::absent) { r.inputs[i] = Disposition::unknown_class; continue; }
            if (found != Lookup::present) return Result::invalid;
        }
        ++r.accepted_count; // Historical section is dropped when this remains zero.
        if (role < 0) { r.inputs[i] = Disposition::unsupported; continue; }
        if (selected[role] >= 0) { r.inputs[i] = Disposition::duplicate; continue; }
        selected[role] = static_cast<int>(i); r.inputs[i] = Disposition::orphan;
    }
    if (r.accepted_count == 0) return Result::no_outfit;
    bool overflow = false;
    auto add = [&](const char* role, int parent, int input, Value d, Tag tag = Tag::none) -> int {
        if (r.count >= max_nodes || parent < -1 || (parent >= 0 && static_cast<std::size_t>(parent) >= r.count)) { overflow = true; return -1; }
        const int index = static_cast<int>(r.count++);
        Node& n = r.nodes[index]; n.role = role; n.parent = parent; n.input = input; n.directive = d; n.tag = tag;
        if (input >= 0) { n.value = in[input].value; r.inputs[input] = Disposition::emitted; }
        return index;
    };
    auto piece = [&](int role, int parent, Value d = Value::explicit_input, Tag tag = Tag::none) { return add(roles[role], parent, selected[role], d, tag); };
    const int body = piece(0, -1, selected[0] < 0 ? Value::default_body : Value::explicit_input);
    for (int i = 1; i <= 4; ++i) if (selected[i] >= 0) piece(i, body);
    if (selected[5] >= 0) { int torso = piece(5, body); if (selected[6] >= 0) piece(6, torso); }
    bool armor = false; for (int i = 8; i <= 11; ++i) armor |= selected[i] >= 0;
    if (selected[7] >= 0 || armor) {
        int suit = piece(7, body, selected[7] < 0 ? Value::default_undersuit : Value::explicit_input);
        for (int i = 8; i <= 11; ++i) if (selected[i] >= 0) piece(i, suit);
    }
    const bool explicit_head = selected[12] >= 0;
    int head = piece(12, body, explicit_head ? Value::explicit_input : Value::default_head);
    int eyes;
    if (explicit_head) eyes = piece(13, head, selected[13] < 0 ? Value::explicit_head_default_eyes : Value::explicit_input);
    else eyes = add(roles[13], head, -1, Value::default_eyes);
    add("Lens_ItemPort", eyes, -1, sq42 ? Value::lens_sq42 : Value::lens_pu);
    if (explicit_head) {
        piece(14, head, selected[14] < 0 ? Value::explicit_head_default_teeth : Value::explicit_input);
        if (selected[15] >= 0) piece(15, head);
        if (selected[16] >= 0) { int hair = piece(16, head); if (selected[17] >= 0) add("Material_Variant", hair, selected[17], Value::explicit_input); }
    } else {
        add(roles[14], head, -1, Value::default_teeth, Tag::head_accessory);
        int hair = add(roles[16], head, -1, Value::default_hair, Tag::head_hair_male);
        add("Material_Variant", hair, -1, Value::default_hair_color);
        for (int i = 13; i <= 17; ++i) if (selected[i] >= 0) r.inputs[selected[i]] = Disposition::overridden_default;
    }
    for (int i = 18; i <= 22; ++i) if (selected[i] >= 0) piece(i, head);
    int mobi = add("mobiglas_attach", body, -1, Value::mobiglas);
    add("mobiglas_screen_attach", mobi, -1, Value::mobiglas_screen, Tag::mobiglas);
    add("legacy_mobiglas_screen_attach", mobi, -1, Value::legacy_mobiglas_screen, Tag::mobiglas);
    if (overflow) return Result::overflow;
    output = std::move(r); return Result::ok;
}
Result plan(const Input* in, std::size_t count, bool sq42, LookupFn lookup, void* context, Recipe& output) { return plan_impl(in, count, sq42, lookup, context, output, false); }
Result plan_accepted(const Input* in, std::size_t count, bool sq42, Recipe& output) { return plan_impl(in, count, sq42, nullptr, nullptr, output, true); }
}
