// SPDX-License-Identifier: GPL-3.0-only
// Actual external module and copied synthetic public services. No game/backend/render use.
#include <sco_api.h>
#include <sc_gamerules.h>
#include <sc_quantum.h>
#include <sc_game_spatial.h>
#include <sc_relocation.h>
#include <sco_content.h>
#include <sco_settings.h>
#include <sco_widgets.h>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace {
unsigned checks=0,reads=0,queries=0,holds=0,releases=0,enables=0,requests=0;
std::thread::id gameThread;
sco_plugin* owner=nullptr;
bool rulesCap=true,controlCap=true,relocationCap=true,enabled=false,held=false;
int32_t movesAllowed=1;
sc_gamerules_state snapshot{};
sco_result rulesResult=SCO_OK,queryResult=SCO_OK;
const void* rulesTable=nullptr;
std::string reply,root;
std::map<std::string,sco_command> commands;
std::vector<std::pair<sco_event_fn,void*>> ticks;
struct Node { uint32_t flags=0; std::string label; };
struct Panel { std::map<uint32_t,Node> nodes; sco_widget_event_fn fn=nullptr;void* ctx=nullptr; } panel;
void Check(bool ok,const char* why) {++checks;if(!ok){std::fprintf(stderr,"quantum GameRules: %s; reply=%s\n",why,reply.c_str());std::exit(1);}}
int Has(const char* name) {
    return (!std::strcmp(name,"game.gamerules.read")&&rulesCap) ||
        (!std::strcmp(name,"game.quantum.control")&&controlCap) ||
        (!std::strcmp(name,SC_RELOCATION_NAME)&&relocationCap);
}
sco_result Register(sco_plugin* self,const sco_command* command) {Check(self==owner,"command owner");commands[command->name]=*command;return SCO_OK;}
sco_result Subscribe(sco_plugin*,const char* name,sco_event_fn fn,void* ctx) {Check(!std::strcmp(name,"tick"),"tick topic");ticks.emplace_back(fn,ctx);return SCO_OK;}
sco_result Unsubscribe(sco_plugin*,const char*,sco_event_fn fn) {for(auto it=ticks.begin();it!=ticks.end();++it)if(it->first==fn){ticks.erase(it);return SCO_OK;}return SCO_NOT_FOUND;}
void Tick(uint32_t now) {const auto copy=ticks;for(auto [fn,ctx]:copy)fn("tick",&now,ctx);}
sco_result GetBool(sco_plugin*,const char* name,int32_t* out) {if(std::strcmp(name,"relocation"))return SCO_NOT_FOUND;*out=movesAllowed;return SCO_OK;}
sco_result GetString(sco_plugin*,const char* name,char* out,uint32_t* n) {if(std::strcmp(name,"data_root"))return SCO_NOT_FOUND;if(*n<=root.size()){*n=static_cast<uint32_t>(root.size()+1);return SCO_TOO_MANY;}std::memcpy(out,root.c_str(),root.size()+1);*n=static_cast<uint32_t>(root.size()+1);return SCO_OK;}
sco_settings_v1 settings{};
sco_result Rules(sco_plugin* self,sc_gamerules_state* out) {
    ++reads;Check(self==owner&&out&&out->size==sizeof(*out),"copied rules owner/record");
    if(std::this_thread::get_id()!=gameThread)return SCO_WRONG_THREAD;
    if(rulesResult!=SCO_OK)return rulesResult;
    *out=snapshot;return SCO_OK;
}
sc_gamerules_v1 rules{};
sco_result Enable(sco_plugin* self,uint32_t on) {Check(self==owner&&on<=1,"owned enable");++enables;enabled=on!=0;if(!on)held=false;return SCO_OK;}
sco_result Hold(sco_plugin* self,uint32_t ms) {Check(self==owner&&ms&&ms<=1000,"bounded owned hold");if(std::this_thread::get_id()!=gameThread)return SCO_WRONG_THREAD;++holds;if(!enabled)return SCO_UNAVAILABLE;held=true;return SCO_OK;}
sco_result Release(sco_plugin* self) {Check(self==owner,"owned cancellation");++releases;held=false;return enabled?SCO_OK:SCO_NOT_FOUND;}
sco_result State(sco_plugin*,sc_quantum_state* out) {if(std::this_thread::get_id()!=gameThread)return SCO_WRONG_THREAD;*out={sizeof(*out),enabled?1u:0u,held?1u:0u,SC_QUANTUM_CHARGING,0,1,1};return SCO_OK;}
sc_quantum_v1 control{sizeof(control),0,Enable,Hold,Release,State};
sco_result ContentItem(sco_plugin*,sco_content_kind kind,uint32_t i,sco_content_item* out) {Check(kind==SCO_CONTENT_LIST,"content kind");if(i)return SCO_NOT_FOUND;*out={};out->size=sizeof(*out);out->id=1;out->kind=kind;std::snprintf(out->pack,sizeof(out->pack),"synthetic");std::snprintf(out->name,sizeof(out->name),"lists/locations.txt");return SCO_OK;}
sco_result ContentRead(sco_plugin*,uint64_t id,void* out,uint32_t* n) {Check(id==1,"content id");constexpr const char* bytes="S | Port | port_zone | 10\n";const auto size=static_cast<uint32_t>(std::strlen(bytes));if(*n<size){*n=size;return SCO_TOO_MANY;}std::memcpy(out,bytes,size);*n=size;return SCO_OK;}
sco_content_v1 content{sizeof(content),0,ContentItem,ContentRead};
int FindZone(const char* name,sc_game_zone* out) {if(std::strcmp(name,"port_zone"))return 0;*out={};out->size=sizeof(*out);out->zone_id=77;out->root_id=77;out->epoch=5;out->flags=SC_GAME_ZONE_SYSTEM_UNKNOWN;std::snprintf(out->name,sizeof(out->name),"port_zone");return 1;}
sc_game_spatial_v1 spatial{};
sco_result Relocate(sco_plugin*,const sc_relocation_request*,uint64_t* ticket) {++requests;*ticket=requests;return SCO_OK;}
sco_result Cancel(sco_plugin*,uint64_t) {return SCO_OK;}
sco_result RelStatus(sco_plugin*,uint64_t ticket,sc_relocation_status* out) {out->ticket=ticket;out->state=SC_RELOCATION_PENDING;return SCO_OK;}
sc_relocation_v1 relocation{sizeof(relocation),Relocate,Cancel,RelStatus};
sco_result SetPanel(sco_plugin* self,const char* id,const char* title,const sco_widget_node* nodes,uint32_t count,uint32_t stride,sco_widget_event_fn fn,void* ctx) {
    Check(self==owner&&!std::strcmp(id,"quantum.travel")&&!std::strcmp(title,"Travel")&&stride==sizeof(*nodes),"existing public panel");
    if(std::this_thread::get_id()!=gameThread)return SCO_WRONG_THREAD;
    Panel next;next.fn=fn;next.ctx=ctx;for(uint32_t i=0;i<count;++i){Check(nodes[i].label&&std::strlen(nodes[i].label)<=255,"bounded copied panel label");next.nodes[nodes[i].id]={nodes[i].flags,nodes[i].label};}panel=std::move(next);return SCO_OK;
}
sco_result RemovePanel(sco_plugin*,const char*) {panel={};return SCO_OK;}
sco_widgets_v1 widgets{};
sco_result Query(const char* name,uint32_t version,const void** out) {
    Check(version==0x10000u,"frozen public1.0 query");*out=nullptr;
    if(!std::strcmp(name,SC_GAMERULES_SERVICE_NAME)){++queries;*out=rulesTable;return queryResult;}
    if(!std::strcmp(name,SC_QUANTUM_SERVICE_NAME))*out=&control;
    if(!std::strcmp(name,SCO_WIDGETS_NAME))*out=&widgets;
    if(!std::strcmp(name,SCO_SETTINGS_NAME))*out=&settings;
    if(!std::strcmp(name,SCO_CONTENT_NAME))*out=&content;
    if(!std::strcmp(name,SC_GAME_SPATIAL_NAME))*out=&spatial;
    if(!std::strcmp(name,SC_RELOCATION_NAME))*out=&relocation;
    return *out?SCO_OK:SCO_NOT_FOUND;
}
sco_result Call(const char* name,const sco_arg* args=nullptr,uint32_t n=0) {Check(commands.count(name)!=0,"existing command");const auto& c=commands.at(name);char text[1024]{};const auto result=c.fn(args,n,c.ctx,text,sizeof(text));reply=text;return result;}
void Send(const Panel& target,uint32_t id,uint32_t kind=SCO_WIDGET_BUTTON,double value=0) {Check(target.fn!=nullptr,"published event callback");sco_widget_event e{};e.size=sizeof(e);e.node_id=id;e.kind=kind;e.value=value;target.fn("quantum.travel",&e,target.ctx);}
void Send(uint32_t id,uint32_t kind=SCO_WIDGET_BUTTON,double value=0) {const auto target=panel;Send(target,id,kind,value);}
bool Disabled(uint32_t id) {return (panel.nodes.at(id).flags&SCO_WIDGET_DISABLED)!=0;}
void Refresh() {Send(105);}
void Known(uint64_t generation=1,uint32_t travel=1,uint32_t boost=1,uint32_t group=0) {snapshot={};snapshot.size=sizeof(snapshot);snapshot.valid=1;snapshot.generation=generation;snapshot.quantum_travel_allowed=travel;snapshot.quantum_boost_allowed=boost;snapshot.enforce_group_qt=group;}
}
int main(int argc,char** argv) {
    Check(argc==2,"module path");gameThread=std::this_thread::get_id();
#ifdef _WIN32
    const auto module=LoadLibraryA(argv[1]);Check(module!=nullptr,"load actual module");
    const auto load=reinterpret_cast<sco_result(*)(const sco_api*,sco_plugin*)>(GetProcAddress(module,"sco_plugin_load"));
    const auto unload=reinterpret_cast<void(*)()>(GetProcAddress(module,"sco_plugin_unload"));
#else
    auto* module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!module)std::fprintf(stderr,"%s\n",dlerror());Check(module!=nullptr,"load actual module");
    const auto load=reinterpret_cast<sco_result(*)(const sco_api*,sco_plugin*)>(dlsym(module,"sco_plugin_load"));
    const auto unload=reinterpret_cast<void(*)()>(dlsym(module,"sco_plugin_unload"));
#endif
    Check(load&&unload,"public exports");
    const auto path=std::filesystem::temp_directory_path()/"sco-quantum-gamerules-test";std::filesystem::remove_all(path);std::filesystem::create_directories(path);root=path.string();
    int identity=0;owner=reinterpret_cast<sco_plugin*>(&identity);
    sco_api api{};api.size=sizeof(api);api.major=SCO_API_MAJOR;api.minor=SCO_API_MINOR;api.has=Has;api.query_service=Query;api.register_command=Register;api.subscribe=Subscribe;api.unsubscribe=Unsubscribe;
    settings.size=sizeof(settings);settings.get_bool=GetBool;settings.get_string=GetString;spatial.size=sizeof(spatial);spatial.find_zone=FindZone;
    rules.size=sizeof(rules);rules.read=Rules;rulesTable=&rules;widgets.size=sizeof(widgets);widgets.set_panel=SetPanel;widgets.remove_panel=RemovePanel;Known();
    Check(load(&api,owner)==SCO_OK,"load without mutation");Check(panel.nodes.count(106)&&panel.nodes.count(107),"GameRules snapshot and group policy appear in actual panel");Check(!enables&&!holds&&reads==1,"startup is read-only");Check(Disabled(103)&&panel.nodes[106].label.find("generation=1")!=std::string::npos,"received allowed policy still needs owner lease");
    Send(102,SCO_WIDGET_TOGGLE,1);Check(enabled&&!Disabled(103),"valid allowed rules expose leased boost affordance");Send(103);Check(held&&holds==1,"UI starts bounded hold after fresh rules read");
    rulesCap=false;Tick(250);Check(panel.nodes[106].label.find("unknown/unreadable")!=std::string::npos&&!Disabled(103)&&!Disabled(104),"unknown rules never suppress owned cancellation");const auto noRead=reads;controlCap=false;Send(103);Check(!held&&reads==noRead,"current-held button releases without readable rules/control capability");controlCap=true;Refresh();Check(Disabled(103),"unknown policy pauses new boost UI");const auto noHold=holds;Send(103);Check(holds==noHold&&!held,"forged/stale unknown boost UI cannot start hold");
    sco_arg duration{};duration.type=SCO_ARG_INT;duration.v.i=1000;Check(Call("quantum.hold",&duration,1)==SCO_OK&&held,"commands retain independent provider authority");controlCap=false;Send(104);Check(!held,"explicit release survives control/read capability loss");controlCap=true;rulesCap=true;
    Known(2,0,1,1);Refresh();Check(Disabled(103)&&panel.nodes[103].label.find("forbidden")!=std::string::npos&&panel.nodes[107].label.find("membership unknown")!=std::string::npos,"received travel gate forbids UI without inferring group membership");Send(103);Check(holds==noHold+1,"forbidden UI event never calls hold");
    Known(2,1,0);Refresh();Check(Disabled(103),"boost gate independently forbids UI");
    Check(Call("quantum.hold",&duration,1)==SCO_OK,"provider may authorize command independently of panel policy");Refresh();Check(!Disabled(103)&&panel.nodes[103].label.find("Release")!=std::string::npos,"forbidden rules retain held cancellation affordance");Send(103);Check(!held,"held cancellation survives known forbidden rules");Known(2,1,1,1);Refresh();Check(!Disabled(103)&&panel.nodes[107].label.find("Group QT enforced")!=std::string::npos,"same generation revalidated; enforcement is not group membership");
    for(uint64_t generation:{uint64_t{0},(std::numeric_limits<uint64_t>::max)(),uint64_t{3},uint64_t{1}}){Known(generation);Refresh();Check(!Disabled(103)&&panel.nodes[106].label.find(std::to_string(generation))!=std::string::npos,"generation is opaque, including zero/reuse/decrease");}
    Tick(1000);const auto cachedReads=reads;for(uint32_t now:{1050u,1100u,1150u,1200u})Tick(now);Check(reads==cachedReads,"automatic rules reads cached between250ms panel polls");Tick(1250);Check(reads==cachedReads+1,"automatic rules refresh250ms");const auto beforeUiReads=reads;Known(4,0,0);Send(103);Check(reads==beforeUiReads+1&&!held&&Disabled(103),"boost action refreshes before using cached allowed snapshot");
    Known(5);Tick((std::numeric_limits<uint32_t>::max)()-100);Known(6,0,0);Tick(200);Check(Disabled(103)&&panel.nodes[106].label.find("generation=6")!=std::string::npos,"presentation clock wraps without stale allow");
    for(int bad=0;bad<11;++bad){Known(7);switch(bad){case 0:--snapshot.size;break;case 1:snapshot.valid=2;break;case 2:snapshot.enforce_group_qt=2;break;case 3:snapshot.quantum_travel_allowed=2;break;case 4:snapshot.quantum_boost_allowed=2;break;case 5:snapshot.travel_target_threshold_angle=std::nanf("");break;case 6:snapshot.target_lock_angular_player=INFINITY;break;case 7:snapshot.target_lock_angular_ai=INFINITY;break;case 8:snapshot.target_lock_linear_player=INFINITY;break;case 9:snapshot.target_lock_linear_ai=INFINITY;break;case 10:snapshot.boost_group_distance_threshold=INFINITY;break;}Refresh();Check(Disabled(103)&&panel.nodes[106].label.find("unknown/unreadable")!=std::string::npos,"malformed copied snapshot clears previous permission");}
    Known(8);snapshot.valid=0;Refresh();Check(Disabled(103)&&panel.nodes[106].label.find("not received")!=std::string::npos,"valid0 is unknown even with residual allowed fields");Known(9);snapshot.target_lock_linear_ai=-1;Refresh();Check(!Disabled(103),"finite thresholds not interpreted as invented ranges");
    for(auto result:{SCO_UNAVAILABLE,SCO_FAILED,SCO_WRONG_THREAD,SCO_BAD_ARG}){rulesResult=result;Refresh();Check(Disabled(103),"read refusal clears prior allowed snapshot");}rulesResult=SCO_OK;
    for(uint32_t size=4;size<offsetof(sc_gamerules_v1,read)+sizeof(rules.read);++size){auto table=std::make_unique<unsigned char[]>(size);std::memcpy(table.get(),&rules,size);std::memcpy(table.get(),&size,4);rulesTable=table.get();const auto before=reads;Refresh();Check(Disabled(103)&&reads==before,"physically short rules table refuses before callback access");}rulesTable=&rules;
    rules.read=nullptr;const auto beforeNull=reads;Refresh();Check(Disabled(103)&&reads==beforeNull,"null read callback");rules.read=Rules;rulesTable=nullptr;Refresh();Check(Disabled(103)&&reads==beforeNull,"OK null table refuses");rulesTable=&rules;queryResult=SCO_FAILED;Refresh();Check(Disabled(103)&&reads==beforeNull,"failed query ignores returned table");queryResult=SCO_UNAVAILABLE;Refresh();Check(Disabled(103)&&reads==beforeNull,"incompatible/unavailable version explicit");queryResult=SCO_NOT_FOUND;Refresh();Check(Disabled(103)&&reads==beforeNull,"missing rules provider explicit");queryResult=SCO_OK;
    rulesCap=false;const auto beforeCap=reads,beforeQuery=queries;Refresh();Check(Disabled(103)&&reads==beforeCap&&queries==beforeQuery,"read capability gates query and native read");rulesCap=true;Known(10);Refresh();Check(!Disabled(103),"read capability reacquisition reopens current table");
    const auto beforeWrong=holds;Send(103,SCO_WIDGET_TOGGLE);Check(holds==beforeWrong,"wrong event kind cannot start boost");
    {const auto current=panel;sco_widget_event shortEvent{};shortEvent.size=sizeof(shortEvent)-1;shortEvent.node_id=103;shortEvent.kind=SCO_WIDGET_BUTTON;current.fn("quantum.travel",&shortEvent,current.ctx);Check(holds==beforeWrong,"short event record ignored");}
    api.has=nullptr;const auto noHas=reads;Refresh();Check(Disabled(103)&&reads==noHas,"null root has callback refuses read and new UI hold");api.has=Has;Refresh();
    sco_result wrong=SCO_OK;std::thread worker([&]{wrong=Call("quantum.control_status");});worker.join();Check(wrong==SCO_WRONG_THREAD,"wrong thread refusal propagated");Refresh();Check(!Disabled(103),"game thread refresh restores validated copy");
    Known(11,0,0);Refresh();sco_arg trip[2]{};trip[0].type=SCO_ARG_STRING;trip[0].v.s="Port";trip[1].type=SCO_ARG_FLOAT;trip[1].v.f=500;Check(Call("quantum.travel",trip,2)==SCO_OK&&requests==1,"GameRules QT UI policy never gates game.relocation");
    const auto stale=panel;const auto finalHolds=holds;unload();Check(ticks.empty()&&!held&&!enabled&&panel.nodes.empty(),"unload clears panel/cache and owned control state");Send(stale,103);Check(holds==finalHolds,"callback after unload inert");commands.clear();Known(0);Check(load(&api,owner)==SCO_OK,"reload");Send(102,SCO_WIDGET_TOGGLE,1);Send(stale,103);Check(holds==finalHolds,"old callback cannot act in new owner session");Send(103);Check(holds==finalHolds+1&&held,"new owner callback live with fresh rules");const auto oldReleases=releases;Send(stale,104);Check(held&&releases==oldReleases,"old callback cannot release new owner lease");unload();
#ifdef _WIN32
    FreeLibrary(module);
#else
    dlclose(module);
#endif
    std::filesystem::remove_all(path);std::printf("quantum GameRules presentation: %u checks PASS; actual module, synthetic public providers, no game\n",checks);
}
