// SPDX-License-Identifier: GPL-3.0-only
#include "inspector.h"
#include "sc_actors.h"
#include "sc_entities.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#ifdef SCO_INSPECTOR_CANDIDATE
#include "sc_actor_stats.h"
#include "sc_game_build.h"
#include "sc_gamerules.h"
#include "sc_inventory.h"
#include "sc_input.h"
#include "sco_widgets.h"
#include <cstring>
#endif

namespace sco::features::inspector {
namespace {

// Test the complete member, not just its first byte; short-circuit before reading it.
#define HAS_FN(table, type, member) \
    ((table) && (table)->size >= offsetof(type, member) + sizeof((table)->member) && (table)->member)

const sco_api* g_api = nullptr;
sco_plugin* g_self = nullptr;
const sco_plugin_info kInfo = { sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR,
                               "inspector", "1.0.0", "sc-offline" };

const char* Missing(sco_result result) { return result == SCO_UNAVAILABLE ? "unavailable" : "unknown"; }

sco_result Read(const sco_arg*, uint32_t nargs, void*, char* reply, uint32_t reply_size) {
    if (nargs || (!reply && reply_size)) return SCO_BAD_ARG;
    if (!g_api || !g_self) return SCO_UNAVAILABLE;

    char actor_text[32] = "unavailable", entity_text[32] = "unavailable";
    char current_text[48] = "unavailable", maximum_text[48] = "unavailable";
    char zone_text[32] = "unavailable", position_text[160] = "unavailable", rotation_text[216] = "unavailable";
    uint64_t actor = 0, entity = 0;
    const void* table = nullptr;
    const sc_actors_v1* actors = nullptr;
    if (HAS_FN(g_api, sco_api, query_service) &&
        g_api->query_service(SC_ACTORS_NAME, SC_ACTORS_VERSION_1_0, &table) == SCO_OK)
        actors = static_cast<const sc_actors_v1*>(table);
    if (HAS_FN(actors, sc_actors_v1, local_player) && HAS_FN(g_api, sco_api, has) &&
        g_api->has("game.actors.local_player")) {
        const sco_result r = actors->local_player(&actor, &entity);
        std::snprintf(actor_text, sizeof(actor_text), "%s", Missing(r));
        std::snprintf(entity_text, sizeof(entity_text), "%s", Missing(r));
        if (r == SCO_OK) {
            if (actor) std::snprintf(actor_text, sizeof(actor_text), "%llu", static_cast<unsigned long long>(actor));
            if (entity) std::snprintf(entity_text, sizeof(entity_text), "%llu", static_cast<unsigned long long>(entity));
        } else {
            actor = 0;
            entity = 0;
        }
    }
    if (HAS_FN(actors, sc_actors_v1, health) && HAS_FN(g_api, sco_api, has) && g_api->has("game.actors.health")) {
        float current = std::numeric_limits<float>::quiet_NaN(), maximum = current;
        const sco_result r = entity ? actors->health(entity, &current, &maximum) : SCO_NOT_FOUND;
        std::snprintf(current_text, sizeof(current_text), "%s", Missing(r));
        std::snprintf(maximum_text, sizeof(maximum_text), "%s", Missing(r));
        if (r == SCO_OK) {
            if (std::isfinite(current)) std::snprintf(current_text, sizeof(current_text), "%.9g", current);
            // The SDK explicitly defines zero maximum as unknown, not a zero-sized pool.
            if (std::isfinite(maximum) && maximum > 0) std::snprintf(maximum_text, sizeof(maximum_text), "%.9g", maximum);
        }
    }
    table = nullptr;
    const sc_entities_v1* entities = nullptr;
    if (HAS_FN(g_api, sco_api, query_service) &&
        g_api->query_service(SC_ENTITIES_NAME, SC_ENTITIES_VERSION_1_0, &table) == SCO_OK)
        entities = static_cast<const sc_entities_v1*>(table);
    if (HAS_FN(entities, sc_entities_v1, get_transform) && HAS_FN(g_api, sco_api, has) &&
        g_api->has("game.entities.transform")) {
        const double unknown = std::numeric_limits<double>::quiet_NaN();
        double pos[3] = { unknown, unknown, unknown }, rot[4] = { unknown, unknown, unknown, unknown };
        uint64_t zone = 0;
        const sco_result r = entity ? entities->get_transform(entity, pos, rot, &zone) : SCO_NOT_FOUND;
        std::snprintf(zone_text, sizeof(zone_text), "%s", Missing(r));
        std::snprintf(position_text, sizeof(position_text), "%s", Missing(r));
        std::snprintf(rotation_text, sizeof(rotation_text), "%s", Missing(r));
        if (r == SCO_OK && zone) {
            std::snprintf(zone_text, sizeof(zone_text), "%llu", static_cast<unsigned long long>(zone));
            if (std::isfinite(pos[0]) && std::isfinite(pos[1]) && std::isfinite(pos[2]))
                std::snprintf(position_text, sizeof(position_text), "(%.17g,%.17g,%.17g)", pos[0], pos[1], pos[2]);
            if (std::isfinite(rot[0]) && std::isfinite(rot[1]) && std::isfinite(rot[2]) && std::isfinite(rot[3]) &&
                (rot[0] != 0 || rot[1] != 0 || rot[2] != 0 || rot[3] != 0))
                std::snprintf(rotation_text, sizeof(rotation_text), "(%.17g,%.17g,%.17g,%.17g)", rot[0], rot[1], rot[2], rot[3]);
        }
    }
    char text[768];
    std::snprintf(text, sizeof(text), "actor=%s entity=%s health.current=%s health.maximum=%s zone=%s position=%s rotation=%s",
                  actor_text, entity_text, current_text, maximum_text, zone_text, position_text, rotation_text);
    if (HAS_FN(g_api, sco_api, log)) g_api->log(g_self, SCO_LOG_INFO, text);
    if (reply_size) std::snprintf(reply, reply_size, "%s", text);
    return SCO_OK;
}

#ifdef SCO_INSPECTOR_CANDIDATE
constexpr uint32_t kLimit = 256;
struct Snapshot {
    uint64_t generation = 0;
    sc_game_build_info build{};
    sc_gamerules_state rules{};
    sc_inventory_snapshot inventory{};
    uint32_t actions = 0, count = 0;
    sco_result build_result = SCO_UNAVAILABLE, rules_result = SCO_UNAVAILABLE;
    sco_result inventory_result = SCO_UNAVAILABLE, input_result = SCO_UNAVAILABLE, params_result = SCO_UNAVAILABLE;
    float stats[SC_ACTOR_STAT_LAST_1_1 + 1]{};
    sco_result stat_result[SC_ACTOR_STAT_LAST_1_1 + 1]{};
    sc_gamerules_param_info params[kLimit]{};
};
Snapshot g_snapshot;
uint32_t g_tab = 0, g_page = 0;
bool g_panel = false;
constexpr const char* kPanel = "inspector.main";
template<class T> const T* Service(const char* name, uint32_t version) {
    const void* table = nullptr;
    return HAS_FN(g_api, sco_api, query_service) && g_api->query_service(name, version, &table) == SCO_OK
        ? static_cast<const T*>(table) : nullptr;
}
bool Terminated(const char* text, size_t size) { return std::memchr(text, 0, size) != nullptr; }
const char* Status(sco_result r) { return r == SCO_OK ? "available" : Missing(r); }
// Only this admission path supplies parameter records to commands AND widgets.
// Never publish even a valid prefix if a later page is malformed or incomplete.
sco_result CopyParams(Snapshot& next) {
    const auto* rules = Service<sc_gamerules_v1>(SC_GAMERULES_SERVICE_NAME, SC_GAMERULES_VERSION_1_1);
    if (!HAS_FN(rules, sc_gamerules_v1, list_params)) return SCO_UNAVAILABLE;
    uint32_t offset = 0, expected = 0;
    do {
        sc_gamerules_param_info page[SC_GAMERULES_PARAM_MAX_PAGE]{};
        for (auto& p : page) p.size = sizeof(p);
        uint32_t count = UINT32_MAX, total = UINT32_MAX;
        const auto r = rules->list_params(g_self, offset, page, sizeof(page[0]), SC_GAMERULES_PARAM_MAX_PAGE, &count, &total);
        if (r != SCO_OK) return r;
        if (total > kLimit || offset > total || count > SC_GAMERULES_PARAM_MAX_PAGE ||
            count != (total - offset < SC_GAMERULES_PARAM_MAX_PAGE ? total - offset : SC_GAMERULES_PARAM_MAX_PAGE) ||
            (offset && total != expected)) return SCO_FAILED;
        expected = total;
        for (uint32_t i = 0; i < count; ++i) {
            const auto& p = page[i];
            if (p.size < sizeof(p) || (p.type != SC_GAMERULES_PARAM_INT && p.type != SC_GAMERULES_PARAM_FLOAT) ||
                !Terminated(p.name, sizeof(p.name)) || !p.name[0] || !Terminated(p.group, sizeof(p.group))) return SCO_FAILED;
        }
        for (uint32_t i = 0; i < count; ++i) next.params[offset + i] = page[i];
        offset += count;
    } while (offset < expected);
    next.count = expected;
    return SCO_OK;
}
void Refresh() {
    Snapshot next{};
    next.generation = g_snapshot.generation + 1;
    for (auto& r : next.stat_result) r = SCO_UNAVAILABLE;
    const auto* b = Service<sc_game_build_v1>(SC_GAME_BUILD_SERVICE_NAME, SC_GAME_BUILD_SERVICE_VERSION);
    next.build.size = sizeof(next.build);
    if (HAS_FN(b, sc_game_build_v1, read)) {
        next.build_result = b->read(g_self, &next.build);
        if (next.build.size < sizeof(next.build)) next.build_result = SCO_FAILED;
    }
    uint64_t actor = 0, entity = 0;
    const auto* a = Service<sc_actors_v1>(SC_ACTORS_NAME, SC_ACTORS_VERSION_1_0);
    if (HAS_FN(a, sc_actors_v1, local_player) && g_api->has("game.actors.local_player") &&
        a->local_player(&actor, &entity) != SCO_OK) entity = 0;
    uint32_t last = SC_ACTOR_STAT_LAST_1_1;
    auto* s = Service<sc_actor_stats_v1>(SC_ACTOR_STATS_NAME, SC_ACTOR_STATS_VERSION_1_1);
    if (!HAS_FN(s, sc_actor_stats_v1, read)) {
        s = Service<sc_actor_stats_v1>(SC_ACTOR_STATS_NAME, SC_ACTOR_STATS_VERSION_1_0); last = SC_ACTOR_STAT_LAST_1_0;
    }
    if (entity && HAS_FN(s, sc_actor_stats_v1, read) && g_api->has("game.actor_stats.read")) {
        for (uint32_t id = 1; id <= last; ++id) {
            float value = std::numeric_limits<float>::quiet_NaN();
            auto r = s->read(entity, id, &value);
            if (r == SCO_OK && !std::isfinite(value)) r = SCO_FAILED;
            next.stat_result[id] = r;
            if (r == SCO_OK) next.stats[id] = value;
        }
    }
    const auto* rules = Service<sc_gamerules_v1>(SC_GAMERULES_SERVICE_NAME, SC_GAMERULES_VERSION_1_0);
    next.rules.size = sizeof(next.rules);
    if (HAS_FN(rules, sc_gamerules_v1, read) && g_api->has("game.gamerules.read")) {
        next.rules_result = rules->read(g_self, &next.rules);
        if (next.rules.size < sizeof(next.rules) || !next.rules.valid ||
            !std::isfinite(next.rules.travel_target_threshold_angle) ||
            !std::isfinite(next.rules.target_lock_angular_player) || !std::isfinite(next.rules.target_lock_angular_ai) ||
            !std::isfinite(next.rules.target_lock_linear_player) || !std::isfinite(next.rules.target_lock_linear_ai) ||
            !std::isfinite(next.rules.boost_group_distance_threshold)) next.rules_result = SCO_FAILED;
    }
    auto* inventory = Service<sc_inventory_v1>(SC_INVENTORY_SERVICE_NAME, SC_INVENTORY_SERVICE_VERSION_1_1);
    if (!HAS_FN(inventory, sc_inventory_v1, snapshot))
        inventory = Service<sc_inventory_v1>(SC_INVENTORY_SERVICE_NAME, SC_INVENTORY_SERVICE_VERSION_1_0);
    next.inventory.size = sizeof(next.inventory); next.inventory.count = SC_INVENTORY_MAX_PORTS;
    if (HAS_FN(inventory, sc_inventory_v1, snapshot) && g_api->has("game.inventory.read")) {
        next.inventory_result = inventory->snapshot(g_self, &next.inventory);
        if (next.inventory.size < SC_INVENTORY_SNAPSHOT_SIZE_1_0 || next.inventory.count > SC_INVENTORY_MAX_PORTS)
            next.inventory_result = SCO_FAILED;
        if (next.inventory_result == SCO_OK) {
            for (uint32_t i = 0; i < next.inventory.count; ++i)
                if (next.inventory.ports[i].size < sizeof(sc_inventory_port)) next.inventory_result = SCO_FAILED;
        }
    }
    const auto* input = Service<sc_input_v1>(SC_INPUT_SERVICE_NAME, SC_INPUT_SERVICE_VERSION);
    if (HAS_FN(input, sc_input_v1, count)) next.input_result = input->count(g_self, &next.actions);

    next.params_result = CopyParams(next);
    if (next.params_result != SCO_OK) { next.count = 0; std::memset(next.params, 0, sizeof(next.params)); }
    g_snapshot = next; g_page = 0;
}
uint32_t Rows() {
    if (g_tab == 1) return SC_ACTOR_STAT_LAST_1_1;
    if (g_tab == 2 && g_snapshot.inventory_result == SCO_OK) return g_snapshot.inventory.count;
    if (g_tab == 3 && g_snapshot.params_result == SCO_OK) return g_snapshot.count;
    return 1;
}
void CachedText(char* text, size_t size) {
    const auto& s = g_snapshot;
    if (g_tab == 1) {
        const uint32_t id = g_page + 1;
        if (s.stat_result[id] == SCO_OK && s.generation)
            std::snprintf(text, size, "stat=%u value=%.9g (raw; units unverified)", id, s.stats[id]);
        else std::snprintf(text, size, "stat=%u value=%s", id, s.generation ? Status(s.stat_result[id]) : "unavailable");
    } else if (g_tab == 2 && s.inventory_result == SCO_OK && s.inventory.count) {
        const auto& p = s.inventory.ports[g_page];
        std::snprintf(text, size, "port=%u valid=%u entity=%llu type=%llu count=%u (copied; walk may be limited)",
            g_page, p.valid, static_cast<unsigned long long>(p.entity_id), static_cast<unsigned long long>(p.item_class_id), s.inventory.count);
    } else if (g_tab == 3 && s.params_result == SCO_OK && s.count) {
        const auto& p = s.params[g_page];
        std::snprintf(text, size, "param=%s group=%s type=%u source=definition total=%u", p.name, p.group, p.type, s.count);
    } else if (g_tab == 2 || g_tab == 3) {
        const auto r = g_tab == 2 ? s.inventory_result : s.params_result;
        std::snprintf(text, size, "%s=%s count=0", g_tab == 2 ? "inventory" : "params", Status(r));
    } else {
        std::snprintf(text, size, "build=%s rules=%s inventory=%s input=%s params=%s pe=%u pin_matches=%u actions=%u",
            Status(s.build_result), Status(s.rules_result), Status(s.inventory_result), Status(s.input_result),
            Status(s.params_result), s.build_result == SCO_OK ? s.build.pe_timestamp : 0,
            s.build_result == SCO_OK ? s.build.pin_matches : 0, s.input_result == SCO_OK ? s.actions : 0);
    }
}
void RenderPanel();
sco_result Candidate(const sco_arg*, uint32_t nargs, void* ctx, char* reply, uint32_t size) {
    if (nargs || (!reply && size)) return SCO_BAD_ARG;
    if (!g_api || !g_self) return SCO_UNAVAILABLE;
    const auto action = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx));
    if (action == 10) Refresh();
    else if (action >= 20 && action <= 23) { g_tab = action - 20; g_page = 0; }
    else if (action == 50 && g_page) --g_page;
    else if (action == 51 && g_page + 1 < Rows()) ++g_page;
    char row[256], text[512]; CachedText(row, sizeof(row));
    std::snprintf(text, sizeof(text), "generation=%llu sample=%s page=%u %s", static_cast<unsigned long long>(g_snapshot.generation),
        g_snapshot.generation ? "manual" : "never", g_page, row);
    g_api->log(g_self, SCO_LOG_INFO, text);
    if (size) std::snprintf(reply, size, "%s", text);
    RenderPanel();
    return SCO_OK;
}
void PanelEvent(const char* panel, const sco_widget_event* event, void* ctx) {
    if (!g_api || !g_panel || ctx != g_self || !panel || std::strcmp(panel, kPanel) ||
        !event || event->size < sizeof(*event) || event->kind != SCO_WIDGET_BUTTON) return;
    const auto id = event->node_id;
    if (id != 10 && !(id >= 20 && id <= 23) && id != 50 && id != 51) return;
    (void)Candidate(nullptr, 0, reinterpret_cast<void*>(static_cast<uintptr_t>(id)), nullptr, 0);
}
const sco_widgets_v1* Widgets() {
    const auto* w = Service<sco_widgets_v1>(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0);
    return HAS_FN(w, sco_widgets_v1, set_panel) && HAS_FN(w, sco_widgets_v1, remove_panel) ? w : nullptr;
}
void RenderPanel() {
    const auto* widgets = Widgets();
    if (!widgets) return; // The host also withdraws owner-scoped callbacks on unload.
    char text[256]; CachedText(text, sizeof(text));
    sco_widget_node nodes[8]{};
    constexpr uint32_t ids[] = {10, 20, 21, 22, 23, 50, 51};
    const char* labels[] = {"Sample now", "Diagnostics", "Stats", "Inventory", "Parameter definitions", "Previous (cached)", "Next (cached)"};
    for (uint32_t i = 0; i < 7; ++i) {
        nodes[i].size = sizeof(nodes[i]); nodes[i].id = ids[i]; nodes[i].kind = SCO_WIDGET_BUTTON; nodes[i].label = labels[i];
    }
    nodes[7].size = sizeof(nodes[7]); nodes[7].id = 30; nodes[7].kind = SCO_WIDGET_LABEL; nodes[7].label = text;
    if (widgets->set_panel(g_self, kPanel, "Inspector (manual copies)", nodes, 8, sizeof(nodes[0]), PanelEvent, g_self) == SCO_OK)
        g_panel = true;
}
sco_result RegisterCandidate() {
    const char* names[] = {"inspector.refresh", "inspector.status", "inspector.stats", "inspector.inventory", "inspector.params", "inspector.previous", "inspector.next"};
    constexpr uint32_t actions[] = {10, 20, 21, 22, 23, 50, 51};
    for (uint32_t i = 0; i < 7; ++i) {
        const sco_command cmd = {sizeof(sco_command), 0, names[i], names[i], "Manual snapshot or cached navigation", nullptr,
            nullptr, 0, sizeof(sco_arg_def), Candidate, reinterpret_cast<void*>(static_cast<uintptr_t>(actions[i]))};
        const auto r = g_api->register_command(g_self, &cmd);
        if (r != SCO_OK) return r;
    }
    RenderPanel();
    return SCO_OK;
}

#endif

}  // namespace

const sco_plugin_info* Query() { return &kInfo; }

sco_result Load(const sco_api* api, sco_plugin* self) {
    if (!api || !self || g_api) return SCO_BAD_ARG;
    if (!HAS_FN(api, sco_api, has) || !HAS_FN(api, sco_api, log) ||
        !HAS_FN(api, sco_api, register_command) || !HAS_FN(api, sco_api, query_service) ||
        api->major != SCO_API_MAJOR || api->minor < SCO_API_MINOR)
        return SCO_UNAVAILABLE;
    const sco_command cmd = { sizeof(sco_command), 0, "inspector.read", "Read player",
                              "Read the current player without changing the game", nullptr,
                              nullptr, 0, sizeof(sco_arg_def), Read, nullptr };
    g_api = api;
    g_self = self;
    sco_result r = api->register_command(self, &cmd);
#ifdef SCO_INSPECTOR_CANDIDATE
    if (r == SCO_OK) r = RegisterCandidate();
#endif
    if (r != SCO_OK) Unload();
    return r;
}

void Unload() {
#ifdef SCO_INSPECTOR_CANDIDATE
    if (g_panel) {
        if (const auto* widgets = Widgets()) (void)widgets->remove_panel(g_self, kPanel);
    }
    g_panel = false; g_snapshot = {}; g_tab = g_page = 0;
#endif
    // The host removes commands at unload. No borrowed service, id or callback context survives.
    g_api = nullptr;
    g_self = nullptr;
}

#undef HAS_FN
}  // namespace sco::features::inspector
