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
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#if defined(_WIN32)
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
uint64_t Self() { return networkActive?1:0; }
sco_result GetPeers(sco_net_peer* out,uint32_t* count) { if(*count<2) return SCO_TOO_MANY;out[0]={1,0};out[1]={2,0};*count=networkActive?2:0;return SCO_OK; }
sco_result Name(uint64_t,char* out,uint32_t cap) { std::snprintf(out,cap,"Remote");return SCO_OK; }
sco_result Send(sco_plugin*,const char*,const void*,uint32_t) { ++sendCalls;return SCO_OK; }
sco_result Channel(sco_plugin*,const char* id,uint32_t flags,uint32_t max,sco_net_on_message cb,void*) {
    if(std::strstr(id,"pose")) CHECK(!flags && max==mpwire::kMaxPose);
    else if(std::strstr(id,"spawn") && !std::strstr(id,"despawn")) CHECK(flags==SCO_NET_RELIABLE && max==mpwire::kMaxSpawn);
    else CHECK(flags==SCO_NET_RELIABLE && max==16);
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
sco_result Transform(sco_plugin* s,uint64_t id,uint64_t z,const double[3],const double[4]) { CHECK(s==owner && id>=101 && z==7);++transformCalls;return SCO_OK; }
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
    sco_api api{};api.size=sizeof(api);api.major=SCO_API_MAJOR;api.minor=SCO_API_MINOR;api.register_command=Register;api.subscribe=Subscribe;api.unsubscribe=Unsubscribe;api.query_service=Query;
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
    CHECK(load(&api,owner)==SCO_OK);CHECK(commands.size()==3 && messages.size()==3 && events.size()==3);
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
#if defined(_WIN32)
    FreeLibrary(module);
#else
    dlclose(module);
#endif
    std::printf("multiplayer module: %u passed, %u failed (synthetic public services; no sockets/game)\n",passed,failed);
    return failed?1:0;
}
