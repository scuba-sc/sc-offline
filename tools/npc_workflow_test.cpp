// SPDX-License-Identifier: GPL-3.0-only
// Synthetic public providers; no engine bytes or implementation are linked.
#include "../plugins/npc/npc.h"
#include "sc_actors.h"
#include "sc_entities.h"
#include "sc_spatial.h"
#include <cmath>
#if defined(NPC_EXTERNAL_TEST)
namespace entry {
const sco_plugin_info* Query() { return sco_plugin_query(); }
sco_result Load(const sco_api* api, sco_plugin* self) { return sco_plugin_load(api, self); }
void Unload() { sco_plugin_unload(); }
}
#elif defined(WORKFLOW_DYNAMIC_TEST)
#include "workflow_module.h"
namespace entry = test_module;
#else
namespace entry = sco::features::npc;
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <bit>
#include <string>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
namespace {
std::map<std::string, sco_command> commands;
int owner_tag;
sco_plugin* const owner = reinterpret_cast<sco_plugin*>(&owner_tag);
int spawned = 0, removed = 0, pose_calls = 0, transform_calls = 0, registrations = 0;
bool streamed = false, publish_actors = true, publish_spatial = true, publish_entities = true;
uint64_t spawn_id = 101, pose_zone = 91;
std::set<uint64_t> issued;
// Synthetic public-service state for NPCs this plugin did not spawn. These are
// deliberately separate from the owner-issued tickets, not host/engine objects.
const std::set<uint64_t> foreign_sentinels{9001, 9002};
const std::set<uint64_t> crew_sentinels{9101, 9102};
std::set<uint64_t> foreign_ids = foreign_sentinels, crew_ids = crew_sentinels;
std::map<uint64_t, int> despawn_attempts;
int fail_spawn_at = -1;
bool auto_ids = false;
double pose_pos[3] = {1,2,3}, pose_rot[4] = {0,0,0,1};
sco_result spawn_result = SCO_OK, remove_result = SCO_OK, transform_result = SCO_OK;
int pose_result = 1, fail_registration = -1;
const char* missing_cap = nullptr;
sc_actors_v1 actors{};
sc_entities_v1 entities{};
sc_spatial_v1 spatial{};
int Alive(uint64_t id) { CHECK(id == spawn_id); return streamed; }
sco_result Transform(uint64_t id, double pos[3], double rot[4], uint64_t* zone) {
    CHECK(id == spawn_id); ++transform_calls;
    if (!streamed) return SCO_NOT_FOUND;
    pos[0] = pose_pos[0]; pos[1] = 5; pos[2] = 3.2;
    rot[0] = rot[1] = rot[2] = 0; rot[3] = 1; *zone = 91;
    return transform_result;
}
sco_result QueryService(const char* name, uint32_t version, const void** out) {
    CHECK(version == 0x10000);
    *out = !std::strcmp(name, SC_ACTORS_NAME) ? (publish_actors ? static_cast<const void*>(&actors) : nullptr) :
           !std::strcmp(name, SC_ENTITIES_NAME) ? (publish_entities ? static_cast<const void*>(&entities) : nullptr) :
           (publish_spatial ? static_cast<const void*>(&spatial) : nullptr);
    return *out ? SCO_OK : SCO_NOT_FOUND;
}
int Has(const char* cap) { return !missing_cap || std::strcmp(cap, missing_cap); }
void Log(sco_plugin* self, sco_log_level, const char*) { CHECK(self == owner); }
sco_result Register(sco_plugin* self, const sco_command* cmd) {
    CHECK(self == owner); CHECK(cmd->ctx);
    if (registrations++ == fail_registration) return SCO_FAILED;
    commands[cmd->name] = *cmd; return SCO_OK;
}
int Pose(double pos[3], double rot[4], uint64_t* zone) {
    ++pose_calls;
    if (!pose_result) return 0;
    std::memcpy(pos, pose_pos, sizeof(pose_pos)); std::memcpy(rot, pose_rot, sizeof(pose_rot)); *zone = pose_zone;
    return 1;
}
sco_result Spawn(sco_plugin* self, const char* archetype, uint64_t zone, const double pos[3], uint64_t* id) {
    CHECK(self == owner); CHECK(!std::strcmp(archetype, "synthetic_actor"));
    CHECK(zone == 91 && std::isfinite(pos[0]) && std::isfinite(pos[1]) && std::isfinite(pos[2]));
    ++spawned; *id = auto_ids ? static_cast<uint64_t>(spawned + 100) : spawn_id;
    if (spawned == fail_spawn_at) return SCO_FAILED;
    if (spawn_result == SCO_OK && *id) issued.insert(*id);
    return spawn_result;
}
sco_result Remove(sco_plugin* self, uint64_t id) {
    ++despawn_attempts[id]; // Record even a forbidden attempt before rejecting it.
    CHECK(self == owner);
    CHECK(!foreign_ids.count(id)); CHECK(!crew_ids.count(id));
    CHECK(issued.count(id)); ++removed;
    if (remove_result == SCO_OK || remove_result == SCO_NOT_FOUND) issued.erase(id);
    return remove_result;
}
void Reset() {
    using namespace entry;
    Unload(); commands.clear(); spawned = removed = pose_calls = transform_calls = registrations = 0;
    streamed = false; publish_actors = publish_spatial = publish_entities = true;
    spawn_id = 101; pose_zone = 91; pose_result = 1; fail_registration = -1; missing_cap = nullptr;
    issued.clear(); fail_spawn_at = -1; auto_ids = false;
    foreign_ids = foreign_sentinels; crew_ids = crew_sentinels; despawn_attempts.clear();
    pose_pos[0]=1; pose_pos[1]=2; pose_pos[2]=3; pose_rot[0]=pose_rot[1]=pose_rot[2]=0; pose_rot[3]=1;
    spawn_result = remove_result = transform_result = SCO_OK;
    actors = {}; actors.size = sizeof(actors); actors.spawn_npc = Spawn; actors.despawn = Remove;
    spatial = {}; spatial.size = sizeof(spatial); spatial.player_pose = Pose;
    entities = {}; entities.size = sizeof(entities); entities.alive = Alive; entities.get_transform = Transform;
}
sco_api Api() {
    sco_api api{}; api.size = sizeof(api); api.major = 1; api.minor = 1;
    api.has = Has; api.log = Log; api.register_command = Register; api.query_service = QueryService;
    return api;
}
std::string reply;
sco_result Run(const char* name, const sco_arg* args = nullptr, uint32_t nargs = 0) {
    auto cmd = commands.at(name); char text[512]{};
    const auto r = cmd.fn(args, nargs, cmd.ctx, text, sizeof(text)); reply = text; return r;
}
sco_result SpawnOne() {
    sco_arg args[2]{}; args[0].type = SCO_ARG_STRING; args[0].v.s = "synthetic_actor";
    args[1].type = SCO_ARG_INT; args[1].v.i = 1;
    return Run("npc.spawn", args, 2);
}
sco_result RemoveId(int64_t id) { sco_arg arg{}; arg.type = SCO_ARG_INT; arg.v.i = id; return Run("npc.remove", &arg, 1); }
void Contains(const char* text) { CHECK(reply.find(text) != std::string::npos); }
void Lifecycle() {
    Reset(); auto api = Api(); using namespace entry;
    CHECK(Load(&api, owner) == SCO_OK); CHECK(commands.size() == 4);
    CHECK(std::strcmp(Query()->name, "npc") == 0);
    CHECK(SpawnOne() == SCO_OK && spawned == 1);
    CHECK(Run("npc.observe") == SCO_OK); Contains("pending=1"); Contains("visible=0");
    streamed = true;
    CHECK(Run("npc.observe") == SCO_OK); Contains("visible=1"); CHECK(transform_calls == 1);
    CHECK(RemoveId(999) == SCO_NOT_FOUND && removed == 0);
    CHECK(RemoveId(101) == SCO_OK && removed == 1);
    CHECK(RemoveId(101) == SCO_NOT_FOUND && removed == 1);
    streamed = false;
    CHECK(Run("npc.observe") == SCO_OK); Contains("removing=1");
    Unload(); Unload(); CHECK(removed == 1);
    CHECK(Run("npc.observe") == SCO_UNAVAILABLE);
}
void CheckForeignAndCrewUntouched() {
    CHECK(foreign_ids == foreign_sentinels); CHECK(crew_ids == crew_sentinels);
    for (const auto id : foreign_sentinels) CHECK(despawn_attempts.count(id) == 0);
    for (const auto id : crew_sentinels) CHECK(despawn_attempts.count(id) == 0);
}
void ClearAndUnloadOwnershipIsolation() {
    Reset(); auto api = Api(); using namespace entry;
    auto_ids = true; CHECK(Load(&api, owner) == SCO_OK);
    CHECK(SpawnOne() == SCO_OK); CHECK(SpawnOne() == SCO_OK);
    CHECK(issued == std::set<uint64_t>({101, 102}));
    for (const auto id : foreign_sentinels) CHECK(RemoveId(static_cast<int64_t>(id)) == SCO_NOT_FOUND);
    for (const auto id : crew_sentinels) CHECK(RemoveId(static_cast<int64_t>(id)) == SCO_NOT_FOUND);
    CHECK(despawn_attempts.empty() && removed == 0);

    CHECK(Run("npc.clear") == SCO_OK); CHECK(issued.empty() && removed == 2);
    CHECK((despawn_attempts == std::map<uint64_t, int>{{101, 1}, {102, 1}}));
    CheckForeignAndCrewUntouched();
    CHECK(Run("npc.clear") == SCO_OK && removed == 2);
    CheckForeignAndCrewUntouched();

    // Keep new owned tickets outstanding so unload, not only clear, must clean up.
    CHECK(SpawnOne() == SCO_OK); CHECK(SpawnOne() == SCO_OK);
    CHECK(issued == std::set<uint64_t>({103, 104})); CHECK(!streamed);
    Unload(); CHECK(issued.empty() && removed == 4);
    CHECK((despawn_attempts == std::map<uint64_t, int>{{101, 1}, {102, 1}, {103, 1}, {104, 1}}));
    CheckForeignAndCrewUntouched();
    const auto attempts = despawn_attempts;
    Unload(); CHECK(despawn_attempts == attempts && removed == 4);
    CheckForeignAndCrewUntouched();
    CHECK(Run("npc.clear") == SCO_UNAVAILABLE);
}
void DeferredRemovalAndCleanup() {
    Reset(); auto api = Api(); using namespace entry;
    CHECK(Load(&api, owner) == SCO_OK); CHECK(SpawnOne() == SCO_OK);
    CHECK(!streamed); CHECK(RemoveId(101) == SCO_OK && removed == 1 && issued.empty()); // Pre-stream ticket.
    CHECK(despawn_attempts.at(101) == 1);
    CHECK(Run("npc.observe") == SCO_OK); Contains("removing=1");
    Unload(); Unload(); CHECK(removed == 1 && despawn_attempts.at(101) == 1);
    Reset(); api = Api(); CHECK(Load(&api, owner) == SCO_OK); CHECK(SpawnOne() == SCO_OK);
    remove_result = SCO_WRONG_THREAD;
    CHECK(RemoveId(101) == SCO_WRONG_THREAD); CHECK(removed == 1);
    remove_result = SCO_OK; Unload(); Unload(); CHECK(removed == 2);
    Reset(); api = Api(); CHECK(Load(&api, owner) == SCO_OK); CHECK(SpawnOne() == SCO_OK);
    missing_cap = "game.actors.despawn"; Unload(); Unload();
    CHECK(removed == 0 && despawn_attempts.empty() && issued.count(101) == 1);
    // Cleanup refusal retains the provider's ticket; host owner-release is not exercised here.
}
void LocationNegatives() {
    Reset(); auto api = Api(); using namespace entry;
    CHECK(Load(&api, owner) == SCO_OK);
    pose_zone = 0; CHECK(SpawnOne() == SCO_FAILED && spawned == 0); pose_zone = 91;
    pose_result = 0; CHECK(SpawnOne() == SCO_FAILED && spawned == 0); pose_result = 1;
    for (double bad : {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        pose_pos[0] = bad; CHECK(SpawnOne() == SCO_BAD_ARG && spawned == 0);
        pose_pos[0] = 1; pose_rot[1] = bad; CHECK(SpawnOne() == SCO_BAD_ARG && spawned == 0); pose_rot[1]=0;
    }
    pose_rot[3] = 0; CHECK(SpawnOne() == SCO_BAD_ARG && spawned == 0); pose_rot[3] = 1;
    spawn_id = 0; CHECK(SpawnOne() == SCO_FAILED); Unload(); CHECK(removed == 0);
}
void ProviderAndMemberNegatives() {
    Reset(); auto api = Api(); using namespace entry;
    CHECK(Load(&api, owner) == SCO_OK);
    publish_actors = false; CHECK(SpawnOne() == SCO_UNAVAILABLE); publish_actors = true;
    publish_spatial = false; CHECK(SpawnOne() == SCO_UNAVAILABLE); publish_spatial = true;
    for (const char* cap : {"game.actors.spawn_npc", "game.actors.despawn"}) {
        missing_cap = cap; CHECK(SpawnOne() == SCO_UNAVAILABLE && spawned == 0);
    }
    missing_cap = nullptr;
    const auto actor_size = actors.size;
    for (uint32_t n = 0; n < offsetof(sc_actors_v1, despawn) + sizeof(actors.despawn); ++n) {
        actors.size = n; CHECK(SpawnOne() == SCO_UNAVAILABLE && spawned == 0);
    }
    actors.size = actor_size;
    actors.spawn_npc = nullptr; CHECK(SpawnOne() == SCO_UNAVAILABLE); actors.spawn_npc = Spawn;
    actors.despawn = nullptr; CHECK(SpawnOne() == SCO_UNAVAILABLE); actors.despawn = Remove;
    for (uint32_t n = 0; n < offsetof(sc_spatial_v1, player_pose) + sizeof(spatial.player_pose); ++n) {
        spatial.size = n; CHECK(SpawnOne() == SCO_UNAVAILABLE && pose_calls == 0);
    }
    spatial.size = sizeof(spatial); spatial.player_pose = nullptr; CHECK(SpawnOne() == SCO_UNAVAILABLE); spatial.player_pose = Pose;
    CHECK(SpawnOne() == SCO_OK);
    publish_entities = false; CHECK(Run("npc.observe") == SCO_UNAVAILABLE); publish_entities = true;
    missing_cap = "game.entities.transform"; CHECK(Run("npc.observe") == SCO_UNAVAILABLE); missing_cap = nullptr;
    for (uint32_t n = 0; n < offsetof(sc_entities_v1, get_transform) + sizeof(entities.get_transform); ++n) {
        entities.size = n; CHECK(Run("npc.observe") == SCO_UNAVAILABLE);
    }
    entities.size = sizeof(entities); entities.alive = nullptr; CHECK(Run("npc.observe") == SCO_UNAVAILABLE); entities.alive = Alive;
    entities.get_transform = nullptr; CHECK(Run("npc.observe") == SCO_UNAVAILABLE); entities.get_transform = Transform;
    streamed = true; pose_pos[0] = std::numeric_limits<double>::infinity();
    CHECK(Run("npc.observe") == SCO_OK); Contains("unknown=1"); Contains("visible=0");
    pose_pos[0] = 1; transform_result = SCO_WRONG_THREAD;
    CHECK(Run("npc.observe") == SCO_OK); Contains("unknown=1");
    actors.size = offsetof(sc_actors_v1, despawn) + sizeof(actors.despawn) - 1;
    CHECK(RemoveId(101) == SCO_UNAVAILABLE && removed == 0); actors.size = sizeof(actors);
    Unload(); CHECK(removed == 1);
}
void HostAndRegistrationNegatives() {
    Reset(); using namespace entry;
    auto api = Api(); CHECK(Load(nullptr, owner) == SCO_BAD_ARG); CHECK(Load(&api, nullptr) == SCO_BAD_ARG);
    for (uint32_t n = 0; n < offsetof(sco_api, query_service) + sizeof(api.query_service); ++n) {
        api.size = n; CHECK(Load(&api, owner) == SCO_UNAVAILABLE && registrations == 0);
    }
    api = Api(); api.query_service = nullptr; CHECK(Load(&api, owner) == SCO_UNAVAILABLE);
    api = Api(); api.has = nullptr; CHECK(Load(&api, owner) == SCO_UNAVAILABLE);
    api = Api(); api.log = nullptr; CHECK(Load(&api, owner) == SCO_UNAVAILABLE);
    api = Api(); api.register_command = nullptr; CHECK(Load(&api, owner) == SCO_UNAVAILABLE);
    api = Api(); api.major = 2; CHECK(Load(&api, owner) == SCO_UNAVAILABLE);
    api = Api(); api.minor = 0; CHECK(Load(&api, owner) == SCO_UNAVAILABLE);
    for (int n = 0; n < 4; ++n) {
        Reset(); api = Api(); fail_registration = n;
        CHECK(Load(&api, owner) == SCO_FAILED); Unload(); Unload();
        commands.clear(); fail_registration = -1; CHECK(Load(&api, owner) == SCO_OK); Unload();
    }
}
void BoundedPartialAndHighId() {
    Reset(); auto api = Api(); using namespace entry;
    CHECK(Load(&api, owner) == SCO_OK);
    spawn_id = 0x8000000000000065ULL;
    CHECK(SpawnOne() == SCO_OK);
    CHECK(RemoveId(std::bit_cast<int64_t>(spawn_id)) == SCO_OK); CHECK(removed == 1); Unload();
    Reset(); api = Api(); auto_ids = true; fail_spawn_at = 2;
    CHECK(Load(&api, owner) == SCO_OK);
    sco_arg args[2]{}; args[0].type = SCO_ARG_STRING; args[0].v.s = "synthetic_actor";
    args[1].type = SCO_ARG_INT; args[1].v.i = 2;
    CHECK(Run("npc.spawn", args, 2) == SCO_FAILED); Contains("1 of 2");
    Unload(); CHECK(removed == 1);
    Reset(); api = Api(); auto_ids = true; CHECK(Load(&api, owner) == SCO_OK);
    for (uint32_t n = 0; n < SC_ACTORS_MAX_NPCS; ++n) CHECK(SpawnOne() == SCO_OK);
    CHECK(SpawnOne() == SCO_TOO_MANY && spawned == static_cast<int>(SC_ACTORS_MAX_NPCS));
    CHECK(Run("npc.clear") == SCO_OK && removed == static_cast<int>(SC_ACTORS_MAX_NPCS));
    CHECK(issued.empty() && despawn_attempts.size() == SC_ACTORS_MAX_NPCS && !streamed);
    CHECK(Run("npc.clear") == SCO_OK && removed == static_cast<int>(SC_ACTORS_MAX_NPCS));
    CHECK(SpawnOne() == SCO_TOO_MANY && spawned == static_cast<int>(SC_ACTORS_MAX_NPCS)); // Retained tickets still consume quota.
    Unload(); Unload(); CHECK(issued.empty() && removed == static_cast<int>(SC_ACTORS_MAX_NPCS));
    for (const auto& [id, attempts] : despawn_attempts) {
        CHECK(id >= 101 && id < 101 + SC_ACTORS_MAX_NPCS && attempts == 1);
    }
    CheckForeignAndCrewUntouched();
}
void CommandNegatives() {
    Reset(); auto api = Api(); using namespace entry;
    CHECK(Load(&api, owner) == SCO_OK);
    CHECK(Run("npc.spawn") == SCO_BAD_ARG); CHECK(RemoveId(0) == SCO_BAD_ARG);
    auto cmd = commands.at("npc.spawn");
    CHECK(cmd.fn(nullptr, 2, cmd.ctx, nullptr, 32) == SCO_BAD_ARG);
    sco_arg args[2]{}; args[0].type = SCO_ARG_STRING; args[0].v.s = "synthetic_actor"; args[1].type = SCO_ARG_INT;
    args[1].v.i=11; CHECK(Run("npc.spawn", args, 2) == SCO_BAD_ARG);
    args[1].v.i=1; args[0].type=SCO_ARG_INT; CHECK(Run("npc.spawn", args, 2) == SCO_BAD_ARG);
    args[0].type=SCO_ARG_STRING; args[0].v.s=""; CHECK(Run("npc.spawn", args, 2) == SCO_BAD_ARG);
    CHECK(spawned == 0);
    spawn_result = SCO_FAILED; CHECK(SpawnOne() == SCO_FAILED); CHECK(removed == 0);
    spawn_result = SCO_OK; CHECK(SpawnOne() == SCO_OK);
    CHECK(SpawnOne() == SCO_FAILED); // A duplicate live session id cannot be tracked twice.
    CHECK(Run("npc.clear") == SCO_OK); CHECK(removed == 1);
    CHECK(Run("npc.clear") == SCO_OK); CHECK(removed == 1);
    Unload();
}
}
int main(int argc, char** argv) {
#ifdef WORKFLOW_DYNAMIC_TEST
    test_module::Open(argc, argv);
#else
    (void)argc; (void)argv;
#endif
    Lifecycle(); ClearAndUnloadOwnershipIsolation(); DeferredRemovalAndCleanup(); LocationNegatives(); ProviderAndMemberNegatives();
    HostAndRegistrationNegatives(); CommandNegatives(); BoundedPartialAndHighId();
#ifdef NPC_EXTERNAL_TEST
    Reset(); auto api = Api();
    CHECK(sco_plugin_query() == sco::features::npc::Query());
    CHECK(sco_plugin_load(&api, owner) == SCO_OK); CHECK(SpawnOne() == SCO_OK);
    sco_plugin_unload(); sco_plugin_unload(); CHECK(removed == 1);
    std::puts("npc_workflow: 9 cases passed (including external adapter)");
#else
    std::puts("npc_workflow: 8 cases passed");
#ifdef WORKFLOW_DYNAMIC_TEST
    test_module::Close();
#endif
#endif
}
