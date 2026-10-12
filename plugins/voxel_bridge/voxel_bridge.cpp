// SPDX-License-Identifier: GPL-3.0-only
// Optional command-first SDK consumer. No private spawn, tracker, ground or relocation path.
#include <sco_api.h>
#include <sco_ipc.h>
#include <sco_settings.h>
#include <sco_ui.h>
#include <sco_widgets.h>
#include <sc_spatial.h>
#include <sc_game_spatial.h>
#include <sc_entities.h>
#include <sc_world.h>
#include <sc_voxel_bridge.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <type_traits>

namespace {
// The extent predicate is checked BEFORE reading a possibly partial callback.
#define METHOD(t, m) ((t) && (t)->size >= offsetof(std::remove_pointer_t<decltype(t)>, m) + sizeof((t)->m) && (t)->m)
const sco_api* api = nullptr;
sco_plugin* self = nullptr;
const sco_ipc_v1* ipc = nullptr;
const sc_spatial_v1* spatial = nullptr;
const sc_game_spatial_v1* poses = nullptr;
const sc_entities_v1* entities = nullptr;
const sc_world_v1* world = nullptr;
const sco_ui_v1* ui = nullptr;
const sco_widgets_v1* widgets = nullptr;
bool panelLive = false, inWorld = false;
uint32_t peerAge = 0;
vx_sc_state lastWire = {};
char areaName[96] = "", uiReply[192] = "";
void UpdatePanel();
struct RefreshPanel { ~RefreshPanel() { UpdatePanel(); } };
uint64_t channel = 0;
uint32_t epoch = 0;
bool linked = false;
const sco_plugin_info info = {sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, "voxel_bridge", "1.0.0", "scubamount"};
constexpr size_t maxCrates = 4096;
struct Config { char block[96] = "CargoBox_050x050x050_Metal"; double size = 0.5, pivot = 0; int radius = 2; } cfg;
struct Area { bool valid = false; uint64_t zone = 0; double pos[3] = {}, rot[4] = {}, right[3] = {}, forward[3] = {}, up[3] = {}; } area;
std::unordered_map<uint64_t, uint64_t> crates;
std::unordered_set<uint64_t> pending; // one ticket per live unspawned key, bounded by maxCrates
std::unordered_set<uint64_t> cleanup; // failed initiation: retained, never pruned via alive=0
std::unordered_map<uint64_t, uint64_t> scanned;
uint32_t dropped = 0;
char error[192] = "";
char peerStatus[64] = "", peerLog[VX_MSG_LOG_MAX + 1] = "";
uint64_t Now() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
void Reason(const char* reason) {
    std::snprintf(error, sizeof(error), "%s", reason);
    if (METHOD(api, log)) api->log(self, SCO_LOG_WARN, error);
}
template<class T> const T* Query(const char* name, uint32_t version) {
    const void* table = nullptr;
    return METHOD(api, query_service) && api->query_service(name, version, &table) == SCO_OK ? static_cast<const T*>(table) : nullptr;
}
void Services() {
    ipc = Query<sco_ipc_v1>(SCO_IPC_NAME, SCO_IPC_VERSION_1_0);
    spatial = Query<sc_spatial_v1>(SC_SPATIAL_SERVICE_NAME, SC_SPATIAL_SERVICE_VERSION);
    poses = Query<sc_game_spatial_v1>(SC_GAME_SPATIAL_NAME, SC_GAME_SPATIAL_VERSION);
    entities = Query<sc_entities_v1>(SC_ENTITIES_NAME, SC_ENTITIES_VERSION_1_0);
    world = Query<sc_world_v1>(SC_WORLD_NAME, SC_WORLD_VERSION_1_0);
}
bool Ready() {
    return METHOD(ipc, create) && METHOD(ipc, peer_age_ms) && METHOD(ipc, block_read) && METHOD(ipc, block_write) &&
        METHOD(ipc, ring_init) && METHOD(ipc, ring_push) && METHOD(ipc, ring_pop) && METHOD(ipc, close) &&
        METHOD(spatial, player_pose) && METHOD(entities, spawn) && METHOD(entities, despawn) && METHOD(world, raycast);
}
bool ValidClass(const char* s) {
    size_t n = 0;
    for (; s[n]; ++n) if (n >= 95 || !((s[n] >= 'a' && s[n] <= 'z') || (s[n] >= 'A' && s[n] <= 'Z') || (s[n] >= '0' && s[n] <= '9') || s[n] == '_')) return false;
    return n > 0;
}
void LoadConfig() {
    cfg = Config();
    const auto* settings = Query<sco_settings_v1>(SCO_SETTINGS_NAME, SCO_SETTINGS_VERSION_1_0);
    uint32_t size = sizeof(cfg.block); char block[96] = {};
    if (METHOD(settings, get_string) && settings->get_string(self, "block", block, &size) == SCO_OK && std::memchr(block, 0, sizeof(block)) && ValidClass(block)) std::memcpy(cfg.block, block, sizeof(block));
    double v = 0; int64_t r = 0;
    if (METHOD(settings, get_float)) {
        if (settings->get_float(self, "size", &v) == SCO_OK && std::isfinite(v) && v >= 0.1 && v <= 4) cfg.size = v;
        if (settings->get_float(self, "pivot", &v) == SCO_OK && std::isfinite(v) && v >= -1 && v <= 1) cfg.pivot = v;
    }
    if (METHOD(settings, get_int) && settings->get_int(self, "radius", &r) == SCO_OK && r >= 1 && r <= 4) cfg.radius = static_cast<int>(r);
}
double Dot(const double a[3], const double b[3]) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
bool ValidPose(double p[3], double q[4]) {
    double norm = 0;
    for (unsigned i = 0; i < 3; ++i) if (!std::isfinite(p[i])) return false;
    for (unsigned i = 0; i < 4; ++i) { if (!std::isfinite(q[i])) return false; norm += q[i]*q[i]; }
    if (!std::isfinite(norm) || norm < 1e-12) return false;
    for (unsigned i = 0; i < 4; ++i) q[i] /= std::sqrt(norm);
    return true;
}
void Axes(const double q[4], double right[3], double forward[3], double up[3]) {
    const double x=q[0], y=q[1], z=q[2], w=q[3];
    right[0]=1-2*(y*y+z*z); right[1]=2*(x*y+w*z); right[2]=2*(x*z-w*y);
    forward[0]=2*(x*y-w*z); forward[1]=1-2*(x*x+z*z); forward[2]=2*(y*z+w*x);
    up[0]=2*(x*z+w*y); up[1]=2*(y*z-w*x); up[2]=1-2*(x*x+y*y);
}
void ToLocal(double x, double y, double z, double out[3]) {
    for (int i=0; i<3; ++i) out[i]=area.pos[i]+cfg.size*(area.right[i]*x+area.up[i]*(y-VX_BASE_Y)-area.forward[i]*z);
}
void ToVoxel(const double p[3], double out[3]) {
    double d[3]; for (int i=0; i<3; ++i) d[i]=p[i]-area.pos[i];
    out[0]=Dot(d,area.right)/cfg.size; out[1]=Dot(d,area.up)/cfg.size+VX_BASE_Y; out[2]=-Dot(d,area.forward)/cfg.size;
}
uint64_t Key(int x,int y,int z) {
    return (static_cast<uint64_t>(x & 0x1fffff)<<42)|(static_cast<uint64_t>(y & 0x1fffff)<<21)|static_cast<uint64_t>(z & 0x1fffff);
}
void Unkey(uint64_t k,int& x,int& y,int& z) {
    auto ext=[](uint64_t v) { return static_cast<int>((v & 0x100000) ? (v | ~0x1fffffull) : v); };
    x=ext((k>>42)&0x1fffff); y=ext((k>>21)&0x1fffff); z=ext(k&0x1fffff);
}
void Remove(uint64_t id) {
    if (!id) return;
    if (METHOD(entities, despawn) && entities->despawn(self,id)==SCO_OK) return;
    cleanup.insert(id); Reason("crate cleanup pending: game.entities.despawn unavailable/refused; provider retains unload ownership");
}
void Clear() {
    for (const auto& c:crates) Remove(c.second);
    crates.clear(); pending.clear(); dropped=0;
}
void RetryCleanup() {
    unsigned budget=8;
    for (auto it=cleanup.begin(); it!=cleanup.end() && budget; --budget) {
        if (METHOD(entities, despawn) && entities->despawn(self,*it)==SCO_OK) it=cleanup.erase(it);
        else ++it;
    }
}
bool Push(uint32_t type,const void* p,uint32_t size) { return channel && ipc->ring_push(self,channel,VX_OFF_TO_PEER,type,p,size)==SCO_OK; }
void Close() {
    Clear();
    if (channel && METHOD(ipc, close)) ipc->close(self,channel);
    channel=0; linked=false; inWorld=false;peerAge=0; area=Area(); scanned.clear();
    lastWire={};areaName[0]=peerStatus[0]=peerLog[0]=0;
}
bool Anchor() {
    double p[3],q[4]; uint64_t zone=0;
    if (!METHOD(spatial, player_pose) || !spatial->player_pose(p,q,&zone) || !zone || !ValidPose(p,q)) { Reason("anchor unavailable: valid player pose not observed"); return false; }
    Clear(); scanned.clear();
    area.valid=true; area.zone=zone; std::memcpy(area.pos,p,sizeof(p)); std::memcpy(area.rot,q,sizeof(q));
    Axes(q,area.right,area.forward,area.up);
    if (++epoch==0) ++epoch;
    areaName[0]=0;
    if (METHOD(spatial,zone_name)) spatial->zone_name(zone,areaName,sizeof(areaName));
    const vx_msg_area message={epoch,0}; Push(VX_MSG_AREA,&message,sizeof(message)); return true;
}
void Apply(const vx_msg_solids& msg) {
    if (!area.valid || msg.epoch!=epoch) return;
    const int64_t bx=static_cast<int64_t>(msg.sx)*16, by=static_cast<int64_t>(msg.sy)*16, bz=static_cast<int64_t>(msg.sz)*16;
    if (bx < -VX_MAX_XZ || bx+15 > VX_MAX_XZ || bz < -VX_MAX_XZ || bz+15 > VX_MAX_XZ || by < VX_MIN_Y || by+15 > VX_MAX_Y) return;
    for (int i=0; i<4096; ++i) {
        const uint64_t key=Key(static_cast<int>(bx)+(i&15),static_cast<int>(by)+(i>>8),static_cast<int>(bz)+((i>>4)&15));
        const bool solid=((msg.bits[i>>6]>>(i&63))&1)!=0; auto it=crates.find(key);
        if (solid && it==crates.end()) {
            if (crates.size()+cleanup.size()>=maxCrates) { ++dropped; continue; }
            crates.emplace(key,0); pending.insert(key);
        } else if (!solid && it!=crates.end()) { Remove(it->second); crates.erase(it); pending.erase(key); }
    }
}
void Sanitize(char* s,size_t size) {
    s[size-1]=0; for (char* p=s; *p; ++p) if (static_cast<unsigned char>(*p)<0x20 || static_cast<unsigned char>(*p)>0x7e) *p='?';
}
bool Drain() {
    uint8_t data[sizeof(vx_msg_solids)] = {};
    for (unsigned i=0;i<64;++i) {
        uint32_t type=0,size=sizeof(data); sco_result r=ipc->ring_pop(self,channel,VX_OFF_FROM_PEER,&type,data,&size);
        if (r==SCO_NOT_FOUND) return true;
        if (r==SCO_TOO_MANY && size<=VX_RING_FROM_PEER) {
            std::vector<uint8_t> skip(size); r=ipc->ring_pop(self,channel,VX_OFF_FROM_PEER,&type,skip.data(),&size);
            if (r==SCO_OK) continue;
        }
        if (r!=SCO_OK) { Reason("malformed peer ring: link closed, owned crates cleaned"); return false; }
        if (type==VX_MSG_SOLIDS && size==sizeof(vx_msg_solids)) { vx_msg_solids m; std::memcpy(&m,data,sizeof(m)); Apply(m); }
        else if (type==VX_MSG_CLEAR && size==sizeof(vx_msg_area)) { vx_msg_area m; std::memcpy(&m,data,sizeof(m)); if (m.epoch==epoch) Clear(); }
        else if (type==VX_MSG_LOG && size>=1 && size<=VX_MSG_LOG_MAX) {
            std::memcpy(peerLog,data,size); peerLog[size]=0; Sanitize(peerLog,sizeof(peerLog));
            if (METHOD(api, log)) api->log(self,SCO_LOG_INFO,peerLog);
        }
    }
    return true;
}
void Spawn() {
    if (!std::strcmp(cfg.block,"none")) { pending.clear(); return; }
    for (unsigned n=0;n<8 && !pending.empty();) {
        uint64_t key=*pending.begin(); pending.erase(pending.begin()); auto it=crates.find(key);
        if (it==crates.end() || it->second) continue;
        int x,y,z; Unkey(key,x,y,z); double p[3]; ToLocal(x+0.5,y+cfg.pivot,z+0.5,p); uint64_t id=0;
        sco_result r=entities->spawn(self,cfg.block,area.zone,p,area.rot,&id);
        if (r==SCO_OK && id) it->second=id;
        else { crates.erase(it); ++dropped; Reason("crate spawn unavailable/refused: provider quota, class or streamed zone; no private fallback"); }
        ++n;
    }
}
void Scan(const double feet[3],uint64_t now) {
    const int rx=static_cast<int>(std::floor(feet[0]/8)),rz=static_cast<int>(std::floor(feet[2]/8)),band=static_cast<int>(std::floor(feet[1]/16));
    int best=-1,bx=0,bz=0;
    for(int z=-cfg.radius;z<=cfg.radius;++z) for(int x=-cfg.radius;x<=cfg.radius;++x) {
        auto it=scanned.find(Key(rx+x,band,rz+z)); if(it!=scanned.end() && now-it->second<3000) continue;
        int d=x*x+z*z; if(best<0 || d<best) { best=d;bx=rx+x;bz=rz+z; }
    }
    if(best<0) return;
    if(scanned.size()>4096) scanned.clear(); scanned[Key(bx,band,bz)]=now;
    struct Message { vx_msg_ground header; vx_ground columns[64]; } m={};
    m.header.x0=bx*8;m.header.z0=bz*8;m.header.epoch=epoch;
    for(int i=0;i<64;++i) {
        int x=bx*8+(i&7),z=bz*8+(i>>3); double a[3],b[3],dir[3],v[3];
        ToLocal(x+0.5,feet[1]+2.5,z+0.5,a); ToLocal(x+0.5,std::floor(feet[1])-20,z+0.5,b);
        for(int j=0;j<3;++j) dir[j]=b[j]-a[j];
        sc_world_hit hit={}; hit.size=sizeof(hit);
        sco_result r=world->raycast(area.zone,a,dir,std::sqrt(Dot(dir,dir)),&hit);
        if(r==SCO_UNAVAILABLE || r==SCO_WRONG_THREAD || r==SCO_FAILED) { Reason("ground scan unavailable: game.world.raycast refused; no private fallback"); return; }
        if(r!=SCO_OK) continue;
        bool finite=true; for(double value:hit.pos) finite=finite && std::isfinite(value); if(!finite) continue;
        ToVoxel(hit.pos,v); if(!std::isfinite(v[1])) continue;
        vx_ground& col=m.columns[m.header.count++];col.x=x;col.z=z;col.top=static_cast<float>(v[1]);
    }
    Push(VX_MSG_GROUND,&m,static_cast<uint32_t>(sizeof(m.header)+m.header.count*sizeof(vx_ground)));
}
void Tick(const char*,const void*,void*) {
    RefreshPanel refresh;
    RetryCleanup(); if(!channel) return;
    uint32_t age=0; sco_result r=ipc->peer_age_ms(self,channel,&age);
    bool fresh=r==SCO_OK && age<8000;
    if(r!=SCO_OK && r!=SCO_NOT_FOUND) { Reason("peer/channel closed: owned crates cleaned"); Close(); return; }
    if(!fresh && linked) { Reason("peer heartbeat stale at 8000 ms: owned crates cleaned"); Close(); return; }
    linked=fresh;peerAge=fresh?age:0;inWorld=false;
    if(!area.valid && !Anchor()) return;
    if(fresh) {
        vx_peer_state state={};
        if(ipc->block_read(self,channel,VX_OFF_PEER_STATE,&state,sizeof(state))==SCO_OK) {
            bool valid=true; for(double value:state.feet) valid=valid && std::isfinite(value) && std::fabs(value)<=1e7;
            if(!valid) { Reason("malformed peer state: link closed, owned crates cleaned"); Close(); return; }
            inWorld=(state.flags & VX_PEER_IN_WORLD)!=0;
            std::memcpy(peerStatus,state.status,sizeof(peerStatus)); Sanitize(peerStatus,sizeof(peerStatus));
        }
        if(!Drain()) { Close(); return; }
    }
    vx_sc_state state={};state.time_ms=Now();state.epoch=epoch;state.block_size=static_cast<float>(cfg.size);
    double p[3],q[4];uint64_t zone=0;
    bool have=spatial->player_pose(p,q,&zone) && zone && ValidPose(p,q);
    if(have) state.flags|=VX_SC_SPAWNED;
    if(have && zone!=area.zone) {
        if(!METHOD(poses,pose_to_zone) || !poses->pose_to_zone(zone,area.zone,p,q,p,q) || !ValidPose(p,q)) { have=false; Reason("cross-zone pose unavailable: game.spatial.pose_to_zone absent/refused; no private fallback"); }
    }
    if(have) {
        state.flags|=VX_SC_ANCHORED;ToVoxel(p,state.feet);
        double right[3],forward[3],up[3];Axes(q,right,forward,up);
        const double d[]={Dot(forward,area.right),Dot(forward,area.up),-Dot(forward,area.forward)};
        constexpr double degrees=180.0/3.14159265358979323846;
        state.yaw=static_cast<float>(std::atan2(-d[0],d[2])*degrees);state.pitch=static_cast<float>(-std::asin(std::fmax(-1.0,std::fmin(1.0,d[1])))*degrees);
    }
    if(!have || std::fabs(state.feet[0])>2000 || std::fabs(state.feet[2])>2000 || std::fabs(state.feet[1]-VX_BASE_Y)>2000) state.flags|=VX_SC_PAUSED;
    // Focus/view dimensions deliberately remain unknown (zero): no product window globals.
    lastWire=state;
    ipc->block_write(self,channel,VX_OFF_SC_STATE,&state,sizeof(state));
    if(fresh && !(state.flags & VX_SC_PAUSED)) { Spawn();Scan(state.feet,state.time_ms); }
}
sco_result Toggle(const sco_arg*,uint32_t nargs,void*,char* out,uint32_t size) {
    if(nargs) return SCO_BAD_ARG;
    if(channel) { Close(); std::snprintf(out,size,"Voxel bridge closed; cleanup initiated (%zu pending)",cleanup.size()); return SCO_OK; }
    Services();
    if(!Ready()) { std::snprintf(out,size,"voxel_bridge unavailable: complete sco.ipc, teleport.spatial, game.entities.spawn/despawn and game.world.raycast required"); return SCO_UNAVAILABLE; }
    if(!cleanup.empty()) { std::snprintf(out,size,"voxel_bridge unavailable: pending owned cleanup (%zu)",cleanup.size()); return SCO_UNAVAILABLE; }
    LoadConfig();uint64_t id=0;sco_result r=ipc->create(self,VX_CHANNEL_NAME,VX_CHANNEL_BYTES,VX_LAYOUT_ID,VX_LAYOUT_VERSION,&id);
    if(r==SCO_OK) r=ipc->ring_init(self,id,VX_OFF_TO_PEER,VX_RING_TO_PEER,SCO_IPC_TO_PEER);
    if(r==SCO_OK) r=ipc->ring_init(self,id,VX_OFF_FROM_PEER,VX_RING_FROM_PEER,SCO_IPC_FROM_PEER);
    if(r!=SCO_OK) { if(id) ipc->close(self,id); std::snprintf(out,size,"voxel_bridge unavailable: IPC open failed (%d)",static_cast<int>(r)); return r; }
    channel=id;error[0]=0;
    // Keep epochs monotonic across rapid close/re-open within one module lifetime.
    if (++epoch==0) ++epoch;
    std::snprintf(out,size,"Voxel bridge open; waiting for local peer"); return SCO_OK;
}
sco_result AnchorCommand(const sco_arg*,uint32_t nargs,void*,char* out,uint32_t size) {
    if(nargs) return SCO_BAD_ARG;
    if(!channel) { std::snprintf(out,size,"anchor unavailable: open voxel bridge first"); return SCO_UNAVAILABLE; }
    bool ok=Anchor();std::snprintf(out,size,"%s",ok?"Building area made here":error);return ok?SCO_OK:SCO_UNAVAILABLE;
}
sco_result Status(const sco_arg*,uint32_t nargs,void*,char* out,uint32_t size) {
    if(nargs) return SCO_BAD_ARG;
    std::snprintf(out,size,"Voxel bridge: link %s, peer %s, area %s, %zu crates (%zu to spawn, %u not shown), cleanup %zu; %s; G2 relocation not approved: unavailable; legacy txt import unavailable: use settings; focus/view unavailable; Voxel panel %s",channel?"open":"closed",linked?"linked":"not answering",area.valid?"made":"none",crates.size(),pending.size(),dropped,cleanup.size(),error,panelLive?"available":"unavailable (sco.widgets absent/refused)");return SCO_OK;
}
void PanelEvent(const char* id,const sco_widget_event* event,void*) {
    if(!api || !panelLive || !event || event->size<sizeof(*event) || std::strcmp(id,"voxel_bridge.voxel") || event->kind!=SCO_WIDGET_BUTTON) return;
    if(event->node_id==1) Toggle(nullptr,0,nullptr,uiReply,sizeof(uiReply));
    else if(event->node_id==2) AnchorCommand(nullptr,0,nullptr,uiReply,sizeof(uiReply));
    UpdatePanel();
}
void UpdatePanel() {
    if(!api || !METHOD(widgets,set_panel) || !METHOD(widgets,remove_panel)) { panelLive=false;return; }
    char lines[8][256]={};
    std::snprintf(lines[0],sizeof(lines[0]),"Link: %s; peer %s%s (%u ms)",channel?"open":"closed",linked?"linked":"not answering",inWorld?" in its world":"",peerAge);
    std::snprintf(lines[1],sizeof(lines[1]),"Voxel game: %s",peerStatus);
    std::snprintf(lines[2],sizeof(lines[2]),"Last message: %s",peerLog);
    std::snprintf(lines[3],sizeof(lines[3]),"Area: %s%s; block %.0f %.0f %.0f",area.valid?(areaName[0]?areaName:"observed zone"):"none",(lastWire.flags & VX_SC_PAUSED)?" (outside/paused)":"",lastWire.feet[0],lastWire.feet[1],lastWire.feet[2]);
    std::snprintf(lines[4],sizeof(lines[4]),"Crates: %zu (%zu pending, %u not shown); cleanup: %zu",crates.size(),pending.size(),dropped,cleanup.size());
    std::snprintf(lines[5],sizeof(lines[5]),"Blocks: %s, %.2f m",cfg.block,cfg.size);
    std::snprintf(lines[6],sizeof(lines[6]),"%s",uiReply);
    std::snprintf(lines[7],sizeof(lines[7]),"%s",error);
    sco_widget_node nodes[12]={};
    for(unsigned i=0;i<12;++i) { nodes[i].size=sizeof(nodes[i]);nodes[i].id=i+1;nodes[i].kind=i<2?SCO_WIDGET_BUTTON:SCO_WIDGET_LABEL; }
    nodes[0].label=channel?"Close the bridge (Ctrl+F9)":"Open the bridge (Ctrl+F9)";
    nodes[1].label="Move the area here";
    for(unsigned i=0;i<8;++i) nodes[i+2].label=lines[i];
    nodes[10].label="Follow voxel player unavailable: G2 relocation not approved; focus/view unknown";
    nodes[11].label="Legacy voxel_bridge.txt import unavailable: use block/size/pivot/radius settings";
    panelLive=widgets->set_panel(self,"voxel_bridge.voxel","Voxel",nodes,12,sizeof(nodes[0]),PanelEvent,nullptr)==SCO_OK;
}
}
extern "C" {
SCO_EXPORT const sco_plugin_info* sco_plugin_query() { return &info; }
SCO_EXPORT sco_result sco_plugin_load(const sco_api* a,sco_plugin* s) {
    if(!a || !s || !METHOD(a,register_command) || !METHOD(a,subscribe) || !METHOD(a,unsubscribe) || a->major!=SCO_API_MAJOR) return SCO_BAD_ARG;
    api=a;self=s;Services();
    const char* names[]={"voxel_bridge.toggle","voxel_bridge.anchor","voxel_bridge.status"};
    const sco_command_fn callbacks[]={Toggle,AnchorCommand,Status};
    for(unsigned i=0;i<3;++i) { sco_command c={};c.size=sizeof(c);c.name=names[i];c.title=names[i];c.fn=callbacks[i];sco_result r=api->register_command(self,&c);if(r!=SCO_OK) return r; }
    sco_result result=api->subscribe(self,"tick",Tick,nullptr);
    ui=Query<sco_ui_v1>(SCO_UI_NAME,SCO_UI_VERSION_1_0);
    if(METHOD(ui,bind_hotkey)) ui->bind_hotkey(self,"ctrl+f9","voxel_bridge.toggle",nullptr,0);
    widgets=Query<sco_widgets_v1>(SCO_WIDGETS_NAME,SCO_WIDGETS_VERSION_1_0);
    UpdatePanel();
    return result;
}
SCO_EXPORT void sco_plugin_unload() {
    if(!api) return;
    api->unsubscribe(self,"tick",Tick);
    if(METHOD(ui,unbind_hotkey)) ui->unbind_hotkey(self,"ctrl+f9");
    if(METHOD(widgets,remove_panel)) widgets->remove_panel(self,"voxel_bridge.voxel");
    panelLive=false;widgets=nullptr;uiReply[0]=0;
    Close();RetryCleanup(); // provider release hook owns any still-pending entities after unload
    cleanup.clear();api=nullptr;self=nullptr;ipc=nullptr;spatial=nullptr;poses=nullptr;entities=nullptr;world=nullptr;ui=nullptr;
}
}
