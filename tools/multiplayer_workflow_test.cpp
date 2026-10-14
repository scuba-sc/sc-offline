// SPDX-License-Identifier: GPL-3.0-only
// Actual independently loaded module, synthetic public services only. No sockets,
// game, private host API or authentic EAC/admission evidence.
#include <sco_api.h>
#include <sco_net.h>
#include <sco_storage.h>
#include <sco_widgets.h>
#include <sc_private_session.h>
#include <sc_spatial.h>
#include <sc_game_spatial.h>
#include <sc_entities.h>
#include <sc_actors.h>
#include "../plugins/multiplayer/multiplayer_wire.h"
#include "../plugins/multiplayer/relay_wire.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
#include <chrono>
#include <map>
#include <string>
#include <thread>
#include <vector>
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace {
unsigned passed=0,failed=0;
#define CHECK(x) do { if(x) ++passed;else { ++failed;std::printf("FAIL %d: %s\n",__LINE__,#x); } } while(0)
sco_plugin* owner=reinterpret_cast<sco_plugin*>(uintptr_t{42});
std::map<std::string,sco_command> commands;
std::map<std::string,sco_event_fn> events;
std::map<std::string,sco_net_on_message> messages;
std::map<std::string,std::string> db,prior;
unsigned hostCalls=0,joinCalls=0,leaveCalls=0,spawnCalls=0,removeCalls=0,transformCalls=0,sendCalls=0;
bool brokerPresent=true,networkActive=false,admitted=true,inTx=false;
uint32_t state=0;
sco_widget_event_fn panelCallback=nullptr;
bool maskSeen=false,rememberSeen=false,hostSeen=false,joinSeen=false,panelRemoved=false;
std::string passInPanel;
namespace rw=sco::relay::wire;
uint64_t selfId=1;                               // this player's peer id in the synthetic session
std::vector<uint64_t> members={1,2};             // the session's peers, self among them
bool serverIsRelay=false;                        // peer 1 is a relay server (its name says so)
std::vector<std::string> modLog,panelRows;       // mod.log lines the module wrote; the panel's "Other players" rows
std::string panelStatus,panelReply;
std::map<std::string,uint32_t> channelFlags,channelMax;
std::map<std::string,unsigned> sentCount;
std::map<std::string,std::vector<uint8_t>> sentLast;
bool refuseNear=false;                           // sco.net refuses to register a multiplayer.near.<slot> channel
using Bytes=std::vector<uint8_t>;
void LogFn(sco_plugin* s,sco_log_level,const char* text) { CHECK(s==owner && text);modLog.emplace_back(text?text:""); }
unsigned LogCount(const char* part) { unsigned n=0;for(const auto& l:modLog) if(l.find(part)!=std::string::npos) ++n;return n; }
sco_result Register(sco_plugin* s,const sco_command* c) {
    if(s!=owner || !c || c->size<sizeof(*c) || !c->fn || !c->name) return SCO_BAD_ARG;
    if(c->nargs && (!c->args || c->arg_def_size!=sizeof(sco_arg_def))) return SCO_BAD_ARG;
    if(commands.count(c->name)) return SCO_BAD_ARG;
    commands[c->name]=*c;return SCO_OK;
}
sco_result Subscribe(sco_plugin*,const char* event,sco_event_fn fn,void*) { events[event]=fn;return SCO_OK; }
sco_result Unsubscribe(sco_plugin*,const char* event,sco_event_fn) { events.erase(event);return SCO_OK; }
sco_result SetPanel(sco_plugin* s,const char* id,const char*,const sco_widget_node* nodes,uint32_t n,uint32_t stride,sco_widget_event_fn cb,void*) {
    if(s!=owner || std::strcmp(id,"multiplayer.session") || stride!=sizeof(*nodes)) return SCO_BAD_ARG;
    panelCallback=cb;hostSeen=joinSeen=maskSeen=rememberSeen=false;
    for(uint32_t i=0;i<n;++i) {
        if(nodes[i].id==1 && nodes[i].kind==SCO_WIDGET_BUTTON) hostSeen=true;
        if(nodes[i].id==2 && nodes[i].kind==SCO_WIDGET_BUTTON) joinSeen=true;
        if(nodes[i].id==6) { maskSeen=(nodes[i].flags&SCO_WIDGET_PASSWORD)!=0;passInPanel=nodes[i].text; }
        if(nodes[i].id==11) rememberSeen=nodes[i].value!=0;
        if(nodes[i].id==12) panelStatus=nodes[i].label;
        if(nodes[i].id==14) panelReply=nodes[i].label;
        if(nodes[i].id==16) { panelRows.clear();for(uint32_t j=0;j<nodes[i].item_count;++j) panelRows.emplace_back(nodes[i].items[j]); }
    }
    return SCO_OK;
}
sco_result RemovePanel(sco_plugin*,const char*) { panelCallback=nullptr;panelRemoved=true;return SCO_OK; }
sco_result Put(sco_plugin*,const char* key,const void* value,uint32_t size) { db[key]=std::string(static_cast<const char*>(value),size);return SCO_OK; }
sco_result Get(sco_plugin*,const char* key,void* out,uint32_t* size) {
    auto it=db.find(key);if(it==db.end()) { *size=0;return SCO_NOT_FOUND; }
    const auto needed=static_cast<uint32_t>(it->second.size());if(*size<needed) { *size=needed;return SCO_TOO_MANY; }
    std::memcpy(out,it->second.data(),needed);*size=needed;return SCO_OK;
}
sco_result Delete(sco_plugin*,const char* key) { return db.erase(key)?SCO_OK:SCO_NOT_FOUND; }
sco_result Begin(sco_plugin*) { if(inTx) return SCO_BAD_ARG;prior=db;inTx=true;return SCO_OK; }
sco_result Commit(sco_plugin*) { if(!inTx) return SCO_BAD_ARG;inTx=false;return SCO_OK; }
sco_result Rollback(sco_plugin*) { db=prior;inTx=false;return SCO_OK; }
sco_result Host(sco_plugin* s,const sc_private_session_host_request_v1* r) {
    ++hostCalls;CHECK(s==owner && r && r->size==sizeof(*r) && r->consent==1 && r->_pad==0);
    CHECK(r->passphrase_len>=12 && r->passphrase_len<=256 && r->player_name_len==5 && !std::memcmp(r->player_name,"Pilot",5));
    CHECK(r->port==64091 && r->max_peers==8);
    if(!admitted) return SCO_UNAVAILABLE;
    networkActive=true;state=SC_PRIVATE_SESSION_HOSTING;return SCO_OK;
}
sco_result Join(sco_plugin* s,const sc_private_session_join_request_v1* r) {
    ++joinCalls;CHECK(s==owner && r && r->size==sizeof(*r) && r->consent==1 && r->_pad==0);
    CHECK(r->address_len==9 && !std::memcmp(r->address,"127.0.0.1",9));
    CHECK(r->passphrase_len>=12 && r->player_name_len==5);
    if(!admitted) return SCO_UNAVAILABLE;
    networkActive=true;state=SC_PRIVATE_SESSION_CONNECTED;return SCO_OK;
}
sco_result Leave(sco_plugin*) { ++leaveCalls;networkActive=false;state=0;return SCO_OK; }
sco_result State(sc_private_session_state_v1* out) { if(!admitted) return SCO_UNAVAILABLE;out->state=state;out->peer_count=networkActive?2:0;return SCO_OK; }
int Active() { return networkActive?1:0; }
uint64_t Self() { return networkActive?selfId:0; }
sco_result GetPeers(sco_net_peer* out,uint32_t* count) {
    if(*count<members.size()) return SCO_TOO_MANY;
    for(size_t i=0;i<members.size();++i) out[i]={members[i],0};
    *count=networkActive?static_cast<uint32_t>(members.size()):0;return SCO_OK;
}
sco_result Name(uint64_t id,char* out,uint32_t cap) { std::snprintf(out,cap,"%s",id==1 && serverIsRelay?"Relay Server":"Remote");return SCO_OK; }
sco_result Send(sco_plugin*,const char* channel,const void* data,uint32_t len) {
    ++sendCalls;++sentCount[channel];
    sentLast[channel]=Bytes(static_cast<const uint8_t*>(data),static_cast<const uint8_t*>(data)+len);return SCO_OK;
}
sco_result Channel(sco_plugin*,const char* id,uint32_t flags,uint32_t max,sco_net_on_message cb,void*) {
    if(refuseNear && !std::strncmp(id,"multiplayer.near.",17)) return SCO_BAD_ARG;
    channelFlags[id]=flags;channelMax[id]=max;
    if(!std::strcmp(id,"multiplayer.pose")) CHECK(!flags && max==mpwire::kMaxPose);
    else if(!std::strcmp(id,"multiplayer.spawn")) CHECK(flags==SCO_NET_RELIABLE && max==mpwire::kMaxSpawn);
    else if(!std::strcmp(id,"multiplayer.despawn")) CHECK(flags==SCO_NET_RELIABLE && max==16);
    else if(!std::strcmp(id,"multiplayer.relay")) CHECK(flags==(SCO_NET_RELIABLE|SCO_NET_FROM_HOST) && max==rw::kRelayMaxBytes);
    else if(!std::strcmp(id,"multiplayer.near_up")) CHECK(flags==SCO_NET_TO_HOST && max==rw::kNearUpMaxBytes);
    else if(!std::strncmp(id,"multiplayer.near.",17)) CHECK(flags==SCO_NET_FROM_HOST && max==rw::kNearDownMaxBytes);
    else CHECK(!"a channel the module should not register");
    messages[id]=cb;return SCO_OK;
}
sco_result Unchannel(sco_plugin*,const char* id) { messages.erase(id);return SCO_OK; }
int PlayerPose(double p[3],double q[4],uint64_t* z) { p[0]=p[1]=p[2]=0;q[0]=q[1]=q[2]=0;q[3]=1;*z=7;return 1; }
int Record(uint64_t id,sc_game_zone* out) { if(id!=7) return 0;out->epoch=9;out->zone_id=7;out->root_id=7;out->parent_id=0;std::snprintf(out->name,sizeof(out->name),"Synthetic_Zone");return 1; }
int FindZone(const char* name,sc_game_zone* out) { return !std::strcmp(name,"Synthetic_Zone")?Record(7,out):0; }
int Convert(uint64_t,uint64_t,const double p[3],const double q[4],double op[3],double oq[4]) { std::memcpy(op,p,24);std::memcpy(oq,q,32);return 1; }
sco_result SpawnActor(sco_plugin* s,const char* cls,uint64_t z,const double[3],uint64_t* id) { CHECK(s==owner && z==7 && std::strstr(cls,"Civilian"));++spawnCalls;*id=100+spawnCalls;return SCO_OK; }
sco_result RemoveActor(sco_plugin* s,uint64_t id) { CHECK(s==owner && id>=101);++removeCalls;return SCO_OK; }
int Alive(uint64_t) { return 1; }
double lastGhostX=0;
sco_result Transform(sco_plugin* s,uint64_t id,uint64_t z,const double p[3],const double[4]) { CHECK(s==owner && id>=101 && z==7);++transformCalls;lastGhostX=p[0];return SCO_OK; }
sco_net_v1 net{};sco_widgets_v1 widgets{};sco_storage_v1 storage{};sc_private_session_v1 broker{};
sc_spatial_v1 spatial{};sc_game_spatial_v1 poses{};sc_actors_v1 actors{};sc_entities_v1 entities{};
sco_result Query(const char* name,uint32_t,const void** out) {
    *out=nullptr;
    if(!std::strcmp(name,SCO_NET_NAME)) *out=&net;
    else if(!std::strcmp(name,SCO_WIDGETS_NAME)) *out=&widgets;
    else if(!std::strcmp(name,SCO_STORAGE_NAME)) *out=&storage;
    else if(!std::strcmp(name,SC_PRIVATE_SESSION_NAME) && brokerPresent) *out=&broker;
    else if(!std::strcmp(name,SC_SPATIAL_SERVICE_NAME)) *out=&spatial;
    else if(!std::strcmp(name,SC_GAME_SPATIAL_NAME)) *out=&poses;
    else if(!std::strcmp(name,SC_ACTORS_NAME)) *out=&actors;
    else if(!std::strcmp(name,SC_ENTITIES_NAME)) *out=&entities;
    return *out?SCO_OK:SCO_NOT_FOUND;
}
void UI(uint32_t id,uint32_t kind,const char* text=nullptr,double value=0) {
    CHECK(panelCallback!=nullptr);if(!panelCallback) return;
    sco_widget_event event{};event.size=sizeof(event);event.node_id=id;event.kind=kind;event.text=text;event.value=value;
    panelCallback("multiplayer.session",&event,nullptr);
}
void Tick() { CHECK(events.count("tick"));if(events.count("tick")) events["tick"]("tick",nullptr,nullptr); }
void StateEvent(bool active) {
    sco_net_state_event e{};e.size=sizeof(e);e.active=active?1:0;if(!active) std::snprintf(e.reason,sizeof(e.reason),"the host ended the session");
    CHECK(events.count(SCO_NET_EVENT_STATE));if(events.count(SCO_NET_EVENT_STATE)) events[SCO_NET_EVENT_STATE](SCO_NET_EVENT_STATE,&e,nullptr);
}
void PeerEvent(uint32_t what,uint64_t id) {
    sco_net_peer_event e{};e.size=sizeof(e);e.what=what;e.peer.peer_id=id;
    CHECK(events.count(SCO_NET_EVENT_PEER));if(events.count(SCO_NET_EVENT_PEER)) events[SCO_NET_EVENT_PEER](SCO_NET_EVENT_PEER,&e,nullptr);
}
void Deliver(const char* channel,uint64_t from,const Bytes& bytes) {
    CHECK(messages.count(channel));if(messages.count(channel)) messages.at(channel)(from,bytes.data(),static_cast<uint32_t>(bytes.size()),nullptr);
}
std::string StatusText() { char out[512]="";commands.at("multiplayer.status").fn(nullptr,0,nullptr,out,sizeof(out));return out; }
Bytes PoseBytes(uint32_t seq,double x) {
    mpwire::Pose pose{};pose.seq=seq;pose.levels=1;std::snprintf(pose.lv[0].name,sizeof(pose.lv[0].name),"Synthetic_Zone");pose.lv[0].rot[3]=1;pose.lv[0].pos[0]=x;
    uint8_t data[mpwire::kMaxPose];const size_t n=mpwire::EncodePose(pose,data,sizeof(data));return Bytes(data,data+n);
}
Bytes SpawnBytes(uint64_t peer) {
    mpwire::Spawn spawn{};spawn.kind=mpwire::kAvatar;spawn.sessionId=mpwire::SessionId(peer,mpwire::kAvatar);std::snprintf(spawn.cls,sizeof(spawn.cls),"Synthetic_Human");
    uint8_t data[mpwire::kMaxSpawn];const size_t n=mpwire::EncodeSpawn(spawn,data,sizeof(data));return Bytes(data,data+n);
}
Bytes RelayBytes(bool nearby,uint16_t maxPlayers,std::initializer_list<rw::RelayEntry> players) {
    rw::RelayRoster roster;roster.nearbyOnly=nearby;roster.maxPlayers=maxPlayers;roster.players.assign(players.begin(),players.end());return rw::EncodeRelay(roster);
}
void Reset() { commands.clear();events.clear();messages.clear();panelRemoved=false;networkActive=false;state=0; }
}
int main(int argc,char** argv) {
    if(argc!=2) return 2;
#if defined(_WIN32)
    HMODULE module=LoadLibraryA(argv[1]);if(!module) return 2;
    auto symbol=[&](const char* n){return reinterpret_cast<void*>(GetProcAddress(module,n));};
#else
    void* module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!module) { std::puts(dlerror());return 2; }
    auto symbol=[&](const char* n){return dlsym(module,n);};
#endif
    auto query=reinterpret_cast<const sco_plugin_info* (*)()>(symbol("sco_plugin_query"));
    auto load=reinterpret_cast<sco_result (*)(const sco_api*,sco_plugin*)>(symbol("sco_plugin_load"));
    auto unload=reinterpret_cast<void (*)()>(symbol("sco_plugin_unload"));
    CHECK(query && load && unload);if(!query || !load || !unload) return 2;
    CHECK(!std::strcmp(query()->name,"multiplayer"));
    sco_api api{};api.size=sizeof(api);api.major=SCO_API_MAJOR;api.minor=SCO_API_MINOR;api.register_command=Register;api.subscribe=Subscribe;api.unsubscribe=Unsubscribe;api.query_service=Query;api.log=LogFn;
    net.size=sizeof(net);net.is_active=Active;net.self_peer=Self;net.get_peers=GetPeers;net.get_peer_name=Name;net.send_channel=Send;net.register_channel=Channel;net.unregister_channel=Unchannel;
    widgets.size=sizeof(widgets);widgets.set_panel=SetPanel;widgets.remove_panel=RemovePanel;
    storage.size=sizeof(storage);storage.put=Put;storage.get=Get;storage.del=Delete;storage.begin=Begin;storage.commit=Commit;storage.rollback=Rollback;
    broker.size=sizeof(broker);broker.request_host=Host;broker.request_join=Join;broker.leave=Leave;broker.get_state=State;
    spatial.size=sizeof(spatial);spatial.player_pose=PlayerPose;
    poses.size=sizeof(poses);poses.zone_record=Record;poses.find_zone=FindZone;poses.pose_to_zone=Convert;
    actors.size=sizeof(actors);actors.spawn_npc=SpawnActor;actors.despawn=RemoveActor;
    entities.size=sizeof(entities);entities.alive=Alive;entities.set_transform=Transform;
    db={{"name","Pilot"},{"join_address","127.0.0.1"},{"host_port","64091"},{"join_port","64091"},{"show_ships","1"},{"remember_passphrase","0"},{"passphrase","ignored stale synthetic secret"}};
    CHECK(load(nullptr,owner)==SCO_BAD_ARG);
    CHECK(load(&api,owner)==SCO_OK);CHECK(commands.size()==3 && messages.size()==5 && events.size()==3);
    CHECK(hostSeen && joinSeen && maskSeen && !rememberSeen && passInPanel.empty());
    CHECK(hostCalls==0 && joinCalls==0 && sendCalls==0);Tick();CHECK(hostCalls==0 && joinCalls==0);
    char output[512];sco_arg who{};who.type=SCO_ARG_STRING;who.v.s="Remote";
    CHECK(commands.at("multiplayer.goto").fn(&who,1,nullptr,output,sizeof(output))==SCO_UNAVAILABLE);CHECK(std::strstr(output,"G2"));
    UI(1,SCO_WIDGET_BUTTON);CHECK(hostCalls==0); // empty/short passphrase refused locally
    UI(6,SCO_WIDGET_TEXT_INPUT,"synthetic passphrase");CHECK(hostCalls==0);
    UI(1,SCO_WIDGET_BUTTON);CHECK(hostCalls==1 && passInPanel.empty());CHECK(!db.count("passphrase"));
    const char* keys[]={"name","join_address","avatar_class","host_port","join_port","show_ships","remember_passphrase"};
    for(const auto* key:keys) CHECK(db.count(key));
    mpwire::Spawn spawn{};spawn.kind=mpwire::kAvatar;spawn.sessionId=mpwire::SessionId(2,mpwire::kAvatar);std::snprintf(spawn.cls,sizeof(spawn.cls),"Synthetic_Human");
    uint8_t data[mpwire::kMaxPose];size_t len=mpwire::EncodeSpawn(spawn,data,sizeof(data));
    messages.at("multiplayer.spawn")(999,data,static_cast<uint32_t>(len),nullptr);Tick();CHECK(spawnCalls==0);
    messages.at("multiplayer.spawn")(2,data,static_cast<uint32_t>(len),nullptr);
    mpwire::Pose pose{};pose.seq=1;pose.levels=1;std::snprintf(pose.lv[0].name,sizeof(pose.lv[0].name),"Synthetic_Zone");pose.lv[0].rot[3]=1;pose.lv[0].pos[0]=3;
    len=mpwire::EncodePose(pose,data,sizeof(data));messages.at("multiplayer.pose")(2,data,static_cast<uint32_t>(len),nullptr);
    Tick();CHECK(spawnCalls==1 && transformCalls>=1);
    data[0]=99;messages.at("multiplayer.pose")(2,data,static_cast<uint32_t>(len),nullptr);Tick();CHECK(spawnCalls==1);
    len=mpwire::EncodeDespawn(mpwire::kAvatar,mpwire::SessionId(2,mpwire::kAvatar),data,sizeof(data));CHECK(len==16);
    messages.at("multiplayer.despawn")(2,data,static_cast<uint32_t>(len),nullptr);CHECK(removeCalls==1);
    CHECK(commands.at("multiplayer.leave").fn(nullptr,0,nullptr,output,sizeof(output))==SCO_OK);CHECK(!networkActive);
    UI(6,SCO_WIDGET_TEXT_INPUT,"synthetic passphrase");UI(2,SCO_WIDGET_BUTTON);CHECK(joinCalls==1 && passInPanel.empty());
    UI(11,SCO_WIDGET_TOGGLE,nullptr,1);UI(6,SCO_WIDGET_TEXT_INPUT,"synthetic remembered passphrase");UI(3,SCO_WIDGET_BUTTON);UI(1,SCO_WIDGET_BUTTON);
    CHECK(db.at("remember_passphrase")=="1" && db.at("passphrase")=="synthetic remembered passphrase" && rememberSeen);
    UI(11,SCO_WIDGET_TOGGLE,nullptr,0);CHECK(!db.count("passphrase"));
    unload();CHECK(panelRemoved && messages.empty() && events.empty());unload();
    Reset();brokerPresent=false;const unsigned priorHosts=hostCalls;
    CHECK(load(&api,owner)==SCO_OK);CHECK(commands.size()==3 && maskSeen);UI(6,SCO_WIDGET_TEXT_INPUT,"synthetic passphrase");UI(1,SCO_WIDGET_BUTTON);CHECK(hostCalls==priorHosts);
    CHECK(commands.at("multiplayer.leave").fn(nullptr,0,nullptr,output,sizeof(output))==SCO_UNAVAILABLE);
    unload();Reset();brokerPresent=true;admitted=false;
    CHECK(load(&api,owner)==SCO_OK);UI(6,SCO_WIDGET_TEXT_INPUT,"synthetic passphrase");UI(1,SCO_WIDGET_BUTTON);CHECK(!networkActive);unload();
    // ---- a sco-relay server hosts the session: peer 1 is the server, never a player ----------------------
    // Golden bytes of multiplayer.relay, the same vectors as sco-relay's tests/server/test_relay_wire.cpp.
    CHECK(std::strcmp(rw::kRelayChannel,"multiplayer.relay")==0);
    CHECK(RelayBytes(true,15,{{2,0,1},{5,3,2}})==Bytes({0x01,0x01,0x0F,0x00,0x02,0x00, 0x02,0,0,0,0,0,0,0,0x00,0x00,0x01, 0x05,0,0,0,0,0,0,0,0x03,0x00,0x02}));
    CHECK(RelayBytes(false,15,{})==Bytes({0x01,0x00,0x0F,0x00,0x00,0x00}));
    CHECK(RelayBytes(false,400,{{0x0102030405060708ull,300,255}})==Bytes({0x01,0x00,0x90,0x01,0x01,0x00,0x08,0x07,0x06,0x05,0x04,0x03,0x02,0x01,0x2C,0x01,0xFF}));
    Reset();brokerPresent=true;admitted=true;modLog.clear();serverIsRelay=true;selfId=3;members={3,1,2};
    CHECK(load(&api,owner)==SCO_OK);
    CHECK(messages.count("multiplayer.relay")==1 && channelFlags.at("multiplayer.relay")==(SCO_NET_RELIABLE|SCO_NET_FROM_HOST) && channelMax.at("multiplayer.relay")==rw::kRelayMaxBytes);
    CHECK(modLog.empty()); // loading says nothing and opens nothing
    networkActive=true;state=SC_PRIVATE_SESSION_CONNECTED;
    StateEvent(true);PeerEvent(SCO_NET_PEER_JOINED,1);PeerEvent(SCO_NET_PEER_JOINED,2);Tick();
    // Before the server says what it is, the join event of peer 1 makes no record: only peer 2 is counted.
    CHECK(StatusText().find("1 other players")!=std::string::npos && StatusText().find("via relay")==std::string::npos);
    Deliver("multiplayer.spawn",2,SpawnBytes(2));Deliver("multiplayer.pose",2,PoseBytes(1,3));
    const unsigned spawnsBefore=spawnCalls;Tick();CHECK(spawnCalls==spawnsBefore+1);
    // The roster, from the server, lists peers 2 and 3 (this player) and not the server.
    const Bytes roster=RelayBytes(false,15,{{2,0,1},{3,1,4}});
    Deliver("multiplayer.relay",2,RelayBytes(false,15,{{2,0,1},{3,1,4},{9,2,1}})); // not from the host: ignored
    CHECK(LogCount("relay server found")==0);
    Deliver("multiplayer.relay",1,roster);Tick();
    CHECK(LogCount("relay server found")==1 && LogCount("joined relay server as peer 3 in slot 1 (generation 4): 2 of 15 players in the session, nearby-only off")==1);
    CHECK(StatusText().find("1 other players; via relay server (not a player), slot 1, nearby-only off")!=std::string::npos);
    CHECK(panelRows.size()==1 && panelRows[0].find("Relay")==std::string::npos && panelStatus.find("1 peers; via relay server")!=std::string::npos);
    Deliver("multiplayer.relay",1,roster);CHECK(LogCount("joined relay")==1 && LogCount("relay roster:")==0); // the same roster again is silent
    // The server sends no pose or spawn and is never made a player, even if it did.
    const unsigned spawnsNow=spawnCalls;
    Deliver("multiplayer.spawn",1,SpawnBytes(1));Deliver("multiplayer.pose",1,PoseBytes(1,5));Tick();
    CHECK(spawnCalls==spawnsNow && panelRows.size()==1 && StatusText().find("1 other players")!=std::string::npos);
    // A roster that is not the contract is dropped, and said once.
    Bytes cut=roster;cut.pop_back();
    Deliver("multiplayer.relay",1,cut);Deliver("multiplayer.relay",1,Bytes({0x02,0x00,0x0F,0x00,0x00,0x00}));
    CHECK(LogCount("ignored a malformed multiplayer.relay")==1 && StatusText().find("malformed relay messages ignored")!=std::string::npos);
    // A new player: the roster changes, and the line says so.
    members={3,1,2,4};Deliver("multiplayer.relay",1,RelayBytes(false,15,{{2,0,1},{3,1,4},{4,2,1}}));
    CHECK(LogCount("relay roster: 3 of 15 players in the session, slot 1 (generation 4), nearby-only off")==1);
    // Players leave one at a time; the session ends: the module says it left the relay session and forgets the server.
    const unsigned removesBefore=removeCalls;
    PeerEvent(SCO_NET_PEER_LEFT,2);Tick();CHECK(removeCalls==removesBefore+1 && panelRows.empty());
    networkActive=false;state=SC_PRIVATE_SESSION_IDLE;StateEvent(false);
    CHECK(LogCount("left the relay session")==1 && StatusText().find("via relay")==std::string::npos);
    unload();CHECK(messages.empty() && events.empty());
    // A session host that is a player (the unmodified module hosting) is a player: its first spawn makes its record.
    Reset();modLog.clear();serverIsRelay=false;selfId=2;members={2,1};
    CHECK(load(&api,owner)==SCO_OK);networkActive=true;state=SC_PRIVATE_SESSION_CONNECTED;
    StateEvent(true);PeerEvent(SCO_NET_PEER_JOINED,1);Tick();
    CHECK(StatusText().find("0 other players")!=std::string::npos);
    Deliver("multiplayer.spawn",1,SpawnBytes(1));CHECK(StatusText().find("1 other players")!=std::string::npos && LogCount("relay")==0);
    unload();
    // ---- nearby-only: poses go to the relay server on multiplayer.near_up and come back on multiplayer.near.<slot> ----
    {   // Golden bytes of the two messages, the same vectors as sco-relay's tests/server/test_relay_wire.cpp.
        const Bytes pose3={0xAA,0xBB,0xCC};
        CHECK(rw::EncodeNearUp(0x1122334455667788ull,1.0,-2.0,0.5,pose3.data(),pose3.size())==Bytes({0x01,0,0,0,0,0,0,0, 0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11, 0,0,0,0,0,0,0xF0,0x3F, 0,0,0,0,0,0,0x00,0xC0, 0,0,0,0,0,0,0xE0,0x3F, 0xAA,0xBB,0xCC}));
        CHECK(rw::EncodeNearDown(2,10,pose3.data(),pose3.size())==Bytes({0x01,0x02,0,0,0,0,0,0, 0x0A,0,0,0,0,0,0,0, 0xAA,0xBB,0xCC}));
        CHECK(rw::Fnv1a64("OOC_Stanton_2b_Daymar",21)==0x7b9a22aa73a012a6ull);
        CHECK(std::strcmp(rw::kNearUpChannel,"multiplayer.near_up")==0 && rw::NearDownChannel(1)=="multiplayer.near.1");
    }
    Reset();brokerPresent=true;admitted=true;modLog.clear();serverIsRelay=true;selfId=3;members={3,1,2,4,5};sentCount.clear();sentLast.clear();
    CHECK(load(&api,owner)==SCO_OK);
    CHECK(messages.size()==5 && channelFlags.at("multiplayer.near_up")==SCO_NET_TO_HOST && channelMax.at("multiplayer.near_up")==rw::kNearUpMaxBytes);
    networkActive=true;state=SC_PRIVATE_SESSION_CONNECTED;StateEvent(true);
    for(uint64_t id:{uint64_t(4),uint64_t(5)}) { PeerEvent(SCO_NET_PEER_JOINED,id);Deliver("multiplayer.spawn",id,SpawnBytes(id)); }
    Tick(); // no roster yet: the pose goes to the other players, as it does in a session a player hosts
    CHECK(sentCount["multiplayer.pose"]==1 && sentCount["multiplayer.near_up"]==0);
    // The roster: nearby-only on, this player in slot 1, generation 7.
    const Bytes nearRoster=RelayBytes(true,15,{{2,0,1},{3,1,7},{4,2,1},{5,3,1}});
    Deliver("multiplayer.relay",1,nearRoster);
    CHECK(LogCount("joined relay server as peer 3 in slot 1 (generation 7): 4 of 15 players in the session, nearby-only on")==1);
    CHECK(LogCount("nearby-only: poses go to the relay server on multiplayer.near_up and come back on multiplayer.near.1")==1);
    CHECK(messages.count("multiplayer.near.1")==1 && channelFlags.at("multiplayer.near.1")==SCO_NET_FROM_HOST && channelMax.at("multiplayer.near.1")==rw::kNearDownMaxBytes);
    CHECK(StatusText().find("slot 1, nearby-only on")!=std::string::npos);
    sentCount.clear();std::this_thread::sleep_for(std::chrono::milliseconds(110));Tick();
    // The pose went up on near_up with the area (a hash of the outermost zone) and the position in it, and not on multiplayer.pose.
    CHECK(sentCount["multiplayer.pose"]==0 && sentCount["multiplayer.near_up"]==1);
    {
        rw::NearUp up;const Bytes& upBytes=sentLast["multiplayer.near_up"];
        CHECK(rw::DecodeNearUp(upBytes.data(),upBytes.size(),&up) && up.area==rw::Fnv1a64("Synthetic_Zone",14) && up.x==0 && up.y==0 && up.z==0);
        mpwire::Pose upPose{};
        CHECK(mpwire::DecodePose(up.pose,up.poseLen,upPose) && upPose.levels==1 && !std::strcmp(upPose.lv[0].name,"Synthetic_Zone") && upPose.seq>=1);
    }
    // Poses come back on this player's own channel. Each of these is dropped; none makes a ghost for peer 4.
    const Bytes poseFour=PoseBytes(5,11);
    const unsigned spawnsBeforeNear=spawnCalls;
    const auto down=[&](uint8_t generation,uint64_t sender) { return rw::EncodeNearDown(generation,sender,poseFour.data(),poseFour.size()); };
    Deliver("multiplayer.near.1",1,down(6,4));      // for an earlier occupant of the slot
    Deliver("multiplayer.near.1",1,down(8,4));      // for a later one
    Deliver("multiplayer.near.1",2,down(7,4));      // not from the host
    Deliver("multiplayer.near.1",1,down(7,3));      // the sender is this player itself
    Deliver("multiplayer.near.1",1,down(7,1));      // the sender is the server
    Deliver("multiplayer.near.1",1,down(7,99));     // the sender is not in the session
    Tick();CHECK(spawnCalls==spawnsBeforeNear && LogCount("ignored a malformed multiplayer.near")==0);
    Bytes dirty=down(7,4);dirty[3]=1;               // a reserved byte that is not zero
    Deliver("multiplayer.near.1",1,dirty);
    const Bytes whole=down(7,4);
    Deliver("multiplayer.near.1",1,Bytes(whole.begin(),whole.begin()+16)); // a header and no pose
    Bytes garbage=down(7,4);garbage[16]=99;         // a pose that is not a pose
    Deliver("multiplayer.near.1",1,garbage);
    Tick();CHECK(spawnCalls==spawnsBeforeNear && LogCount("ignored a malformed multiplayer.near")==1);
    // The right one: generation 7, from the host, naming peer 4.
    Deliver("multiplayer.near.1",1,down(7,4));Tick();
    CHECK(spawnCalls==spawnsBeforeNear+1 && lastGhostX==11);
    // The roster says nearby-only is off: the module stops listening on its slot and sends its pose to the other players again.
    Deliver("multiplayer.relay",1,RelayBytes(false,15,{{2,0,1},{3,1,7},{4,2,1},{5,3,1}}));
    CHECK(messages.count("multiplayer.near.1")==0 && LogCount("nearby-only is off: poses go to the other players on multiplayer.pose")==1);
    sentCount.clear();std::this_thread::sleep_for(std::chrono::milliseconds(110));Tick();
    CHECK(sentCount["multiplayer.pose"]==1 && sentCount["multiplayer.near_up"]==0);
    // On again, in another seat: the old channel goes and the new one comes.
    Deliver("multiplayer.relay",1,RelayBytes(true,15,{{2,0,1},{3,2,1},{4,1,1}}));
    CHECK(messages.count("multiplayer.near.1")==0 && messages.count("multiplayer.near.2")==1);
    CHECK(LogCount("come back on multiplayer.near.2")==1);
    // The session ends: nothing stays registered.
    networkActive=false;state=SC_PRIVATE_SESSION_IDLE;StateEvent(false);
    CHECK(messages.count("multiplayer.near.2")==0 && messages.size()==5);
    unload();CHECK(messages.empty() && events.empty());
    // ---- sco.net refuses the multiplayer.near.<slot> channel: this player's own pose falls back to multiplayer.pose, the
    // module says what that means once per session (every roster retries), and it listens as soon as the channel is accepted.
    {
        const char* line="cannot listen on multiplayer.near.1 (3): this player sends its poses on multiplayer.pose and will not see the players the server forwards on that channel";
        Reset();modLog.clear();serverIsRelay=true;selfId=3;members={3,1,2};sentCount.clear();sentLast.clear();refuseNear=true;
        CHECK(load(&api,owner)==SCO_OK);
        networkActive=true;state=SC_PRIVATE_SESSION_CONNECTED;StateEvent(true);
        const Bytes nearRosterAgain=RelayBytes(true,15,{{2,0,1},{3,1,4}});
        for(int i=0;i<4;++i) Deliver("multiplayer.relay",1,nearRosterAgain);                     // one roster per announce interval
        Deliver("multiplayer.relay",1,RelayBytes(true,15,{{2,0,1},{3,1,4},{4,2,1}}));          // and one that changes
        CHECK(LogCount(line)==1 && LogCount("cannot listen")==1);
        CHECK(messages.count("multiplayer.near.1")==0 && LogCount("nearby-only: poses go to the relay server")==0);
        sentCount.clear();std::this_thread::sleep_for(std::chrono::milliseconds(110));Tick();
        CHECK(sentCount["multiplayer.pose"]==1 && sentCount["multiplayer.near_up"]==0);          // the own pose falls back
        refuseNear=false;                                                                        // the channel is accepted at the next roster
        Deliver("multiplayer.relay",1,nearRosterAgain);
        CHECK(messages.count("multiplayer.near.1")==1 && LogCount("come back on multiplayer.near.1")==1 && LogCount("cannot listen")==1);
        sentCount.clear();std::this_thread::sleep_for(std::chrono::milliseconds(110));Tick();
        CHECK(sentCount["multiplayer.near_up"]==1 && sentCount["multiplayer.pose"]==0);
        // A new session warns again, once.
        networkActive=false;state=SC_PRIVATE_SESSION_IDLE;StateEvent(false);
        refuseNear=true;networkActive=true;state=SC_PRIVATE_SESSION_CONNECTED;StateEvent(true);
        for(int i=0;i<3;++i) Deliver("multiplayer.relay",1,nearRosterAgain);
        CHECK(LogCount(line)==2 && messages.count("multiplayer.near.1")==0);
        refuseNear=false;
        unload();CHECK(messages.empty() && events.empty());
    }
#if defined(_WIN32)
    FreeLibrary(module);
#else
    dlclose(module);
#endif
    std::printf("multiplayer module: %u passed, %u failed (synthetic public services; no sockets/game)\n",passed,failed);
    return failed?1:0;
}
