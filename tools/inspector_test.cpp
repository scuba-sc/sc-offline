// SPDX-License-Identifier: GPL-3.0-only
// Direct public-API fake: no host or game implementation is linked.
#include "../plugins/inspector/inspector.h"
#include "sc_actors.h"
#include "sc_actor_stats.h"
#include "sc_entities.h"
#include "sc_game_build.h"
#include "sc_gamerules.h"
#include "sc_inventory.h"
#include "sc_input.h"
#include "sco_widgets.h"

#include <cstddef>
#if defined(INSPECTOR_EXTERNAL_TEST)
namespace entry {
const sco_plugin_info* Query() { return sco_plugin_query(); }
sco_result Load(const sco_api* api, sco_plugin* self) { return sco_plugin_load(api, self); }
void Unload() { sco_plugin_unload(); }
}
#elif defined(WORKFLOW_DYNAMIC_TEST)
#include "workflow_module.h"
namespace entry = test_module;
#else
namespace entry = sco::features::inspector;
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <map>

namespace {
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); std::exit(1); } } while (0)
sco_command command{};
std::map<std::string, sco_command> commands;
int registrations = 0;
sco_plugin* const owner = reinterpret_cast<sco_plugin*>(&registrations);
std::string logged;
int diagnostic_service_queries = 0;
int widget_service_queries = 0;
const sc_actors_v1* actors = nullptr;
const sc_entities_v1* entities = nullptr;
const sc_game_build_v1* build_service = nullptr;
const sc_actor_stats_v1* stats_service = nullptr;
const sc_gamerules_v1* rules_service = nullptr;
const sc_inventory_v1* inventory_service = nullptr;
const sc_input_v1* input_service = nullptr;
bool rules_tail_available = true;
bool widgets_published = true;
sco_widgets_v1 widget_fixture_table{};
sco_widget_node widget_nodes[16]{};
char widget_cells[16][256]{};
uint32_t widget_node_count = 0;
sco_widget_event_fn widget_callback = nullptr;
void* widget_context = nullptr;
int widget_sets = 0, widget_removes = 0;
constexpr uint32_t W_REFRESH = 10, W_STATUS = 11, W_DIAGNOSTICS = 20, W_INVENTORY = 21, W_ACTIONS = 22,
    W_PARAMETERS = 23, W_TABLE = 30, W_ACTION_NAME = 40, W_LOOKUP = 41,
    W_BROWSE = 42, W_COUNT = 43, W_PREVIOUS = 50, W_NEXT = 51;
uint32_t metadata_queries = 0;
uint32_t metadata_calls = 0;
sc_gamerules_param_info param_fixtures[17]{};
void Log(sco_plugin* self, sco_log_level level, const char* text) {
    CHECK(self == owner && level == SCO_LOG_INFO);
    logged = text;
}
sco_result player_result = SCO_OK, health_result = SCO_OK, transform_result = SCO_OK;
uint64_t player_actor = 41, player_entity = 42, transform_zone = 43;
float health_current = 75, health_maximum = 0;
double position[3] = {1, 2, 3}, rotation[4] = {0, 0, 0, 1};
bool write_outputs = true;
int player_calls = 0, health_calls = 0, transform_calls = 0;
int build_calls = 0, stats_calls = 0, rules_calls = 0, inventory_calls = 0;
uint32_t inventory_fixture_count = 1, inventory_fixture_flags = 0;
sc_inventory_port inventory_fixture_ports[3]{};
uint32_t build_record_size = sizeof(sc_game_build_info);
float stat_value = 0.375f;
float stat_values[SC_ACTOR_STAT_LAST_1_0 + 1] = {};
bool stat_nonfinite = false;
const char* disabled_capability = nullptr;
sco_result registration_result = SCO_OK;
int Has(const char* name) { return !disabled_capability || std::strcmp(name, disabled_capability) != 0; }
sco_result QueryService(const char* name, uint32_t version, const void** out) {
    if (!name || !out) return SCO_BAD_ARG;
    if (std::strcmp(name, SCO_WIDGETS_NAME) == 0) ++widget_service_queries;
    else ++diagnostic_service_queries;
    *out = nullptr;
    if (std::strcmp(name, SC_ACTORS_NAME) == 0 && version == SC_ACTORS_VERSION_1_0) *out = actors;
    else if (std::strcmp(name, SC_ENTITIES_NAME) == 0 && version == SC_ENTITIES_VERSION_1_0) *out = entities;
    else if (std::strcmp(name, SC_GAME_BUILD_SERVICE_NAME) == 0 && version == SC_GAME_BUILD_SERVICE_VERSION) *out = build_service;
    else if (std::strcmp(name, SC_ACTOR_STATS_NAME) == 0 && version == SC_ACTOR_STATS_VERSION_1_0) *out = stats_service;
    else if (std::strcmp(name, SC_GAMERULES_SERVICE_NAME) == 0 &&
             (version == SC_GAMERULES_VERSION_1_0 || version == SC_GAMERULES_VERSION_1_1) &&
             (version == SC_GAMERULES_VERSION_1_0 || rules_tail_available)) *out = rules_service;
    else if (std::strcmp(name, SC_INVENTORY_SERVICE_NAME) == 0 && version == SC_INVENTORY_SERVICE_VERSION) *out = inventory_service;
    else if (std::strcmp(name, SC_INPUT_SERVICE_NAME) == 0 && version == SC_INPUT_SERVICE_VERSION) *out = input_service;
    else if (std::strcmp(name, SCO_WIDGETS_NAME) == 0 && widgets_published &&
             (version == SCO_WIDGETS_VERSION_1_0 || version == SCO_WIDGETS_VERSION_1_1)) *out = &widget_fixture_table;
    else return SCO_NOT_FOUND;
    return *out ? SCO_OK : SCO_NOT_FOUND;
}
sco_result WidgetSet(sco_plugin*, const char* id, const char* title, const sco_widget_node* nodes,
                     uint32_t count, uint32_t stride, sco_widget_event_fn fn, void* ctx) {
    CHECK(id && std::strcmp(id, "inspector.main") == 0 && title && std::strcmp(title, "Inspector") == 0);
    CHECK(nodes && count <= 16 && stride >= sizeof(sco_widget_node));
    for (uint32_t n = 0; n < count; ++n) {
        widget_nodes[n] = nodes[n];
        if (nodes[n].label) {
            std::snprintf(widget_cells[n], sizeof(widget_cells[n]), "%s", nodes[n].label);
            widget_nodes[n].label = widget_cells[n];
        }
        if (nodes[n].text) {
            std::snprintf(widget_cells[n], sizeof(widget_cells[n]), "%s", nodes[n].text);
            widget_nodes[n].text = widget_cells[n];
        }
        if (nodes[n].items && nodes[n].item_count) {
            static const char* cells[16]{};
            for (uint32_t c = 0; c < nodes[n].item_count; ++c) {
                std::snprintf(widget_cells[c], sizeof(widget_cells[c]), "%s", nodes[n].items[c]);
                cells[c] = widget_cells[c];
            }
            widget_nodes[n].items = cells;
        }
    }
    widget_node_count = count; widget_callback = fn; widget_context = ctx; ++widget_sets;
    return SCO_OK;
}
sco_result WidgetRemove(sco_plugin*, const char* id) {
    CHECK(id && std::strcmp(id, "inspector.main") == 0); ++widget_removes; widget_callback = nullptr; widget_context = nullptr;
    return SCO_OK;
}
void InitWidgets(uint32_t size = sizeof(sco_widgets_v1)) {
    widget_fixture_table = {size, 0, WidgetSet, WidgetRemove, nullptr, nullptr, nullptr};
}
void WidgetEvent(uint32_t id, uint32_t kind, const char* text = nullptr) {
    CHECK(widget_callback);
    sco_widget_event event{}; event.size = sizeof(event); event.node_id = id; event.kind = kind; event.text = text;
    widget_callback("inspector.main", &event, widget_context);
}
const sco_widget_node* WidgetNode(uint32_t id) {
    for (uint32_t n = 0; n < widget_node_count; ++n)
        if (widget_nodes[n].id == id) return &widget_nodes[n];
    return nullptr;
}
sco_result InputCount(sco_plugin*, uint32_t* out) { ++metadata_calls; *out = SC_INPUT_ACTION_COUNT; return SCO_OK; }
sco_result InputById(sco_plugin*, uint32_t id, sc_input_action_info* out) {
    ++metadata_calls;
    if (!out || out->size < sizeof(*out)) return SCO_BAD_ARG;
    if (id == SC_ACTION_TOGGLE_CURSOR_INPUT) { out->id = id; out->flags = 0; std::snprintf(out->name, sizeof(out->name), "toggle_cursor_input"); }
    else if (id == SC_ACTION_ATTACK1) { out->id = id; out->flags = 0; std::snprintf(out->name, sizeof(out->name), "attack1"); }
    // Synthetic reserved-flag fixtures exercise forward handling, not production registry facts.
    else if (id == SC_INPUT_ACTION_COUNT) {
        out->id = SC_INPUT_ACTION_COUNT; out->flags = SC_INPUT_ACTION_SENTINEL; std::snprintf(out->name, sizeof(out->name), "fixture_last");
    } else if (id > SC_ACTION_THROW_UNDERHAND && id < SC_INPUT_ACTION_COUNT) {
        out->id = id; out->flags = SC_INPUT_ACTION_BUILD_UNRESOLVED; std::snprintf(out->name, sizeof(out->name), "unresolved_%u", id);
    } else return SCO_NOT_FOUND;
    return SCO_OK;
}
sco_result InputByName(sco_plugin*, const char* name, sc_input_action_info* out) {
    ++metadata_calls;
    if (!name) return SCO_BAD_ARG;
    if (std::strcmp(name, "fixture_last") == 0)
        return InputById(nullptr, SC_INPUT_ACTION_COUNT, out);
    if (std::strcmp(name, "toggle_cursor_input") == 0) return InputById(nullptr, SC_ACTION_TOGGLE_CURSOR_INPUT, out);
    if (std::strcmp(name, "attack1") == 0) return InputById(nullptr, SC_ACTION_ATTACK1, out);
    return SCO_NOT_FOUND;
}
sco_result RulesList(sco_plugin*, uint32_t offset, sc_gamerules_param_info* out, uint32_t stride,
                     uint32_t cap, uint32_t* count, uint32_t* total) {
    ++metadata_calls;
    if (!out || !count || !total || stride < sizeof(*out) || cap != 1) return SCO_BAD_ARG;
    if (offset >= 17) return SCO_BAD_ARG;
    std::memcpy(out, &param_fixtures[offset], sizeof(*out));
    *count = 1; *total = 17;
    return SCO_OK;
}
void InitParamFixtures() {
    static const char* const names[] = {"valid", "enforce_group_qt", "quantum_travel_allowed",
        "quantum_boost_allowed", "travel_target_threshold_angle", "target_lock_angular_player",
        "target_lock_angular_ai", "target_lock_linear_player", "target_lock_linear_ai",
        "boost_group_distance_threshold", "group_qt", "qt_enabled", "qt_boost", "target_angle",
        "lock_angular", "lock_linear", "boost_distance"};
    for (uint32_t index = 0; index < 17; ++index) {
        param_fixtures[index] = {};
        param_fixtures[index].size = sizeof(sc_gamerules_param_info);
        param_fixtures[index].type = index % 2 ? SC_GAMERULES_PARAM_FLOAT : SC_GAMERULES_PARAM_INT;
        std::snprintf(param_fixtures[index].name, sizeof(param_fixtures[index].name), "%s", names[index]);
        std::snprintf(param_fixtures[index].group, sizeof(param_fixtures[index].group), "%s", index < 10 ? "" : "synthetic");
    }
}
sco_result Player(uint64_t* actor, uint64_t* entity) {
    ++player_calls;
    if (write_outputs) { *actor = player_actor; *entity = player_entity; }
    return player_result;
}
sco_result Health(uint64_t entity, float* current, float* maximum) {
    CHECK(entity == 42);
    ++health_calls;
    if (write_outputs) { *current = health_current; *maximum = health_maximum; }
    return health_result;
}
sco_result Transform(uint64_t entity, double pos[3], double rot[4], uint64_t* zone) {
    CHECK(entity == 42);
    ++transform_calls;
    if (write_outputs) {
        std::memcpy(pos, position, sizeof(position));
        std::memcpy(rot, rotation, sizeof(rotation));
        *zone = transform_zone;
    }
    return transform_result;
}
sco_result BuildRead(sco_plugin*, sc_game_build_info* out) {
    ++build_calls;
    out->pe_timestamp = 0x12345678; out->size_of_image = 0x12340000;
    out->pack_major = 2; out->pack_minor = 4; out->pack_patch = 0;
    out->verified_build[0] = 4; out->verified_build[1] = 10; out->verified_build[2] = 197; out->verified_build[3] = 2878;
    out->pin_matches = 0; out->size = build_record_size;
    return SCO_OK;
}
sco_result StatRead(uint64_t entity, uint32_t stat, float* out) {
    CHECK(entity == 42); ++stats_calls;
    *out = stat_nonfinite ? std::numeric_limits<float>::infinity() :
        (stat_values[stat] ? stat_values[stat] : stat_value);
    return SCO_OK;
}
sco_result RulesRead(sco_plugin*, sc_gamerules_state* out) {
    ++rules_calls; out->size = sizeof(*out); out->valid = 1; out->generation = 9;
    out->enforce_group_qt = 1; out->quantum_travel_allowed = 0; out->quantum_boost_allowed = 1;
    out->travel_target_threshold_angle = 12.5f;
    out->target_lock_angular_player = 0.1f; out->target_lock_angular_ai = 0.2f;
    out->target_lock_linear_player = 0.3f; out->target_lock_linear_ai = 0.4f;
    out->boost_group_distance_threshold = 0.5f;
    return SCO_OK;
}
sco_result InventoryRead(sco_plugin*, sc_inventory_snapshot* out) {
    CHECK(out->count == SC_INVENTORY_MAX_PORTS);
    ++inventory_calls; out->count = inventory_fixture_count; out->generation = 11;
    for (uint32_t index = 0; index < inventory_fixture_count; ++index)
        out->ports[index] = inventory_fixture_ports[index];
    out->flags = inventory_fixture_flags; out->total_seen = inventory_fixture_count;
    return SCO_OK;
}
uint64_t TotalProviderReads() {
    return static_cast<uint64_t>(build_calls + player_calls + health_calls + transform_calls +
                                 stats_calls + rules_calls + inventory_calls);
}
std::string Read() {
    char reply[1024]{};
    const auto found = commands.find("inspector.read");
    CHECK(found != commands.end());
    command = found->second;
    CHECK(command.fn(nullptr, 0, command.ctx, reply, sizeof(reply)) == SCO_OK);
    CHECK(logged == reply);
    return reply;
}
void Contains(const std::string& text, const char* part) {
    if (text.find(part) == std::string::npos)
        std::fprintf(stderr, "expected substring: [%s]\nactual reply: [%s]\n", part, text.c_str());
    CHECK(text.find(part) != std::string::npos);
}
sco_result Register(sco_plugin* self, const sco_command* cmd) {
    CHECK(self == owner);
    ++registrations;
    CHECK(cmd && cmd->name);
    commands[cmd->name] = *cmd;
    return registration_result;
}
sco_result Invoke(const char* name, char* reply, uint32_t capacity) {
    const auto found = commands.find(name);
    CHECK(found != commands.end());
    command = found->second;
    return command.fn(nullptr, command.nargs, command.ctx, reply, capacity);
}
sco_result InvokePage(const char* name, sco_arg* page, char* reply, uint32_t capacity) {
    const auto found = commands.find(name);
    CHECK(found != commands.end());
    command = found->second;
    return command.fn(page, 1, command.ctx, reply, capacity);
}
sco_api Api() {
    sco_api api{};
    api.size = sizeof(api);
    api.major = SCO_API_MAJOR;
    api.minor = SCO_API_MINOR;
    api.has = Has;
    api.log = Log;
    api.register_command = Register;
    api.query_service = QueryService;
    return api;
}
void RetainedWidgetWorkflow() {
    using namespace entry;
    auto api = Api();
    char reply[256]{};
    InitWidgets(); widgets_published = true;
    sc_actors_v1 a{}; a.size = sizeof(a); a.local_player = Player;
    sc_game_build_v1 b{}; b.size = sizeof(b); b.read = BuildRead;
    sc_actor_stats_v1 s{}; s.size = sizeof(s); s.read = StatRead;
    sc_gamerules_v1 r{}; r.size = sizeof(r); r.read = RulesRead; r.list_params = RulesList;
    sc_inventory_v1 i{}; i.size = sizeof(i); i.snapshot = InventoryRead;
    sc_input_v1 input{}; input.size = sizeof(input); input.count = InputCount; input.by_name = InputByName;
    actors = &a; build_service = &b; stats_service = &s; rules_service = &r;
    inventory_service = &i; input_service = &input; inventory_fixture_count = 3;
    InitParamFixtures();
    const int diagnostic_queries_before_load = diagnostic_service_queries;
    const int widget_queries_before_load = widget_service_queries;
    CHECK(Load(&api, owner) == SCO_OK);
    CHECK(diagnostic_service_queries == diagnostic_queries_before_load);
    CHECK(widget_service_queries == widget_queries_before_load + 1);
    CHECK(widget_callback && widget_node_count == 14 && widget_sets > 0);
    const uint64_t reads = TotalProviderReads();
    const uint32_t metadata = metadata_calls;
    const int diagnostic_queries = diagnostic_service_queries;
    for (int n = 0; n < 100; ++n) {
        WidgetEvent(W_DIAGNOSTICS, SCO_WIDGET_BUTTON);
        WidgetEvent(W_INVENTORY, SCO_WIDGET_BUTTON);
        WidgetEvent(W_ACTIONS, SCO_WIDGET_BUTTON);
        WidgetEvent(W_PARAMETERS, SCO_WIDGET_BUTTON);
        WidgetEvent(W_ACTION_NAME, SCO_WIDGET_TEXT_INPUT, "attack1");
    }
    CHECK(TotalProviderReads() == reads && metadata_calls == metadata &&
          diagnostic_service_queries == diagnostic_queries);
    const int build_calls_before_refresh = build_calls;
    const int player_calls_before_refresh = player_calls;
    const int stats_calls_before_refresh = stats_calls;
    const int rules_calls_before_refresh = rules_calls;
    const int inventory_calls_before_refresh = inventory_calls;
    WidgetEvent(W_REFRESH, SCO_WIDGET_BUTTON);
    CHECK(build_calls - build_calls_before_refresh == 1 &&
          player_calls - player_calls_before_refresh == 1 &&
          stats_calls - stats_calls_before_refresh == SC_ACTOR_STAT_LAST_1_0 &&
          rules_calls - rules_calls_before_refresh == 1 &&
          inventory_calls - inventory_calls_before_refresh == 1);
    CHECK(TotalProviderReads() - reads == SC_ACTOR_STAT_LAST_1_0 + 4 &&
          diagnostic_service_queries - diagnostic_queries == 5 && metadata_calls == metadata);
    CHECK(WidgetNode(W_STATUS) && WidgetNode(W_STATUS)->label &&
          std::strstr(WidgetNode(W_STATUS)->label, "Snapshot 1"));
    const uint64_t refreshed = TotalProviderReads();
    WidgetEvent(W_INVENTORY, SCO_WIDGET_BUTTON);
    WidgetEvent(W_NEXT, SCO_WIDGET_BUTTON);
    WidgetEvent(W_PREVIOUS, SCO_WIDGET_BUTTON);
    CHECK(TotalProviderReads() == refreshed);
    WidgetEvent(W_ACTIONS, SCO_WIDGET_BUTTON);
    WidgetEvent(W_ACTION_NAME, SCO_WIDGET_TEXT_INPUT, "attack1");
    CHECK(metadata_calls == metadata);
    const uint32_t metadata_before_lookup = metadata_calls;
    WidgetEvent(W_LOOKUP, SCO_WIDGET_BUTTON);
    CHECK(metadata_calls == metadata_before_lookup + 2);
    CHECK(WidgetNode(W_TABLE) && WidgetNode(W_TABLE)->items &&
          std::strstr(WidgetNode(W_TABLE)->items[0], "attack1"));
    WidgetEvent(W_BROWSE, SCO_WIDGET_BUTTON);
    CHECK(metadata_calls == metadata_before_lookup + 2 + 17);
    CHECK(WidgetNode(W_TABLE) && WidgetNode(W_TABLE)->items &&
          std::strstr(WidgetNode(W_TABLE)->items[0], "valid"));
    WidgetEvent(W_NEXT, SCO_WIDGET_BUTTON);
    CHECK(WidgetNode(W_TABLE) && WidgetNode(W_TABLE)->items &&
          std::strstr(WidgetNode(W_TABLE)->items[0], "enforce_group_qt"));
    WidgetEvent(W_COUNT, SCO_WIDGET_BUTTON);
    CHECK(metadata_calls == metadata_before_lookup + 2 + 17 + 1);
    CHECK(WidgetNode(W_STATUS) && WidgetNode(W_STATUS)->label &&
          std::strstr(WidgetNode(W_STATUS)->label, "Named action count"));
    const uint64_t reads_before_diagnostics = TotalProviderReads();
    const uint32_t metadata_before_diagnostics = metadata_calls;
    const int queries_before_diagnostics = diagnostic_service_queries;
    WidgetEvent(W_DIAGNOSTICS, SCO_WIDGET_BUTTON);
    CHECK(TotalProviderReads() == reads_before_diagnostics &&
          metadata_calls == metadata_before_diagnostics &&
          diagnostic_service_queries == queries_before_diagnostics);
    disabled_capability = "game.actor_stats.read";
    const uint64_t reads_before_failed_refresh = TotalProviderReads();
    const uint32_t metadata_before_failed_refresh = metadata_calls;
    const int queries_before_failed_refresh = diagnostic_service_queries;
    WidgetEvent(W_REFRESH, SCO_WIDGET_BUTTON);
    CHECK(TotalProviderReads() - reads_before_failed_refresh == 4 &&
          metadata_calls == metadata_before_failed_refresh &&
          diagnostic_service_queries - queries_before_failed_refresh == 5);
    CHECK(WidgetNode(W_STATUS) && WidgetNode(W_STATUS)->label &&
          std::strstr(WidgetNode(W_STATUS)->label, "Snapshot 2") &&
          std::strstr(WidgetNode(W_STATUS)->label, "stats=unavailable"));
    CHECK(WidgetNode(W_TABLE) && WidgetNode(W_TABLE)->items &&
          std::strstr(WidgetNode(W_TABLE)->items[0], "stats: unavailable"));
    disabled_capability = nullptr;
    auto old = widget_callback; auto ctx = widget_context;
    sco_widget_event forged{}; forged.size = sizeof(forged); forged.node_id = W_REFRESH; forged.kind = SCO_WIDGET_TOGGLE;
    old("inspector.main", &forged, ctx);
    CHECK(Invoke("inspector.status", reply, sizeof(reply)) == SCO_OK && std::strstr(reply, "generation=2"));
    forged.kind = SCO_WIDGET_BUTTON; old("forged.panel", &forged, ctx);
    CHECK(Invoke("inspector.status", reply, sizeof(reply)) == SCO_OK && std::strstr(reply, "generation=2"));
    Unload();
    CHECK(widget_removes > 0 && !widget_callback);
    const uint32_t sets = static_cast<uint32_t>(widget_sets);
    const uint64_t provider_reads_before_reload = TotalProviderReads();
    const uint32_t metadata_before_reload = metadata_calls;
    const int diagnostic_queries_before_reload = diagnostic_service_queries;
    old("inspector.main", &forged, ctx);
    CHECK(widget_sets == static_cast<int>(sets) && TotalProviderReads() == provider_reads_before_reload &&
          metadata_calls == metadata_before_reload && diagnostic_service_queries == diagnostic_queries_before_reload);
    const int widget_queries_before_reload = widget_service_queries;
    CHECK(Load(&api, owner) == SCO_OK);
    CHECK(TotalProviderReads() == provider_reads_before_reload && metadata_calls == metadata_before_reload);
    CHECK(diagnostic_service_queries == diagnostic_queries_before_reload);
    CHECK(widget_service_queries == widget_queries_before_reload + 1);
    CHECK(Invoke("inspector.status", reply, sizeof(reply)) == SCO_OK && std::strstr(reply, "sample=never"));
    CHECK(widget_sets > static_cast<int>(sets));
    Unload();
    InitWidgets(static_cast<uint32_t>(offsetof(sco_widgets_v1, remove_panel)));
    const int diagnostic_queries_before_short_widget_load = diagnostic_service_queries;
    const int widget_queries_before_short_widget_load = widget_service_queries;
    CHECK(Load(&api, owner) == SCO_OK && !widget_callback);
    CHECK(diagnostic_service_queries == diagnostic_queries_before_short_widget_load);
    CHECK(widget_service_queries == widget_queries_before_short_widget_load + 1);
    CHECK(commands.count("inspector.read") == 1); Unload();
    widgets_published = false;
    const int diagnostic_queries_before_missing_widget_load = diagnostic_service_queries;
    const int widget_queries_before_missing_widget_load = widget_service_queries;
    CHECK(Load(&api, owner) == SCO_OK && !widget_callback);
    CHECK(diagnostic_service_queries == diagnostic_queries_before_missing_widget_load);
    CHECK(widget_service_queries == widget_queries_before_missing_widget_load + 2);
    CHECK(commands.count("inspector.read") == 1); Unload(); widgets_published = true;
    actors = nullptr; build_service = nullptr; stats_service = nullptr;
    rules_service = nullptr; inventory_service = nullptr; input_service = nullptr;
}
void RegistersReadOnlyCommand() {
    using namespace entry;
    auto api = Api();
    const int before = registrations;
    CHECK(Load(&api, owner) == SCO_OK);
    CHECK(registrations == before + 10);
    CHECK(commands.count("inspector.read") == 1 && commands.count("inspector.refresh") == 1 &&
          commands.count("inspector.status") == 1 && commands.count("inspector.stats") == 1 &&
          commands.count("inspector.gamerules") == 1 && commands.count("inspector.inventory") == 1 &&
          commands.count("inspector.action_count") == 1 && commands.count("inspector.action") == 1 &&
          commands.count("inspector.action_id") == 1 && commands.count("inspector.params") == 1);
    command = commands.at("inspector.read");
    CHECK(command.nargs == 0 && command.fn && command.capability == nullptr);
    const auto* info = Query();
    CHECK(info && info->size == sizeof(*info));
    CHECK(std::strcmp(info->name, "inspector") == 0);
    CHECK(std::strcmp(info->version, "1.0.0") == 0);
    Unload();
    Unload();
}
void RefreshIsManualAndCopiesProviders() {
    using namespace entry;
    auto api = Api();
    sc_actors_v1 a{}; a.size = sizeof(a); a.local_player = Player; a.health = Health;
    sc_game_build_v1 b{}; b.size = sizeof(b); b.read = BuildRead;
    sc_actor_stats_v1 s{}; s.size = sizeof(s); s.read = StatRead;
    sc_gamerules_v1 r{}; r.size = sizeof(r); r.read = RulesRead;
    sc_inventory_v1 i{}; i.size = sizeof(i); i.snapshot = InventoryRead;
    actors = &a; build_service = &b; stats_service = &s; rules_service = &r; inventory_service = &i;
    const uint64_t reads_before_load = TotalProviderReads();
    const int queries_before_load = diagnostic_service_queries;
    const int widgets_before_load = widget_service_queries;
    CHECK(Load(&api, owner) == SCO_OK);
    CHECK(TotalProviderReads() == reads_before_load);
    CHECK(diagnostic_service_queries == queries_before_load);
    CHECK(widget_service_queries == widgets_before_load + 1);

    char reply[256]{};
    const int queries_before_refresh = diagnostic_service_queries;
    const int build_calls_before_refresh = build_calls;
    const int player_calls_before_refresh = player_calls;
    const int stats_calls_before_refresh = stats_calls;
    const int rules_calls_before_refresh = rules_calls;
    const int inventory_calls_before_refresh = inventory_calls;
    CHECK(Invoke("inspector.refresh", reply, sizeof(reply)) == SCO_OK);
    CHECK(std::strstr(reply, "generation=1") && std::strstr(reply, "build=available") &&
          std::strstr(reply, "player=available") && std::strstr(reply, "stats=available") &&
          std::strstr(reply, "gamerules=available") && std::strstr(reply, "inventory=available"));
    CHECK(build_calls - build_calls_before_refresh == 1 &&
          player_calls - player_calls_before_refresh == 1 &&
          stats_calls - stats_calls_before_refresh == SC_ACTOR_STAT_LAST_1_0);
    CHECK(rules_calls - rules_calls_before_refresh == 1 &&
          inventory_calls - inventory_calls_before_refresh == 1);
    CHECK(diagnostic_service_queries == queries_before_refresh + 5);
    const uint64_t reads_after = TotalProviderReads();
    stat_value = 0.625f;
    build_service = nullptr; stats_service = nullptr; rules_service = nullptr; inventory_service = nullptr;
    CHECK(TotalProviderReads() == reads_after);

    CHECK(Invoke("inspector.refresh", reply, sizeof(reply)) == SCO_OK);
    CHECK(std::strstr(reply, "generation=2") && std::strstr(reply, "build=unavailable") &&
          std::strstr(reply, "stats=unavailable") && std::strstr(reply, "gamerules=unavailable") &&
          std::strstr(reply, "inventory=unavailable"));

    b.size = offsetof(sc_game_build_v1, read) + sizeof(b.read) - 1; build_service = &b;
    s.size = offsetof(sc_actor_stats_v1, read) + sizeof(s.read) - 1; stats_service = &s;
    r.size = offsetof(sc_gamerules_v1, read) + sizeof(r.read) - 1; rules_service = &r;
    i.size = offsetof(sc_inventory_v1, snapshot) + sizeof(i.snapshot) - 1; inventory_service = &i;
    CHECK(Invoke("inspector.refresh", reply, sizeof(reply)) == SCO_OK);
    CHECK(std::strstr(reply, "generation=3") && std::strstr(reply, "build=unavailable") &&
          std::strstr(reply, "stats=unavailable") && std::strstr(reply, "gamerules=unavailable") &&
          std::strstr(reply, "inventory=unavailable"));
    b.size = sizeof(b); s.size = sizeof(s); r.size = sizeof(r); i.size = sizeof(i);
    stat_nonfinite = true;
    CHECK(Invoke("inspector.refresh", reply, sizeof(reply)) == SCO_OK);
    CHECK(std::strstr(reply, "stats=failed"));
    stat_nonfinite = false;
    build_record_size = sizeof(sc_game_build_info) - 1;
    CHECK(Invoke("inspector.refresh", reply, sizeof(reply)) == SCO_OK);
    CHECK(std::strstr(reply, "build=unavailable"));
    build_record_size = sizeof(sc_game_build_info);
    Unload();
    const uint64_t reads_before_reload = TotalProviderReads();
    CHECK(Load(&api, owner) == SCO_OK);
    CHECK(TotalProviderReads() == reads_before_reload);
    CHECK(Invoke("inspector.refresh", reply, sizeof(reply)) == SCO_OK);
    CHECK(std::strstr(reply, "generation=1") && std::strstr(reply, "build=available") &&
          std::strstr(reply, "player=available") && std::strstr(reply, "stats=available") &&
          std::strstr(reply, "gamerules=available") && std::strstr(reply, "inventory=available"));
    Unload(); actors = nullptr; build_service = nullptr; stats_service = nullptr;
    rules_service = nullptr; inventory_service = nullptr;
}
void RefreshCommandValidationAndUnknownService() {
    using namespace entry;
    auto api = Api();
    CHECK(Load(&api, owner) == SCO_OK);
    char reply[256]{};
    const uint64_t reads = TotalProviderReads();
    CHECK(Invoke("inspector.refresh", reply, sizeof(reply)) == SCO_OK);
    CHECK(std::strstr(reply, "generation=1") && std::strstr(reply, "build=unavailable") &&
          std::strstr(reply, "player=unavailable"));
    CHECK(TotalProviderReads() == reads);
    const void* unknown = nullptr;
    CHECK(QueryService("unknown.service", 0x00010000u, &unknown) == SCO_NOT_FOUND && unknown == nullptr);
    const auto& refresh = commands.at("inspector.refresh");
    CHECK(refresh.fn(nullptr, 1, refresh.ctx, reply, sizeof(reply)) == SCO_BAD_ARG);
    CHECK(refresh.fn(nullptr, 0, refresh.ctx, nullptr, 1) == SCO_BAD_ARG);
    const int queries_before_zero = diagnostic_service_queries;
    CHECK(refresh.fn(nullptr, 0, refresh.ctx, nullptr, 0) == SCO_TOO_MANY);
    CHECK(diagnostic_service_queries == queries_before_zero);
    char small_reply[2] = {'x', '\0'};
    CHECK(refresh.fn(nullptr, 0, refresh.ctx, small_reply, sizeof(small_reply)) == SCO_TOO_MANY);
    CHECK(small_reply[0] == 'x');
    const uint64_t after = TotalProviderReads();
    CHECK(TotalProviderReads() == after);
    Unload();
}
void CachedDiagnosticCommands() {
    using namespace entry;
    auto api = Api();
    sc_actors_v1 a{}; a.size = sizeof(a); a.local_player = Player; a.health = Health;
    sc_game_build_v1 b{}; b.size = sizeof(b); b.read = BuildRead;
    sc_actor_stats_v1 s{}; s.size = sizeof(s); s.read = StatRead;
    sc_gamerules_v1 r{}; r.size = sizeof(r); r.read = RulesRead;
    actors = &a; build_service = &b; stats_service = &s; rules_service = &r;
    stat_values[SC_ACTOR_STAT_HUNGER] = 0.625f;
    CHECK(Load(&api, owner) == SCO_OK);
    char reply[256]{};
    CHECK(Invoke("inspector.status", reply, sizeof(reply)) == SCO_OK);
    Contains(reply, "generation=0"); Contains(reply, "sample=never"); Contains(reply, "stats=unavailable");
    sco_arg page{}; page.type = SCO_ARG_INT; page.v.i = 0;
    const uint64_t reads_before = TotalProviderReads();
    const int queries_before = diagnostic_service_queries;
    CHECK(commands.at("inspector.stats").fn(&page, 1, nullptr, reply, sizeof(reply)) == SCO_UNAVAILABLE);
    CHECK(commands.at("inspector.gamerules").fn(&page, 1, nullptr, reply, sizeof(reply)) == SCO_UNAVAILABLE);
    CHECK(TotalProviderReads() == reads_before && diagnostic_service_queries == queries_before);
    CHECK(Invoke("inspector.refresh", reply, sizeof(reply)) == SCO_OK);
    CHECK(Invoke("inspector.status", reply, sizeof(reply)) == SCO_OK);
    Contains(reply, "generation=1"); Contains(reply, "sample=captured"); Contains(reply, "stats=available");
    CHECK(std::strlen(reply) + 1 <= sizeof(reply));
    CHECK(InvokePage("inspector.stats", &page, reply, sizeof(reply)) == SCO_OK);
    Contains(reply, "stat=hunger value=0.625 history=captured");
    CHECK(commands.at("inspector.gamerules").fn(&page, 1, nullptr, reply, sizeof(reply)) == SCO_OK);
    Contains(reply, "field=valid value=1");
    page.v.i = 3;
    CHECK(commands.at("inspector.stats").fn(&page, 1, nullptr, reply, sizeof(reply)) == SCO_OK);
    Contains(reply, "stat=suit_temperature");
    page.v.i = 4;
    CHECK(commands.at("inspector.gamerules").fn(&page, 1, nullptr, reply, sizeof(reply)) == SCO_OK);
    Contains(reply, "field=travel_target_threshold_angle value=12.5");
    page.v.i = -1;
    CHECK(commands.at("inspector.stats").fn(&page, 1, nullptr, reply, sizeof(reply)) == SCO_BAD_ARG);
    page.v.i = (std::numeric_limits<int64_t>::max)();
    CHECK(commands.at("inspector.gamerules").fn(&page, 1, nullptr, reply, sizeof(reply)) == SCO_BAD_ARG);
    page.v.i = 0; page.type = SCO_ARG_FLOAT;
    CHECK(commands.at("inspector.stats").fn(&page, 1, nullptr, reply, sizeof(reply)) == SCO_BAD_ARG);
    page.type = SCO_ARG_INT;
    CHECK(commands.at("inspector.stats").fn(&page, 0, nullptr, reply, sizeof(reply)) == SCO_BAD_ARG);
    CHECK(commands.at("inspector.stats").fn(&page, 1, nullptr, nullptr, 1) == SCO_BAD_ARG);
    CHECK(commands.at("inspector.stats").fn(&page, 1, nullptr, nullptr, 0) == SCO_TOO_MANY);
    CHECK(commands.at("inspector.status").fn(nullptr, 0, nullptr, nullptr, 0) == SCO_TOO_MANY);
    CHECK(commands.at("inspector.status").fn(nullptr, 0, nullptr, nullptr, 1) == SCO_BAD_ARG);
    char tiny[1] = {'x'};
    const uint64_t cached_reads = TotalProviderReads();
    const int cached_queries = diagnostic_service_queries;
    CHECK(commands.at("inspector.status").fn(nullptr, 0, nullptr, tiny, sizeof(tiny)) == SCO_TOO_MANY);
    CHECK(tiny[0] == 'x');
    CHECK(commands.at("inspector.stats").fn(&page, 1, nullptr, tiny, sizeof(tiny)) == SCO_TOO_MANY);
    CHECK(tiny[0] == 'x');
    CHECK(TotalProviderReads() == cached_reads && diagnostic_service_queries == cached_queries);
    stat_values[SC_ACTOR_STAT_HUNGER] = 0.875f;
    CHECK(Invoke("inspector.status", reply, sizeof(reply)) == SCO_OK);
    Contains(reply, "generation=1");
    page.v.i = 0;
    CHECK(InvokePage("inspector.stats", &page, reply, sizeof(reply)) == SCO_OK);
    Contains(reply, "value=0.625");
    disabled_capability = "game.actor_stats.read";
    CHECK(Invoke("inspector.refresh", reply, sizeof(reply)) == SCO_OK);
    CHECK(InvokePage("inspector.stats", &page, reply, sizeof(reply)) == SCO_UNAVAILABLE);
    CHECK(InvokePage("inspector.gamerules", &page, reply, sizeof(reply)) == SCO_OK);
    disabled_capability = nullptr;
    player_result = SCO_NOT_FOUND;
    CHECK(Invoke("inspector.refresh", reply, sizeof(reply)) == SCO_OK);
    CHECK(InvokePage("inspector.stats", &page, reply, sizeof(reply)) == SCO_UNAVAILABLE);
    CHECK(InvokePage("inspector.gamerules", &page, reply, sizeof(reply)) == SCO_OK);
    player_result = SCO_OK;
    stat_nonfinite = true;
    CHECK(Invoke("inspector.refresh", reply, sizeof(reply)) == SCO_OK);
    page.v.i = 0;
    CHECK(InvokePage("inspector.stats", &page, reply, sizeof(reply)) == SCO_UNAVAILABLE);
    stat_nonfinite = false;
    Unload(); actors = nullptr; build_service = nullptr; stats_service = nullptr; rules_service = nullptr;
    stat_values[SC_ACTOR_STAT_HUNGER] = 0;
}

sco_result WideRulesRead(sco_plugin* self, sc_gamerules_state* out) {
    const auto result = RulesRead(self, out);
    out->generation = (std::numeric_limits<uint64_t>::max)();
    out->travel_target_threshold_angle = out->target_lock_angular_player =
        out->target_lock_angular_ai = out->target_lock_linear_player =
        out->target_lock_linear_ai = out->boost_group_distance_threshold =
            (std::numeric_limits<float>::max)();
    return result;
}
sco_result WideParamsList(sco_plugin*, uint32_t offset, sc_gamerules_param_info* out, uint32_t stride,
                         uint32_t cap, uint32_t* count, uint32_t* total) {
    CHECK(offset == (std::numeric_limits<uint32_t>::max)() - 1);
    CHECK(out && out->size == sizeof(*out) && stride == sizeof(*out) && cap == 1 && count && total);
    out->type = SC_GAMERULES_PARAM_FLOAT;
    std::memset(out->name, 'n', sizeof(out->name) - 1); out->name[sizeof(out->name) - 1] = '\0';
    std::memset(out->group, 'g', sizeof(out->group) - 1); out->group[sizeof(out->group) - 1] = '\0';
    *count = 1; *total = (std::numeric_limits<uint32_t>::max)();
    return SCO_OK;
}
void GameRulesRepliesAreCompleteOrRefused() {
    using namespace entry;
    auto api = Api();
    sc_gamerules_v1 rules{}; rules.size = sizeof(rules); rules.read = WideRulesRead; rules.list_params = WideParamsList;
    rules_service = &rules;
    CHECK(Load(&api, owner) == SCO_OK);
    char reply[1024]{};
    CHECK(Invoke("inspector.refresh", reply, sizeof(reply)) == SCO_OK);
    const char* fields[] = {"travel_target_threshold_angle", "target_lock_angular_player",
        "target_lock_angular_ai", "target_lock_linear_player", "target_lock_linear_ai",
        "boost_group_distance_threshold"};
    for (uint32_t index = 0; index < sizeof(fields) / sizeof(fields[0]); ++index) {
        sco_arg page{}; page.type = SCO_ARG_INT; page.v.i = index + 4;
        const std::string expected = "generation=1 page=" + std::to_string(page.v.i) +
            " field=" + fields[index] + " value=3.40282347e+38 provider_generation=18446744073709551615 history=captured";
        if (index == 0) CHECK(expected.size() >= 128);
        CHECK(InvokePage("inspector.gamerules", &page, reply, sizeof(reply)) == SCO_OK);
        CHECK(expected == reply);
        CHECK(InvokePage("inspector.gamerules", &page, reply, static_cast<uint32_t>(expected.size() + 1)) == SCO_OK);
        CHECK(expected == reply);
        std::memset(reply, 'x', sizeof(reply));
        CHECK(InvokePage("inspector.gamerules", &page, reply, static_cast<uint32_t>(expected.size())) == SCO_TOO_MANY);
        CHECK(reply[0] == 'x' && reply[expected.size() - 1] == 'x');
        char tiny[2] = {'x', 'y'};
        CHECK(InvokePage("inspector.gamerules", &page, tiny, sizeof(tiny)) == SCO_TOO_MANY);
        CHECK(tiny[0] == 'x' && tiny[1] == 'y');
    }
    sco_arg page{}; page.type = SCO_ARG_INT; page.v.i = (std::numeric_limits<uint32_t>::max)() - 1;
    const std::string expected = "page=4294967294 total=4294967295 name=" + std::string(47, 'n') +
        " group=" + std::string(31, 'g') + " type=float source=definition history=metadata";
    CHECK(expected.size() >= 160);
    CHECK(InvokePage("inspector.params", &page, reply, sizeof(reply)) == SCO_OK);
    CHECK(expected == reply);
    CHECK(InvokePage("inspector.params", &page, reply, static_cast<uint32_t>(expected.size() + 1)) == SCO_OK);
    CHECK(expected == reply);
    std::memset(reply, 'x', sizeof(reply));
    CHECK(InvokePage("inspector.params", &page, reply, static_cast<uint32_t>(expected.size())) == SCO_TOO_MANY);
    CHECK(reply[0] == 'x' && reply[expected.size() - 1] == 'x');
    char tiny[2] = {'x', 'y'};
    CHECK(InvokePage("inspector.params", &page, tiny, sizeof(tiny)) == SCO_TOO_MANY);
    CHECK(tiny[0] == 'x' && tiny[1] == 'y');
    Unload(); rules_service = nullptr;
}

void CachedInventoryPagesAndBounds() {
    using namespace entry;
    auto api = Api();
    sc_inventory_v1 inventory{}; inventory.size = sizeof(inventory); inventory.snapshot = InventoryRead;
    inventory_fixture_count = 3; inventory_fixture_flags = 0;
    const uint64_t ids[] = {701, 702, 703};
    const uint32_t categories[] = {SC_ITEM_TYPE_SHIP, SC_ITEM_TYPE_CAPACITOR_ASSIGNMENT_CONTROLLER,
                                   SC_ITEM_TYPE_WEAPON_REGEN_POOL};
    for (uint32_t index = 0; index < 3; ++index) {
        inventory_fixture_ports[index].size = sizeof(sc_inventory_port);
        inventory_fixture_ports[index].valid = 1;
        inventory_fixture_ports[index].item_class_id = categories[index];
        inventory_fixture_ports[index].entity_id = ids[index];
    }
    inventory_service = &inventory;
    CHECK(Load(&api, owner) == SCO_OK);
    char reply[256]{};
    sco_arg page{}; page.type = SCO_ARG_INT; page.v.i = 0;
    CHECK(InvokePage("inspector.inventory", &page, reply, sizeof(reply)) == SCO_UNAVAILABLE);
    const int unsampled_reads = inventory_calls;
    CHECK(Invoke("inspector.refresh", reply, sizeof(reply)) == SCO_OK);
    CHECK(inventory_calls == unsampled_reads + 1);
    const uint64_t cached_reads = TotalProviderReads();
    const int cached_queries = diagnostic_service_queries;
    for (uint32_t index = 0; index < 3; ++index) {
        page.v.i = index;
        CHECK(InvokePage("inspector.inventory", &page, reply, sizeof(reply)) == SCO_OK);
        char expected[32]; std::snprintf(expected, sizeof(expected), "entity=%llu", static_cast<unsigned long long>(ids[index]));
        Contains(reply, "generation=1"); Contains(reply, "total=3"); Contains(reply, expected);
        Contains(reply, index < 2 ? "next=1" : "next=0");
    }
    CHECK(TotalProviderReads() == cached_reads && diagnostic_service_queries == cached_queries);

    inventory_fixture_ports[0].entity_id = 999;
    inventory_fixture_count = 0;
    CHECK(InvokePage("inspector.inventory", &page, reply, sizeof(reply)) == SCO_OK);
    Contains(reply, "entity=703");
    page.v.i = 0;
    inventory_fixture_flags = SC_INVENTORY_FLAG_TRUNCATED | SC_INVENTORY_FLAG_LIMITED;
    CHECK(Invoke("inspector.refresh", reply, sizeof(reply)) == SCO_OK);
    CHECK(InvokePage("inspector.inventory", &page, reply, sizeof(reply)) == SCO_OK);
    Contains(reply, "total=0 page=0 next=0 truncated=1 limited=1 record=none");
    page.v.i = 1;
    CHECK(InvokePage("inspector.inventory", &page, reply, sizeof(reply)) == SCO_BAD_ARG);

    inventory_fixture_count = 3;
    CHECK(Invoke("inspector.refresh", reply, sizeof(reply)) == SCO_OK);
    page.v.i = -1;
    CHECK(InvokePage("inspector.inventory", &page, reply, sizeof(reply)) == SCO_BAD_ARG);
    page.v.i = (std::numeric_limits<int64_t>::max)();
    CHECK(InvokePage("inspector.inventory", &page, reply, sizeof(reply)) == SCO_BAD_ARG);
    page.v.i = 0; page.type = SCO_ARG_FLOAT;
    CHECK(InvokePage("inspector.inventory", &page, reply, sizeof(reply)) == SCO_BAD_ARG);
    page.type = SCO_ARG_INT;
    CHECK(commands.at("inspector.inventory").fn(nullptr, 0, nullptr, reply, sizeof(reply)) == SCO_BAD_ARG);
    CHECK(commands.at("inspector.inventory").fn(nullptr, 1, nullptr, reply, sizeof(reply)) == SCO_BAD_ARG);
    const uint64_t before_bad_reply = TotalProviderReads();
    CHECK(InvokePage("inspector.inventory", &page, nullptr, 1) == SCO_BAD_ARG);
    CHECK(InvokePage("inspector.inventory", &page, nullptr, 0) == SCO_TOO_MANY);
    char tiny[64]; std::memset(tiny, 'x', sizeof(tiny));
    CHECK(InvokePage("inspector.inventory", &page, tiny, sizeof(tiny)) == SCO_TOO_MANY);
    CHECK(tiny[0] == 'x');
    page.v.i = 1;
    CHECK(InvokePage("inspector.inventory", &page, tiny, sizeof(tiny)) == SCO_TOO_MANY);
    CHECK(tiny[0] == 'x');
    CHECK(TotalProviderReads() == before_bad_reply);

    inventory_fixture_count = 0;
    CHECK(Invoke("inspector.refresh", reply, sizeof(reply)) == SCO_OK);
    page.v.i = 0;
    CHECK(InvokePage("inspector.inventory", &page, reply, sizeof(reply)) == SCO_OK);
    Contains(reply, "total=0 page=0 next=0 truncated=1 limited=1 record=none");
    Unload();
    const int before_reload = inventory_calls;
    CHECK(Load(&api, owner) == SCO_OK);
    CHECK(InvokePage("inspector.inventory", &page, reply, sizeof(reply)) == SCO_UNAVAILABLE);
    CHECK(inventory_calls == before_reload);
    Unload(); inventory_service = nullptr; inventory_fixture_count = 1; inventory_fixture_flags = 0;
}

void OnDemandMetadataCommands() {
    using namespace entry;
    auto api = Api();
    sc_input_v1 input{}; input.size = sizeof(input); input.count = InputCount;
    input.by_id = InputById; input.by_name = InputByName;
    sc_gamerules_v1 rules{}; rules.size = sizeof(rules); rules.read = RulesRead; rules.list_params = RulesList;
    input_service = &input; rules_service = &rules; rules_tail_available = true;
    InitParamFixtures();
    CHECK(Load(&api, owner) == SCO_OK);
    char reply[256]{};
    const uint32_t reads_before = metadata_calls;
    CHECK(Invoke("inspector.action_count", reply, sizeof(reply)) == SCO_OK);
    Contains(reply, ("count=" + std::to_string(SC_INPUT_ACTION_COUNT)).c_str());
    CHECK(metadata_calls == reads_before + 1);
    const uint32_t action_count_capacity = static_cast<uint32_t>(std::strlen(reply) + 1);
    char action_count_tiny[96]; std::memset(action_count_tiny, 'x', sizeof(action_count_tiny));
    const uint32_t before_short_action_count = metadata_calls;
    CHECK(Invoke("inspector.action_count", action_count_tiny, action_count_capacity - 1) == SCO_TOO_MANY);
    CHECK(action_count_tiny[0] == 'x' && metadata_calls == before_short_action_count + 1);
    const uint32_t before_exact_action_count = metadata_calls;
    CHECK(Invoke("inspector.action_count", reply, action_count_capacity) == SCO_OK);
    Contains(reply, ("count=" + std::to_string(SC_INPUT_ACTION_COUNT) + " history=metadata").c_str());
    CHECK(metadata_calls == before_exact_action_count + 1);
    const uint32_t before_zero_action_count = metadata_calls;
    CHECK(Invoke("inspector.action_count", nullptr, 0) == SCO_TOO_MANY);
    CHECK(metadata_calls == before_zero_action_count);
    CHECK(Invoke("inspector.action_count", nullptr, 1) == SCO_BAD_ARG);
    CHECK(metadata_calls == before_zero_action_count);

    sco_arg action{}; action.type = SCO_ARG_STRING; action.v.s = "attack1";
    const auto action_command = commands.at("inspector.action");
    CHECK(action_command.fn(&action, 1, nullptr, reply, sizeof(reply)) == SCO_OK);
    Contains(reply, ("id=" + std::to_string(SC_ACTION_ATTACK1) + " name=attack1 flags=0 build=identified").c_str());
    action.v.s = "INVALID";
    CHECK(action_command.fn(&action, 1, nullptr, reply, sizeof(reply)) == SCO_NOT_FOUND);
    action.v.s = "fixture_last";
    CHECK(action_command.fn(&action, 1, nullptr, reply, sizeof(reply)) == SCO_OK);
    Contains(reply, ("id=" + std::to_string(SC_INPUT_ACTION_COUNT) + " name=fixture_last flags=2").c_str());
    action.v.s = "unresolved_200";
    CHECK(action_command.fn(&action, 1, nullptr, reply, sizeof(reply)) == SCO_NOT_FOUND);
    sco_arg id{}; id.type = SCO_ARG_INT; id.v.i = 200;
    CHECK(InvokePage("inspector.action_id", &id, reply, sizeof(reply)) == SCO_OK);
    Contains(reply, "id=200 name=unresolved_200 flags=1 build=unresolved");
    id.v.i = SC_ACTION_INVALID;
    CHECK(InvokePage("inspector.action_id", &id, reply, sizeof(reply)) == SCO_NOT_FOUND);
    id.v.i = -1;
    const uint32_t before_invalid_id = metadata_calls;
    CHECK(InvokePage("inspector.action_id", &id, reply, sizeof(reply)) == SCO_BAD_ARG);
    CHECK(metadata_calls == before_invalid_id);
    id.v.i = static_cast<int64_t>((std::numeric_limits<uint32_t>::max)()) + 1;
    CHECK(InvokePage("inspector.action_id", &id, reply, sizeof(reply)) == SCO_BAD_ARG);
    CHECK(metadata_calls == before_invalid_id);
    id.v.i = SC_INPUT_ACTION_COUNT + 1;
    CHECK(InvokePage("inspector.action_id", &id, reply, sizeof(reply)) == SCO_NOT_FOUND);

    sco_arg page{}; page.type = SCO_ARG_INT;
    const uint32_t before_params = metadata_calls;
    for (uint32_t index = 0; index < 17; ++index) {
        page.v.i = index;
        CHECK(InvokePage("inspector.params", &page, reply, sizeof(reply)) == SCO_OK);
        char expected[64]; std::snprintf(expected, sizeof(expected), "page=%u total=17 name=%s", index, param_fixtures[index].name);
        Contains(reply, expected);
        Contains(reply, index < 10 ? "group= type=" : "group=synthetic type=");
        Contains(reply, "source=definition");
    }
    CHECK(metadata_calls == before_params + 17);
    page.v.i = 0;
    CHECK(InvokePage("inspector.params", &page, reply, 64) == SCO_TOO_MANY);
    const uint32_t before_invalid = metadata_calls;
    page.v.i = -1;
    CHECK(InvokePage("inspector.params", &page, reply, sizeof(reply)) == SCO_BAD_ARG);
    CHECK(metadata_calls == before_invalid);
    page.v.i = 17;
    CHECK(InvokePage("inspector.params", &page, reply, sizeof(reply)) == SCO_BAD_ARG);

    action.v.s = "Attack1";
    CHECK(action_command.fn(&action, 1, nullptr, reply, sizeof(reply)) == SCO_NOT_FOUND);
    action.v.s = "";
    CHECK(action_command.fn(&action, 1, nullptr, reply, sizeof(reply)) == SCO_BAD_ARG);
    action.v.s = "attack1"; action.type = SCO_ARG_INT;
    const uint32_t before_bad_type = metadata_calls;
    CHECK(action_command.fn(&action, 1, nullptr, reply, sizeof(reply)) == SCO_BAD_ARG);
    CHECK(metadata_calls == before_bad_type);
    action.type = SCO_ARG_STRING; action.v.s = "attack1";
    CHECK(action_command.fn(&action, 0, nullptr, reply, sizeof(reply)) == SCO_BAD_ARG);
    char oversized[SC_INPUT_ACTION_NAME_BYTES + 1];
    std::memset(oversized, 'x', sizeof(oversized)); oversized[sizeof(oversized) - 1] = '\0';
    action.v.s = oversized;
    CHECK(action_command.fn(&action, 1, nullptr, reply, sizeof(reply)) == SCO_BAD_ARG);

    rules_tail_available = false;
    rules.size = static_cast<uint32_t>(offsetof(sc_gamerules_v1, list_params));
    page.v.i = 0;
    CHECK(InvokePage("inspector.params", &page, reply, sizeof(reply)) == SCO_UNAVAILABLE);
    rules.size = sizeof(rules);
    rules_tail_available = true;
    Unload(); input_service = nullptr; rules_service = nullptr; rules_tail_available = true;
}

void ReadsCurrentSnapshot() {
    using namespace entry;
    auto api = Api();
    sc_actors_v1 a{};
    a.size = sizeof(a); a.local_player = Player; a.health = Health;
    sc_entities_v1 e{};
    e.size = sizeof(e); e.get_transform = Transform;
    actors = &a; entities = &e;
    CHECK(Load(&api, owner) == SCO_OK);
    const auto text = Read();
    CHECK(text == "actor=41 entity=42 health.current=75 health.maximum=unknown zone=43 position=(1,2,3) rotation=(0,0,0,1)");
    Contains(text, "actor=41"); Contains(text, "entity=42");
    Contains(text, "health.current=75"); Contains(text, "health.maximum=unknown");
    Contains(text, "zone=43"); Contains(text, "position=(1,2,3)");
    Contains(text, "rotation=(0,0,0,1)");
    // Re-query providers on each read; never retain borrowed tables or values.
    actors = nullptr; entities = nullptr;
    const auto missing = Read();
    Contains(missing, "actor=unavailable"); Contains(missing, "entity=unavailable");
    Contains(missing, "health.current=unavailable"); Contains(missing, "position=unavailable");
    CHECK(Read() == "actor=unavailable entity=unavailable health.current=unavailable health.maximum=unavailable zone=unavailable position=unavailable rotation=unavailable");
    Unload();
}
void RejectsIncompleteHostTables() {
    using namespace entry;
    auto api = Api();
    const int before = registrations;
    api.size = offsetof(sco_api, query_service) + sizeof(api.query_service) - 1;
    CHECK(Load(&api, owner) == SCO_UNAVAILABLE);
    CHECK(registrations == before);
    api = Api();
    CHECK(Load(nullptr, owner) == SCO_BAD_ARG);
    CHECK(Load(&api, nullptr) == SCO_BAD_ARG);
    auto rejects = [&](sco_api bad) {
        CHECK(Load(&bad, owner) == SCO_UNAVAILABLE);
        CHECK(registrations == before);
    };
    for (uint32_t size = 0; size < offsetof(sco_api, query_service) + sizeof(api.query_service); ++size) {
        auto short_api = Api(); short_api.size = size; rejects(short_api);
    }
    api = Api(); api.has = nullptr; rejects(api);
    api = Api(); api.log = nullptr; rejects(api);
    api = Api(); api.register_command = nullptr; rejects(api);
    api = Api(); api.query_service = nullptr; rejects(api);
    api = Api(); api.major = 2; rejects(api);
    api = Api(); api.minor = 0; rejects(api);
}
void RejectsIncompleteServiceMembers() {
    using namespace entry;
    auto api = Api();
    sc_actors_v1 a{}; a.size = sizeof(a); a.local_player = Player; a.health = Health;
    sc_entities_v1 e{}; e.size = sizeof(e); e.get_transform = Transform;
    actors = &a; entities = &e;
    CHECK(Load(&api, owner) == SCO_OK);
    for (uint32_t size = 0; size < offsetof(sc_actors_v1, local_player) + sizeof(a.local_player); ++size) {
        a.size = size;
        const int before = player_calls;
        Contains(Read(), "actor=unavailable"); CHECK(player_calls == before);
    }
    a.size = sizeof(a); a.local_player = nullptr;
    Contains(Read(), "actor=unavailable"); a.local_player = Player;
    for (uint32_t size = offsetof(sc_actors_v1, local_player) + sizeof(a.local_player);
         size < offsetof(sc_actors_v1, health) + sizeof(a.health); ++size) {
        a.size = size;
        const int before = health_calls;
        auto text = Read(); Contains(text, "entity=42"); Contains(text, "health.current=unavailable");
        CHECK(health_calls == before);
    }
    a.size = sizeof(a); a.health = nullptr;
    Contains(Read(), "health.current=unavailable"); a.health = Health;
    for (uint32_t size = 0; size < offsetof(sc_entities_v1, get_transform) + sizeof(e.get_transform); ++size) {
        e.size = size;
        const int before = transform_calls;
        auto text = Read(); Contains(text, "health.current=75"); Contains(text, "position=unavailable");
        CHECK(transform_calls == before);
    }
    e.size = sizeof(e); e.get_transform = nullptr;
    Contains(Read(), "position=unavailable");
    for (const char* cap : {"game.actors.local_player", "game.actors.health", "game.entities.transform"}) {
        e.get_transform = Transform;
        disabled_capability = cap;
        const int p = player_calls, h = health_calls, t = transform_calls;
        const auto text = Read();
        if (std::strcmp(cap, "game.actors.local_player") == 0) { Contains(text, "actor=unavailable"); CHECK(player_calls == p); }
        if (std::strcmp(cap, "game.actors.health") == 0) { Contains(text, "health.current=unavailable"); CHECK(health_calls == h); }
        if (std::strcmp(cap, "game.entities.transform") == 0) { Contains(text, "position=unavailable"); CHECK(transform_calls == t); }
    }
    disabled_capability = nullptr;
    Unload(); actors = nullptr; entities = nullptr;
}
void DoesNotInventData() {
    using namespace entry;
    auto api = Api();
    sc_actors_v1 a{}; a.size = sizeof(a); a.local_player = Player; a.health = Health;
    sc_entities_v1 e{}; e.size = sizeof(e); e.get_transform = Transform;
    actors = &a; entities = &e;
    CHECK(Load(&api, owner) == SCO_OK);
    player_result = SCO_NOT_FOUND;
    const int h = health_calls, t = transform_calls;
    auto text = Read(); Contains(text, "entity=unknown"); Contains(text, "health.current=unknown");
    Contains(text, "position=unknown"); CHECK(health_calls == h && transform_calls == t);
    player_result = SCO_OK; player_actor = 0;
    text = Read(); Contains(text, "actor=unknown"); Contains(text, "entity=42"); Contains(text, "health.current=75");
    player_actor = 41; player_entity = 0;
    text = Read(); Contains(text, "actor=41"); Contains(text, "entity=unknown"); player_entity = 42;
    write_outputs = false;
    text = Read(); Contains(text, "entity=unknown"); Contains(text, "position=unknown"); write_outputs = true;
    health_current = 0; health_maximum = 100;
    text = Read(); Contains(text, "health.current=0"); Contains(text, "health.maximum=100");
    health_current = std::numeric_limits<float>::quiet_NaN(); health_maximum = 0;
    text = Read(); Contains(text, "health.current=unknown"); Contains(text, "health.maximum=unknown");
    health_current = 75; health_result = SCO_UNAVAILABLE;
    text = Read(); Contains(text, "health.current=unavailable"); Contains(text, "position=(1,2,3)");
    health_result = SCO_OK; transform_result = SCO_FAILED;
    text = Read(); Contains(text, "health.current=75"); Contains(text, "zone=unknown");
    transform_result = SCO_OK; transform_zone = 0;
    text = Read(); Contains(text, "zone=unknown"); Contains(text, "position=unknown");
    transform_zone = 43; position[0] = std::numeric_limits<double>::infinity(); rotation[3] = 0;
    text = Read(); Contains(text, "zone=43"); Contains(text, "position=unknown"); Contains(text, "rotation=unknown");
    position[0] = 1; rotation[3] = std::numeric_limits<double>::quiet_NaN();
    text = Read(); Contains(text, "position=(1,2,3)"); Contains(text, "rotation=unknown"); rotation[3] = 1;
    a.health = [](uint64_t, float*, float*) { return SCO_OK; };
    e.get_transform = [](uint64_t, double*, double*, uint64_t* zone) { *zone = 43; return SCO_OK; };
    text = Read(); Contains(text, "health.current=unknown"); Contains(text, "zone=43");
    Contains(text, "position=unknown"); Contains(text, "rotation=unknown");
    Unload(); actors = nullptr; entities = nullptr;
}
void CallbackAndUnloadAreSafe() {
    using namespace entry;
    auto api = Api();
    CHECK(Load(&api, owner) == SCO_OK);
    command = commands.at("inspector.read");
    CHECK(Load(&api, owner) == SCO_BAD_ARG);
    char tiny[1] = {'x'};
    CHECK(command.fn(nullptr, 0, command.ctx, tiny, sizeof(tiny)) == SCO_OK && tiny[0] == '\0');
    CHECK(command.fn(nullptr, 0, command.ctx, nullptr, 0) == SCO_OK);
    CHECK(command.fn(nullptr, 0, command.ctx, nullptr, 1) == SCO_BAD_ARG);
    CHECK(command.fn(nullptr, 1, command.ctx, tiny, 1) == SCO_BAD_ARG);
    const auto old = command;
    Unload(); Unload();
    std::memset(&api, 0xa5, sizeof(api));
    const auto previous_log = logged;
    CHECK(old.fn(nullptr, 0, old.ctx, tiny, sizeof(tiny)) == SCO_UNAVAILABLE);
    CHECK(logged == previous_log);
    api = Api(); registration_result = SCO_TOO_MANY;
    CHECK(Load(&api, owner) == SCO_TOO_MANY);
    CHECK(command.fn(nullptr, 0, command.ctx, tiny, sizeof(tiny)) == SCO_UNAVAILABLE);
    registration_result = SCO_OK;
    CHECK(Load(&api, owner) == SCO_OK); Unload();
}
#ifdef INSPECTOR_EXTERNAL_TEST
void ExternalAdapterForwards() {
    using namespace entry;
    CHECK(sco_plugin_query() == Query());
    auto api = Api();
    CHECK(sco_plugin_load(&api, owner) == SCO_OK);
    Contains(Read(), "entity=unavailable");
    sco_plugin_unload(); sco_plugin_unload();
    char reply[1];
    CHECK(command.fn(nullptr, 0, command.ctx, reply, sizeof(reply)) == SCO_UNAVAILABLE);
}
#endif
}  // namespace

int main(int argc, char** argv) {
#ifdef WORKFLOW_DYNAMIC_TEST
    test_module::Open(argc, argv);
#else
    (void)argc; (void)argv;
#endif
    RegistersReadOnlyCommand();
    RetainedWidgetWorkflow();
    RefreshIsManualAndCopiesProviders();
    RefreshCommandValidationAndUnknownService();
    CachedDiagnosticCommands();
    GameRulesRepliesAreCompleteOrRefused();
    CachedInventoryPagesAndBounds();
    OnDemandMetadataCommands();
    ReadsCurrentSnapshot();
    RejectsIncompleteHostTables();
    RejectsIncompleteServiceMembers();
    DoesNotInventData();
    CallbackAndUnloadAreSafe();
#ifdef INSPECTOR_EXTERNAL_TEST
    ExternalAdapterForwards();
    std::puts("inspector_test: 14 cases PASS (including external adapter)");
#else
    std::puts("inspector_test: 13 cases PASS");
#ifdef WORKFLOW_DYNAMIC_TEST
    test_module::Close();
#endif
#endif
}
