// SPDX-License-Identifier: GPL-3.0-only
#include "inspector.h"
#include "sc_actors.h"
#include "sc_actor_stats.h"
#include "sc_entities.h"
#include "sc_game_build.h"
#include "sc_gamerules.h"
#include "sc_inventory.h"
#include "sc_input.h"
#include "sco_widgets.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>

namespace sco::features::inspector {
namespace {

// Test the complete member, not just its first byte; short-circuit before reading it.
#define HAS_FN(table, type, member) \
    ((table) && (table)->size >= offsetof(type, member) + sizeof((table)->member) && (table)->member)

const sco_api* g_api = nullptr;
sco_plugin* g_self = nullptr;
const sco_plugin_info kInfo = { sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR,
                               "inspector", "1.0.0", "sc-offline" };

struct Snapshot {
    uint64_t generation = 0;
    bool sampled = false;
    uint32_t build_status = SCO_UNAVAILABLE;
    sc_game_build_info build{};
    uint32_t player_status = SCO_UNAVAILABLE;
    uint64_t actor = 0;
    uint64_t entity = 0;
    uint32_t stats_status = SCO_UNAVAILABLE;
    float stats[SC_ACTOR_STAT_LAST_1_0 + 1]{};
    bool stats_valid[SC_ACTOR_STAT_LAST_1_0 + 1]{};
    uint32_t rules_status = SCO_UNAVAILABLE;
    sc_gamerules_state rules{};
    uint32_t inventory_status = SCO_UNAVAILABLE;
    sc_inventory_snapshot inventory{};
    uint32_t action_count = 0;
    bool action_count_valid = false;
    sc_input_action_info action{};
    bool action_valid = false;
    sc_gamerules_param_info param{};
    uint32_t param_page = 0;
    uint32_t param_total = 0;
    bool param_valid = false;
    sc_gamerules_param_info params[256]{};
    uint32_t params_count = 0;
    uint32_t params_total = 0;
};
Snapshot g_snapshot;
uint64_t g_provider_reads = 0;
constexpr char kPanelId[] = "inspector.main";
enum : uint32_t { kRefresh = 10, kStatus = 11, kDiagnostics = 20, kInventory = 21,
                  kActions = 22, kParameters = 23, kTable = 30, kActionName = 40,
                  kLookup = 41, kBrowseParams = 42, kRefreshCount = 43,
                  kPrevious = 50, kNext = 51 };
const sco_widgets_v1* g_widgets = nullptr;
bool g_panel = false;
uint32_t g_tab = kDiagnostics, g_page = 0;
char g_query[SC_INPUT_ACTION_NAME_BYTES]{};
char g_widget_status[128] = "Not sampled";

const char* Missing(sco_result result) { return result == SCO_UNAVAILABLE ? "unavailable" : "unknown"; }
const char* Status(uint32_t result) {
    return result == SCO_OK ? "available" : result == SCO_FAILED ? "failed" : Missing(static_cast<sco_result>(result));
}

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

template <typename T>
const T* Service(const char* name, uint32_t version) {
    const void* table = nullptr;
    if (!HAS_FN(g_api, sco_api, query_service)) return nullptr;
    ++g_provider_reads;
    return g_api->query_service(name, version, &table) == SCO_OK ? static_cast<const T*>(table) : nullptr;
}

void Refresh() {
    Snapshot next{};
    next.sampled = true;
    next.generation = g_snapshot.generation + 1;

    if (const auto* build = Service<sc_game_build_v1>(SC_GAME_BUILD_SERVICE_NAME, SC_GAME_BUILD_SERVICE_VERSION);
        HAS_FN(build, sc_game_build_v1, read)) {
        sc_game_build_info value{};
        value.size = sizeof(value);
        ++g_provider_reads;
        const auto result = build->read(g_self, &value);
        if (result == SCO_OK && value.size >= sizeof(value)) {
            next.build = value;
            next.build_status = SCO_OK;
        } else next.build_status = result == SCO_OK ? SCO_UNAVAILABLE : result;
    }

    const auto* actors = Service<sc_actors_v1>(SC_ACTORS_NAME, SC_ACTORS_VERSION_1_0);
    if (HAS_FN(actors, sc_actors_v1, local_player) && HAS_FN(g_api, sco_api, has) &&
        g_api->has("game.actors.local_player")) {
        ++g_provider_reads;
        next.player_status = actors->local_player(&next.actor, &next.entity);
        if (next.player_status == SCO_OK && (!next.actor || !next.entity)) next.player_status = SCO_NOT_FOUND;
        if (next.player_status != SCO_OK) next.actor = next.entity = 0;
    }
    if (next.player_status == SCO_OK) {
        const auto* stats = Service<sc_actor_stats_v1>(SC_ACTOR_STATS_NAME, SC_ACTOR_STATS_VERSION_1_0);
        if (HAS_FN(stats, sc_actor_stats_v1, read) && HAS_FN(g_api, sco_api, has) &&
            g_api->has("game.actor_stats.read")) {
            next.stats_status = SCO_OK;
            for (uint32_t id = 1; id <= SC_ACTOR_STAT_LAST_1_0; ++id) {
                float value = std::numeric_limits<float>::quiet_NaN();
                ++g_provider_reads;
                const auto result = stats->read(next.entity, id, &value);
                if (result == SCO_OK && std::isfinite(value)) {
                    next.stats[id] = value;
                    next.stats_valid[id] = true;
                } else if (result == SCO_OK) next.stats_status = SCO_FAILED;
                else if (result != SCO_NOT_FOUND && next.stats_status == SCO_OK) next.stats_status = result;
            }
        }
    }

    const auto* rules = Service<sc_gamerules_v1>(SC_GAMERULES_SERVICE_NAME, SC_GAMERULES_VERSION_1_0);
    if (HAS_FN(rules, sc_gamerules_v1, read) && HAS_FN(g_api, sco_api, has) && g_api->has("game.gamerules.read")) {
        sc_gamerules_state value{};
        value.size = sizeof(value);
        ++g_provider_reads;
        const auto result = rules->read(g_self, &value);
        if (result == SCO_OK && value.size >= sizeof(value)) {
            next.rules = value;
            next.rules_status = SCO_OK;
            const float* fields[] = {&value.travel_target_threshold_angle, &value.target_lock_angular_player,
                &value.target_lock_angular_ai, &value.target_lock_linear_player, &value.target_lock_linear_ai,
                &value.boost_group_distance_threshold};
            for (const float* field : fields) if (!std::isfinite(*field)) next.rules_status = SCO_FAILED;
        } else next.rules_status = result == SCO_OK ? SCO_UNAVAILABLE : result;
    }

    const auto* inventory = Service<sc_inventory_v1>(SC_INVENTORY_SERVICE_NAME, SC_INVENTORY_SERVICE_VERSION);
    if (HAS_FN(inventory, sc_inventory_v1, snapshot) && HAS_FN(g_api, sco_api, has) &&
        g_api->has("game.inventory.read")) {
        next.inventory.size = sizeof(next.inventory);
        next.inventory.count = SC_INVENTORY_MAX_PORTS;
        ++g_provider_reads;
        const auto result = inventory->snapshot(g_self, &next.inventory);
        if (result == SCO_OK && next.inventory.size >= SC_INVENTORY_SNAPSHOT_SIZE_1_0 &&
            next.inventory.count <= SC_INVENTORY_MAX_PORTS) next.inventory_status = SCO_OK;
        else next.inventory_status = result == SCO_OK ? SCO_FAILED : result;
    }
    g_snapshot = next;
    g_page = 0;
}

const sco_widgets_v1* Widgets() {
    if (!HAS_FN(g_api, sco_api, query_service)) return nullptr;
    const void* table = nullptr;
    if (g_api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_1, &table) != SCO_OK || !table)
        (void)g_api->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, &table);
    auto* widgets = static_cast<const sco_widgets_v1*>(table);
    if (!HAS_FN(widgets, sco_widgets_v1, set_panel) || !HAS_FN(widgets, sco_widgets_v1, remove_panel) ||
        widgets->size < offsetof(sco_widgets_v1, set_panel) + sizeof(widgets->set_panel)) return nullptr;
    return widgets;
}

sco_result AcquireParams(uint32_t offset, sc_gamerules_param_info* out, uint32_t capacity,
                         uint32_t* copied, uint32_t* total) {
    auto* rules = Service<sc_gamerules_v1>(SC_GAMERULES_SERVICE_NAME, SC_GAMERULES_VERSION_1_1);
    if (!HAS_FN(rules, sc_gamerules_v1, list_params))
        rules = Service<sc_gamerules_v1>(SC_GAMERULES_SERVICE_NAME, SC_GAMERULES_VERSION_1_0);
    if (!HAS_FN(rules, sc_gamerules_v1, list_params)) return SCO_UNAVAILABLE;
    return rules->list_params(g_self, offset, out, sizeof(*out), capacity, copied, total);
}

bool RenderPanel();
bool ValidName(const char* name, size_t capacity);
void PanelEvent(const char* panel, const sco_widget_event* event, void* ctx) {
    if (!ctx || ctx != g_self || !g_api || !panel || std::strcmp(panel, kPanelId) || !event ||
        event->size < sizeof(*event) || !g_panel) return;
    const uint32_t id = event->node_id;
    if (event->kind == SCO_WIDGET_BUTTON && id == kRefresh) {
        Refresh();
        std::snprintf(g_widget_status, sizeof(g_widget_status), "Snapshot %llu; stats=%s; manual",
                      static_cast<unsigned long long>(g_snapshot.generation), Status(g_snapshot.stats_status));
    } else if (event->kind == SCO_WIDGET_BUTTON && id == kLookup) {
        sc_input_action_info value{}; value.size = sizeof(value);
        const auto* input = Service<sc_input_v1>(SC_INPUT_SERVICE_NAME, SC_INPUT_SERVICE_VERSION);
        g_snapshot.action_valid = false;
        const auto result = HAS_FN(input, sc_input_v1, by_name) && g_query[0]
            ? input->by_name(g_self, g_query, &value) : SCO_NOT_FOUND;
        if (result == SCO_OK && value.size >= sizeof(value) && ValidName(value.name, sizeof(value.name))) {
            g_snapshot.action = value; g_snapshot.action_valid = true;
            std::snprintf(g_widget_status, sizeof(g_widget_status), "Action %s id=%u", value.name, value.id);
        } else std::snprintf(g_widget_status, sizeof(g_widget_status), "Action lookup unavailable/not found");
    } else if (event->kind == SCO_WIDGET_BUTTON && id == kBrowseParams) {
        g_snapshot.params_count = g_snapshot.params_total = 0;
        uint32_t offset = 0, all = 0;
        sco_result result = SCO_OK;
        while (offset < 256) {
            uint32_t copied = 0, total = 0;
            g_snapshot.params[offset].size = sizeof(sc_gamerules_param_info);
            result = AcquireParams(offset, g_snapshot.params + offset, 1, &copied, &total);
            if (result != SCO_OK || total > 256 || copied != 1) break;
            all = total; ++offset;
            if (offset == total) break;
        }
        if (result == SCO_OK && all <= 256 && offset == all) {
            g_snapshot.params_count = offset; g_snapshot.params_total = all;
            std::snprintf(g_widget_status, sizeof(g_widget_status), "Parameters %u/%u copied", offset, all);
            g_tab = kParameters; g_page = 0;
        } else std::snprintf(g_widget_status, sizeof(g_widget_status), "Parameter browse unavailable");
    } else if (event->kind == SCO_WIDGET_BUTTON && id == kRefreshCount) {
        g_snapshot.action_count_valid = false;
        const auto* input = Service<sc_input_v1>(SC_INPUT_SERVICE_NAME, SC_INPUT_SERVICE_VERSION);
        uint32_t count = 0;
        if (HAS_FN(input, sc_input_v1, count) && input->count(g_self, &count) == SCO_OK) {
            g_snapshot.action_count = count; g_snapshot.action_count_valid = true;
            std::snprintf(g_widget_status, sizeof(g_widget_status), "Named action count: %u", count);
        } else std::snprintf(g_widget_status, sizeof(g_widget_status), "Action count unavailable");
    } else if (event->kind == SCO_WIDGET_BUTTON && (id == kPrevious || id == kNext)) {
        const uint32_t total = g_tab == kInventory ? g_snapshot.inventory.count :
            g_tab == kParameters ? g_snapshot.params_count : g_snapshot.action_count_valid ? 1u : 0u;
        if (id == kPrevious && g_page) --g_page;
        if (id == kNext && g_page + 1 < total) ++g_page;
    } else if (event->kind == SCO_WIDGET_BUTTON && (id == kDiagnostics || id == kInventory || id == kActions || id == kParameters)) {
        g_tab = id; g_page = 0;
    } else if (event->kind == SCO_WIDGET_TEXT_INPUT && id == kActionName && event->text) {
        const size_t len = std::strlen(event->text);
        if (len < sizeof(g_query)) { std::memcpy(g_query, event->text, len + 1); g_snapshot.action_valid = false; }
    } else return;
    RenderPanel();
}

bool RenderPanel() {
    if (!g_widgets || !g_panel) return false;
    char row[4][256]{};
    const char* cells[4] = {"", "", "", ""};
    uint32_t cell_count = 1;
    if (g_tab == kDiagnostics) {
        std::snprintf(row[0], sizeof(row[0]), "Build: %s; player: %s; stats: %s",
            Status(g_snapshot.build_status), Status(g_snapshot.player_status), Status(g_snapshot.stats_status));
        std::snprintf(row[1], sizeof(row[1]), "GameRules: %s; inventory: %s; history: %s",
            Status(g_snapshot.rules_status), Status(g_snapshot.inventory_status), g_snapshot.sampled ? "manual" : "Not sampled");
        cells[0] = row[0]; cells[1] = row[1]; cell_count = 2;
    } else if (g_tab == kInventory) {
        if (!g_snapshot.sampled || g_snapshot.inventory_status != SCO_OK) cells[0] = "Inventory unavailable";
        else if (!g_snapshot.inventory.count) cells[0] = "Inventory empty";
        else if (g_page < g_snapshot.inventory.count) {
            const auto& p = g_snapshot.inventory.ports[g_page];
            std::snprintf(row[0], sizeof(row[0]), "Page %u/%u entity=%llu", g_page + 1, g_snapshot.inventory.count,
                static_cast<unsigned long long>(p.entity_id)); cells[0] = row[0];
        }
    } else if (g_tab == kParameters) {
        if (g_page < g_snapshot.params_count) {
            const auto& p = g_snapshot.params[g_page];
            std::snprintf(row[0], sizeof(row[0]), "Definition %u/%u: %s [%s]", g_page + 1,
                g_snapshot.params_total, p.name, p.group); cells[0] = row[0];
        } else cells[0] = "Browse parameter definitions";
    } else {
        if (g_snapshot.action_valid) {
            std::snprintf(row[0], sizeof(row[0]), "Action %s id=%u", g_snapshot.action.name, g_snapshot.action.id); cells[0] = row[0];
        } else if (g_snapshot.action_count_valid) {
            std::snprintf(row[0], sizeof(row[0]), "Named action count: %u", g_snapshot.action_count); cells[0] = row[0];
        } else cells[0] = "Lookup action by exact name";
    }
    sco_widget_node nodes[16]{};
    auto node = [&](size_t i, uint32_t kind, uint32_t id, uint32_t parent, const char* label) {
        nodes[i].size = sizeof(sco_widget_node); nodes[i].kind = kind; nodes[i].id = id;
        nodes[i].parent = parent; nodes[i].label = label;
    };
    node(0,SCO_WIDGET_COLUMN,1,0,"Inspector"); node(1,SCO_WIDGET_BUTTON,kRefresh,1,"Refresh");
    node(2,SCO_WIDGET_LABEL,kStatus,1,g_widget_status); node(3,SCO_WIDGET_BUTTON,kDiagnostics,1,"Diagnostics");
    node(4,SCO_WIDGET_BUTTON,kInventory,1,"Inventory"); node(5,SCO_WIDGET_BUTTON,kActions,1,"Actions");
    node(6,SCO_WIDGET_BUTTON,kParameters,1,"Parameters"); node(7,SCO_WIDGET_TABLE,kTable,1,"Results");
    nodes[7].columns = 1; nodes[7].items = cells; nodes[7].item_count = cell_count;
    node(8,SCO_WIDGET_TEXT_INPUT,kActionName,1,"Action name"); nodes[8].text = g_query;
    node(9,SCO_WIDGET_BUTTON,kLookup,1,"Lookup"); node(10,SCO_WIDGET_BUTTON,kBrowseParams,1,"Browse parameters");
    node(11,SCO_WIDGET_BUTTON,kRefreshCount,1,"Refresh action count");
    node(12,SCO_WIDGET_BUTTON,kPrevious,1,"Previous"); node(13,SCO_WIDGET_BUTTON,kNext,1,"Next");
    return g_widgets->set_panel(g_self,kPanelId,"Inspector",nodes,14,sizeof(sco_widget_node),PanelEvent,g_self) == SCO_OK;
}

sco_result RefreshCommand(const sco_arg*, uint32_t nargs, void*, char* reply, uint32_t reply_size) {
    if (nargs || (!reply && reply_size)) return SCO_BAD_ARG;
    if (!g_api || !g_self) return SCO_UNAVAILABLE;
    if (!reply_size) return SCO_TOO_MANY;
    Refresh();
    std::snprintf(g_widget_status, sizeof(g_widget_status), "Snapshot %llu; stats=%s; manual",
                  static_cast<unsigned long long>(g_snapshot.generation), Status(g_snapshot.stats_status));
    RenderPanel();
    char text[256];
    std::snprintf(text, sizeof(text), "generation=%llu build=%s player=%s stats=%s gamerules=%s inventory=%s",
        static_cast<unsigned long long>(g_snapshot.generation), Status(g_snapshot.build_status),
        Status(g_snapshot.player_status), Status(g_snapshot.stats_status),
        Status(g_snapshot.rules_status), Status(g_snapshot.inventory_status));
    const size_t required = std::strlen(text) + 1;
    if (reply_size < required) return SCO_TOO_MANY;
    std::memcpy(reply, text, required);
    return SCO_OK;
}

bool Reply(char* reply, uint32_t reply_size, const char* text) {
    const size_t size = std::strlen(text) + 1;
    if (reply_size < size) return false;
    std::memcpy(reply, text, size);
    return true;
}

bool ValidReply(char* reply, uint32_t reply_size) {
    return reply || !reply_size;
}

sco_result StatusCommand(const sco_arg*, uint32_t nargs, void*, char* reply, uint32_t reply_size) {
    if (nargs || !ValidReply(reply, reply_size)) return SCO_BAD_ARG;
    if (!reply_size) return SCO_TOO_MANY;
    char text[192];
    std::snprintf(text, sizeof(text), "generation=%llu sample=%s history=manual build=%s player=%s stats=%s gamerules=%s inventory=%s",
        static_cast<unsigned long long>(g_snapshot.generation), g_snapshot.sampled ? "captured" : "never",
        Status(g_snapshot.build_status), Status(g_snapshot.player_status),
        Status(g_snapshot.stats_status), Status(g_snapshot.rules_status), Status(g_snapshot.inventory_status));
    return Reply(reply, reply_size, text) ? SCO_OK : SCO_TOO_MANY;
}

bool Page(const sco_arg* args, uint32_t nargs, char* reply, uint32_t reply_size, uint32_t max_page,
          uint32_t* page) {
    if (nargs != 1 || !args || args[0].type != SCO_ARG_INT || args[0].v.i < 0 ||
        static_cast<uint64_t>(args[0].v.i) > max_page || !ValidReply(reply, reply_size)) return false;
    *page = static_cast<uint32_t>(args[0].v.i);
    return true;
}

sco_result StatsCommand(const sco_arg* args, uint32_t nargs, void*, char* reply, uint32_t reply_size) {
    uint32_t page = 0;
    if (!Page(args, nargs, reply, reply_size, SC_ACTOR_STAT_LAST_1_0 - 1, &page)) return SCO_BAD_ARG;
    if (!reply_size) return SCO_TOO_MANY;
    if (!g_snapshot.sampled || g_snapshot.stats_status != SCO_OK) return SCO_UNAVAILABLE;
    static const char* const names[] = {"hunger", "thirst", "body_temperature", "suit_temperature",
        "stun", "distortion", "oxygen", "body_radiation", "suit_radiation"};
    static_assert(sizeof(names) / sizeof(names[0]) == SC_ACTOR_STAT_LAST_1_0,
                  "Inspector stat names must cover the public SDK 1.0 stat IDs");
    const uint32_t id = page + 1;
    if (!g_snapshot.stats_valid[id]) return SCO_NOT_FOUND;
    char text[128];
    std::snprintf(text, sizeof(text), "generation=%llu page=%u stat=%s value=%.9g history=captured",
        static_cast<unsigned long long>(g_snapshot.generation), page, names[page], g_snapshot.stats[id]);
    return Reply(reply, reply_size, text) ? SCO_OK : SCO_TOO_MANY;
}

sco_result GameRulesCommand(const sco_arg* args, uint32_t nargs, void*, char* reply, uint32_t reply_size) {
    uint32_t page = 0;
    if (!Page(args, nargs, reply, reply_size, 9, &page)) return SCO_BAD_ARG;
    if (!reply_size) return SCO_TOO_MANY;
    if (!g_snapshot.sampled || g_snapshot.rules_status != SCO_OK) return SCO_UNAVAILABLE;
    const auto& rules = g_snapshot.rules;
    static const char* const names[] = {"valid", "enforce_group_qt", "quantum_travel_allowed",
        "quantum_boost_allowed", "travel_target_threshold_angle", "target_lock_angular_player",
        "target_lock_angular_ai", "target_lock_linear_player", "target_lock_linear_ai",
        "boost_group_distance_threshold"};
    char value[32];
    int value_size = 0;
    switch (page) {
    case 0: value_size = std::snprintf(value, sizeof(value), "%u", rules.valid); break;
    case 1: value_size = std::snprintf(value, sizeof(value), "%u", rules.enforce_group_qt); break;
    case 2: value_size = std::snprintf(value, sizeof(value), "%u", rules.quantum_travel_allowed); break;
    case 3: value_size = std::snprintf(value, sizeof(value), "%u", rules.quantum_boost_allowed); break;
    case 4: value_size = std::snprintf(value, sizeof(value), "%.9g", rules.travel_target_threshold_angle); break;
    case 5: value_size = std::snprintf(value, sizeof(value), "%.9g", rules.target_lock_angular_player); break;
    case 6: value_size = std::snprintf(value, sizeof(value), "%.9g", rules.target_lock_angular_ai); break;
    case 7: value_size = std::snprintf(value, sizeof(value), "%.9g", rules.target_lock_linear_player); break;
    case 8: value_size = std::snprintf(value, sizeof(value), "%.9g", rules.target_lock_linear_ai); break;
    default: value_size = std::snprintf(value, sizeof(value), "%.9g", rules.boost_group_distance_threshold); break;
    }
    if (value_size < 0) return SCO_FAILED;
    if (static_cast<size_t>(value_size) >= sizeof(value)) return SCO_TOO_MANY;
    char text[256];
    const int text_size = std::snprintf(text, sizeof(text), "generation=%llu page=%u field=%s value=%s provider_generation=%llu history=captured",
        static_cast<unsigned long long>(g_snapshot.generation), page, names[page], value,
        static_cast<unsigned long long>(rules.generation));
    if (text_size < 0) return SCO_FAILED;
    if (static_cast<size_t>(text_size) >= sizeof(text)) return SCO_TOO_MANY;
    return Reply(reply, reply_size, text) ? SCO_OK : SCO_TOO_MANY;
}

sco_result InventoryCommand(const sco_arg* args, uint32_t nargs, void*, char* reply, uint32_t reply_size) {
    uint32_t page = 0;
    if (!Page(args, nargs, reply, reply_size, (std::numeric_limits<uint32_t>::max)(), &page)) return SCO_BAD_ARG;
    if (!reply_size) return SCO_TOO_MANY;
    if (!g_snapshot.sampled || g_snapshot.inventory_status != SCO_OK) return SCO_UNAVAILABLE;

    const auto& inventory = g_snapshot.inventory;
    const uint32_t total = inventory.total_seen;
    if (inventory.count == 0) {
        if (page != 0) return SCO_BAD_ARG;
    } else if (page >= inventory.count) {
        return SCO_BAD_ARG;
    }

    char record[96] = "record=none";
    if (inventory.count) {
        const auto& port = inventory.ports[page];
        const char* category = port.valid
            ? sc_inventory_item_type_name(static_cast<uint32_t>(port.item_class_id)) : "unknown";
        if (port.valid && port.item_class_id >= SC_ITEM_TYPE_COUNT_197) category = "unknown";
        if (port.valid && port.entity_id) {
            std::snprintf(record, sizeof(record), "category=%s entity=%llu", category,
                          static_cast<unsigned long long>(port.entity_id));
        } else if (port.valid) {
            std::snprintf(record, sizeof(record), "category=%s entity=unknown", category);
        } else {
            std::snprintf(record, sizeof(record), "category=unknown entity=unknown");
        }
    }
    char text[256];
    std::snprintf(text, sizeof(text),
        "generation=%llu total=%u page=%u next=%u truncated=%u limited=%u %s history=captured",
        static_cast<unsigned long long>(g_snapshot.generation), total, page,
        inventory.count && page + 1 < inventory.count ? 1u : 0u,
        (inventory.flags & SC_INVENTORY_FLAG_TRUNCATED) != 0 ? 1u : 0u,
        (inventory.flags & SC_INVENTORY_FLAG_LIMITED) != 0 ? 1u : 0u, record);
    return Reply(reply, reply_size, text) ? SCO_OK : SCO_TOO_MANY;
}

const sc_input_v1* InputService() {
    const auto* input = Service<sc_input_v1>(SC_INPUT_SERVICE_NAME, SC_INPUT_SERVICE_VERSION);
    return HAS_FN(input, sc_input_v1, count) ? input : nullptr;
}

bool ValidName(const char* name, size_t capacity) {
    return name && std::memchr(name, '\0', capacity) != nullptr;
}

sco_result ActionCountCommand(const sco_arg*, uint32_t nargs, void*, char* reply, uint32_t reply_size) {
    if (nargs || !ValidReply(reply, reply_size)) return SCO_BAD_ARG;
    if (!reply_size) return SCO_TOO_MANY;
    g_snapshot.action_count_valid = false;
    g_snapshot.action_valid = false;
    const auto* input = InputService();
    if (!input) return SCO_UNAVAILABLE;
    uint32_t count = 0;
    const auto result = input->count(g_self, &count);
    if (result != SCO_OK) return result;
    char text[96];
    std::snprintf(text, sizeof(text), "count=%u history=metadata", count);
    if (!Reply(reply, reply_size, text)) return SCO_TOO_MANY;
    g_snapshot.action_count = count;
    g_snapshot.action_count_valid = true;
    return SCO_OK;
}

bool ActionArgs(const sco_arg* args, uint32_t nargs, char* reply, uint32_t reply_size,
                const char** name) {
    if (nargs != 1 || !args || args[0].type != SCO_ARG_STRING || !args[0].v.s ||
        !args[0].v.s[0] || !ValidReply(reply, reply_size)) return false;
    const size_t length = std::strlen(args[0].v.s);
    if (length >= SC_INPUT_ACTION_NAME_BYTES) return false;
    *name = args[0].v.s;
    return true;
}

sco_result ActionResultCommand(const sc_input_v1* input, uint32_t id, const char* requested,
                               bool by_id, char* reply, uint32_t reply_size) {
    g_snapshot.action_valid = false;
    sc_input_action_info value{};
    value.size = sizeof(value);
    const auto result = by_id ? input->by_id(g_self, id, &value)
                              : input->by_name(g_self, requested, &value);
    if (result != SCO_OK) return result;
    if (value.size < sizeof(value) || !ValidName(value.name, sizeof(value.name))) return SCO_FAILED;
    char text[192];
    std::snprintf(text, sizeof(text), "id=%u name=%s flags=%u build=%s history=metadata",
        value.id, value.name, value.flags,
        (value.flags & SC_INPUT_ACTION_BUILD_UNRESOLVED) ? "unresolved" : "identified");
    if (!Reply(reply, reply_size, text)) return SCO_TOO_MANY;
    g_snapshot.action = value;
    g_snapshot.action_valid = true;
    if (requested) std::snprintf(g_widget_status, sizeof(g_widget_status), "Action %s id=%u", value.name, value.id);
    return SCO_OK;
}

sco_result ActionCommand(const sco_arg* args, uint32_t nargs, void*, char* reply, uint32_t reply_size) {
    const char* name = nullptr;
    if (!ActionArgs(args, nargs, reply, reply_size, &name)) return SCO_BAD_ARG;
    if (!reply_size) return SCO_TOO_MANY;
    const auto* input = InputService();
    if (!HAS_FN(input, sc_input_v1, by_name)) {
        g_snapshot.action_valid = false;
        return SCO_UNAVAILABLE;
    }
    return ActionResultCommand(input, 0, name, false, reply, reply_size);
}

sco_result ActionIdCommand(const sco_arg* args, uint32_t nargs, void*, char* reply, uint32_t reply_size) {
    uint32_t id = 0;
    if (!Page(args, nargs, reply, reply_size, (std::numeric_limits<uint32_t>::max)(), &id)) return SCO_BAD_ARG;
    if (!reply_size) return SCO_TOO_MANY;
    g_snapshot.action_valid = false;
    const auto* input = InputService();
    if (!HAS_FN(input, sc_input_v1, by_id)) return SCO_UNAVAILABLE;
    return ActionResultCommand(input, id, nullptr, true, reply, reply_size);
}

sco_result ParamsCommand(const sco_arg* args, uint32_t nargs, void*, char* reply, uint32_t reply_size) {
    uint32_t page = 0;
    if (!Page(args, nargs, reply, reply_size, (std::numeric_limits<uint32_t>::max)(), &page)) return SCO_BAD_ARG;
    if (!reply_size) return SCO_TOO_MANY;
    g_snapshot.param_valid = false;
    const auto* rules = Service<sc_gamerules_v1>(SC_GAMERULES_SERVICE_NAME, SC_GAMERULES_VERSION_1_1);
    if (!HAS_FN(rules, sc_gamerules_v1, list_params))
        rules = Service<sc_gamerules_v1>(SC_GAMERULES_SERVICE_NAME, SC_GAMERULES_VERSION_1_0);
    if (!HAS_FN(rules, sc_gamerules_v1, list_params)) return SCO_UNAVAILABLE;
    sc_gamerules_param_info value{};
    value.size = sizeof(value);
    uint32_t copied = 0, total = 0;
    const auto result = AcquireParams(page, &value, 1, &copied, &total);
    if (result != SCO_OK) return result;
    if (copied != 1 || value.size < sizeof(value) || !ValidName(value.name, sizeof(value.name)) ||
        !ValidName(value.group, sizeof(value.group))) return SCO_FAILED;
    if (value.type != SC_GAMERULES_PARAM_INT && value.type != SC_GAMERULES_PARAM_FLOAT) return SCO_FAILED;
    char text[256];
    const int text_size = std::snprintf(text, sizeof(text), "page=%u total=%u name=%s group=%s type=%s source=definition history=metadata",
        page, total, value.name, value.group, value.type == SC_GAMERULES_PARAM_INT ? "int" : "float");
    if (text_size < 0) return SCO_FAILED;
    if (static_cast<size_t>(text_size) >= sizeof(text)) return SCO_TOO_MANY;
    if (!Reply(reply, reply_size, text)) return SCO_TOO_MANY;
    g_snapshot.param = value;
    g_snapshot.param_page = page;
    g_snapshot.param_total = total;
    g_snapshot.param_valid = true;
    return SCO_OK;
}

const sco_arg_def kPageArg = {"page", SCO_ARG_INT, 0, "Zero-based record page"};
const sco_arg_def kActionArg = {"name", SCO_ARG_STRING, 0, "Exact action name"};

}  // namespace

const sco_plugin_info* Query() { return &kInfo; }

sco_result Load(const sco_api* api, sco_plugin* self) {
    if (!api || !self || g_api) return SCO_BAD_ARG;
    if (!HAS_FN(api, sco_api, has) || !HAS_FN(api, sco_api, log) ||
        !HAS_FN(api, sco_api, register_command) || !HAS_FN(api, sco_api, query_service) ||
        api->major != SCO_API_MAJOR || api->minor < SCO_API_MINOR)
        return SCO_UNAVAILABLE;
    g_snapshot = {};
    const sco_command cmd = { sizeof(sco_command), 0, "inspector.read", "Read player",
                              "Read the current player without changing the game", nullptr,
                              nullptr, 0, sizeof(sco_arg_def), Read, nullptr };
    const sco_command refresh = { sizeof(sco_command), 0, "inspector.refresh", "Refresh snapshot",
                                  "Capture copied diagnostic sections on request", nullptr,
                                  nullptr, 0, sizeof(sco_arg_def), RefreshCommand, nullptr };
    const sco_command status = { sizeof(sco_command), 0, "inspector.status", "Snapshot status",
                                 "Show the latest captured diagnostic section status", nullptr,
                                 nullptr, 0, sizeof(sco_arg_def), StatusCommand, nullptr };
    const sco_command stats = { sizeof(sco_command), 0, "inspector.stats", "Actor stats",
                                "Read one named stat from the latest snapshot", nullptr,
                                &kPageArg, 1, sizeof(sco_arg_def), StatsCommand, nullptr };
    const sco_command gamerules = { sizeof(sco_command), 0, "inspector.gamerules", "GameRules state",
                                    "Read one live field from the latest snapshot", nullptr,
                                    &kPageArg, 1, sizeof(sco_arg_def), GameRulesCommand, nullptr };
    const sco_command inventory = { sizeof(sco_command), 0, "inspector.inventory", "Inventory page",
                                    "Read one copied inventory port from the latest snapshot", nullptr,
                                    &kPageArg, 1, sizeof(sco_arg_def), InventoryCommand, nullptr };
    const sco_command action_count = { sizeof(sco_command), 0, "inspector.action_count", "Action count",
        "Read the action metadata count on request", nullptr, nullptr, 0, sizeof(sco_arg_def), ActionCountCommand, nullptr };
    const sco_command action = { sizeof(sco_command), 0, "inspector.action", "Action lookup",
        "Look up exact action metadata by name", nullptr, &kActionArg, 1, sizeof(sco_arg_def), ActionCommand, nullptr };
    const sco_command action_id = { sizeof(sco_command), 0, "inspector.action_id", "Action id",
        "Look up action metadata by numeric id", nullptr, &kPageArg, 1, sizeof(sco_arg_def), ActionIdCommand, nullptr };
    const sco_command params = { sizeof(sco_command), 0, "inspector.params", "Parameter definitions",
        "Read one GameRules parameter definition, without live values", nullptr,
        &kPageArg, 1, sizeof(sco_arg_def), ParamsCommand, nullptr };
    g_api = api;
    g_self = self;
    auto r = api->register_command(self, &cmd);
    if (r == SCO_OK) r = api->register_command(self, &refresh);
    if (r == SCO_OK) r = api->register_command(self, &status);
    if (r == SCO_OK) r = api->register_command(self, &stats);
    if (r == SCO_OK) r = api->register_command(self, &gamerules);
    if (r == SCO_OK) r = api->register_command(self, &inventory);
    if (r == SCO_OK) r = api->register_command(self, &action_count);
    if (r == SCO_OK) r = api->register_command(self, &action);
    if (r == SCO_OK) r = api->register_command(self, &action_id);
    if (r == SCO_OK) r = api->register_command(self, &params);
    g_widgets = Widgets();
    if (r == SCO_OK && g_widgets) {
        g_widget_status[0] = '\0';
        std::snprintf(g_widget_status, sizeof(g_widget_status), "Not sampled");
        g_tab = kDiagnostics; g_page = 0; g_query[0] = '\0';
        g_panel = true;
        if (!RenderPanel()) { g_panel = false; g_widgets = nullptr; }
    }
    if (r != SCO_OK) Unload();
    return r;
}

void Unload() {
    // The host removes commands at unload. No borrowed service, id or callback context survives.
    if (g_panel && g_widgets && g_api && g_self) (void)g_widgets->remove_panel(g_self, kPanelId);
    g_api = nullptr;
    g_self = nullptr;
    g_widgets = nullptr;
    g_panel = false;
    g_tab = kDiagnostics; g_page = 0; g_query[0] = '\0';
    std::snprintf(g_widget_status, sizeof(g_widget_status), "Not sampled");
    g_snapshot = {};
    g_provider_reads = 0;
}

#undef HAS_FN
}  // namespace sco::features::inspector
