// SPDX-License-Identifier: GPL-3.0-only
#include "crew.h"
#include "sc_actors.h"
#include "sc_spatial.h"
#include "sc_vehicles.h"
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cmath>
#include <limits>
namespace sco::features::crew {
namespace {
#define HAS_FN(t, type, member) ((t) && (t)->size >= offsetof(type,member) + sizeof((t)->member) && (t)->member)
enum class Phase { empty, seating, seated, remove_pending, removing };
struct Job {
    Phase phase = Phase::empty;
    bool player = false;
    uint64_t actor = 0, ship = 0;
    uint32_t seat = 0, since = 0, next = 0, links = 0;
};
struct State {
    const sco_api* api = nullptr;
    sco_plugin* self = nullptr;
    bool subscribed = false;
    bool clock_known = false;
    uint64_t pinned = 0;
    uint32_t now = 0;
    Job jobs[64]{};
} g;
const sco_plugin_info info{sizeof(sco_plugin_info),SCO_API_MAJOR,SCO_API_MINOR,"crew","1.0.0","sc-offline"};
template<class T> const T* Open(const char* name) {
    const void* out = nullptr;
    if (!HAS_FN(g.api,sco_api,query_service) || g.api->query_service(name,0x10000u,&out) != SCO_OK) return nullptr;
    return static_cast<const T*>(out);
}
sco_result Answer(sco_result r,char* reply,uint32_t size,const char* text) {
    if (reply && size) std::snprintf(reply,size,"%s",text);
    if (HAS_FN(g.api,sco_api,log)) g.api->log(g.self,r == SCO_OK ? SCO_LOG_INFO : SCO_LOG_WARN,text);
    return r;
}
bool Cap(const char* name) { return g.api && g.api->has(name); }
sco_result Validate(const sco_arg* args,uint32_t nargs,bool string,char* reply,uint32_t size) {
    if (!g.api || !g.self) return SCO_UNAVAILABLE;
    if ((!reply && size) || nargs != (string ? 1u : 0u) ||
        (string && (!args || args[0].type != SCO_ARG_STRING || !args[0].v.s || !*args[0].v.s))) return SCO_BAD_ARG;
    return SCO_OK;
}
Job* Free() { for (auto& j : g.jobs) if (j.phase == Phase::empty) return &j; return nullptr; }
Job* Actor(uint64_t id) { for (auto& j : g.jobs) if (j.phase != Phase::empty && j.actor == id) return &j; return nullptr; }
bool Owned(const Job* j) { return j && !j->player && (j->phase == Phase::seating || j->phase == Phase::seated); }
bool Reserved(uint64_t ship,uint32_t seat) {
    for (const auto& j : g.jobs) if (j.phase == Phase::seating && j.ship == ship && j.seat == seat) return true;
    return false;
}
bool Usable(const sc_vehicle_seat& s) { return (s.flags & (SC_SEAT_USABLE | SC_SEAT_USABLE_KNOWN)) == (SC_SEAT_USABLE | SC_SEAT_USABLE_KNOWN); }
sco_result Ship(const sc_vehicles_v1* v,uint64_t& ship) {
    if (!Cap("game.vehicles.seats") || !HAS_FN(v,sc_vehicles_v1,player_ship) || !HAS_FN(v,sc_vehicles_v1,seats)) return SCO_UNAVAILABLE;
    if (g.pinned) {
        uint32_t n = 0,more = 0;
        const auto r = v->seats(g.pinned,nullptr,0,&n,&more);
        if (r == SCO_OK) { ship = g.pinned; return SCO_OK; }
        if (r != SCO_NOT_FOUND) return r;
        g.pinned = 0;
    }
    const auto r = v->player_ship(&ship); return r == SCO_OK && !ship ? SCO_FAILED : r;
}
sco_result Seats(const sc_vehicles_v1* v,uint64_t& ship,sc_vehicle_seat (&seats)[128],uint32_t& count,uint32_t& more) {
    auto r = Ship(v,ship); if (r != SCO_OK) return r;
    r = v->seats(ship,seats,128,&count,&more);
    if (r == SCO_OK) {
        if (count > 128) return SCO_FAILED;
        for (uint32_t i = 0; i < count; ++i) seats[i].name[SC_VEHICLE_SEAT_NAME_MAX-1] = 0;
    }
    return r;
}
sco_result Remove(Job& j,const sc_actors_v1* actors) {
    if (j.player || j.phase == Phase::removing || j.phase == Phase::empty) return SCO_OK;
    j.phase = Phase::remove_pending;
    if (!Cap("game.actors.despawn") || !HAS_FN(actors,sc_actors_v1,despawn)) return SCO_UNAVAILABLE;
    const auto r = actors->despawn(g.self,j.actor);
    if (r == SCO_OK || r == SCO_NOT_FOUND) j.phase = Phase::removing;
    j.next = g.now + 1500u;
    // Accepted removal relinquishes authority, but not the bounded session ticket.
    return r == SCO_NOT_FOUND ? SCO_OK : r;
}
sco_result Player(const sc_actors_v1* a,uint64_t& id) {
    if (!Cap("game.actors.local_player") || !HAS_FN(a,sc_actors_v1,local_player)) return SCO_UNAVAILABLE;
    const auto r = a->local_player(&id,nullptr); return r == SCO_OK && !id ? SCO_NOT_FOUND : r;
}
bool Matches(const char* name,const char* words) {
    char seat[SC_VEHICLE_SEAT_NAME_MAX]{},text[48]{};
    std::snprintf(seat,sizeof(seat),"%s",name); std::snprintf(text,sizeof(text),"%s",words);
    for (auto& c : seat) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    bool word = false;
    for (char* p = text; *p;) {
        while (*p == ' ') ++p;
        if (!*p) break;
        char* start = p; while (*p && *p != ' ') ++p;
        if (*p) *p++ = 0;
        word = true; if (!std::strstr(seat,start)) return false;
    }
    return word;
}
sco_result Target(const sco_arg* args,uint32_t n,void*,char* reply,uint32_t size) {
    const auto valid = Validate(args,n,false,reply,size); if (valid != SCO_OK) return valid;
    const auto* v = Open<sc_vehicles_v1>(SC_VEHICLES_SERVICE_NAME); uint64_t ship = 0;
    if (!Cap("game.vehicles.seats") || !HAS_FN(v,sc_vehicles_v1,player_ship)) return Answer(SCO_UNAVAILABLE,reply,size,"game.vehicles unavailable");
    const auto r = v->player_ship(&ship);
    if (r != SCO_OK || !ship) return Answer(r == SCO_OK ? SCO_FAILED : r,reply,size,"No current ship");
    g.pinned = ship; return Answer(SCO_OK,reply,size,"Targeting current ship (session only)");
}
sco_result Power(const sco_arg* args,uint32_t n,void*,char* reply,uint32_t size) {
    const auto valid = Validate(args,n,false,reply,size); if (valid != SCO_OK) return valid;
    const auto* v = Open<sc_vehicles_v1>(SC_VEHICLES_SERVICE_NAME); uint64_t ship = 0;
    if (!Cap("game.vehicles.flight_ready") || !HAS_FN(v,sc_vehicles_v1,power_on)) return Answer(SCO_UNAVAILABLE,reply,size,"Flight Ready unavailable");
    auto r = Ship(v,ship); if (r == SCO_OK) r = v->power_on(g.self,ship);
    return Answer(r,reply,size,r == SCO_OK ? "Flight Ready requested" : "Flight Ready refused (ship authority/provider readiness)");
}
sco_result Fill(const sco_arg* args,uint32_t n,void*,char* reply,uint32_t size) {
    const auto valid = Validate(args,n,true,reply,size); if (valid != SCO_OK) return valid;
    if (std::strlen(args[0].v.s) > 127) return SCO_BAD_ARG;
    const auto* v = Open<sc_vehicles_v1>(SC_VEHICLES_SERVICE_NAME);
    const auto* a = Open<sc_actors_v1>(SC_ACTORS_NAME);
    const auto* sp = Open<sc_spatial_v1>(SC_SPATIAL_SERVICE_NAME);
    if (!Cap("game.vehicles.seat") || !Cap("game.actors.spawn_npc") || !Cap("game.actors.despawn") ||
        !HAS_FN(v,sc_vehicles_v1,seat) || !HAS_FN(v,sc_vehicles_v1,seat_occupant) ||
        !HAS_FN(a,sc_actors_v1,spawn_npc) || !HAS_FN(a,sc_actors_v1,despawn) || !HAS_FN(sp,sc_spatial_v1,player_pose))
        return Answer(SCO_UNAVAILABLE,reply,size,"Owned crew spawn/removal or seating unavailable");
    sc_vehicle_seat seats[128]{}; uint64_t ship = 0; uint32_t count = 0,more = 0;
    auto r = Seats(v,ship,seats,count,more); if (r != SCO_OK) return Answer(r,reply,size,"Ship seats unavailable");
    const double unknown = std::numeric_limits<double>::quiet_NaN();
    double pos[3] = {unknown,unknown,unknown},rot[4] = {}; uint64_t zone = 0;
    if (!sp->player_pose(pos,rot,&zone) || !zone) return Answer(SCO_FAILED,reply,size,"No fresh player pose");
    pos[0] += 1.5;
    for (auto x : pos) if (!std::isfinite(x)) return Answer(SCO_BAD_ARG,reply,size,"Invalid player position");
    uint32_t added = 0,skipped = 0; r = SCO_OK;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& s = seats[i];
        if (!Usable(s) || s.occupant_id || (s.flags & SC_SEAT_OCCUPIED) || Reserved(ship,s.index)) { ++skipped; continue; }
        auto* j = Free(); if (!j) { r = SCO_TOO_MANY; break; }
        uint64_t id = 0; r = a->spawn_npc(g.self,args[0].v.s,zone,pos,&id);
        if (r != SCO_OK) break;
        if (!id || Actor(id)) { r = SCO_FAILED; break; }
        *j = {Phase::seating,false,id,ship,s.index,g.now,g.now+750u,0}; ++added;
    }
    char text[192]; std::snprintf(text,sizeof(text),"Seating %u owned NPCs; skipped=%u; partial seats=%u; batch result=%u",added,skipped,more ? 1u : 0u,static_cast<unsigned>(r));
    return Answer(r != SCO_OK ? r : added || !count ? SCO_OK : SCO_FAILED,reply,size,text);
}
sco_result Sit(const sco_arg* args,uint32_t n,void*,char* reply,uint32_t size) {
    const auto valid = Validate(args,n,true,reply,size); if (valid != SCO_OK) return valid;
    if (std::strlen(args[0].v.s) > 47) return SCO_BAD_ARG;
    const auto* v = Open<sc_vehicles_v1>(SC_VEHICLES_SERVICE_NAME);
    const auto* a = Open<sc_actors_v1>(SC_ACTORS_NAME);
    if (!Cap("game.vehicles.seat") || !HAS_FN(v,sc_vehicles_v1,seat) || !HAS_FN(v,sc_vehicles_v1,seat_occupant)) return Answer(SCO_UNAVAILABLE,reply,size,"Player seating unavailable");
    uint64_t me = 0; auto r = Player(a,me); if (r != SCO_OK) return Answer(r,reply,size,"No local player actor");
    sc_vehicle_seat seats[128]{}; uint64_t ship = 0; uint32_t count = 0,more = 0;
    r = Seats(v,ship,seats,count,more); if (r != SCO_OK) return Answer(r,reply,size,"Ship seats unavailable");
    for (uint32_t i = 0; i < count; ++i) {
        const auto& s = seats[i]; if (!Matches(s.name,args[0].v.s) || !Usable(s)) continue;
        if (s.occupant_id == me) return Answer(SCO_OK,reply,size,"Already seated");
        const auto* mine = Actor(me);
        if (Reserved(ship,s.index) && (!mine || mine->ship != ship || mine->seat != s.index))
            return Answer(SCO_FAILED,reply,size,"Seat reserved by a pending crew job; clear or wait first");
        auto* crew = Actor(s.occupant_id);
        if ((s.occupant_id || (s.flags & SC_SEAT_OCCUPIED)) && !Owned(crew)) return Answer(SCO_FAILED,reply,size,"Foreign/unknown occupant is not ours to move");
        auto* j = Actor(me); if (!j) j = Free(); if (!j) return Answer(SCO_TOO_MANY,reply,size,"Crew session ticket budget full");
        if (Owned(crew)) { r = Remove(*crew,a); if (r != SCO_OK) return Answer(r,reply,size,"Owned crew removal refused; retained for retry"); }
        *j = {Phase::seating,true,me,ship,s.index,g.now,g.now,0};
        return Answer(SCO_OK,reply,size,"Player seat request queued");
    }
    return Answer(SCO_FAILED,reply,size,"No matching usable seat in copied batch");
}
sco_result Stand(const sco_arg* args,uint32_t n,void*,char* reply,uint32_t size) {
    const auto valid = Validate(args,n,false,reply,size); if (valid != SCO_OK) return valid;
    const auto* v = Open<sc_vehicles_v1>(SC_VEHICLES_SERVICE_NAME);
    const auto* a = Open<sc_actors_v1>(SC_ACTORS_NAME);
    if (!Cap("game.vehicles.seat") || !HAS_FN(v,sc_vehicles_v1,eject)) return Answer(SCO_UNAVAILABLE,reply,size,"Eject unavailable");
    uint64_t me = 0; (void)Player(a,me);
    sc_vehicle_seat seats[128]{}; uint64_t ship = 0; uint32_t count = 0,more = 0;
    auto r = Seats(v,ship,seats,count,more); if (r != SCO_OK) return Answer(r,reply,size,"Ship seats unavailable");
    for (auto& j : g.jobs) if (j.ship == ship && j.phase == Phase::seating) { if (j.player) j = {}; else j.phase = Phase::seated; }
    uint32_t up = 0,foreign = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const auto id = seats[i].occupant_id;
        if (id && (id == me || Owned(Actor(id)))) { const auto e = v->eject(g.self,id); if (e == SCO_OK) ++up; else r = e; }
        else if (id || (seats[i].flags & SC_SEAT_OCCUPIED)) ++foreign;
    }
    char text[128]; std::snprintf(text,sizeof(text),"Stood up %u; foreign/unknown skipped=%u; partial seats=%u",up,foreign,more ? 1u : 0u);
    return Answer(r,reply,size,text);
}
sco_result Clear(const sco_arg* args,uint32_t n,void*,char* reply,uint32_t size) {
    const auto valid = Validate(args,n,false,reply,size); if (valid != SCO_OK) return valid;
    const auto* v = Open<sc_vehicles_v1>(SC_VEHICLES_SERVICE_NAME); uint64_t ship = 0;
    auto r = Ship(v,ship); if (r != SCO_OK) return Answer(r,reply,size,"Ship seats unavailable");
    const auto* a = Open<sc_actors_v1>(SC_ACTORS_NAME);
    uint32_t started = 0,pending = 0;
    for (auto& j : g.jobs) if (j.phase != Phase::empty && !j.player && j.ship == ship) {
        if (j.phase == Phase::removing) { ++pending; continue; }
        const auto removed = Remove(j,a); if (removed == SCO_OK) ++started; else r = removed;
    }
    char text[160]; std::snprintf(text,sizeof(text),"Removal started=%u; pending tickets=%u; world disappearance unconfirmed",started,pending);
    return Answer(r,reply,size,text);
}
void Tick(const char*,const void* data,void*) {
    if (!g.api || !data) return;
    g.now = *static_cast<const uint32_t*>(data);
    if (!g.clock_known) {
        // A reload's first command can precede its first tick. Never compare a
        // fresh job against the host process's absolute uptime from zero.
        for (auto& j : g.jobs) if (j.phase == Phase::seating) {
            j.since = g.now; j.next = g.now + (j.player ? 0u : 750u);
        }
        g.clock_known = true;
    }
    const auto* v = Open<sc_vehicles_v1>(SC_VEHICLES_SERVICE_NAME);
    const auto* a = Open<sc_actors_v1>(SC_ACTORS_NAME);
    for (auto& j : g.jobs) {
        if (j.phase == Phase::remove_pending) {
            if (static_cast<int32_t>(g.now-j.next) >= 0) (void)Remove(j,a);
            continue;
        }
        if (j.phase != Phase::seating) continue;
        const bool readable = Cap("game.vehicles.seats") && Cap("game.vehicles.seat") &&
            HAS_FN(v,sc_vehicles_v1,seat_occupant) && HAS_FN(v,sc_vehicles_v1,seat);
        if (!readable || static_cast<int32_t>(g.now-j.next) < 0) {
            if (g.now-j.since >= 30000u) { if (j.player) j = {}; else (void)Remove(j,a); }
            continue;
        }
        uint64_t in = 0; const auto r = v->seat_occupant(j.ship,j.seat,&in);
        if (r == SCO_OK && in == j.actor) { j.phase = Phase::seated; continue; }
        if (r == SCO_NOT_FOUND || g.now-j.since >= 30000u || j.links >= 5u) { if (j.player) j = {}; else (void)Remove(j,a); continue; }
        // Unknown/foreign occupants are never evicted; retry until the deadline.
        if (r == SCO_OK && !in && v->seat(g.self,j.actor,j.ship,j.seat) == SCO_OK) ++j.links;
        j.next = g.now + 1500u;
    }
}
}
const sco_plugin_info* Query() { return &info; }
sco_result Load(const sco_api* api,sco_plugin* self) {
    if (!api || !self || g.api) return SCO_BAD_ARG;
    if (api->major != SCO_API_MAJOR || api->minor < SCO_API_MINOR ||
        !HAS_FN(api,sco_api,has) || !HAS_FN(api,sco_api,log) || !HAS_FN(api,sco_api,query_service) ||
        !HAS_FN(api,sco_api,register_command) || !HAS_FN(api,sco_api,subscribe) || !HAS_FN(api,sco_api,unsubscribe)) return SCO_UNAVAILABLE;
    g.api = api; g.self = self;
    const sco_arg_def seat{"seat",SCO_ARG_STRING,0,"Words from seat name (at most 47 characters)"};
    const sco_arg_def npc{"npc",SCO_ARG_STRING,0,"Caller-supplied NPC archetype (at most 127 characters)"};
    const sco_command commands[] = {
        {sizeof(sco_command),0,"crew.target","Target current ship","Pin current ship for this session","game.vehicles.seats",nullptr,0,sizeof(sco_arg_def),Target,&g},
        {sizeof(sco_command),0,"crew.sit","Sit in seat","Local player only","game.vehicles.seat",&seat,1,sizeof(sco_arg_def),Sit,&g},
        {sizeof(sco_command),0,"crew.stand_all","Stand up","Local player and owned crew only","game.vehicles.seat",nullptr,0,sizeof(sco_arg_def),Stand,&g},
        {sizeof(sco_command),0,"crew.fill","Fill empty seats","Spawn owned crew into usable seats","game.actors.spawn_npc",&npc,1,sizeof(sco_arg_def),Fill,&g},
        {sizeof(sco_command),0,"crew.clear","Remove crew","Only NPCs this plugin spawned","game.actors.despawn",nullptr,0,sizeof(sco_arg_def),Clear,&g},
        {sizeof(sco_command),0,"crew.power_on","Flight Ready","Provider validates ship authority","game.vehicles.flight_ready",nullptr,0,sizeof(sco_arg_def),Power,&g}
    };
    for (const auto& c : commands) { const auto r = api->register_command(self,&c); if (r != SCO_OK) { g = {}; return r; } }
    const auto r = api->subscribe(self,"tick",Tick,nullptr);
    if (r != SCO_OK) { g = {}; return r; }
    g.subscribed = true; return SCO_OK;
}
void Unload() {
    if (g.subscribed && HAS_FN(g.api,sco_api,unsubscribe)) g.api->unsubscribe(g.self,"tick",Tick);
    g = {}; // Host removes commands and releases actors-owned NPCs; no shutdown game calls.
}
}
