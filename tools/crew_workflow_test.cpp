// SPDX-License-Identifier: GPL-3.0-only
// Actual loaded consumer; all IDs and public providers are synthetic.
#include "workflow_module.h"
#include "sc_actors.h"
#include "sc_spatial.h"
#include "sc_vehicles.h"
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
namespace {
std::map<std::string,sco_command> commands;
int tag;
sco_plugin* owner = reinterpret_cast<sco_plugin*>(&tag);
sco_event_fn tick = nullptr;
int queries = 0;
bool published = false, player = true, partial = false, unknown_usable = false, accept_link = true;
const char* missing_cap = nullptr;
sco_result seat_result = SCO_OK, remove_result = SCO_OK, seats_result = SCO_OK;
int fail_spawn_at = -1;
const void* actors_override = nullptr;
const void* vehicles_override = nullptr;
const void* spatial_override = nullptr;
int spawned = 0, removed = 0, seated = 0, ejected = 0, powered = 0;
std::map<uint64_t,bool> owned;
uint64_t occupants[3]{};
sc_actors_v1 actors{};
sc_spatial_v1 spatial{};
sc_vehicles_v1 vehicles{};
sco_result Player(uint64_t* id,uint64_t*) { *id = player ? 700u : 0u; return player ? SCO_OK : SCO_NOT_FOUND; }
sco_result Ship(uint64_t* id) { *id = 800; return SCO_OK; }
sco_result Seats(uint64_t ship,sc_vehicle_seat* out,uint32_t max,uint32_t* n,uint32_t* more) {
    CHECK(ship == 800); *n = max ? (partial ? 1u : 3u) : 0u; *more = partial || !max;
    for (uint32_t i = 0; i < *n; ++i) {
        out[i] = {}; out[i].index = i; out[i].flags = SC_SEAT_USABLE_KNOWN | (i == 2 ? 0u : SC_SEAT_USABLE);
        if (unknown_usable) out[i].flags &= ~SC_SEAT_USABLE_KNOWN;
        out[i].occupant_id = occupants[i]; if (occupants[i]) out[i].flags |= SC_SEAT_OCCUPIED;
        std::snprintf(out[i].name,sizeof(out[i].name),"pilot %u",i);
    }
    return seats_result;
}
sco_result Occupant(uint64_t ship,uint32_t index,uint64_t* id) { CHECK(ship == 800 && index < 3); *id = occupants[index]; return SCO_OK; }
sco_result Seat(sco_plugin* self,uint64_t id,uint64_t ship,uint32_t index) {
    CHECK(self == owner && ship == 800 && index < 2); CHECK(id == 700 || owned[id]);
    CHECK(!occupants[index] || occupants[index] == id); ++seated; if (accept_link && seat_result == SCO_OK) occupants[index] = id; return seat_result;
}
sco_result Eject(sco_plugin* self,uint64_t id) {
    CHECK(self == owner); CHECK(id == 700 || owned[id]); ++ejected;
    for (auto& in : occupants) if (in == id) in = 0; return SCO_OK;
}
sco_result Power(sco_plugin* self,uint64_t ship) { CHECK(self == owner && ship == 800); ++powered; return SCO_OK; }
int Pose(double p[3],double q[4],uint64_t* z) { if (!player) return 0; p[0]=1; p[1]=2; p[2]=3; q[0]=q[1]=q[2]=0; q[3]=1; *z=900; return 1; }
sco_result Spawn(sco_plugin* self,const char* name,uint64_t zone,const double*,uint64_t* id) {
    CHECK(self == owner && zone == 900 && !std::strcmp(name,"synthetic_actor")); *id = 1000u + static_cast<uint64_t>(++spawned); if (spawned == fail_spawn_at) return SCO_FAILED; owned[*id] = true; return SCO_OK;
}
sco_result Remove(sco_plugin* self,uint64_t id) { CHECK(self == owner && owned[id]); ++removed; if (remove_result == SCO_OK) owned[id] = false; return remove_result; }
void Providers() {
    published = player = accept_link = true; partial = unknown_usable = false;
    missing_cap = nullptr; fail_spawn_at = -1; seat_result = remove_result = seats_result = SCO_OK;
    actors_override = vehicles_override = spatial_override = nullptr; spawned = removed = seated = ejected = powered = 0;
    owned.clear(); for (auto& in : occupants) in = 0;
    actors = {}; actors.size = sizeof(actors); actors.local_player = Player; actors.spawn_npc = Spawn; actors.despawn = Remove;
    spatial = {}; spatial.size = sizeof(spatial); spatial.player_pose = Pose;
    vehicles = {}; vehicles.size = sizeof(vehicles); vehicles.player_ship = Ship; vehicles.seats = Seats;
    vehicles.seat_occupant = Occupant; vehicles.seat = Seat; vehicles.eject = Eject; vehicles.power_on = Power;
}
sco_result Query(const char* name, uint32_t version, const void** out) {
    CHECK(version == 0x10000u); ++queries; *out = nullptr;
    if (published) {
        if (!std::strcmp(name,SC_ACTORS_NAME)) *out = actors_override ? actors_override : &actors;
        if (!std::strcmp(name,SC_SPATIAL_SERVICE_NAME)) *out = spatial_override ? spatial_override : &spatial;
        if (!std::strcmp(name,SC_VEHICLES_SERVICE_NAME)) *out = vehicles_override ? vehicles_override : &vehicles;
    }
    return *out ? SCO_OK : SCO_NOT_FOUND;
}
int Has(const char* name) { return !missing_cap || std::strcmp(name,missing_cap); }
void Log(sco_plugin* self, sco_log_level, const char*) { CHECK(self == owner); }
sco_result Register(sco_plugin* self, const sco_command* c) {
    CHECK(self == owner); CHECK(c->ctx); commands[c->name] = *c; return SCO_OK;
}
sco_result Subscribe(sco_plugin* self, const char* event, sco_event_fn fn, void*) {
    CHECK(self == owner); CHECK(!std::strcmp(event,"tick")); tick = fn; return SCO_OK;
}
sco_result Unsubscribe(sco_plugin* self, const char* event, sco_event_fn fn) {
    CHECK(self == owner); CHECK(!std::strcmp(event,"tick")); CHECK(tick == fn); tick = nullptr; return SCO_OK;
}
sco_api Api() {
    sco_api api{}; api.size = sizeof(api); api.major = 1; api.minor = 1;
    api.has = Has; api.log = Log; api.register_command = Register; api.query_service = Query;
    api.subscribe = Subscribe; api.unsubscribe = Unsubscribe; return api;
}
std::string reply;
sco_result Run(const char* name, const char* text = nullptr) {
    auto c = commands.at(name); char out[512]{};
    sco_arg arg{}; arg.type = SCO_ARG_STRING; arg.v.s = text;
    auto r = c.fn(text ? &arg : nullptr,text ? 1u : 0u,c.ctx,out,sizeof(out)); reply = out; return r;
}
void Missing() {
    auto api = Api(); CHECK(test_module::Load(&api,owner) == SCO_OK);
    CHECK(commands.size() == 6); CHECK(tick);
    for (auto name : {"crew.target","crew.clear","crew.power_on","crew.stand_all"})
        CHECK(Run(name) == SCO_UNAVAILABLE);
    CHECK(Run("crew.fill","synthetic_actor") == SCO_UNAVAILABLE);
    CHECK(Run("crew.sit","pilot") == SCO_UNAVAILABLE); CHECK(queries > 0);
    test_module::Unload(); CHECK(!tick); test_module::Unload();
    // The host, not the export, owns command withdrawal.
    commands.clear(); CHECK(test_module::Load(&api,owner) == SCO_OK);
    CHECK(Run("crew.target") == SCO_UNAVAILABLE); test_module::Unload(); commands.clear();
    std::puts("PASS missing providers / command withdrawal stand-in / reload / unload twice");
}
void Jobs() {
    Providers(); auto api = Api(); CHECK(test_module::Load(&api,owner) == SCO_OK);
    uint32_t initial = 0; tick("tick",&initial,nullptr);
    CHECK(Run("crew.target") == SCO_OK); CHECK(Run("crew.power_on") == SCO_OK); CHECK(powered == 1);
    CHECK(Run("crew.fill","synthetic_actor") == SCO_OK); CHECK(spawned == 2);
    uint32_t now = 1000; tick("tick",&now,nullptr); CHECK(seated == 2);
    now = 3000; tick("tick",&now,nullptr); CHECK(seated == 2);
    CHECK(Run("crew.fill","synthetic_actor") == SCO_FAILED); CHECK(spawned == 2);
    CHECK(Run("crew.stand_all") == SCO_OK); CHECK(ejected == 2);
    CHECK(Run("crew.sit","PILOT 0") == SCO_OK); now = 4000; tick("tick",&now,nullptr); CHECK(occupants[0] == 700);
    CHECK(Run("crew.clear") == SCO_OK); CHECK(removed == 2); CHECK(occupants[0] == 700);
    CHECK(Run("crew.clear") == SCO_OK); CHECK(removed == 2);
    test_module::Unload(); test_module::Unload(); CHECK(!tick); commands.clear();
    std::puts("PASS target / Flight Ready / fill / seat confirmation / stand / player sit / owned clear");
}
void Reservation() {
    Providers(); auto api = Api(); CHECK(test_module::Load(&api,owner) == SCO_OK);
    CHECK(Run("crew.fill","synthetic_actor") == SCO_OK);
    CHECK(Run("crew.sit","pilot 0") == SCO_FAILED);
    CHECK(reply.find("reserved") != std::string::npos);
    test_module::Unload(); commands.clear(); std::puts("PASS player seat refuses pending crew reservation");
}
void End() {
    const int calls = removed;
    test_module::Unload(); CHECK(!tick); test_module::Unload(); CHECK(removed == calls);
    commands.clear(); // Host command withdrawal stand-in.
    // Public actors contract's host owner-release stand-in, not real game cleanup.
    for (auto& item : owned) item.second = false;
}
void Guards() {
    Providers(); auto api = Api(); CHECK(test_module::Load(&api,owner) == SCO_OK);
    const auto check = [&](const void* whole,size_t bytes,size_t offset,size_t width,const void*& override,const char* cmd,const char* arg) {
        for (int mode : {0,1}) {
            const size_t length = mode ? bytes : offset + width - 1;
            void* memory = std::malloc(length); CHECK(memory); std::memcpy(memory,whole,length);
            const auto size = static_cast<uint32_t>(length); std::memcpy(memory,&size,sizeof(size));
            if (mode) std::memset(static_cast<char*>(memory)+offset,0,width);
            override = memory; CHECK(Run(cmd,arg) == SCO_UNAVAILABLE); override = nullptr; std::free(memory);
            CHECK(spawned == 0 && removed == 0 && seated == 0 && ejected == 0 && powered == 0);
        }
    };
#define GUARD(table,type,member,override,cmd,arg) check(&table,sizeof(table),offsetof(type,member),sizeof(table.member),override,cmd,arg)
    GUARD(vehicles,sc_vehicles_v1,player_ship,vehicles_override,"crew.fill","synthetic_actor");
    GUARD(vehicles,sc_vehicles_v1,seats,vehicles_override,"crew.fill","synthetic_actor");
    GUARD(vehicles,sc_vehicles_v1,seat_occupant,vehicles_override,"crew.fill","synthetic_actor");
    GUARD(vehicles,sc_vehicles_v1,seat,vehicles_override,"crew.fill","synthetic_actor");
    GUARD(vehicles,sc_vehicles_v1,eject,vehicles_override,"crew.stand_all",nullptr);
    GUARD(vehicles,sc_vehicles_v1,power_on,vehicles_override,"crew.power_on",nullptr);
    GUARD(actors,sc_actors_v1,local_player,actors_override,"crew.sit","pilot 0");
    GUARD(actors,sc_actors_v1,spawn_npc,actors_override,"crew.fill","synthetic_actor");
    GUARD(actors,sc_actors_v1,despawn,actors_override,"crew.fill","synthetic_actor");
    GUARD(spatial,sc_spatial_v1,player_pose,spatial_override,"crew.fill","synthetic_actor");
#undef GUARD
    End();
#define API_GUARD(member) do { \
    api = Api(); api.member = nullptr; CHECK(test_module::Load(&api,owner) == SCO_UNAVAILABLE); \
    api = Api(); const size_t size = offsetof(sco_api,member) + sizeof(api.member)-1; \
    auto* memory = static_cast<sco_api*>(std::malloc(size)); CHECK(memory); std::memcpy(memory,&api,size); memory->size = static_cast<uint32_t>(size); \
    CHECK(test_module::Load(memory,owner) == SCO_UNAVAILABLE); std::free(memory); \
} while (0)
    API_GUARD(has); API_GUARD(log); API_GUARD(query_service); API_GUARD(register_command); API_GUARD(subscribe); API_GUARD(unsubscribe);
#undef API_GUARD
    test_module::Unload(); commands.clear();
    std::puts("PASS full-null / physically truncated methods: vehicles 6, actors 3, spatial 1, base API 6");
}
void PartialForeignNoPlayer() {
    Providers(); partial = true; auto api = Api(); CHECK(test_module::Load(&api,owner) == SCO_OK);
    CHECK(Run("crew.fill","synthetic_actor") == SCO_OK); CHECK(spawned == 1); CHECK(reply.find("partial seats=1") != std::string::npos);
    CHECK(Run("crew.clear") == SCO_OK); CHECK(removed == 1); End();
    Providers(); unknown_usable = true; CHECK(test_module::Load(&api,owner) == SCO_OK);
    CHECK(Run("crew.fill","synthetic_actor") == SCO_FAILED); CHECK(spawned == 0);
    CHECK(Run("crew.sit","pilot") == SCO_FAILED); End();
    Providers(); occupants[0] = 6001; occupants[1] = 6002; CHECK(test_module::Load(&api,owner) == SCO_OK);
    CHECK(Run("crew.fill","synthetic_actor") == SCO_FAILED); CHECK(spawned == 0);
    CHECK(Run("crew.sit","pilot 0") == SCO_FAILED);
    CHECK(Run("crew.stand_all") == SCO_OK); CHECK(ejected == 0);
    CHECK(Run("crew.clear") == SCO_OK); CHECK(removed == 0); CHECK(occupants[0] == 6001 && occupants[1] == 6002); End();
    Providers(); player = false; CHECK(test_module::Load(&api,owner) == SCO_OK);
    CHECK(Run("crew.fill","synthetic_actor") == SCO_FAILED); CHECK(Run("crew.sit","pilot") == SCO_NOT_FOUND); CHECK(spawned == 0); End();
    Providers(); fail_spawn_at = 2; CHECK(test_module::Load(&api,owner) == SCO_OK);
    CHECK(Run("crew.fill","synthetic_actor") == SCO_FAILED); CHECK(reply.find("Seating 1 owned") != std::string::npos);
    CHECK(Run("crew.clear") == SCO_OK); CHECK(removed == 1); End();
    std::puts("PASS partial seats / unknown-unusable / foreign NPCs untouched / no player / partial spawn");
}
void ReopenTimeoutPending() {
    Providers(); auto api = Api(); CHECK(test_module::Load(&api,owner) == SCO_OK);
    uint32_t initial = 0; tick("tick",&initial,nullptr);
    CHECK(Run("crew.fill","synthetic_actor") == SCO_OK);
    published = false; CHECK(Run("crew.power_on") == SCO_UNAVAILABLE);
    uint32_t now = 1000; tick("tick",&now,nullptr); CHECK(seated == 0);
    published = true; tick("tick",&now,nullptr); CHECK(seated == 2);
    published = false; CHECK(Run("crew.clear") == SCO_UNAVAILABLE); CHECK(removed == 0);
    published = true; CHECK(Run("crew.clear") == SCO_OK); CHECK(removed == 2); End();
    Providers(); accept_link = false; seat_result = SCO_WRONG_THREAD; remove_result = SCO_WRONG_THREAD;
    CHECK(test_module::Load(&api,owner) == SCO_OK); initial = 0; tick("tick",&initial,nullptr); CHECK(Run("crew.fill","synthetic_actor") == SCO_OK);
    now = 1000; tick("tick",&now,nullptr); CHECK(seated == 2 && removed == 0);
    now = 30000; tick("tick",&now,nullptr); CHECK(removed == 2); const auto calls = seated;
    remove_result = SCO_OK; now = 32000; tick("tick",&now,nullptr); CHECK(removed == 4 && seated == calls);
    now = 34000; tick("tick",&now,nullptr); CHECK(removed == 4);
    CHECK(Run("crew.clear") == SCO_OK); CHECK(removed == 4); CHECK(reply.find("pending tickets=2") != std::string::npos); End();
    // Accepted removals never make room in the finite session ticket budget.
    Providers(); CHECK(test_module::Load(&api,owner) == SCO_OK);
    for (int batch = 0; batch < 32; ++batch) { CHECK(Run("crew.fill","synthetic_actor") == SCO_OK); CHECK(Run("crew.clear") == SCO_OK); }
    CHECK(spawned == 64 && removed == 64); CHECK(Run("crew.fill","synthetic_actor") == SCO_TOO_MANY); CHECK(spawned == 64); End();
    std::puts("PASS failed service reopen / tick deadline / wrong-thread pending removal retry / no ticket recycling / owner-release stand-in");
}
void BadInputsAndCapabilities() {
    Providers(); auto api = Api(); CHECK(test_module::Load(&api,owner) == SCO_OK);
    CHECK(Run("crew.fill") == SCO_BAD_ARG); CHECK(Run("crew.sit") == SCO_BAD_ARG);
    CHECK(Run("crew.target","extra") == SCO_BAD_ARG);
    CHECK(Run("crew.sit","   ") == SCO_FAILED);
    for (auto cap : {"game.vehicles.seats","game.vehicles.seat","game.actors.spawn_npc","game.actors.despawn"}) {
        missing_cap = cap; CHECK(Run("crew.fill","synthetic_actor") == SCO_UNAVAILABLE); CHECK(spawned == 0);
    }
    missing_cap = "game.actors.local_player"; CHECK(Run("crew.sit","pilot") == SCO_UNAVAILABLE);
    missing_cap = "game.vehicles.flight_ready"; CHECK(Run("crew.power_on") == SCO_UNAVAILABLE);
    CHECK(powered == 0); missing_cap = nullptr; seats_result = SCO_WRONG_THREAD;
    CHECK(Run("crew.fill","synthetic_actor") == SCO_WRONG_THREAD); CHECK(spawned == 0); End();
    std::puts("PASS invalid input / missing capability / provider wrong-thread propagation");
}
void EpochBeforeFirstTick() {
    Providers(); auto api = Api(); CHECK(test_module::Load(&api,owner) == SCO_OK);
    CHECK(Run("crew.fill","synthetic_actor") == SCO_OK);
    uint32_t now = 100000; tick("tick",&now,nullptr); CHECK(removed == 0);
    now = 100750; tick("tick",&now,nullptr); CHECK(seated == 2);
    End();
}
void FifthLink() {
    Providers(); accept_link = false; auto api = Api(); CHECK(test_module::Load(&api,owner) == SCO_OK);
    uint32_t initial = 0; tick("tick",&initial,nullptr);
    CHECK(Run("crew.fill","synthetic_actor") == SCO_OK);
    for (uint32_t now : {1000u,3000u,5000u,7000u,9000u}) tick("tick",&now,nullptr);
    CHECK(seated == 10); occupants[0] = 1001; occupants[1] = 1002;
    uint32_t now = 11000; tick("tick",&now,nullptr); CHECK(removed == 0);
    test_module::Unload(); commands.clear(); std::puts("PASS fifth accepted link confirmed before link budget cleanup");
}
}
int main(int argc,char** argv) {
    test_module::Open(argc,argv); CHECK(!std::strcmp(test_module::Query()->name,"crew"));
    Missing(); Jobs(); FifthLink(); Reservation(); Guards(); PartialForeignNoPlayer(); ReopenTimeoutPending(); BadInputsAndCapabilities(); EpochBeforeFirstTick(); test_module::Close(); return 0;
}
