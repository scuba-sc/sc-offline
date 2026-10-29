// SPDX-License-Identifier: GPL-3.0-only
#include "build.h"
#include "build_state.h"
#include "picker.h"
#include "sc_entities.h"
#include "sc_actors.h"
#include "sc_spawn.h"
#include "sc_spatial.h"
#include "sc_prefabs.h"
#include "sc_owned.h"
#include "sc_world_control.h"
#include "sco_ui.h"
#include <cmath>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <initializer_list>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace sco::features::build {
namespace {
#define HAS(t, type, m) ((t) && (t)->size >= offsetof(type, m) + sizeof((t)->m) && (t)->m)
constexpr uint32_t kMax = 128; // Includes pending spawns/removals; absence never frees a slot.
enum class Kind { Entity, Npc };
struct Placement { uint64_t id = 0, receipt = 0; Kind kind = Kind::Entity; bool removing = false; };
struct State {
    const sco_api* api = nullptr; sco_plugin* self = nullptr;
    Placement placed[kMax]{}; uint32_t count = 0;
    uint64_t lease = 0, preview = 0, ghost = 0, zone = 0;
    bool active = false, clocked = false, suppressed = false;
    uint32_t stillSince = 0;
    char selected[SC_PREFABS_MAX_PATH + 1]{};
    double reach = 50, yaw = 0, lastPos[3]{}, lastRot[4]{}, lastCamera[3]{}, lastCameraRot[4]{};
} g;
const sco_plugin_info kInfo{sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, "build", "1.0.0", "sc-offline"};
template<class T> const T* Open(const char* name) {
    const void* out = nullptr;
    if (!HAS(g.api, sco_api, query_service) || g.api->query_service(name, 0x10000u, &out) != SCO_OK) return nullptr;
    return static_cast<const T*>(out);
}
sco_result Reply(sco_result r, char* out, uint32_t size, const char* text) {
    picker::Message(text);
    if (out && size) std::snprintf(out, size, "%s", text);
    if (HAS(g.api, sco_api, log)) g.api->log(g.self, r == SCO_OK ? SCO_LOG_INFO : SCO_LOG_WARN, text);
    return r;
}
bool Cap(const char* name) { return HAS(g.api, sco_api, has) && g.api->has(name); }
bool Finite(const double* a, uint32_t n) { for (uint32_t i=0;i<n;++i) if (!std::isfinite(a[i])) return false; return true; }
bool Normalize(double q[4]) {
    if (!Finite(q,4)) return false;
    double norm=0; for (int i=0;i<4;++i) norm+=q[i]*q[i];
    if (!std::isfinite(norm) || norm <= 0) return false;
    for (int i=0;i<4;++i) q[i]/=std::sqrt(norm);
    return true;
}
void Multiply(const double a[4], const double b[4], double out[4]) {
    out[0]=a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1];
    out[1]=a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0];
    out[2]=a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3];
    out[3]=a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2];
}
bool Ends(const char* s, const char* suffix) {
    const auto n=std::strlen(s), m=std::strlen(suffix); if(n<m)return false;
    for(size_t i=0;i<m;++i) if(std::tolower(static_cast<unsigned char>(s[n-m+i]))!=suffix[i])return false;
    return true;
}
bool Prefab(const char* name) { return name && Ends(name,".socpak"); }
bool Path(const char* name) {
    const size_t n=std::strlen(name);
    if (!n || n>SC_PREFABS_MAX_PATH || std::strncmp(name,"ObjectContainers/",17) || !Ends(name,".socpak")) return false;
    const char* component=name;
    for(const char* p=name;;++p) {
        const unsigned char c=static_cast<unsigned char>(*p);
        if(!c || c=='/') {
            const size_t len=static_cast<size_t>(p-component);
            if(!len || (len==1 && component[0]=='.') || (len==2 && component[0]=='.' && component[1]=='.')) return false;
            if(!c) return true;
            component=p+1;
        } else if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_' || c=='-' || c=='.' || c=='{' || c=='}')) return false;
    }
}
bool Npc(const char* name) {
    for(const auto* prefix:{"NPC_","AIShip_CrewProfiles","PU_Pilots","PU_Human","Vanduul_Pilot"})
        if(!std::strncmp(name,prefix,std::strlen(prefix))) return true;
    return false;
}
sco_result Despawn(uint64_t& id) {
    if(!id)return SCO_OK;
    const auto* ent=Open<sc_entities_v1>(SC_ENTITIES_NAME);
    if(!HAS(ent,sc_entities_v1,despawn))return SCO_UNAVAILABLE;
    const auto r=ent->despawn(g.self,id);
    if(r==SCO_OK || r==SCO_NOT_FOUND)id=0; // Accepted/cancelled authority, not disappearance.
    return r;
}
sco_result RemovePreviews() {
    const auto a=Despawn(g.ghost), b=Despawn(g.preview);
    return a!=SCO_OK && a!=SCO_NOT_FOUND ? a : b;
}
sco_result Stop() {
    g.active=false; g.clocked=false;
    const auto previews=RemovePreviews();
    if(g.lease) {
        const auto* ctl=Open<sc_world_control_v1>(SC_WORLD_CONTROL_NAME);
        if(!HAS(ctl,sc_world_control_v1,camera_release)) return SCO_UNAVAILABLE;
        const auto r=ctl->camera_release(g.self,g.lease);
        if(r!=SCO_OK && r!=SCO_NOT_FOUND)return r; // Retain lease for provider/host restore retry.
        g.lease=0;
    }
    return previews;
}
sco_result Remove(Placement& p) {
    if(p.removing)return SCO_OK;
    const auto* own=Open<sc_owned_v1>(SC_OWNED_NAME);
    if(p.kind==Kind::Entity ? !HAS(own,sc_owned_v1,remove_build) : !HAS(own,sc_owned_v1,remove_npc)) return SCO_UNAVAILABLE;
    uint64_t ticket=0;
    const auto r=p.kind==Kind::Entity ? own->remove_build(g.self,p.id,&ticket) : own->remove_npc(g.self,p.id,&ticket);
    if(r==SCO_OK) {p.receipt=ticket;p.removing=true;} // Never use alive() as a completion detector.
    return r;
}
bool WorldReady() {
    const auto* world=Open<sc_world_v1>(SC_WORLD_NAME);
    return HAS(world,sc_world_v1,raycast) && Cap("game.world.raycast");
}
sco_result Pose(double p[3],double q[4],uint64_t& zone,double ahead,bool ground) {
    const auto* sp=Open<sc_spatial_v1>(SC_SPATIAL_SERVICE_NAME);
    const auto* world=Open<sc_world_v1>(SC_WORLD_NAME);
    if(!HAS(sp,sc_spatial_v1,player_pose) || (ground && (!HAS(world,sc_world_v1,raycast) || !Cap("game.world.raycast"))))return SCO_UNAVAILABLE;
    for(int i=0;i<3;++i)p[i]=std::numeric_limits<double>::quiet_NaN();
    for(int i=0;i<4;++i)q[i]=std::numeric_limits<double>::quiet_NaN();
    zone=0;
    if(!sp->player_pose(p,q,&zone) || !zone)return SCO_NOT_FOUND;
    if(!Finite(p,3) || !Normalize(q))return SCO_BAD_ARG;
    const double fwd[3]{2*(q[0]*q[1]-q[3]*q[2]),1-2*(q[0]*q[0]+q[2]*q[2]),2*(q[1]*q[2]+q[3]*q[0])};
    const double up[3]{2*(q[0]*q[2]+q[3]*q[1]),2*(q[1]*q[2]-q[3]*q[0]),1-2*(q[0]*q[0]+q[1]*q[1])};
    for(int i=0;i<3;++i)p[i]+=fwd[i]*ahead;
    if(ground) {
        double from[3],dir[3];for(int i=0;i<3;++i){from[i]=p[i]+up[i]*1.2;dir[i]=-up[i];}
        sc_world_hit hit{};hit.size=sizeof(hit);
        const auto r=world->raycast(zone,from,dir,50,&hit);
        if(r==SCO_OK) {if(!Finite(hit.pos,3))return SCO_FAILED;std::memcpy(p,hit.pos,sizeof(hit.pos));}
        else if(r!=SCO_NOT_FOUND)return r;
    }
    return Finite(p,3)?SCO_OK:SCO_BAD_ARG;
}
sco_result Spawn(const char* name,uint64_t zone,const double p[3],const double q[4],bool placement,uint64_t& id,Kind& kind) {
    id=0;kind=Npc(name)?Kind::Npc:Kind::Entity;
    const auto* own=Open<sc_owned_v1>(SC_OWNED_NAME);
    const auto* ent=Open<sc_entities_v1>(SC_ENTITIES_NAME);
    if(kind==Kind::Npc) {
        if(!placement)return SCO_UNAVAILABLE; // Actors IDs cannot be used as owned entity skip IDs.
        const auto* actors=Open<sc_actors_v1>(SC_ACTORS_NAME);
        if(!HAS(actors,sc_actors_v1,spawn_npc) || !HAS(own,sc_owned_v1,remove_npc) || !Cap("game.actors.spawn_npc"))return SCO_UNAVAILABLE;
        return actors->spawn_npc(g.self,name,zone,p,&id);
    }
    if(!HAS(ent,sc_entities_v1,despawn) || (placement && !HAS(own,sc_owned_v1,retain_build)) ||
       (placement && !HAS(own,sc_owned_v1,remove_build)))return SCO_UNAVAILABLE;
    sco_result r;
    if(Prefab(name)) {
        if(!Path(name))return SCO_BAD_ARG;
        const auto* prefab=Open<sc_prefabs_v1>(SC_PREFABS_NAME);
        if(!HAS(prefab,sc_prefabs_v1,spawn) || !Cap("game.prefabs.spawn"))return SCO_UNAVAILABLE;
        r=prefab->spawn(g.self,name,zone,p,q,&id);
    } else {
        const auto* cls=Open<sc_spawn_service_v1>(SC_SPAWN_SERVICE_NAME);
        if(!HAS(cls,sc_spawn_service_v1,class_exists) || !HAS(ent,sc_entities_v1,spawn) || !Cap("game.entities.spawn"))return SCO_UNAVAILABLE;
        if(!cls->class_exists(name))return SCO_NOT_FOUND;
        r=ent->spawn(g.self,name,zone,p,q,&id);
    }
    if(r!=SCO_OK)return r;
    if(!id)return SCO_FAILED;
    if(placement) {
        r=own->retain_build(g.self,id); // Never relinquish authority with entities.keep.
        if(r!=SCO_OK) { (void)Despawn(id); return r; }
    }
    return SCO_OK;
}
sco_result Place(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    if(!args || n!=2 || args[0].type!=SCO_ARG_STRING || args[1].type!=SCO_ARG_FLOAT || !args[0].v.s ||
       !*args[0].v.s || std::strlen(args[0].v.s)>SC_PREFABS_MAX_PATH || (!out&&size) ||
       !std::isfinite(args[1].v.f) || args[1].v.f<0 || args[1].v.f>100 || (Prefab(args[0].v.s)&&!Path(args[0].v.s)))
        return Reply(SCO_BAD_ARG,out,size,"Supply class or canonical ObjectContainers/.../*.socpak and ahead 0..100 m");
    if(g.count==kMax)return Reply(SCO_TOO_MANY,out,size,"Loaded-session placement quota (including removals) reached");
    if(!WorldReady())return Reply(SCO_UNAVAILABLE,out,size,"Build ground placement unavailable: game.world missing/disabled; no private fallback");
    double p[3],q[4];uint64_t zone=0;auto r=Pose(p,q,zone,args[1].v.f,true);
    if(r!=SCO_OK)return Reply(r,out,size,"Fresh player/ground pose unavailable; no placement requested");
    uint64_t id=0;Kind kind; r=Spawn(args[0].v.s,zone,p,q,true,id,kind);
    if(r!=SCO_OK)return Reply(r,out,size,"Owned build provider refused placement; no private fallback");
    if(!id)return Reply(SCO_FAILED,out,size,"Provider returned zero ID; host release must reconcile ownership");
    g.placed[g.count++]={id,0,kind,false};
    return Reply(SCO_OK,out,size,"Owned placement initiated; streaming unconfirmed, undo authority retained while loaded; removed on unload");
}
sco_result Undo(const sco_arg*,uint32_t n,void*,char* out,uint32_t size) {
    if(n || (!out&&size))return SCO_BAD_ARG;
    for(uint32_t i=g.count;i>0;--i)if(!g.placed[i-1].removing) {
        const auto r=Remove(g.placed[i-1]);
        return Reply(r,out,size,r==SCO_OK?"Removal REQUESTED, not disappearance; receipt retained":"Owned removal refused; authority unchanged, no foreign fallback");
    }
    return Reply(SCO_OK,out,size,"Nothing retained to undo");
}
sco_result Clear(const sco_arg*,uint32_t n,void*,char* out,uint32_t size) {
    if(n || (!out&&size))return SCO_BAD_ARG;
    sco_result result=SCO_OK;for(uint32_t i=0;i<g.count;++i){const auto r=Remove(g.placed[i]);if(r!=SCO_OK)result=r;}
    return Reply(result,out,size,"Owned cleanup requested; pending receipts do not confirm disappearance");
}
bool ControlReady() {
    const auto* ctl=Open<sc_world_control_v1>(SC_WORLD_CONTROL_NAME);
    const auto* world=Open<sc_world_v1>(SC_WORLD_NAME);
    const auto* sp=Open<sc_spatial_v1>(SC_SPATIAL_SERVICE_NAME);
    return Cap("game.world.free_camera") && Cap("game.world.raycast_filtered") && WorldReady() &&
        HAS(ctl,sc_world_control_v1,raycast_filtered) && HAS(ctl,sc_world_control_v1,camera_acquire) &&
        HAS(ctl,sc_world_control_v1,camera_step) && HAS(ctl,sc_world_control_v1,camera_release) &&
        HAS(world,sc_world_v1,camera) && HAS(sp,sc_spatial_v1,world_to_local) && HAS(sp,sc_spatial_v1,local_to_world);
}
sco_result Toggle(const sco_arg*,uint32_t n,void*,char* out,uint32_t size) {
    if(n || (!out&&size))return SCO_BAD_ARG;
    if(g.active || g.lease)return Reply(Stop(),out,size,"Build stopped; preview cleanup/camera restore requested");
    if(!ControlReady())return Reply(SCO_UNAVAILABLE,out,size,"Build aim/freecam unavailable: G5 has no production adapter; no private fallback");
    const auto* ctl=Open<sc_world_control_v1>(SC_WORLD_CONTROL_NAME);
    if(!HAS(ctl,sc_world_control_v1,camera_acquire))return SCO_UNAVAILABLE;
    uint64_t lease=0;const auto r=ctl->camera_acquire(g.self,&lease);
    if(r!=SCO_OK || !lease)return Reply(r==SCO_OK?SCO_FAILED:r,out,size,"Public free-camera lease refused; no fallback");
    g.lease=lease;g.active=true;g.clocked=false;
    return Reply(SCO_OK,out,size,"Build camera lease acquired; use build.select/aim/drop/rotate/reach");
}
sco_result Select(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    if(!args || n!=2 || args[0].type!=SCO_ARG_STRING || args[1].type!=SCO_ARG_FLOAT || !args[0].v.s || !*args[0].v.s ||
       std::strlen(args[0].v.s)>SC_PREFABS_MAX_PATH || !std::isfinite(args[1].v.f) || args[1].v.f<5 || args[1].v.f>300 ||
       (Prefab(args[0].v.s)&&!Path(args[0].v.s)))return SCO_BAD_ARG;
    const auto r=RemovePreviews();if(r!=SCO_OK&&r!=SCO_NOT_FOUND)return Reply(r,out,size,"Preview removal refused; selection unchanged");
    std::snprintf(g.selected,sizeof(g.selected),"%s",args[0].v.s);g.reach=args[1].v.f;g.clocked=false;g.suppressed=false;
    return Reply(SCO_OK,out,size,"Selection copied; aim/freecam still requires enabled public G5");
}
sco_result Aim(double camera[3],double cameraRot[4],double p[3],double q[4],uint64_t& zone) {
    if(!g.active || !ControlReady())return SCO_UNAVAILABLE;
    const auto* world=Open<sc_world_v1>(SC_WORLD_NAME);
    const auto* sp=Open<sc_spatial_v1>(SC_SPATIAL_SERVICE_NAME);
    const auto* ctl=Open<sc_world_control_v1>(SC_WORLD_CONTROL_NAME);
    if(!HAS(world,sc_world_v1,camera) || !HAS(ctl,sc_world_control_v1,raycast_filtered) ||
       !HAS(sp,sc_spatial_v1,world_to_local) || !HAS(sp,sc_spatial_v1,local_to_world))return SCO_UNAVAILABLE;
    auto r=Pose(p,q,zone,0,false);if(r!=SCO_OK)return r;
    for(int i=0;i<3;++i)camera[i]=std::numeric_limits<double>::quiet_NaN();
    for(int i=0;i<4;++i)cameraRot[i]=std::numeric_limits<double>::quiet_NaN();
    r=world->camera(camera,cameraRot,nullptr);if(r!=SCO_OK)return r;
    if(!Finite(camera,3)||!Normalize(cameraRot))return SCO_FAILED;
    const double* c=cameraRot;
    const double fwd[3]{2*(c[0]*c[1]-c[3]*c[2]),1-2*(c[0]*c[0]+c[2]*c[2]),2*(c[1]*c[2]+c[3]*c[0])};
    double end[3],from[3],localEnd[3],dir[3];for(int i=0;i<3;++i)end[i]=camera[i]+fwd[i]*g.reach;
    if(!sp->world_to_local(zone,camera,from)||!sp->world_to_local(zone,end,localEnd))return SCO_NOT_FOUND;
    double length=0;for(int i=0;i<3;++i){dir[i]=localEnd[i]-from[i];length+=dir[i]*dir[i];}
    length=std::sqrt(length);if(!std::isfinite(length)||length<=0)return SCO_BAD_ARG;
    // Only preview/ghost IDs created by this plugin through entities/prefabs. Never player/foreign IDs.
    uint64_t skips[2]{};uint32_t count=0;if(g.preview)skips[count++]=g.preview;if(g.ghost&&g.ghost!=g.preview)skips[count++]=g.ghost;
    sc_world_hit hit{};hit.size=sizeof(hit);r=ctl->raycast_filtered(g.self,zone,from,dir,length,skips,count,&hit);
    if(r!=SCO_OK && r!=SCO_NOT_FOUND)return r;
    for(int i=0;i<3;++i)p[i]=r==SCO_OK?hit.pos[i]-dir[i]/length*0.3:localEnd[i];
    const double up[3]{2*(q[0]*q[2]+q[3]*q[1]),2*(q[1]*q[2]-q[3]*q[0]),1-2*(q[0]*q[0]+q[1]*q[1])};
    double down[3];for(int i=0;i<3;++i)down[i]=-up[i];
    hit={};hit.size=sizeof(hit);r=ctl->raycast_filtered(g.self,zone,p,down,2000,skips,count,&hit);
    if(r==SCO_OK)std::memcpy(p,hit.pos,sizeof(hit.pos));else if(r!=SCO_NOT_FOUND)return r;
    const double h=g.yaw*3.14159265358979323846/360;const double turn[4]{0,0,std::sin(h),std::cos(h)};
    double rotated[4];Multiply(q,turn,rotated);std::memcpy(q,rotated,sizeof(rotated));
    return Finite(p,3)&&Normalize(q)?SCO_OK:SCO_FAILED;
}
sco_result AimCommand(const sco_arg*,uint32_t n,void*,char* out,uint32_t size) {
    if(n || (!out&&size))return SCO_BAD_ARG;
    double c[3],cq[4],p[3],q[4];uint64_t zone=0;
    return Reply(Aim(c,cq,p,q,zone),out,size,"Aim requires enabled public G5 filtered rays/camera; no private fallback");
}
sco_result Drop(const sco_arg*,uint32_t n,void*,char* out,uint32_t size) {
    if(n || (!out&&size))return SCO_BAD_ARG;
    if(!g.active || !g.selected[0] || !ControlReady())return Reply(SCO_UNAVAILABLE,out,size,"No public aim preview available; G5 disabled/missing");
    if(g.count==kMax)return SCO_TOO_MANY;
    uint64_t id=0;Kind kind=Kind::Entity;sco_result r;
    if(g.ghost) {
        const auto* own=Open<sc_owned_v1>(SC_OWNED_NAME);
        if(!HAS(own,sc_owned_v1,retain_build)||!HAS(own,sc_owned_v1,remove_build))return SCO_UNAVAILABLE;
        r=own->retain_build(g.self,g.ghost);if(r==SCO_OK){id=g.ghost;g.ghost=0;g.suppressed=true;}
    } else {
        double c[3],cq[4],p[3],q[4];uint64_t zone=0;r=Aim(c,cq,p,q,zone);
        if(r==SCO_OK)r=Spawn(g.selected,zone,p,q,true,id,kind);
    }
    if(r==SCO_OK && id)g.placed[g.count++]={id,0,kind,false};
    return Reply(r,out,size,"Placement initiation only; retained while loaded, removed on unload");
}
sco_result Rotate(const sco_arg*,uint32_t n,void*,char* out,uint32_t size) {
    if (n) return SCO_BAD_ARG;
    g.yaw = std::fmod(g.yaw + 45, 360);
    g.clocked = false;
    g.suppressed = false;
    return Reply(SCO_OK,out,size,"Preview rotation advanced 45 degrees");
}
sco_result Reach(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    if(!args||n!=1||args[0].type!=SCO_ARG_FLOAT||!std::isfinite(args[0].v.f)||args[0].v.f<5||args[0].v.f>300)return SCO_BAD_ARG;
    g.reach=args[0].v.f;g.clocked=false;g.suppressed=false;return Reply(SCO_OK,out,size,"Preview reach updated");
}
sco_result Camera(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    if(!args||n!=3)return SCO_BAD_ARG;
    sc_world_camera_step step{};step.size=sizeof(step);step.rotation_xyzw[3]=1;double norm=0;
    for(int i=0;i<3;++i){if(args[i].type!=SCO_ARG_FLOAT||!std::isfinite(args[i].v.f))return SCO_BAD_ARG;step.translation[i]=args[i].v.f;norm+=args[i].v.f*args[i].v.f;}
    if(!std::isfinite(norm)||norm>100)return SCO_BAD_ARG;
    if(!g.active||!ControlReady())return Reply(SCO_UNAVAILABLE,out,size,"Public camera control disabled; no private input fallback");
    const auto* ctl=Open<sc_world_control_v1>(SC_WORLD_CONTROL_NAME);
    if(!HAS(ctl,sc_world_control_v1,camera_step))return SCO_UNAVAILABLE;
    const auto r=ctl->camera_step(g.self,g.lease,&step);
    if(r!=SCO_OK){(void)Stop();return Reply(r,out,size,"Camera step refused; stopped and requested public restore");}
    return Reply(r,out,size,"Bounded public camera step requested");
}
uint32_t Count() {uint32_t count=0;for(uint32_t i=0;i<g.count;++i)if(!g.placed[i].removing)++count;return count;}
int Active(){return g.active;}
picker::Observation ObservePicker(){return {g.reach,g.active,Count(),g.count-Count()};}
const sc_offline_build_state_v1 kState{sizeof(sc_offline_build_state_v1),Count,Active};
sco_result Status(const sco_arg*,uint32_t n,void*,char* out,uint32_t size) {
    if(n||(!out&&size))return SCO_BAD_ARG;
    char text[512];std::snprintf(text,sizeof(text),"Build retained=%u removal-requested=%u preview=%s; G5 aim/freecam %s; no across-reload persistence; %s",Count(),g.count-Count(),g.preview||g.ghost?"pending/streamed":"none",ControlReady()?"enabled":"unavailable (no production adapter)",picker::state.source);
    return Reply(SCO_OK,out,size,text);
}
void Input() {
#ifdef _WIN32
    DWORD process=0;GetWindowThreadProcessId(GetForegroundWindow(),&process);
    if(process!=GetCurrentProcessId())return;
    static bool down[5]{};
    const int keys[]{VK_LBUTTON, 'R', VK_OEM_4, VK_OEM_6, VK_BACK};
    bool pressed[5]{};
    for(int i=0;i<5;++i){const bool key=(GetAsyncKeyState(keys[i])&0x8000)!=0;pressed[i]=key&&!down[i];down[i]=key;}
    char reply[192];
    if(pressed[1])(void)Rotate(nullptr,0,nullptr,reply,sizeof(reply));
    if(pressed[2] || pressed[3]) {
        sco_arg arg{};arg.type=SCO_ARG_FLOAT;arg.v.f=g.reach+(pressed[3]?5:-5);
        if(arg.v.f<5)arg.v.f=5;if(arg.v.f>300)arg.v.f=300;
        (void)Reach(&arg,1,nullptr,reply,sizeof(reply));
    }
    if(pressed[4])(void)Undo(nullptr,0,nullptr,reply,sizeof(reply));
    if(pressed[0])(void)Drop(nullptr,0,nullptr,reply,sizeof(reply));
#endif
}
void Tick(const char*,const void* data,void*) {
    picker::Tick();
    if(!g.api || !g.active || !data || !g.selected[0])return;
    if(!ControlReady()){(void)Stop();return;}
    Input();
    double c[3],cq[4],p[3],q[4];uint64_t zone=0;
    const auto r=Aim(c,cq,p,q,zone);if(r!=SCO_OK){(void)Stop();return;}
    const uint32_t now=*static_cast<const uint32_t*>(data);
    double delta=0,dot=0;for(int i=0;i<3;++i)delta+=(c[i]-g.lastCamera[i])*(c[i]-g.lastCamera[i]);
    for(int i=0;i<4;++i)dot+=cq[i]*g.lastCameraRot[i];
    bool moved=!g.clocked || zone!=g.zone || delta>0.0025 || std::fabs(dot)<0.9998;
    for(int i=0;i<3;++i)if(std::fabs(p[i]-g.lastPos[i])>0.05)moved=true;
    for(int i=0;i<4;++i)if(std::fabs(q[i]-g.lastRot[i])>0.0001)moved=true;
    if(moved) {
        if(Despawn(g.ghost)!=SCO_OK&&g.ghost)return;
        g.stillSince=now;g.clocked=true;g.suppressed=false;
    }
    g.zone=zone;std::memcpy(g.lastPos,p,sizeof(p));std::memcpy(g.lastRot,q,sizeof(q));
    std::memcpy(g.lastCamera,c,sizeof(c));std::memcpy(g.lastCameraRot,cq,sizeof(cq));
    const auto* ent=Open<sc_entities_v1>(SC_ENTITIES_NAME);
    if(!g.preview) {
        const char* marker=g.selected;
        if(Prefab(g.selected)) {
            const auto* cls=Open<sc_spawn_service_v1>(SC_SPAWN_SERVICE_NAME);
            marker=nullptr;
            if(HAS(cls,sc_spawn_service_v1,class_exists))for(const auto* candidate:{"PlayerDeco_Flair_Hanger_Flag_UEE_1","PlayerDeco_Flair_Heart_Table_1_a"})
                if(cls->class_exists(candidate)){marker=candidate;break;}
            if(!marker)return; // Refuse moving a prefab as a live marker.
        }
        Kind kind;const auto spawned=Spawn(marker,zone,p,q,false,g.preview,kind);
        if(spawned!=SCO_OK)return;
    } else if(!g.ghost && HAS(ent,sc_entities_v1,set_transform)) {
        (void)ent->set_transform(g.self,g.preview,zone,p,q); // Pending IDs remain owned; no alive pruning.
    }
    if(Prefab(g.selected)&&!g.ghost&&!g.suppressed&&static_cast<uint32_t>(now-g.stillSince)>500) {
        Kind kind;(void)Spawn(g.selected,zone,p,q,false,g.ghost,kind);
    }
}
} // namespace
const sco_plugin_info* Query(){return &kInfo;}
sco_result Load(const sco_api* api,sco_plugin* self) {
    if(!api||!self||g.api)return SCO_BAD_ARG;
    if(!HAS(api,sco_api,log) || !HAS(api,sco_api,has) ||
       !HAS(api,sco_api,query_service) || !HAS(api,sco_api,register_command) || !HAS(api,sco_api,provide_service) || !HAS(api,sco_api,subscribe) || api->major!=SCO_API_MAJOR || api->minor<SCO_API_MINOR)return SCO_UNAVAILABLE;
    g.api=api;g.self=self;
    const sco_arg_def place[]={{"object",SCO_ARG_STRING,0,"Buildable class or canonical content prefab path"},{"ahead",SCO_ARG_FLOAT,0,"0..100 metres ahead, grounded"}};
    const sco_arg_def select[]={{"object",SCO_ARG_STRING,0,"Buildable class or canonical content prefab path"},{"reach",SCO_ARG_FLOAT,0,"5..300 metres"}};
    const sco_arg_def reach{"metres",SCO_ARG_FLOAT,0,"5..300 metres"};
    const sco_arg_def camera[]={{"x",SCO_ARG_FLOAT,0,"Relative world metres"},{"y",SCO_ARG_FLOAT,0,"Relative world metres"},{"z",SCO_ARG_FLOAT,0,"Relative world metres; total <=10"}};
    struct Command {const char* name; sco_command_fn fn;const sco_arg_def* args;uint32_t count;};
    const Command commands[]={{"build.place",Place,place,2},{"build.undo",Undo,nullptr,0},{"build.clear",Clear,nullptr,0},
        {"build.toggle",Toggle,nullptr,0},{"build.select",Select,select,2},{"build.aim",AimCommand,nullptr,0},
        {"build.drop",Drop,nullptr,0},{"build.rotate",Rotate,nullptr,0},{"build.reach",Reach,&reach,1},
        {"build.camera",Camera,camera,3},{"build.status",Status,nullptr,0}};
    for(const auto& c:commands) {
        const sco_command command{sizeof(sco_command),0,c.name,c.name,"Public owned build; no private fallback",nullptr,c.args,c.count,sizeof(sco_arg_def),c.fn,&g};
        const auto r=api->register_command(self,&command);if(r!=SCO_OK){Unload();return r;}
    }
    auto r=api->provide_service(self,SC_OFFLINE_BUILD_STATE_NAME,SC_OFFLINE_BUILD_STATE_VERSION,&kState);
    if(r==SCO_OK)r=api->subscribe(self,"tick",Tick,&g);
    if(r!=SCO_OK){Unload();return r;}
    const auto* ui=Open<sco_ui_v1>(SCO_UI_NAME);
    if(HAS(ui,sco_ui_v1,bind_hotkey))(void)ui->bind_hotkey(self,"f6","build.toggle",nullptr,0);
    picker::Load(api,self,ObservePicker);
    return SCO_OK;
}
void Unload() {
    if(!g.api)return;
    if(Stop()!=SCO_OK)g.api->log(g.self,SCO_LOG_WARN,"Build preview/camera cleanup refused; public host owner-release required");
    char reply[192];if(Clear(nullptr,0,nullptr,reply,sizeof(reply))!=SCO_OK)g.api->log(g.self,SCO_LOG_WARN,"Build removal refused; host owner-release required (no private deletion)");
    picker::Unload();
    g={};
}
#undef HAS
} // namespace sco::features::build
