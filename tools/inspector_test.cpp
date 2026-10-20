// SPDX-License-Identifier: GPL-3.0-only
// Direct public-API fake: no host or game implementation is linked.
#include "../plugins/inspector/inspector.h"
#include "sc_actors.h"
#include "sc_entities.h"
#ifdef SCO_INSPECTOR_CANDIDATE
#include "sc_actor_stats.h"
#include "sc_game_build.h"
#include "sc_gamerules.h"
#include "sc_inventory.h"
#include "sc_input.h"
#include "sco_widgets.h"
#include <map>
#endif

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

namespace {
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); std::exit(1); } } while (0)
sco_command command{};
#ifdef SCO_INSPECTOR_CANDIDATE
std::map<std::string, sco_command> commands;
const sc_actor_stats_v1* stats = nullptr;
const sc_game_build_v1* build = nullptr;
const sc_gamerules_v1* rules = nullptr;
const sc_inventory_v1* inventory = nullptr;
const sc_input_v1* input = nullptr;
const sco_widgets_v1* widgets = nullptr;
bool stats11 = true, rules11 = true;
int diagnostic_queries = 0, stats_calls = 0, params_calls = 0, removes = 0;
uint32_t max_stat = 0, malformed = 0;
sco_widget_event_fn panel_callback = nullptr;
void* panel_context = nullptr;
std::string panel_text;
#endif
int registrations = 0;
sco_plugin* const owner = reinterpret_cast<sco_plugin*>(&registrations);
std::string logged;
const sc_actors_v1* actors = nullptr;
const sc_entities_v1* entities = nullptr;
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
const char* disabled_capability = nullptr;
sco_result registration_result = SCO_OK;
int Has(const char* name) { return !disabled_capability || std::strcmp(name, disabled_capability) != 0; }
sco_result QueryService(const char* name, uint32_t version, const void** out) {
#ifdef SCO_INSPECTOR_CANDIDATE
    *out = nullptr;
    if (!std::strcmp(name, SCO_WIDGETS_NAME)) { *out = widgets; return *out ? SCO_OK : SCO_NOT_FOUND; }
    ++diagnostic_queries;
    if (!std::strcmp(name, SC_ACTOR_STATS_NAME)) {
        CHECK(version == SC_ACTOR_STATS_VERSION_1_0 || version == SC_ACTOR_STATS_VERSION_1_1);
        if (stats11 || version == SC_ACTOR_STATS_VERSION_1_0) *out = stats;
    } else if (!std::strcmp(name, SC_GAMERULES_SERVICE_NAME)) {
        if (rules11 || version == SC_GAMERULES_VERSION_1_0) *out = rules;
    } else if (!std::strcmp(name, SC_GAME_BUILD_SERVICE_NAME)) *out = build;
    else if (!std::strcmp(name, SC_INVENTORY_SERVICE_NAME)) *out = inventory;
    else if (!std::strcmp(name, SC_INPUT_SERVICE_NAME)) *out = input;
    else {
#endif
    CHECK(version == 0x00010000u);
    *out = std::strcmp(name, SC_ACTORS_NAME) == 0 ? static_cast<const void*>(actors)
                                                 : static_cast<const void*>(entities);
#ifdef SCO_INSPECTOR_CANDIDATE
    }
#endif
    return *out ? SCO_OK : SCO_NOT_FOUND;
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
std::string Read() {
    char reply[1024]{};
    CHECK(command.fn(nullptr, 0, command.ctx, reply, sizeof(reply)) == SCO_OK);
    CHECK(logged == reply);
    return reply;
}
void Contains(const std::string& text, const char* part) { CHECK(text.find(part) != std::string::npos); }
sco_result Register(sco_plugin* self, const sco_command* cmd) {
    CHECK(self == owner);
    ++registrations;
#ifdef SCO_INSPECTOR_CANDIDATE
    commands[cmd->name] = *cmd;
    if (!std::strcmp(cmd->name, "inspector.read")) command = *cmd;
#else
    command = *cmd;
#endif
    return registration_result;
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
void RegistersReadOnlyCommand() {
    using namespace entry;
    auto api = Api();
    CHECK(Load(&api, owner) == SCO_OK);
#ifdef SCO_INSPECTOR_CANDIDATE
    CHECK(registrations == 8);
#else
    CHECK(registrations == 1);
#endif
    CHECK(std::strcmp(command.name, "inspector.read") == 0);
    CHECK(command.nargs == 0 && command.fn && command.capability == nullptr);
    const auto* info = Query();
    CHECK(info && info->size == sizeof(*info));
    CHECK(std::strcmp(info->name, "inspector") == 0);
    CHECK(std::strcmp(info->version, "1.0.0") == 0);
    Unload();
    Unload();
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
    Contains(text, "actor=41"); Contains(text, "entity=42");
    Contains(text, "health.current=75"); Contains(text, "health.maximum=unknown");
    Contains(text, "zone=43"); Contains(text, "position=(1,2,3)");
    Contains(text, "rotation=(0,0,0,1)");
    // Re-query providers on each read; never retain borrowed tables or values.
    actors = nullptr; entities = nullptr;
    const auto missing = Read();
    Contains(missing, "actor=unavailable"); Contains(missing, "entity=unavailable");
    Contains(missing, "health.current=unavailable"); Contains(missing, "position=unavailable");
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
#ifdef SCO_INSPECTOR_CANDIDATE
std::string Invoke(const char* name) {
    const auto c = commands.at(name);
    char reply[1024]{};
    CHECK(c.fn(nullptr, 0, c.ctx, reply, sizeof(reply)) == SCO_OK);
    return reply;
}
sco_result Stat(uint64_t entity, uint32_t id, float* value) {
    CHECK(entity == 42 && id >= 1 && id <= (stats11 ? 29u : 9u));
    ++stats_calls; if (id > max_stat) max_stat = id;
    *value = id == 28 ? std::numeric_limits<float>::infinity() : static_cast<float>(id);
    return id == 27 ? SCO_NOT_FOUND : SCO_OK;
}
sco_result Params(sco_plugin*, uint32_t offset, sc_gamerules_param_info* out,
                  uint32_t stride, uint32_t cap, uint32_t* count, uint32_t* total) {
    CHECK(stride == sizeof(*out) && cap == 64 && (offset == 0 || offset == 64));
    ++params_calls;
    if (malformed == 12) { *count = 0; *total = 0; return SCO_OK; }
    *total = 65; *count = offset ? 1 : 64;
    for (uint32_t i = 0; i < *count; ++i) {
        CHECK(out[i].size == sizeof(out[i]));
        out[i].type = i % 2 ? SC_GAMERULES_PARAM_INT : SC_GAMERULES_PARAM_FLOAT;
        std::snprintf(out[i].name, sizeof(out[i].name), "parameter%u", offset + i);
        std::snprintf(out[i].group, sizeof(out[i].group), "%s", i % 2 ? "" : "test");
    }
    if (offset) {
        if (malformed == 1) out[0].size = sizeof(*out) - 1;
        if (malformed == 2) out[0].type = 0;
        if (malformed == 3) std::memset(out[0].name, 'x', sizeof(out[0].name));
        if (malformed == 4) std::memset(out[0].group, 'x', sizeof(out[0].group));
        if (malformed == 5) *count = 65;
        if (malformed == 6) *count = 0;
        if (malformed == 7) *total = 66;
        if (malformed == 8) *total = 257;
        if (malformed == 9) *total = 63;
        if (malformed == 10) return SCO_UNAVAILABLE;
        if (malformed == 11) out[0].type = UINT32_MAX;
    }
    if (!offset && malformed == 13) *count = 0;
    if (!offset && malformed == 14) *total = UINT32_MAX;
    return SCO_OK;
}
sco_result SetPanel(sco_plugin* self, const char* id, const char*, const sco_widget_node* nodes,
                    uint32_t count, uint32_t stride, sco_widget_event_fn fn, void* ctx) {
    CHECK(self == owner && !std::strcmp(id, "inspector.main") && count == 8 && stride == sizeof(*nodes));
    for (uint32_t i = 0; i < count; ++i) CHECK(nodes[i].size == sizeof(*nodes));
    panel_text = nodes[7].label; panel_callback = fn; panel_context = ctx;
    return SCO_OK;
}
sco_result RemovePanel(sco_plugin* self, const char* id) {
    CHECK(self == owner && !std::strcmp(id, "inspector.main"));
    ++removes; panel_callback = nullptr; panel_context = nullptr; panel_text.clear();
    return SCO_OK;
}
void Click(uint32_t id) {
    CHECK(panel_callback);
    const sco_widget_event event{sizeof(sco_widget_event), id, SCO_WIDGET_BUTTON, 0, 0, nullptr};
    panel_callback("inspector.main", &event, panel_context);
}
void CandidateSnapshots() {
    auto api = Api();
    sc_actors_v1 a{}; a.size = sizeof(a); a.local_player = Player; actors = &a;
    sc_actor_stats_v1 s{}; s.size = sizeof(s); s.read = Stat; stats = &s;
    sc_gamerules_v1 r{}; r.size = sizeof(r); r.list_params = Params;
    r.read = [](sco_plugin*, sc_gamerules_state* out) { out->valid = 1; return SCO_OK; }; rules = &r;
    sc_game_build_v1 b{}; b.size = sizeof(b);
    b.read = [](sco_plugin*, sc_game_build_info* out) { out->pin_matches = 1; return SCO_OK; }; build = &b;
    sc_inventory_v1 inv{}; inv.size = sizeof(inv);
    inv.snapshot = [](sco_plugin*, sc_inventory_snapshot* out) {
        out->count = 2;
        for (auto& p : out->ports) p.size = sizeof(p);
        out->ports[0].entity_id = 501; out->ports[1].entity_id = 502;
        return SCO_OK;
    }; inventory = &inv;
    sc_input_v1 in{}; in.size = sizeof(in);
    in.count = [](sco_plugin*, uint32_t* count) { *count = 1267; return SCO_OK; }; input = &in;
    sco_widgets_v1 w{}; w.size = sizeof(w); w.set_panel = SetPanel; w.remove_panel = RemovePanel; widgets = &w;
    CHECK(entry::Load(&api, owner) == SCO_OK);
    Contains(Invoke("inspector.status"), "sample=never");
    const int before = stats_calls; Click(10);
    CHECK(stats_calls == before + 29 && max_stat == 29);
    Contains(Invoke("inspector.status"), "build=available rules=available inventory=available input=available params=available");
    const int queries = diagnostic_queries, calls = params_calls;
    Contains(Invoke("inspector.stats"), "stat=1 value=1");
    for (int i = 1; i < 29; ++i) {
        const auto text = Invoke("inspector.next");
        if (i == 26 || i == 27) Contains(text, "value=unknown");
        if (i == 28) Contains(text, "stat=29 value=29");
    }
    Contains(Invoke("inspector.inventory"), "entity=501");
    Click(51); Contains(panel_text, "entity=502");
    Contains(Invoke("inspector.params"), "param=parameter0");
    Click(51); Contains(panel_text, "param=parameter1");
    CHECK(diagnostic_queries == queries && params_calls == calls);
    stats = nullptr; rules = nullptr; build = nullptr; inventory = nullptr; input = nullptr;
    Click(23); Contains(panel_text, "param=parameter0"); CHECK(diagnostic_queries == queries);
    Invoke("inspector.refresh");
    Contains(Invoke("inspector.params"), "params=unavailable count=0");
    Contains(Invoke("inspector.stats"), "value=unavailable");
    stats = &s; rules = &r; stats11 = false; max_stat = 0;
    const int old_calls = stats_calls; Invoke("inspector.refresh");
    CHECK(stats_calls == old_calls + 9 && max_stat == 9);
    Invoke("inspector.stats"); for (int i = 1; i < 10; ++i) Invoke("inspector.next");
    Contains(Invoke("inspector.previous"), "stat=9 value=9");
    Contains(Invoke("inspector.next"), "stat=10 value=unavailable"); stats11 = true;
    for (malformed = 1; malformed <= 11; ++malformed) {
        Invoke("inspector.refresh"); const auto text = Invoke("inspector.params");
        CHECK(text.find("param=parameter") == std::string::npos); Contains(text, "count=0");
        Click(23); CHECK(panel_text.find("param=parameter") == std::string::npos);
    }
    malformed = 0; rules11 = false; Invoke("inspector.refresh");
    Contains(Invoke("inspector.params"), "params=unavailable"); rules11 = true;
    malformed = 12; Invoke("inspector.refresh");
    Contains(Invoke("inspector.params"), "params=available count=0");
    for (malformed = 13; malformed <= 14; ++malformed) {
        Invoke("inspector.refresh"); Contains(Invoke("inspector.params"), "count=0");
        CHECK(panel_text.find("param=parameter") == std::string::npos);
    }
    malformed = 0;
    for (uint32_t size = 0; size < offsetof(sc_gamerules_v1, list_params) + sizeof(r.list_params); ++size) {
        r.size = size; const int n = params_calls; Invoke("inspector.refresh");
        CHECK(params_calls == n); Contains(Invoke("inspector.params"), "params=unavailable");
    }
    r.size = sizeof(r); r.list_params = nullptr;
    Invoke("inspector.refresh"); Contains(Invoke("inspector.params"), "params=unavailable"); r.list_params = Params;
    for (uint32_t size = 0; size < offsetof(sc_actor_stats_v1, read) + sizeof(s.read); ++size) {
        s.size = size; const int n = stats_calls; Invoke("inspector.refresh"); CHECK(stats_calls == n);
    }
    s.size = sizeof(s); s.read = nullptr; const int n = stats_calls;
    Invoke("inspector.refresh"); CHECK(stats_calls == n);
    build = &b; inventory = &inv; input = &in;
    for (uint32_t size = 0; size < offsetof(sc_game_build_v1, read) + sizeof(b.read); ++size) {
        b.size = size; Invoke("inspector.refresh"); Contains(Invoke("inspector.status"), "build=unavailable");
    }
    b.size = sizeof(b); b.read = nullptr;
    Invoke("inspector.refresh"); Contains(Invoke("inspector.status"), "build=unavailable");
    for (uint32_t size = 0; size < offsetof(sc_inventory_v1, snapshot) + sizeof(inv.snapshot); ++size) {
        inv.size = size; Invoke("inspector.refresh"); Contains(Invoke("inspector.inventory"), "inventory=unavailable");
    }
    inv.size = sizeof(inv); inv.snapshot = nullptr;
    Invoke("inspector.refresh"); Contains(Invoke("inspector.inventory"), "inventory=unavailable");
    for (uint32_t size = 0; size < offsetof(sc_input_v1, count) + sizeof(in.count); ++size) {
        in.size = size; Invoke("inspector.refresh"); Contains(Invoke("inspector.status"), "input=unavailable");
    }
    in.size = sizeof(in); in.count = nullptr;
    Invoke("inspector.refresh"); Contains(Invoke("inspector.status"), "input=unavailable");
    const auto callback = panel_callback; const auto context = panel_context;
    const auto saved_command = commands.at("inspector.refresh"); const int removed = removes;
    entry::Unload(); CHECK(removes == removed + 1 && !panel_callback);
    sco_widget_event event{sizeof(event), 10, SCO_WIDGET_BUTTON, 0, 0, nullptr};
    const int q = diagnostic_queries; callback("inspector.main", &event, context);
    CHECK(saved_command.fn(nullptr, 0, saved_command.ctx, nullptr, 0) == SCO_UNAVAILABLE);
    CHECK(diagnostic_queries == q); entry::Unload(); CHECK(removes == removed + 1);
    for (uint32_t size = 0; size < offsetof(sco_widgets_v1, remove_panel) + sizeof(w.remove_panel); ++size) {
        w.size = size; CHECK(entry::Load(&api, owner) == SCO_OK); CHECK(!panel_callback); entry::Unload();
    }
    w.size = sizeof(w); w.remove_panel = nullptr;
    CHECK(entry::Load(&api, owner) == SCO_OK); CHECK(!panel_callback); entry::Unload();
    w.remove_panel = RemovePanel; w.set_panel = nullptr;
    CHECK(entry::Load(&api, owner) == SCO_OK); CHECK(!panel_callback); entry::Unload();
    widgets = nullptr; CHECK(entry::Load(&api, owner) == SCO_OK);
    Contains(Invoke("inspector.status"), "sample=never"); entry::Unload();
    actors = nullptr; stats = nullptr; rules = nullptr; build = nullptr; inventory = nullptr; input = nullptr;
    std::puts("inspector candidate manual/cached/malformed/withdrawal/unload cases PASS");
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
    ReadsCurrentSnapshot();
    RejectsIncompleteHostTables();
    RejectsIncompleteServiceMembers();
    DoesNotInventData();
    CallbackAndUnloadAreSafe();
#ifdef SCO_INSPECTOR_CANDIDATE
    CandidateSnapshots();
#endif
#ifdef INSPECTOR_EXTERNAL_TEST
    ExternalAdapterForwards();
    std::puts("inspector_test: 7 cases PASS (including external adapter)");
#else
    std::puts("inspector_test: 6 cases PASS");
#ifdef WORKFLOW_DYNAMIC_TEST
    test_module::Close();
#endif
#endif
}
