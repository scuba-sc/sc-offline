// SPDX-License-Identifier: GPL-3.0-only
// External consumer: host-owned G13 control, sco.net channels, copied spatial
// observations and owner-scoped ghosts. Never sockets, private engine or G2.
#include <sco_api.h>
#include <sco_net.h>
#include <sco_storage.h>
#include <sco_widgets.h>
#include <sc_private_session.h>
#include <sc_spatial.h>
#include <sc_game_spatial.h>
#include <sc_actors.h>
#include <sc_entities.h>
#include <sc_vehicles.h>
#include "multiplayer_wire.h"
#include "relay_wire.h"
#include "motion.h"
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
#define METHOD(t,m) ((t) && (t)->size >= offsetof(std::remove_pointer_t<decltype(t)>,m)+sizeof((t)->m) && (t)->m)
const sco_api* api=nullptr;
sco_plugin* self=nullptr;
const sco_net_v1* net=nullptr;
const sc_private_session_v1* broker=nullptr;
const sco_storage_v1* store=nullptr;
const sco_widgets_v1* widgets=nullptr;
const sc_spatial_v1* spatial=nullptr;
const sc_game_spatial_v1* poses=nullptr;
const sc_actors_v1* actors=nullptr;
const sc_entities_v1* entities=nullptr;
const sc_vehicles_v1* vehicles=nullptr;
const sco_plugin_info info={sizeof(sco_plugin_info),SCO_API_MAJOR,SCO_API_MINOR,"multiplayer","1.0.0","scubamount"};
namespace rw=sco::relay::wire;
enum ChannelIndex : unsigned { kPoseChannel,kSpawnChannel,kDespawnChannel,kRelayChannel,kNearUpChannel,kChannelCount };
constexpr const char* channels[]={"multiplayer.pose","multiplayer.spawn","multiplayer.despawn","multiplayer.relay","multiplayer.near_up"};
static_assert(sizeof(channels)/sizeof(channels[0])==kChannelCount,"one name per channel");
constexpr uint64_t kHostPeer=1; // sco.net: the session host is always peer 1
constexpr const char* panelId="multiplayer.session";
constexpr const char* defaultAvatar="PU_Human-Microtech-DeskWorker-Male-DistroHub_Civilian_01";
struct Settings {
    char name[65]="Pilot",address[16]="",avatar[64]="",pass[257]="";
    int hostPort=64091,joinPort=64091;
    bool showShips=true,remember=false;
} cfg;
char reply[256]="No session. Host/Join require fresh local activation; default loopback only.";
char reason[128]="";
bool panelLive=false,wasActive=false,announced=false;
unsigned registeredChannels=0;
uint32_t sequence=0;
uint64_t lastSend=0,shipId=0;
char announcedShip[64]="";
struct Ghost { uint64_t id=0,since=0,lastAttempt=0; char cls[64]=""; bool actor=false; };
struct Peer {
    uint64_t id=0;
    char name[65]="",avatarClass[64]="",shipClass[64]="";
    mpmotion::Track track; // this player's last poses: the ghosts are drawn from them (motion.h)
    Ghost avatar{},ship{};
};
std::vector<Peer> peers;
struct Removal { uint64_t id; bool actor; };
std::vector<Removal> cleanup; // bounded by ghost budget, never pruned by ambiguous alive=0
// A sco-relay server hosts the session and says so with multiplayer.relay: who is in, in which slot.
// It is a server, never a player: it gets no peer record and is not counted or listed.
struct Relay {
    bool seen=false,nearby=false;
    uint16_t maxPlayers=0;
    int slot=-1;uint8_t generation=0; // this player's seat in the last roster; slot -1: not listed
    bool joinLogged=false;            // the "joined relay server" line was written (the first roster that listed this player)
    std::vector<uint64_t> players;    // sorted peer ids of that roster
    unsigned bad=0;                   // malformed relay messages this session
    bool nearWarned=false;            // the "cannot listen on multiplayer.near.<slot>" warning was written this session
} relay;
uint64_t Now() { return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
void Wipe(char* s,size_t n) { volatile char* p=s;while(n--) *p++=0; }
template<size_t N> bool Text(char (&out)[N],const char* value,size_t max=N-1) {
    if(!value) return false;
    size_t n=0;for(;n<=max && value[n];++n) if(static_cast<unsigned char>(value[n])<0x20) return false;
    if(n>max || n>=N) return false;
    std::memcpy(out,value,n);out[n]=0;return true;
}
// One line in mod.log as "[multiplayer] text". Never a passphrase, an address or a player's name.
void Log(sco_log_level level,const char* format,...) {
    if(!METHOD(api,log) || !self) return;
    char text[320];va_list args;va_start(args,format);
    std::vsnprintf(text,sizeof(text),format,args);va_end(args);
    text[sizeof(text)-1]=0;api->log(self,level,text);
}
unsigned long long Ull(uint64_t v) { return static_cast<unsigned long long>(v); }
template<class T> const T* Query(const char* name,uint32_t version) {
    const void* table=nullptr;
    return METHOD(api,query_service) && api->query_service(name,version,&table)==SCO_OK ? static_cast<const T*>(table):nullptr;
}
void Services() {
    broker=Query<sc_private_session_v1>(SC_PRIVATE_SESSION_NAME,SC_PRIVATE_SESSION_VERSION);
    spatial=Query<sc_spatial_v1>(SC_SPATIAL_SERVICE_NAME,SC_SPATIAL_SERVICE_VERSION);
    poses=Query<sc_game_spatial_v1>(SC_GAME_SPATIAL_NAME,SC_GAME_SPATIAL_VERSION);
    actors=Query<sc_actors_v1>(SC_ACTORS_NAME,SC_ACTORS_VERSION_1_0);
    entities=Query<sc_entities_v1>(SC_ENTITIES_NAME,SC_ENTITIES_VERSION_1_0);
    vehicles=Query<sc_vehicles_v1>(SC_VEHICLES_SERVICE_NAME,SC_VEHICLES_SERVICE_VERSION);
}
bool Active() { return METHOD(net,is_active) && net->is_active(); }
sc_private_session_state_v1 State() {
    sc_private_session_state_v1 out{};out.size=sizeof(out);
    if(!METHOD(broker,get_state) || broker->get_state(&out)!=SCO_OK) out.state=SC_PRIVATE_SESSION_IDLE;
    return out;
}
void UpdatePanel();
bool Read(const char* key,char* out,uint32_t cap) {
    if(!METHOD(store,get) || cap<2) return false;
    std::vector<char> buf(cap,0);uint32_t size=cap-1;
    if(store->get(self,key,buf.data(),&size)!=SCO_OK || size>=cap || std::memchr(buf.data(),0,size)) return false;
    std::memcpy(out,buf.data(),size);out[size]=0;return true;
}
int ReadInt(const char* key,int fallback,int minimum,int maximum) {
    char text[24];if(!Read(key,text,sizeof(text))) return fallback;
    char* end=nullptr;const long v=std::strtol(text,&end,10);
    return !text[0] || *end || v<minimum || v>maximum ? fallback:static_cast<int>(v);
}
void LoadSettings() {
    cfg=Settings();Text(cfg.avatar,defaultAvatar);
    char text[257];
    if(Read("name",text,sizeof(text)) && text[0]) Text(cfg.name,text);
    if(Read("join_address",text,sizeof(text))) Text(cfg.address,text);
    if(Read("avatar_class",text,sizeof(text)) && mpwire::ClassOk(text,std::strlen(text))) Text(cfg.avatar,text);
    cfg.hostPort=ReadInt("host_port",64091,1024,65535);
    cfg.joinPort=ReadInt("join_port",64091,1024,65535);
    cfg.showShips=ReadInt("show_ships",1,0,1)!=0;
    cfg.remember=ReadInt("remember_passphrase",0,0,1)!=0;
    if(cfg.remember && Read("passphrase",text,sizeof(text))) Text(cfg.pass,text);
    Wipe(text,sizeof(text));
}
sco_result SaveSettings() {
    if(!METHOD(store,put) || !METHOD(store,del) || !METHOD(store,begin) || !METHOD(store,commit) || !METHOD(store,rollback)) return SCO_UNAVAILABLE;
    sco_result r=store->begin(self);if(r!=SCO_OK) return r;
    const auto put=[&](const char* key,const char* value) { if(r==SCO_OK) r=store->put(self,key,value,static_cast<uint32_t>(std::strlen(value))); };
    const auto number=[&](const char* key,int v) { char s[24];std::snprintf(s,sizeof(s),"%d",v);put(key,s); };
    put("name",cfg.name);put("join_address",cfg.address);put("avatar_class",cfg.avatar);
    number("host_port",cfg.hostPort);number("join_port",cfg.joinPort);number("show_ships",cfg.showShips?1:0);number("remember_passphrase",cfg.remember?1:0);
    if(cfg.remember) put("passphrase",cfg.pass);
    else if(r==SCO_OK) { r=store->del(self,"passphrase");if(r==SCO_NOT_FOUND) r=SCO_OK; }
    if(r==SCO_OK) r=store->commit(self);
    if(r!=SCO_OK) store->rollback(self);
    return r;
}
void RetryCleanup() {
    for(auto it=cleanup.begin();it!=cleanup.end();) {
        sco_result r=SCO_UNAVAILABLE;
        if(it->actor && METHOD(actors,despawn)) r=actors->despawn(self,it->id);
        if(!it->actor && METHOD(entities,despawn)) r=entities->despawn(self,it->id);
        if(r==SCO_OK) it=cleanup.erase(it);else ++it;
    }
}
void Remove(Ghost& g) {
    if(g.id) cleanup.push_back({g.id,g.actor});
    g=Ghost();
}
void OnNear(uint64_t sender,const void* data,uint32_t len,void*);
void UpdateNearChannel();
char nearChannel[48]=""; // multiplayer.near.<slot> while this player listens on it, else empty
// Nearby-only: the relay server sends a player the poses of the players near it on a channel of its own slot,
// multiplayer.near.<slot>, FROM_HOST. The slot is the one the latest roster gave this player.
void SetNearChannel(const char* wanted) {
    if(!std::strcmp(wanted,nearChannel)) return;
    if(nearChannel[0]) { if(METHOD(net,unregister_channel)) net->unregister_channel(self,nearChannel);nearChannel[0]=0; }
    if(!wanted[0] || !METHOD(net,register_channel)) return;
    const sco_result r=net->register_channel(self,wanted,SCO_NET_FROM_HOST,static_cast<uint32_t>(rw::kNearDownMaxBytes),OnNear,nullptr);
    if(r==SCO_OK) std::snprintf(nearChannel,sizeof(nearChannel),"%s",wanted);
    else if(!relay.nearWarned) {
        // Once per session: every roster (every announce interval) tries again, and a refusal would repeat for the whole session.
        // Only this player's own pose falls back (SendPose sends it on multiplayer.pose); the copies the server forwards
        // to this slot go nowhere, so this player sees nobody it is told about that way.
        relay.nearWarned=true;
        Log(SCO_LOG_WARN,"cannot listen on %s (%d): this player sends its poses on multiplayer.pose and will not see the players the server forwards on that channel",wanted,static_cast<int>(r));
    }
}
void ResetRelay() {
    if(relay.seen) Log(SCO_LOG_INFO,"left the relay session");
    SetNearChannel("");
    relay=Relay();
}
void Clear() { for(auto& p:peers) { Remove(p.avatar);Remove(p.ship); }peers.clear();RetryCleanup();announced=false;announcedShip[0]=0;shipId=0;ResetRelay(); }
Peer* Find(uint64_t id) { for(auto& p:peers) if(p.id==id) return &p;return nullptr; }
void DropPeer(uint64_t id) {
    auto it=std::find_if(peers.begin(),peers.end(),[&](const Peer& p){return p.id==id;});
    if(it!=peers.end()) { Remove(it->avatar);Remove(it->ship);peers.erase(it);RetryCleanup(); }
}
Peer* Add(uint64_t id) {
    if(!id || !METHOD(net,self_peer) || id==net->self_peer() || !Active()) return nullptr;
    if(relay.seen && id==kHostPeer) return nullptr; // the relay server is not a player
    if(auto* p=Find(id)) return p;
    // Only authenticated session members get a record. Never trust raw sender IDs.
    sco_net_peer members[SCO_NET_MAX_PEERS]{};uint32_t count=SCO_NET_MAX_PEERS;
    if(!METHOD(net,get_peers) || net->get_peers(members,&count)!=SCO_OK || count>SCO_NET_MAX_PEERS || peers.size()>=SCO_NET_MAX_PEERS-1) return nullptr;
    bool member=false;for(uint32_t i=0;i<count;++i) if(members[i].peer_id==id) member=true;
    if(!member) return nullptr;
    peers.emplace_back();auto& p=peers.back();p.id=id;
    if(!METHOD(net,get_peer_name) || net->get_peer_name(id,p.name,sizeof(p.name))!=SCO_OK) std::snprintf(p.name,sizeof(p.name),"peer %llu",static_cast<unsigned long long>(id));
    p.name[sizeof(p.name)-1]=0;return &p;
}
void IngestPose(uint64_t sender,const mpwire::Pose& next) {
    Peer* p=Add(sender);if(!p) return;
    p->track.Add(next,Now()); // a pose that is not newer is dropped; a silence, a teleport or other frames start the track over
}
void OnPose(uint64_t sender,const void* data,uint32_t len,void*) {
    mpwire::Pose next{};if(!mpwire::DecodePose(data,len,next)) return;
    IngestPose(sender,next);
}
void OnSpawn(uint64_t sender,const void* data,uint32_t len,void*) {
    mpwire::Spawn s{};if(!mpwire::DecodeSpawn(data,len,sender,s)) return;
    Peer* p=Add(sender);if(!p) return;
    char* dst=s.kind==mpwire::kAvatar?p->avatarClass:p->shipClass;
    Ghost& g=s.kind==mpwire::kAvatar?p->avatar:p->ship;
    if(std::strcmp(dst,s.cls)) Remove(g);
    std::memcpy(dst,s.cls,sizeof(s.cls));RetryCleanup();
}
void OnDespawn(uint64_t sender,const void* data,uint32_t len,void*) {
    uint8_t kind=0;if(!mpwire::DecodeDespawn(data,len,sender,kind)) return;
    Peer* p=Find(sender);if(!p) return;
    if(kind==mpwire::kAvatar) { Remove(p->avatar);p->avatarClass[0]=0; }
    else { Remove(p->ship);p->shipClass[0]=0; }
    RetryCleanup();
}
void NoteBadRelay(const char* what) {
    // One line per session, then only the count (shown by multiplayer.status): a broken or foreign server cannot flood mod.log.
    if(++relay.bad==1) Log(SCO_LOG_WARN,"ignored a malformed %s message from the session host; check that host and players run the same plugin build",what);
}
// multiplayer.relay: the relay server says who is in the session. FROM_HOST, so sco.net delivers it only
// from the host; this checks again. It also tells this player's slot and whether nearby-only is on.
void OnRelay(uint64_t sender,const void* data,uint32_t len,void*) {
    if(sender!=kHostPeer || !Active() || !METHOD(net,self_peer)) return;
    const uint64_t me=net->self_peer();
    if(!me || me==kHostPeer) return; // the host reads no roster from itself
    rw::RelayRoster roster;
    if(!rw::DecodeRelay(static_cast<const uint8_t*>(data),len,&roster)) { NoteBadRelay("multiplayer.relay");return; }
    const bool first=!relay.seen;
    int slot=-1;uint8_t generation=0;std::vector<uint64_t> ids;
    for(const auto& e:roster.players) { ids.push_back(e.peer);if(e.peer==me) { slot=e.slot;generation=e.generation; } }
    std::sort(ids.begin(),ids.end());
    const bool changed=first || ids!=relay.players || slot!=relay.slot || generation!=relay.generation || roster.nearbyOnly!=relay.nearby;
    relay.seen=true;relay.nearby=roster.nearbyOnly;relay.maxPlayers=roster.maxPlayers;
    relay.slot=slot;relay.generation=generation;relay.players=std::move(ids);
    if(first) {
        DropPeer(kHostPeer); // the join event may have made a record for the server before it said what it is
        Log(SCO_LOG_INFO,"relay server found: peer %llu hosts the session and relays players; it is not shown or counted as a player",Ull(kHostPeer));
    }
    if(changed) {
        if(slot<0) Log(SCO_LOG_WARN,"relay roster: %zu of %u players in the session, this player is not listed yet",relay.players.size(),static_cast<unsigned>(relay.maxPlayers));
        else if(!relay.joinLogged) {
            relay.joinLogged=true;
            Log(SCO_LOG_INFO,"joined relay server as peer %llu in slot %d (generation %u): %zu of %u players in the session, nearby-only %s",Ull(me),slot,static_cast<unsigned>(generation),relay.players.size(),static_cast<unsigned>(relay.maxPlayers),relay.nearby?"on":"off");
        } else Log(SCO_LOG_INFO,"relay roster: %zu of %u players in the session, slot %d (generation %u), nearby-only %s",relay.players.size(),static_cast<unsigned>(relay.maxPlayers),slot,static_cast<unsigned>(generation),relay.nearby?"on":"off");
    }
    UpdateNearChannel();
}
// multiplayer.near.<slot>, FROM_HOST: a pose the server forwarded because its sender is near this player. The
// header names the sender and the generation of this slot; a message for an earlier occupant of the slot is
// dropped, and so is anything not the contract. The pose then goes the way a pose from multiplayer.pose goes.
void OnNear(uint64_t sender,const void* data,uint32_t len,void*) {
    if(sender!=kHostPeer || !relay.seen || relay.slot<0 || !relay.nearby || !nearChannel[0] || !Active()) return;
    rw::NearDown down;
    const rw::NearDownVerdict verdict=rw::DecodeNearDown(static_cast<const uint8_t*>(data),len,relay.generation,&down);
    if(verdict==rw::NearDownVerdict::OtherGeneration) return;
    mpwire::Pose next{};
    if(verdict!=rw::NearDownVerdict::Accepted || !mpwire::DecodePose(down.pose,down.poseLen,next)) { NoteBadRelay("multiplayer.near");return; }
    IngestPose(down.sender,next);
}
// The channel the host never sends on and a player never receives on needs a callback all the same.
void OnNeverReceived(uint64_t,const void*,uint32_t,void*) {}
// Where this player's pose goes: with nearby-only on, to the server on multiplayer.near_up (the server forwards it to
// the players near this one; never also on multiplayer.pose, which would hand it to everyone), else to the other players.
void SendPose(const mpwire::Pose& pose,const uint8_t* data,size_t len) {
    if(relay.seen && relay.nearby && relay.slot>=0 && nearChannel[0]) {
        const auto& top=pose.lv[pose.levels-1]; // the outermost zone is the area; the position is in that zone
        const std::vector<uint8_t> up=rw::EncodeNearUp(rw::Fnv1a64(top.name,std::strlen(top.name)),top.pos[0],top.pos[1],top.pos[2],data,len);
        if(!up.empty()) net->send_channel(self,channels[kNearUpChannel],up.data(),static_cast<uint32_t>(up.size()));
        return;
    }
    net->send_channel(self,channels[kPoseChannel],data,static_cast<uint32_t>(len));
}
void UpdateNearChannel() {
    char wanted[48]="";
    if(relay.seen && relay.nearby && relay.slot>=0) std::snprintf(wanted,sizeof(wanted),"%s%d",rw::kNearDownPrefix,relay.slot);
    if(!std::strcmp(wanted,nearChannel)) return;
    const bool was=nearChannel[0]!=0;
    SetNearChannel(wanted);
    if(nearChannel[0]) Log(SCO_LOG_INFO,"nearby-only: poses go to the relay server on multiplayer.near_up and come back on %s",nearChannel);
    else if(was) Log(SCO_LOG_INFO,"nearby-only is off: poses go to the other players on multiplayer.pose");
}
void SendSpawn(uint8_t kind,const char* cls) {
    if(!METHOD(net,send_channel) || !METHOD(net,self_peer)) return;
    mpwire::Spawn s{};s.kind=kind;s.sessionId=mpwire::SessionId(net->self_peer(),kind);std::snprintf(s.cls,sizeof(s.cls),"%s",cls);
    uint8_t buf[mpwire::kMaxSpawn];const size_t n=mpwire::EncodeSpawn(s,buf,sizeof(buf));
    if(n) net->send_channel(self,channels[kSpawnChannel],buf,static_cast<uint32_t>(n));
}
void SendDespawn(uint8_t kind) {
    if(!Active() || !METHOD(net,send_channel) || !METHOD(net,self_peer)) return;
    // Legacy v1 despawn encoder was inconsistent (16 written, 12 declared).
    // v1 reader expects 16; keep the header and declare the full wire extent.
    uint8_t buf[mpwire::kDespawn];const size_t n=mpwire::EncodeDespawn(kind,mpwire::SessionId(net->self_peer(),kind),buf,sizeof(buf));
    if(n) net->send_channel(self,channels[kDespawnChannel],buf,static_cast<uint32_t>(n));
}
bool Capture(mpwire::Pose& out) {
    if(!METHOD(spatial,player_pose) || !METHOD(poses,zone_record) || !METHOD(poses,pose_to_zone)) return false;
    double pos[3],rot[4];uint64_t zone=0;
    if(!spatial->player_pose(pos,rot,&zone) || !zone || !mpwire::PoseOk(pos,rot)) return false;
    std::array<sc_game_zone,16> chain{};unsigned count=0;uint64_t id=zone;
    while(id) {
        if(count==chain.size()) return false;
        for(unsigned i=0;i<count;++i) if(chain[i].zone_id==id) return false;
        auto& z=chain[count];z.size=sizeof(z);
        if(!poses->zone_record(id,&z) || !z.epoch || !z.root_id || z.zone_id!=id || (z.flags&SC_GAME_ZONE_NAME_TRUNCATED) || !std::memchr(z.name,0,sizeof(z.name))) return false;
        ++count;id=z.parent_id;
    }
    sc_game_zone final{};final.size=sizeof(final);
    if(!poses->zone_record(zone,&final)) return false;
    for(unsigned i=0;i<count;++i) if(chain[i].epoch!=final.epoch) return false;
    out=mpwire::Pose();out.seq=++sequence;
    const unsigned levels=std::min(count,static_cast<unsigned>(mpwire::kMaxLevels));
    // Preserve closest frames and the complete root when the ancestry is longer.
    for(unsigned i=0;i<levels;++i) {
        const unsigned index=(count>levels && i==levels-1)?count-1:i;const auto& z=chain[index];
        auto& l=out.lv[i];const size_t n=std::strlen(z.name);
        if(!mpwire::ZoneNameOk(z.name,n)) return false;
        std::memcpy(l.name,z.name,n+1);
        if(!poses->pose_to_zone(zone,z.zone_id,pos,rot,l.pos,l.rot) || !mpwire::PoseOk(l.pos,l.rot)) return false;
    }
    out.levels=static_cast<uint8_t>(levels);
    shipId=0;
    if(cfg.showShips && METHOD(vehicles,player_ship) && vehicles->player_ship(&shipId)==SCO_OK && shipId && METHOD(entities,get_transform)) {
        double sp[3],sq[4];uint64_t sz=0;
        if(entities->get_transform(shipId,sp,sq,&sz)==SCO_OK && sz && mpwire::PoseOk(sp,sq)) {
            // Ship section starts at 1, outside the player's own innermost frame.
            if(levels>1) {
                out.ship=true;out.shipFirst=1;
                for(unsigned i=1;i<levels;++i) {
                    const unsigned index=(count>levels && i==levels-1)?count-1:i;
                    if(!poses->pose_to_zone(sz,chain[index].zone_id,sp,sq,out.shipPos[i],out.shipRot[i]) || !mpwire::PoseOk(out.shipPos[i],out.shipRot[i])) { out.ship=false;break; }
                }
            }
        }
    }
    return true;
}
void Announce(bool force) {
    if(force || !announced) { SendSpawn(mpwire::kAvatar,cfg.avatar);announced=true; }
    char cls[64]="";uint32_t size=sizeof(cls);
    if(cfg.showShips && shipId && METHOD(entities,class_of) && entities->class_of(shipId,cls,&size)==SCO_OK && std::memchr(cls,0,sizeof(cls)) && mpwire::ClassOk(cls,std::strlen(cls))) {
        if(force || std::strcmp(announcedShip,cls)) { SendSpawn(mpwire::kShip,cls);std::memcpy(announcedShip,cls,sizeof(cls)); }
    } else if(announcedShip[0]) { SendDespawn(mpwire::kShip);announcedShip[0]=0; }
}
void OnPeer(const char*,const void* data,void*) {
    if(!data) return;
    const auto* e=static_cast<const sco_net_peer_event*>(data);if(e->size<sizeof(*e)) return;
    // The host (peer 1) may be a relay server, which is not a player and does not say so until its first
    // multiplayer.relay. A host that is a player gets its record from its first pose or spawn instead.
    if(e->what==SCO_NET_PEER_JOINED) { if(e->peer.peer_id!=kHostPeer) Add(e->peer.peer_id);if(Active()) Announce(true); }
    if(e->what==SCO_NET_PEER_LEFT) DropPeer(e->peer.peer_id);
}
void OnState(const char*,const void* data,void*) {
    if(!data) return;
    const auto* e=static_cast<const sco_net_state_event*>(data);if(e->size<sizeof(*e)) return;
    if(!e->active) { Clear();std::memcpy(reason,e->reason,sizeof(reason));reason[sizeof(reason)-1]=0; }
    else { reason[0]=0;announced=false;lastSend=0;ResetRelay(); }
}
bool Resolve(const Peer& p,bool ship,uint64_t now,uint64_t& zone,double pos[3],double rot[4]) {
    const mpwire::Pose* latest=p.track.Newest();
    if(!latest || !METHOD(poses,find_zone) || !METHOD(poses,zone_record)) return false;
    const unsigned first=ship?latest->shipFirst:0;
    if(ship && !latest->ship) return false;
    for(unsigned i=first;i<latest->levels;++i) {
        sc_game_zone z{};z.size=sizeof(z);
        if(!poses->find_zone(latest->lv[i].name,&z) || !z.zone_id || !z.epoch || !z.root_id || (z.flags&SC_GAME_ZONE_NAME_TRUNCATED)) continue;
        sc_game_zone current{};current.size=sizeof(current);
        if(!poses->zone_record(z.zone_id,&current) || current.epoch!=z.epoch || std::strcmp(current.name,latest->lv[i].name)) return false;
        // Where the ghost is now: between the buffered samples, kept moving for a short while when the newest is late,
        // and at the newest sample at once after a silence, a teleport or a change of frames (motion.h).
        if(!p.track.Sample(now,ship,i,pos,rot)) return false;
        zone=z.zone_id;
        return mpwire::PoseOk(pos,rot);
    }
    return false;
}
void UpdateGhost(Peer& p,bool ship,uint64_t now) {
    Ghost& g=ship?p.ship:p.avatar;const char* cls=ship?p.shipClass:p.avatarClass;
    // Remote avatar class is informational: instantiate only the locally chosen
    // avatar, never a peer-chosen actor archetype. Ship classes remain provider-validated.
    if(!ship && cls[0]) cls=cfg.avatar;
    if(!cls[0] || (ship && !cfg.showShips) || !p.track.Valid() || now-p.track.LastArrivalMs()>10000) { Remove(g);return; }
    uint64_t zone=0;double pos[3],rot[4];
    if(!Resolve(p,ship,now,zone,pos,rot) || !METHOD(spatial,player_pose) || !METHOD(poses,pose_to_zone)) { Remove(g);return; }
    double local[3],q[4],here[3],r[4];uint64_t playerZone=0;
    if(!spatial->player_pose(local,q,&playerZone) || !playerZone || !poses->pose_to_zone(playerZone,zone,local,q,here,r)) { Remove(g);return; }
    double distance=0;for(unsigned i=0;i<3;++i) distance+=(here[i]-pos[i])*(here[i]-pos[i]);
    if(!std::isfinite(distance) || distance>1e8) { Remove(g);return; }
    if(g.id && std::strcmp(g.cls,cls)) Remove(g);
    if(!g.id) {
        // Failed cleanup owns the budget until successfully initiated/provider unload.
        if(peers.size()*2+cleanup.size()>32 || (g.lastAttempt && now-g.lastAttempt<30000)) return;
        g.lastAttempt=now;uint64_t id=0;sco_result result=SCO_UNAVAILABLE;
        if(ship && METHOD(entities,spawn) && METHOD(entities,despawn)) result=entities->spawn(self,cls,zone,pos,rot,&id);
        if(!ship && METHOD(actors,spawn_npc) && METHOD(actors,despawn)) result=actors->spawn_npc(self,cls,zone,pos,&id);
        if(result!=SCO_OK || !id) return;
        g.id=id;g.actor=!ship;g.since=now;std::snprintf(g.cls,sizeof(g.cls),"%s",cls);
    }
    if(!METHOD(entities,alive) || !entities->alive(g.id)) {
        if(now-g.since>60000) Remove(g); // retain ownership removal; never recycle solely on alive=0
        return;
    }
    if(METHOD(entities,set_transform)) entities->set_transform(self,g.id,zone,pos,rot);
}
// What the status line and the panel add when the session host is a relay server.
void RelayNote(char* out,size_t cap) {
    if(!relay.seen) { out[0]=0;return; }
    char seat[24];
    if(relay.slot<0) std::snprintf(seat,sizeof(seat),"not listed");else std::snprintf(seat,sizeof(seat),"slot %d",relay.slot);
    std::snprintf(out,cap,"; via relay server (not a player), %s, nearby-only %s%s",seat,relay.nearby?"on":"off",relay.bad?", malformed relay messages ignored":"");
}
sco_result Status(const sco_arg*,uint32_t nargs,void*,char* out,uint32_t cap) {
    if(nargs) return SCO_BAD_ARG;
    const auto state=State();
    char note[128];RelayNote(note,sizeof(note));
    std::snprintf(out,cap,"Multiplayer: G13 %s; state %u; %zu other players%s; %zu cleanup pending; Host/Join UI %s; G2 goto unavailable%s%s",METHOD(broker,request_host)?"available":"unavailable (admission/provider)",state.state,peers.size(),note,cleanup.size(),panelLive?"available":"unavailable",reason[0]?"; ":"",reason);return SCO_OK;
}
sco_result Leave(const sco_arg*,uint32_t nargs,void*,char* out,uint32_t cap) {
    if(nargs) return SCO_BAD_ARG;
    if(Active()) { SendDespawn(mpwire::kAvatar);SendDespawn(mpwire::kShip); }
    const sco_result result=METHOD(broker,leave)?broker->leave(self):SCO_UNAVAILABLE;
    Clear();std::snprintf(out,cap,"%s",result==SCO_OK?"Left private session; ghost cleanup initiated":"Leave unavailable: G13 absent/admission refused or another owner");return result;
}
sco_result GoTo(const sco_arg* args,uint32_t nargs,void*,char* out,uint32_t cap) {
    if(nargs!=1 || !args || args[0].type!=SCO_ARG_STRING || !args[0].v.s) return SCO_BAD_ARG;
    std::snprintf(out,cap,"Player relocation unavailable: G2 mutation is not approved; no private fallback");return SCO_UNAVAILABLE;
}
void Control(bool host) {
    Services();
    if(!cfg.name[0] || std::strlen(cfg.pass)<SC_PRIVATE_SESSION_MIN_PASSPHRASE || cfg.hostPort<1024 || cfg.hostPort>65535 || cfg.joinPort<1024 || cfg.joinPort>65535) {
        std::snprintf(reply,sizeof(reply),"Pick a name, a 12-256 byte passphrase and ports 1024-65535");return;
    }
    sco_result result=SCO_UNAVAILABLE;
    if(host && METHOD(broker,request_host)) {
        sc_private_session_host_request_v1 r{};r.size=sizeof(r);r.consent=1;r.port=static_cast<uint16_t>(cfg.hostPort);r.max_peers=8;
        r.player_name=cfg.name;r.player_name_len=static_cast<uint32_t>(std::strlen(cfg.name));r.passphrase=cfg.pass;r.passphrase_len=static_cast<uint32_t>(std::strlen(cfg.pass));
        result=broker->request_host(self,&r);
    } else if(!host && METHOD(broker,request_join)) {
        sc_private_session_join_request_v1 r{};r.size=sizeof(r);r.consent=1;r.port=static_cast<uint16_t>(cfg.joinPort);r.address=cfg.address;r.address_len=static_cast<uint32_t>(std::strlen(cfg.address));
        r.player_name=cfg.name;r.player_name_len=static_cast<uint32_t>(std::strlen(cfg.name));r.passphrase=cfg.pass;r.passphrase_len=static_cast<uint32_t>(std::strlen(cfg.pass));
        result=broker->request_join(self,&r);
    }
    const sco_result saved=SaveSettings();
    // Do not retain an unremembered credential in plugin/UI state after request.
    if(!cfg.remember) Wipe(cfg.pass,sizeof(cfg.pass));
    std::snprintf(reply,sizeof(reply),"%s request %s (%d). Acceptance is not authenticated connection.%s",host?"Host":"Join",result==SCO_OK?"accepted":"refused",static_cast<int>(result),saved==SCO_OK?"":" Settings not saved.");
    // Only the outcome: no address, name or passphrase. The host broker audits its own lifecycle.
    Log(result==SCO_OK?SCO_LOG_INFO:SCO_LOG_WARN,"%s request %s (%d)",host?"host":"join",result==SCO_OK?"accepted: waiting for the session":"refused",static_cast<int>(result));
}
void PanelEvent(const char* id,const sco_widget_event* e,void*) {
    if(!panelLive || !id || std::strcmp(id,panelId) || !e || e->size<sizeof(*e)) return;
    bool save=false;
    if(e->kind==SCO_WIDGET_BUTTON) {
        if(e->node_id==1) Control(true);else if(e->node_id==2) Control(false);
        else if(e->node_id==3) Leave(nullptr,0,nullptr,reply,sizeof(reply));
        else if(e->node_id==13) GoTo(nullptr,0,nullptr,reply,sizeof(reply));
    } else if(e->kind==SCO_WIDGET_TEXT_INPUT && e->text) {
        if(e->node_id==4) save=Text(cfg.name,e->text);
        else if(e->node_id==5) save=Text(cfg.address,e->text);
        else if(e->node_id==6) Text(cfg.pass,e->text,255); // host widgets limit
        else if(e->node_id==9 && mpwire::ClassOk(e->text,std::strlen(e->text))) { save=Text(cfg.avatar,e->text);announced=false; }
    } else if(e->kind==SCO_WIDGET_SLIDER && std::isfinite(e->value) && e->value>=1024 && e->value<=65535 && e->value==std::floor(e->value)) {
        if(e->node_id==7) { cfg.hostPort=static_cast<int>(e->value);save=true; }
        else if(e->node_id==8) { cfg.joinPort=static_cast<int>(e->value);save=true; }
    } else if(e->kind==SCO_WIDGET_TOGGLE && (e->value==0 || e->value==1)) {
        if(e->node_id==10) { cfg.showShips=e->value!=0;save=true; }
        else if(e->node_id==11) { cfg.remember=e->value!=0;save=true; }
    }
    if(save && SaveSettings()!=SCO_OK) std::snprintf(reply,sizeof(reply),"Settings not saved: sco.storage unavailable/refused; session-only values retained");
    UpdatePanel();
}
void UpdatePanel() {
    if(!METHOD(widgets,set_panel) || !METHOD(widgets,remove_panel)) { panelLive=false;return; }
    const auto state=State();char note[128];RelayNote(note,sizeof(note));
    char status[256];std::snprintf(status,sizeof(status),"G13 %s; state %u; %zu peers%s, %zu cleanup pending",METHOD(broker,request_host)?"ready":"unavailable",state.state,peers.size(),note,cleanup.size());
    std::vector<std::string> peerText;std::vector<const char*> rows;
    for(const auto& p:peers) { char s[200];std::snprintf(s,sizeof(s),"%s: %s; avatar %s, ship %s",p.name,p.track.Valid()?"pose observed":"waiting for pose",p.avatar.id?"owned ghost":"not shown",p.ship.id?"owned ghost":"not shown");peerText.emplace_back(s); }
    for(const auto& s:peerText) rows.push_back(s.c_str());
    sco_widget_node nodes[17]{};
    for(unsigned i=0;i<17;++i) { nodes[i].size=sizeof(nodes[i]);nodes[i].id=i+1;nodes[i].kind=SCO_WIDGET_LABEL; }
    for(unsigned i=0;i<3;++i) nodes[i].kind=SCO_WIDGET_BUTTON;
    nodes[0].label="Host private session";nodes[1].label="Join private session";nodes[2].label="Leave";
    for(unsigned i:{3u,4u,5u,8u}) nodes[i].kind=SCO_WIDGET_TEXT_INPUT;
    nodes[3].label="Player name";nodes[3].text=cfg.name;nodes[4].label="Host numeric IPv4 address";nodes[4].text=cfg.address;
    nodes[5].label="Passphrase (12+ bytes)";nodes[5].text=std::strlen(cfg.pass)<=SCO_WIDGETS_MAX_TEXT?cfg.pass:"";nodes[5].flags=SCO_WIDGET_PASSWORD;
    for(unsigned i:{6u,7u}) { nodes[i].kind=SCO_WIDGET_SLIDER;nodes[i].minimum=1024;nodes[i].maximum=65535; }
    nodes[6].label="Host port";nodes[6].value=cfg.hostPort;nodes[7].label="Join port";nodes[7].value=cfg.joinPort;
    nodes[8].label="Local avatar archetype (npcs.txt class)";nodes[8].text=cfg.avatar;
    nodes[9].kind=SCO_WIDGET_TOGGLE;nodes[9].label="Show ships";nodes[9].value=cfg.showShips?1:0;
    nodes[10].kind=SCO_WIDGET_TOGGLE;nodes[10].label="Remember passphrase in plaintext local DB (unsafe)";nodes[10].value=cfg.remember?1:0;
    nodes[11].label=status;
    nodes[12].kind=SCO_WIDGET_BUTTON;nodes[12].label="Go to player (G2 unavailable)";nodes[12].flags=SCO_WIDGET_DISABLED;
    nodes[13].label=reply;nodes[14].label="No socket at load. Default loopback; host must explicitly configure private LAN bind + exact peers. Offline/EAC admission required.";
    nodes[15].kind=SCO_WIDGET_LIST;nodes[15].label="Other players";nodes[15].items=rows.empty()?nullptr:rows.data();nodes[15].item_count=static_cast<uint32_t>(rows.size());nodes[15].index=-1;
    nodes[16].label="Visual co-presence only: no shared physics, damage, missions or inventory. No auto-retry or internet allowlist.";
    panelLive=widgets->set_panel(self,panelId,"Multiplayer",nodes,17,sizeof(nodes[0]),PanelEvent,nullptr)==SCO_OK;
}
void Tick(const char*,const void*,void*) {
    Services();RetryCleanup();
    const bool active=Active() && State().state!=SC_PRIVATE_SESSION_IDLE;
    if(!active) { if(wasActive || !peers.empty()) Clear();wasActive=false;UpdatePanel();return; }
    wasActive=true;const uint64_t now=Now();
    if(!lastSend || now-lastSend>=100) {
        lastSend=now;mpwire::Pose pose{};
        if(Capture(pose) && METHOD(net,send_channel)) {
            uint8_t data[mpwire::kMaxPose];size_t len=mpwire::EncodePose(pose,data,sizeof(data));
            if(len) SendPose(pose,data,len);
        }
        Announce(false);
    }
    for(auto& p:peers) { UpdateGhost(p,true,now);UpdateGhost(p,false,now); }
    RetryCleanup();UpdatePanel();
}
void Unload() {
    if(!api) return;
    if(METHOD(broker,leave)) broker->leave(self);
    Clear();RetryCleanup(); // any refusal stays provider-owned through ordinary owner release
    if(METHOD(widgets,remove_panel)) widgets->remove_panel(self,panelId);
    if(METHOD(net,unregister_channel)) for(unsigned i=0;i<registeredChannels;++i) net->unregister_channel(self,channels[i]);
    if(METHOD(api,unsubscribe)) { api->unsubscribe(self,"tick",Tick);api->unsubscribe(self,SCO_NET_EVENT_STATE,OnState);api->unsubscribe(self,SCO_NET_EVENT_PEER,OnPeer); }
    Wipe(cfg.pass,sizeof(cfg.pass));cleanup.clear();peers.clear();panelLive=wasActive=false;registeredChannels=0;
    api=nullptr;self=nullptr;net=nullptr;broker=nullptr;store=nullptr;widgets=nullptr;spatial=nullptr;poses=nullptr;actors=nullptr;entities=nullptr;vehicles=nullptr;
}
}
extern "C" {
SCO_EXPORT const sco_plugin_info* sco_plugin_query() { return &info; }
SCO_EXPORT sco_result sco_plugin_load(const sco_api* a,sco_plugin* s) {
    if(!a || !s || a->major!=SCO_API_MAJOR || !METHOD(a,register_command) || !METHOD(a,subscribe) || !METHOD(a,unsubscribe)) return SCO_BAD_ARG;
    api=a;self=s;Services();net=Query<sco_net_v1>(SCO_NET_NAME,SCO_NET_VERSION_1_0);
    store=Query<sco_storage_v1>(SCO_STORAGE_NAME,SCO_STORAGE_VERSION_1_0);widgets=Query<sco_widgets_v1>(SCO_WIDGETS_NAME,SCO_WIDGETS_VERSION_1_0);LoadSettings();
    const char* names[]={"multiplayer.status","multiplayer.leave","multiplayer.goto"};const sco_command_fn callbacks[]={Status,Leave,GoTo};
    sco_arg_def who{};who.name="player";who.type=SCO_ARG_STRING;
    sco_result r=SCO_OK;
    for(unsigned i=0;i<3 && r==SCO_OK;++i) { sco_command c{};c.size=sizeof(c);c.name=names[i];c.title=names[i];c.fn=callbacks[i];if(i==2) { c.args=&who;c.nargs=1;c.arg_def_size=sizeof(who); }r=api->register_command(self,&c); }
    if(r==SCO_OK && METHOD(net,register_channel) && METHOD(net,unregister_channel)) {
        // Pose is unreliable and relayed between players; spawn and despawn are reliable. multiplayer.relay is
        // reliable and FROM_HOST: only a session host that speaks the relay contract (sco-relay) ever sends it.
        // multiplayer.near_up is unreliable and TO_HOST (the server reads it, it is not relayed); with nearby-only on, a
        // player's own multiplayer.near.<slot> is registered later, when the roster has told its slot.
        const uint32_t flags[kChannelCount]={0,SCO_NET_RELIABLE,SCO_NET_RELIABLE,SCO_NET_RELIABLE|SCO_NET_FROM_HOST,SCO_NET_TO_HOST};
        const uint32_t sizes[kChannelCount]={mpwire::kMaxPose,mpwire::kMaxSpawn,mpwire::kDespawn,static_cast<uint32_t>(rw::kRelayMaxBytes),static_cast<uint32_t>(rw::kNearUpMaxBytes)};
        const sco_net_on_message callbacksNet[kChannelCount]={OnPose,OnSpawn,OnDespawn,OnRelay,OnNeverReceived};
        for(unsigned i=0;i<kChannelCount && r==SCO_OK;++i) { r=net->register_channel(self,channels[i],flags[i],sizes[i],callbacksNet[i],nullptr);if(r==SCO_OK) ++registeredChannels; }
    }
    if(r==SCO_OK) r=api->subscribe(self,"tick",Tick,nullptr);
    if(r==SCO_OK) r=api->subscribe(self,SCO_NET_EVENT_STATE,OnState,nullptr);
    if(r==SCO_OK) r=api->subscribe(self,SCO_NET_EVENT_PEER,OnPeer,nullptr);
    if(r!=SCO_OK) { Unload();return r; }
    UpdatePanel();return SCO_OK;
}
SCO_EXPORT void sco_plugin_unload() { Unload(); }
}
