// SPDX-License-Identifier: GPL-3.0-only
// Direct public-API fake: no host or game implementation is linked.
#include "../plugins/inspector/inspector.h"
#include "sc_actors.h"
#include "sc_entities.h"

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
    CHECK(version == 0x00010000u);
    *out = std::strcmp(name, SC_ACTORS_NAME) == 0 ? static_cast<const void*>(actors)
                                                 : static_cast<const void*>(entities);
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
    command = *cmd;
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
    CHECK(registrations == 1);
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
