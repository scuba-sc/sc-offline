// SPDX-License-Identifier: GPL-3.0-only
// One synthetic public-service fixture for the actual three native modules.
#include <sco_api.h>
#include <sco_content.h>
#include <sco_widgets.h>
#include <sc_vehicles.h>
#include <sc_actors.h>
#include <sc_spatial.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace {
struct Node {uint32_t kind=0;double value=0;int32_t index=0;std::string text,label;std::vector<std::string> items;};
struct Panel {std::string title;sco_plugin* owner=nullptr;sco_widget_event_fn fn=nullptr;void* ctx=nullptr;std::map<uint32_t,Node> nodes;};
std::map<std::string,Panel> panels;
std::map<std::string,sco_command> commands;
std::string catalog,lastNpc;
bool widgets=true,content=true,vehicle=true;unsigned spawned=0,removed=0,seated=0,ejected=0;
uint64_t nextId=100,occupants[128]{};
std::map<uint64_t,sco_plugin*> owners;
sco_result Set(sco_plugin* self,const char* id,const char* title,const sco_widget_node* nodes,uint32_t count,uint32_t stride,sco_widget_event_fn fn,void* ctx) {
    assert(self && id && title && nodes && count>0 && count<=SCO_WIDGETS_MAX_NODES && stride>=sizeof(*nodes));
    Panel p;p.title=title;p.owner=self;p.fn=fn;p.ctx=ctx;
    for(uint32_t i=0;i<count;++i) {
        const auto& n=*reinterpret_cast<const sco_widget_node*>(reinterpret_cast<const char*>(nodes)+i*stride);
        assert(n.size>=sizeof(n) && n.id && !p.nodes.count(n.id) && n.item_count<=SCO_WIDGETS_MAX_ITEMS);
        assert(n.label && std::strlen(n.label)<=SCO_WIDGETS_MAX_TEXT);
        if(n.kind==SCO_WIDGET_SLIDER)assert(std::isfinite(n.value) && n.value>=n.minimum && n.value<=n.maximum);
        if(n.kind==SCO_WIDGET_COMBO || n.kind==SCO_WIDGET_LIST)assert(n.index==-1 || (n.index>=0 && static_cast<uint32_t>(n.index)<n.item_count));
        if(n.kind==SCO_WIDGET_TABLE)assert(n.columns && n.item_count%n.columns==0);
        Node out;out.kind=n.kind;out.value=n.value;out.index=n.index;out.label=n.label;out.text=n.text?n.text:"";
        for(uint32_t j=0;j<n.item_count;++j){assert(n.items && n.items[j] && std::strlen(n.items[j])<=SCO_WIDGETS_MAX_TEXT);out.items.emplace_back(n.items[j]);}
        p.nodes.emplace(n.id,std::move(out));
    }
    panels[id]=std::move(p);return SCO_OK;
}
sco_result RemovePanel(sco_plugin*,const char* id){panels.erase(id);return SCO_OK;}
sco_widgets_v1 w{sizeof(w),0,Set,RemovePanel};
sco_result Item(sco_plugin*,sco_content_kind kind,uint32_t i,sco_content_item* item) {
    if(kind!=SCO_CONTENT_LIST || i>=3)return SCO_NOT_FOUND;
    item->id=i+1;item->kind=kind;
    std::snprintf(item->pack,sizeof(item->pack),"%s",i==2?"second_pack":"synthetic_catalog");
    std::snprintf(item->name,sizeof(item->name),"lists/%s.txt",i==0?"ships":"npcs");return SCO_OK;
}
sco_result Read(sco_plugin*,uint64_t,void* out,uint32_t* size) {
    if(!out || *size<catalog.size()){*size=static_cast<uint32_t>(catalog.size());return SCO_TOO_MANY;}
    std::memcpy(out,catalog.data(),catalog.size());*size=static_cast<uint32_t>(catalog.size());return SCO_OK;
}
sco_content_v1 c{sizeof(c),0,Item,Read};
sco_result PlayerShip(uint64_t* out){*out=99;return SCO_OK;}
sco_result Seats(uint64_t ship,sc_vehicle_seat* out,uint32_t max,uint32_t* count,uint32_t* more) {
    if(ship!=99)return SCO_NOT_FOUND;
    *count=out?std::min(max,128u):0;*more=128-*count;
    for(uint32_t i=0;i<*count;++i){out[i]={};out[i].index=i;out[i].flags=SC_SEAT_USABLE|SC_SEAT_USABLE_KNOWN;out[i].occupant_id=occupants[i];std::snprintf(out[i].name,sizeof(out[i].name),"synthetic seat %u",i);}
    return SCO_OK;
}
sco_result Occupant(uint64_t ship,uint32_t index,uint64_t* out){if(ship!=99 || index>=128)return SCO_NOT_FOUND;*out=occupants[index];return SCO_OK;}
sco_result Seat(sco_plugin*,uint64_t actor,uint64_t ship,uint32_t index){if(ship!=99 || index>=128)return SCO_BAD_ARG;occupants[index]=actor;++seated;return SCO_OK;}
sco_result Eject(sco_plugin*,uint64_t actor){for(auto& id:occupants)if(id==actor)id=0;++ejected;return SCO_OK;}
sco_result Power(sco_plugin*,uint64_t){return SCO_OK;}
sc_vehicles_v1 v{};
sco_result Local(uint64_t* actor,uint64_t* entity){*actor=1;if(entity)*entity=1;return SCO_OK;}
sco_result Spawn(sco_plugin* self,const char* cls,uint64_t,const double*,uint64_t* id){lastNpc=cls;*id=nextId++;owners[*id]=self;++spawned;return SCO_OK;}
sco_result Despawn(sco_plugin* self,uint64_t id){if(owners[id]!=self)return SCO_BAD_ARG;++removed;return SCO_OK;}
sc_actors_v1 a{};
int Pose(double* pos,double* q,uint64_t* zone){pos[0]=pos[1]=pos[2]=0;q[0]=q[1]=q[2]=0;q[3]=1;*zone=7;return 1;}
sc_spatial_v1 s{};
sco_result Query(const char* name,uint32_t,const void** out) {
    *out=nullptr;
    if(!std::strcmp(name,SCO_WIDGETS_NAME) && widgets)*out=&w;
    else if(!std::strcmp(name,SCO_CONTENT_NAME) && content)*out=&c;
    else if(!std::strcmp(name,SC_VEHICLES_SERVICE_NAME) && vehicle)*out=&v;
    else if(!std::strcmp(name,SC_ACTORS_NAME))*out=&a;
    else if(!std::strcmp(name,SC_SPATIAL_SERVICE_NAME))*out=&s;
    return *out?SCO_OK:SCO_NOT_FOUND;
}
int Has(const char*){return 1;}
void Log(sco_plugin*,sco_log_level,const char*){}
sco_result Register(sco_plugin*,const sco_command* cmd){commands[cmd->name]=*cmd;return SCO_OK;}
sco_result Subscribe(sco_plugin*,const char*,sco_event_fn,void*){return SCO_OK;}
sco_result Unsubscribe(sco_plugin*,const char*,sco_event_fn){return SCO_OK;}
sco_result Invoke(sco_plugin*,const char*,const sco_arg*,uint32_t,sco_invoke_done done,void* ctx){if(done)done(SCO_NOT_FOUND,"plugin absent",ctx);return SCO_NOT_FOUND;}
void Event(const char* panel,uint32_t id,uint32_t kind,int32_t index=-1,const char* text=nullptr,double value=0) {
    auto fn=panels.at(panel).fn;auto ctx=panels.at(panel).ctx;
    sco_widget_event e{sizeof(e),id,kind,index,value,text};fn(panel,&e,ctx);
}
struct Module {
#ifdef _WIN32
    HMODULE handle;
#else
    void* handle;
#endif
    sco_result (*load)(const sco_api*,sco_plugin*);
    void (*unload)();
    explicit Module(const char* path) {
#ifdef _WIN32
        handle=LoadLibraryA(path);assert(handle);load=reinterpret_cast<decltype(load)>(GetProcAddress(handle,"sco_plugin_load"));unload=reinterpret_cast<decltype(unload)>(GetProcAddress(handle,"sco_plugin_unload"));
#else
        handle=dlopen(path,RTLD_NOW|RTLD_LOCAL);if(!handle){std::fprintf(stderr,"%s\n",dlerror());std::abort();}load=reinterpret_cast<decltype(load)>(dlsym(handle,"sco_plugin_load"));unload=reinterpret_cast<decltype(unload)>(dlsym(handle,"sco_plugin_unload"));
#endif
        assert(load && unload);
    }
    ~Module(){
#ifdef _WIN32
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
    }
};
}
int main(int argc,char** argv) {
    assert(argc==4);
    for(int i=0;i<300;++i)catalog+="synthetic_actor_"+std::to_string(i)+"\n";
    v.size=sizeof(v);v.player_ship=PlayerShip;v.seats=Seats;v.seat_occupant=Occupant;v.seat=Seat;v.eject=Eject;v.power_on=Power;
    a.size=sizeof(a);a.local_player=Local;a.spawn_npc=Spawn;a.despawn=Despawn;s.size=sizeof(s);s.player_pose=Pose;
    sco_api api{};api.size=sizeof(api);api.major=SCO_API_MAJOR;api.minor=SCO_API_MINOR;api.has=Has;api.log=Log;api.register_command=Register;api.query_service=Query;api.subscribe=Subscribe;api.unsubscribe=Unsubscribe;api.invoke=Invoke;
    Module ship(argv[1]),crew(argv[2]),npc(argv[3]);
    assert(ship.load(&api,reinterpret_cast<sco_plugin*>(1))==SCO_OK);
    assert(crew.load(&api,reinterpret_cast<sco_plugin*>(2))==SCO_OK);
    assert(npc.load(&api,reinterpret_cast<sco_plugin*>(3))==SCO_OK);
    assert(panels.size()==3 && panels.at("spawn.vehicles").title=="Vehicles" && panels.at("crew.crew").title=="Crew" && panels.at("npc.npcs").title=="NPCs");
    assert(panels.at("npc.npcs").nodes.at(2).items.size()==64);
    Event("npc.npcs",7,SCO_WIDGET_BUTTON);Event("npc.npcs",2,SCO_WIDGET_LIST,0);
    assert(panels.at("npc.npcs").nodes.at(3).text=="synthetic_actor_64");
    Event("npc.npcs",1,SCO_WIDGET_TEXT_INPUT,-1,"actor_299");
    assert(panels.at("npc.npcs").nodes.at(2).items.size()==2); // Same row in two distinct packs is retained.
    Event("npc.npcs",2,SCO_WIDGET_LIST,1);Event("npc.npcs",5,SCO_WIDGET_BUTTON);
    assert(spawned==1 && lastNpc=="synthetic_actor_299");
    Event("npc.npcs",9,SCO_WIDGET_COMBO,0);Event("npc.npcs",10,SCO_WIDGET_BUTTON);assert(removed==1);
    assert(panels.at("crew.crew").nodes.at(21).items.size()==128);
    Event("crew.crew",16,SCO_WIDGET_BUTTON);assert(panels.at("crew.crew").nodes.at(4).items[0]=="synthetic seat 32");
    Event("crew.crew",4,SCO_WIDGET_COMBO,0);Event("crew.crew",3,SCO_WIDGET_TEXT_INPUT,-1,"synthetic_crew");Event("crew.crew",18,SCO_WIDGET_BUTTON);
    assert(spawned==2 && lastNpc=="synthetic_crew");
    occupants[32]=101;Event("crew.crew",19,SCO_WIDGET_BUTTON);assert(ejected==1);
    occupants[32]=101;Event("crew.crew",20,SCO_WIDGET_BUTTON);assert(removed==2);
    occupants[32]=9999;Event("crew.crew",20,SCO_WIDGET_BUTTON);Event("crew.crew",19,SCO_WIDGET_BUTTON);assert(removed==2 && ejected==1);
    Event("spawn.vehicles",16,SCO_WIDGET_TOGGLE,-1,nullptr,1);assert(panels.at("spawn.vehicles").nodes.at(16).value==0);
    Event("spawn.vehicles",17,SCO_WIDGET_BUTTON);assert(panels.at("spawn.vehicles").nodes.at(20).label.find("unavailable")!=std::string::npos);
    vehicle=false;Event("crew.crew",17,SCO_WIDGET_BUTTON);assert(panels.at("crew.crew").nodes.at(21).items.empty());
    auto fn=panels.at("npc.npcs").fn;auto ctx=panels.at("npc.npcs").ctx;sco_widget_event shortEvent{};shortEvent.size=sizeof(shortEvent)-1;fn("npc.npcs",&shortEvent,ctx);
    npc.unload();crew.unload();ship.unload();assert(panels.empty());
    widgets=false;assert(npc.load(&api,reinterpret_cast<sco_plugin*>(3))==SCO_OK);assert(panels.empty());npc.unload();
    widgets=true;content=false;assert(npc.load(&api,reinterpret_cast<sco_plugin*>(3))==SCO_OK);assert(panels.at("npc.npcs").nodes.at(2).items.empty());
    Event("npc.npcs",3,SCO_WIDGET_TEXT_INPUT,-1,"manual_actor");Event("npc.npcs",5,SCO_WIDGET_BUTTON);assert(lastNpc=="manual_actor");npc.unload();assert(panels.empty());
    std::puts("PASS picker-module-workflow: 3 actual modules; catalogs/filter/paging, owned seat actions, foreign refusal, unavailable services, creative revert, startup refusal, unload/reload");
}
