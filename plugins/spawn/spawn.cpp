// SPDX-License-Identifier: GPL-3.0-only
#include "spawn.h"
#include "sc_spawn.h"
#include "sc_entities.h"
#include "sc_spatial.h"
#include "sc_actors.h"
#include "sc_vehicles.h"
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
namespace sco::features::spawn {
namespace {
#define HAS(t, type, m) ((t) && (t)->size >= offsetof(type, m) + sizeof((t)->m) && (t)->m)
constexpr uint32_t kMaxTickets = 64;
enum class Phase { Pending, Boarding, Power, Ready, Cancelling, Removing };
struct Ticket { uint64_t id = 0; Phase phase = Phase::Pending; char seat[64]{}; bool power = false; uint32_t started = 0; bool clocked = false;
    uint32_t lastSeat = 0, lastPower = 0; bool seatTried = false, powerTried = false; };
struct State { const sco_api* api = nullptr; sco_plugin* self = nullptr; Ticket tickets[kMaxTickets]{}; uint32_t count = 0; } g;
const sco_plugin_info kInfo{sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, "spawn", "1.0.0", "sc-offline"};
template<class T> const T* Open(const char* name) {
    const void* out = nullptr;
    if (!HAS(g.api, sco_api, query_service) || g.api->query_service(name, 0x10000u, &out) != SCO_OK) return nullptr;
    return static_cast<const T*>(out);
}
sco_result Answer(sco_result r, char* out, uint32_t size, const char* text) {
    if (out && size) std::snprintf(out,size,"%s",text);
    if (g.api) g.api->log(g.self,r == SCO_OK ? SCO_LOG_INFO : SCO_LOG_WARN,text);
    return r;
}
sco_result RemoveTicket(Ticket& t) {
    if (t.phase == Phase::Removing) return SCO_NOT_FOUND;
    t.phase = Phase::Cancelling; // Stop seat/power retries even if cleanup is refused.
    const auto* ent = Open<sc_entities_v1>(SC_ENTITIES_NAME);
    if (!HAS(ent,sc_entities_v1,despawn) || !g.api->has("game.entities.spawn")) return SCO_UNAVAILABLE;
    auto r = ent->despawn(g.self,t.id);
    if (r == SCO_OK || r == SCO_NOT_FOUND) t.phase = Phase::Removing;
    return r;
}
sco_result Ship(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    if (!g.api) return SCO_UNAVAILABLE;
    if (!args || n != 2 || (!out && size) || args[0].type != SCO_ARG_STRING || args[1].type != SCO_ARG_FLOAT ||
        !args[0].v.s || !*args[0].v.s || std::strlen(args[0].v.s)>63 ||
        !std::isfinite(args[1].v.f) || args[1].v.f<0 || args[1].v.f>10000)
        return Answer(SCO_BAD_ARG,out,size,"Supply class (1 to 63 characters) and height (0 to 10000 m)");
    const auto* cls = Open<sc_spawn_service_v1>(SC_SPAWN_SERVICE_NAME);
    const auto* ent = Open<sc_entities_v1>(SC_ENTITIES_NAME);
    const auto* sp = Open<sc_spatial_v1>(SC_SPATIAL_SERVICE_NAME);
    if (!HAS(cls,sc_spawn_service_v1,class_exists) || !HAS(ent,sc_entities_v1,spawn) ||
        !HAS(ent,sc_entities_v1,despawn) || !HAS(sp,sc_spatial_v1,player_pose) ||
        !g.api->has("spawn.ship") || !g.api->has("game.entities.spawn"))
        return Answer(SCO_UNAVAILABLE,out,size,"Owned spawn/removal, class lookup or player pose unavailable; no fallback");
    if (g.count == kMaxTickets) return Answer(SCO_TOO_MANY,out,size,"Loaded-session ticket quota reached (including pending removal)");
    if (!cls->class_exists(args[0].v.s)) return Answer(SCO_NOT_FOUND,out,size,"Not a spawnable class on this build");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    double p[3]{nan,nan,nan},q[4]{nan,nan,nan,nan}; uint64_t zone=0;
    if (!sp->player_pose(p,q,&zone) || !zone) return Answer(SCO_NOT_FOUND,out,size,"No fresh player pose in a streamed zone");
    double norm=0;
    for (auto v:p) if (!std::isfinite(v)) return Answer(SCO_BAD_ARG,out,size,"Player position not finite");
    for (auto v:q) { if (!std::isfinite(v)) return Answer(SCO_BAD_ARG,out,size,"Player rotation not finite"); norm+=v*v; }
    if (!std::isfinite(norm) || norm<=0) return Answer(SCO_BAD_ARG,out,size,"Player rotation invalid");
    for (auto& v:q) v/=std::sqrt(norm);
    p[2]+=args[1].v.f; // Existing height is an offset along the current zone's Z axis.
    if (!std::isfinite(p[2])) return SCO_BAD_ARG;
    uint64_t id=0; auto r=ent->spawn(g.self,args[0].v.s,zone,p,q,&id);
    if (r!=SCO_OK) return Answer(r,out,size,"Provider refused owned ship spawn");
    bool duplicate=false;
    for (uint32_t i=0;i<g.count;++i) if(g.tickets[i].id==id) duplicate=true;
    if (!id || duplicate) return Answer(SCO_FAILED,out,size,"Provider returned zero/duplicate id; ownership must be reconciled by host release");
    g.tickets[g.count++]={id,Phase::Pending};
    char b[160]; std::snprintf(b,sizeof(b),"Owned ship id=%llu pending streaming; removed on unload",static_cast<unsigned long long>(id));
    return Answer(SCO_OK,out,size,b);
}
bool BoardAvailable(bool power) {
    const auto* v=Open<sc_vehicles_v1>(SC_VEHICLES_SERVICE_NAME);
    const auto* a=Open<sc_actors_v1>(SC_ACTORS_NAME);
    return HAS(v,sc_vehicles_v1,seats) && HAS(v,sc_vehicles_v1,seat_occupant) && HAS(v,sc_vehicles_v1,seat) &&
        HAS(a,sc_actors_v1,local_player) && g.api->has("game.vehicles.seats") && g.api->has("game.vehicles.seat") &&
        g.api->has("game.actors.local_player") && (!power || (HAS(v,sc_vehicles_v1,power_on) &&
        HAS(v,sc_vehicles_v1,player_ship) && g.api->has("game.vehicles.flight_ready")));
}
bool Busy() {
    for(uint32_t i=0;i<g.count;++i) if(g.tickets[i].seat[0] && g.tickets[i].phase!=Phase::Ready && g.tickets[i].phase!=Phase::Removing) return true;
    return false;
}
sco_result Options(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    if(!g.api) return SCO_UNAVAILABLE;
    if(!args || n!=4 || args[2].type!=SCO_ARG_STRING || args[3].type!=SCO_ARG_BOOL ||
       !args[2].v.s || !*args[2].v.s || std::strlen(args[2].v.s)>63 ||
       (args[3].v.i!=0 && args[3].v.i!=1)) return SCO_BAD_ARG;
    const bool board=std::strcmp(args[2].v.s,"none")!=0;
    const bool power=args[3].v.i!=0;
    if(power && std::strcmp(args[2].v.s,"pilot")) return Answer(SCO_BAD_ARG,out,size,"Power requires pilot boarding");
    if(board && (Busy() || !BoardAvailable(power))) return Answer(SCO_UNAVAILABLE,out,size,"Boarding busy or provider unavailable; no spawn requested");
    auto r=Ship(args,2,nullptr,out,size);
    if(r==SCO_OK && board) {
        auto& t=g.tickets[g.count-1];std::snprintf(t.seat,sizeof(t.seat),"%s",args[2].v.s);t.power=power;
    }
    return r;
}
sco_result Board(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    if(!g.api) return SCO_UNAVAILABLE;
    if(!args || n!=3 || (!out && size) || args[0].type!=SCO_ARG_INT || !args[0].v.i ||
       args[1].type!=SCO_ARG_STRING || !args[1].v.s || !*args[1].v.s || std::strlen(args[1].v.s)>63 ||
       !std::strcmp(args[1].v.s,"none") || args[2].type!=SCO_ARG_BOOL ||
       (args[2].v.i!=0 && args[2].v.i!=1) || (args[2].v.i && std::strcmp(args[1].v.s,"pilot"))) return SCO_BAD_ARG;
    for(uint32_t i=0;i<g.count;++i) {
        auto& t=g.tickets[i];
        if(t.id!=static_cast<uint64_t>(args[0].v.i) || t.phase==Phase::Removing || t.phase==Phase::Cancelling) continue;
        if(Busy() || !BoardAvailable(args[2].v.i!=0)) return Answer(SCO_UNAVAILABLE,out,size,"Boarding busy or provider unavailable");
        std::snprintf(t.seat,sizeof(t.seat),"%s",args[1].v.s);t.power=args[2].v.i!=0;t.phase=Phase::Pending;t.clocked=false;t.seatTried=t.powerTried=false;
        return Answer(SCO_OK,out,size,"Owned boarding queued; never removes foreign seat occupants");
    }
    return Answer(SCO_NOT_FOUND,out,size,"Not a ship owned by this plugin");
}
sco_result Startup(const sco_arg*,uint32_t n,void*,char* out,uint32_t size) {
    if(!g.api) return SCO_UNAVAILABLE;
    if(n || (!out && size)) return SCO_BAD_ARG;
    return Answer(SCO_UNAVAILABLE,out,size,"Destination startup unavailable: G2/G3 player relocation/full-pose discovery are not in sdk-v2.0.0; no private fallback");
}
sco_result Remove(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    if (!g.api) return SCO_UNAVAILABLE;
    if (!args || n!=1 || args[0].type!=SCO_ARG_INT || !args[0].v.i || (!out && size)) return SCO_BAD_ARG;
    for(uint32_t i=0;i<g.count;++i) if(g.tickets[i].id==static_cast<uint64_t>(args[0].v.i)) {
        auto r=RemoveTicket(g.tickets[i]);
        return Answer(r,out,size,r==SCO_OK ? "Removal started; disappearance is not confirmed" : "Removal refused/already requested; no foreign deletion");
    }
    return Answer(SCO_NOT_FOUND,out,size,"Not a ship owned by this plugin");
}
sco_result Clear(const sco_arg*,uint32_t n,void*,char* out,uint32_t size) {
    if (!g.api) return SCO_UNAVAILABLE;
    if (n || (!out && size)) return SCO_BAD_ARG;
    sco_result result=SCO_OK;
    for(uint32_t i=0;i<g.count;++i) if(g.tickets[i].phase!=Phase::Removing) {
        auto r=RemoveTicket(g.tickets[i]); if(r!=SCO_OK && r!=SCO_NOT_FOUND) result=r;
    }
    return Answer(result,out,size,"Owned ship cleanup requested; pending removal is not absence");
}
sco_result Status(const sco_arg*,uint32_t n,void*,char* out,uint32_t size) {
    if (!g.api) return SCO_UNAVAILABLE;
    if (n || (!out && size)) return SCO_BAD_ARG;
    const auto* ent=Open<sc_entities_v1>(SC_ENTITIES_NAME);
    if(!HAS(ent,sc_entities_v1,alive) || !g.api->has("game.entities.transform"))
        return Answer(SCO_UNAVAILABLE,out,size,"Spawn observation unavailable; ownership unchanged");
    uint32_t pending=0,ready=0,removing=0,boarding=0;
    for(uint32_t i=0;i<g.count;++i) {
        switch(g.tickets[i].phase){case Phase::Pending:++pending;break;case Phase::Boarding:case Phase::Power:++boarding;break;case Phase::Cancelling:++pending;break;case Phase::Ready:++ready;break;case Phase::Removing:++removing;break;}
    }
    char b[160];std::snprintf(b,sizeof(b),"Spawn status: pending=%u ready=%u removing=%u boarding=%u; ownership retained until removal/unload",pending,ready,removing,boarding);
    return Answer(SCO_OK,out,size,b);
}
bool Matches(const char* name,const char* filter) {
    // Every caller-supplied word must occur in the bounded copied seat name.
    char lower[64]{};char words[64]{};
    if(!std::memchr(name,0,64)) return false;
    for(size_t i=0;name[i];++i) lower[i]=static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
    for(size_t i=0;filter[i];++i) words[i]=static_cast<char>(std::tolower(static_cast<unsigned char>(filter[i])));
    bool any=false;
    char* cur=words;
    while(*cur) {
        while(*cur==' ') ++cur;
        if(!*cur) break;
        char* word=cur;while(*cur && *cur!=' ') ++cur;
        if(*cur) *cur++=0;
        if(!std::strstr(lower,word)) return false;
        any=true;
    }
    return any;
}
void Progress(Ticket& t,uint32_t now) {
    if(!BoardAvailable(t.power)) return;
    const auto* a=Open<sc_actors_v1>(SC_ACTORS_NAME);
    const auto* v=Open<sc_vehicles_v1>(SC_VEHICLES_SERVICE_NAME);
    // Reopened tables are validated independently; never reuse an earlier borrowed pointer.
    if(!HAS(a,sc_actors_v1,local_player) || !HAS(v,sc_vehicles_v1,seats) ||
       !HAS(v,sc_vehicles_v1,seat_occupant) || !HAS(v,sc_vehicles_v1,seat)) return;
    uint64_t actor=0;
    if(a->local_player(&actor,nullptr)!=SCO_OK || !actor) return;
    sc_vehicle_seat seats[64]{};uint32_t count=0,more=0;
    if(v->seats(t.id,seats,64,&count,&more)!=SCO_OK || count>64 || more) return;
    for(uint32_t i=0;i<count;++i) {
        const auto& seat=seats[i];
        const bool pilot=std::strcmp(t.seat,"pilot")==0;
        if((pilot ? !(seat.flags&SC_SEAT_PILOT) : !Matches(seat.name,t.seat)) ||
           (seat.flags&(SC_SEAT_USABLE|SC_SEAT_USABLE_KNOWN))!=(SC_SEAT_USABLE|SC_SEAT_USABLE_KNOWN)) continue;
        uint64_t occupant=0;
        if(v->seat_occupant(t.id,seat.index,&occupant)!=SCO_OK) return;
        if(occupant==actor) {
            if(!t.power) {t.phase=Phase::Ready;return;}
            // game.entities ownership is NOT spawn_as authority for power_on. Require
            // the real public player_ship reading after confirmed pilot boarding.
            if(!HAS(v,sc_vehicles_v1,player_ship) || !HAS(v,sc_vehicles_v1,power_on)) return;
            uint64_t ship=0;
            if(v->player_ship(&ship)!=SCO_OK || ship!=t.id) return;
            t.phase=Phase::Power;
            if(t.powerTried && static_cast<uint32_t>(now-t.lastPower)<1000u) return;
            t.powerTried=true;t.lastPower=now;
            if(v->power_on(g.self,t.id)==SCO_OK) t.phase=Phase::Ready;
            return;
        }
        if(occupant || (seat.flags&SC_SEAT_OCCUPIED)) return; // Never evict/remove a foreign actor.
        t.phase=Phase::Boarding;
        if(t.seatTried && static_cast<uint32_t>(now-t.lastSeat)<1000u) return;
        t.seatTried=true;t.lastSeat=now;
        (void)v->seat(g.self,actor,t.id,seat.index); // Accepted link is not yet confirmed.
        return;
    }
}
void Tick(const char*,const void* data,void*) {
    if (!g.api || !data) return;
    const uint32_t now=*static_cast<const uint32_t*>(data);
    const auto* ent=Open<sc_entities_v1>(SC_ENTITIES_NAME);
    for(uint32_t i=0;i<g.count;++i) {
        auto& t=g.tickets[i];
        if(t.phase==Phase::Ready || t.phase==Phase::Removing) continue;
        if(!t.clocked){t.started=now;t.clocked=true;}
        if(t.phase==Phase::Cancelling || static_cast<uint32_t>(now-t.started)>=60000u) {
            if(t.phase!=Phase::Cancelling) g.api->log(g.self,SCO_LOG_WARN,"Spawn/seat timeout; owned removal requested (not disappearance)");
            (void)RemoveTicket(t);continue;
        }
        if(!HAS(ent,sc_entities_v1,alive) || !g.api->has("game.entities.transform") || !ent->alive(t.id)) continue;
        if(!t.seat[0]) t.phase=Phase::Ready; else Progress(t,now);
    }
}
} // namespace
const sco_plugin_info* Query(){return &kInfo;}
sco_result Load(const sco_api* api,sco_plugin* self) {
    if(!api || !self || g.api) return SCO_BAD_ARG;
    if(!HAS(api,sco_api,has) || !HAS(api,sco_api,log) || !HAS(api,sco_api,register_command) || !HAS(api,sco_api,query_service) ||
       !HAS(api,sco_api,subscribe) || api->major!=SCO_API_MAJOR || api->minor<SCO_API_MINOR) return SCO_UNAVAILABLE;
    g.api=api;g.self=self;
    const sco_arg_def ship[]={{"class",SCO_ARG_STRING,0,"Caller supplied ship class"},{"height",SCO_ARG_FLOAT,0,"0 to 10000 metres in current zone"}};
    const sco_arg_def options[]={{"class",SCO_ARG_STRING,0,"Ship class"},{"height",SCO_ARG_FLOAT,0,"0 to 10000 m"},
       {"seat",SCO_ARG_STRING,0,"none, pilot or all words in seat name"},{"power",SCO_ARG_BOOL,0,"Flight Ready after confirmed pilot boarding"}};
    const sco_arg_def board[]={{"id",SCO_ARG_INT,0,"Owned volatile ship id"},{"seat",SCO_ARG_STRING,0,"pilot or all words in seat name"},
       {"power",SCO_ARG_BOOL,0,"Flight Ready after confirmed pilot boarding"}};
    const sco_arg_def id{"id",SCO_ARG_INT,0,"Owned volatile ship id"};
    const sco_command commands[]={
       {sizeof(sco_command),0,"spawn.ship","Spawn ship","Owned ordinary spawn, no private fallback","spawn.ship",ship,2,sizeof(sco_arg_def),Ship,&g},
       {sizeof(sco_command),0,"spawn.ship_options","Spawn and board","Explicit owned spawn and boarding options","spawn.ship",options,4,sizeof(sco_arg_def),Options,&g},
       {sizeof(sco_command),0,"spawn.board","Board owned ship","Choose a seat after ordinary spawn","game.vehicles.seat",board,3,sizeof(sco_arg_def),Board,&g},
       {sizeof(sco_command),0,"spawn.startup","Startup destination unavailable","Requires future G2/G3",nullptr,nullptr,0,sizeof(sco_arg_def),Startup,&g},
       {sizeof(sco_command),0,"spawn.status","Spawn status","Pending is not absence",nullptr,nullptr,0,sizeof(sco_arg_def),Status,&g},
       {sizeof(sco_command),0,"spawn.cancel","Cancel spawn job","Stop boarding/power and request only owned cleanup","game.entities.spawn",&id,1,sizeof(sco_arg_def),Remove,&g},
       {sizeof(sco_command),0,"spawn.remove","Remove ship","Only this plugin's owned ship","game.entities.spawn",&id,1,sizeof(sco_arg_def),Remove,&g},
       {sizeof(sco_command),0,"spawn.clear","Clear ships","Request owned cleanup","game.entities.spawn",nullptr,0,sizeof(sco_arg_def),Clear,&g}
    };
    for(const auto& c:commands) {auto r=api->register_command(self,&c);if(r!=SCO_OK){Unload();return r;}}
    auto r=api->subscribe(self,"tick",Tick,&g);if(r!=SCO_OK) Unload();return r;
}
void Unload(){if(!g.api)return;char b[160];if(Clear(nullptr,0,nullptr,b,sizeof(b))!=SCO_OK)g.api->log(g.self,SCO_LOG_WARN,"Cleanup refused; host owner-release is required");g={};}
#undef HAS
} // namespace sco::features::spawn
