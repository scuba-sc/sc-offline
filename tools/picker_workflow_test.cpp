// SPDX-License-Identifier: GPL-3.0-only
// One synthetic public-service fixture for the actual three native modules.
#include <sco_api.h>
#include <sco_content.h>
#include <sco_widgets.h>
#include <sc_vehicles.h>
#include <sc_actors.h>
#include <sc_spatial.h>
#include "../plugins/picker.h"
#include "panel_dump.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cctype>
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
struct Node {uint32_t kind=0,flags=0;double value=0;int32_t index=0;std::string text,label;std::vector<std::string> items;};
struct Panel {std::string title;sco_plugin* owner=nullptr;sco_widget_event_fn fn=nullptr;void* ctx=nullptr;std::map<uint32_t,Node> nodes;};
std::map<std::string,Panel> panels;
std::map<std::string,sco_command> commands;
struct ContentFile {std::string pack,name,bytes;};
std::vector<ContentFile> contentFiles;
uint64_t failingReadId=0;
std::string lastNpc;
bool widgets=true,content=true,vehicle=true;unsigned spawned=0,removed=0,seated=0,ejected=0;
uint64_t nextId=100,occupants[128]{};
std::map<uint64_t,sco_plugin*> owners;
sco_result Set(sco_plugin* self,const char* id,const char* title,const sco_widget_node* nodes,uint32_t count,uint32_t stride,sco_widget_event_fn fn,void* ctx) {
    assert(self && id && title && nodes && count>0 && count<=SCO_WIDGETS_MAX_NODES && stride>=sizeof(*nodes));
    Panel p;p.title=title;p.owner=self;p.fn=fn;p.ctx=ctx;
    std::vector<uint32_t> open; // ancestors of the current node: a parent must be a preceding ROW/COLUMN and subtrees contiguous
    for(uint32_t i=0;i<count;++i) {
        const auto& n=*reinterpret_cast<const sco_widget_node*>(reinterpret_cast<const char*>(nodes)+i*stride);
        assert(n.size>=sizeof(n) && n.id && !p.nodes.count(n.id) && n.item_count<=SCO_WIDGETS_MAX_ITEMS);
        assert(n.label && std::strlen(n.label)<=SCO_WIDGETS_MAX_TEXT);
        if(n.kind!=SCO_WIDGET_LABEL && n.kind!=SCO_WIDGET_ROW)assert(*n.label); // interactive labels and section headings are named
        if(n.parent){while(!open.empty() && open.back()!=n.parent)open.pop_back();assert(!open.empty());}else open.clear();
        if(n.kind==SCO_WIDGET_ROW || n.kind==SCO_WIDGET_COLUMN)open.push_back(n.id);
        if(n.kind==SCO_WIDGET_SLIDER)assert(std::isfinite(n.value) && n.value>=n.minimum && n.value<=n.maximum);
        if(n.kind==SCO_WIDGET_COMBO || n.kind==SCO_WIDGET_LIST)assert(n.index==-1 || (n.index>=0 && static_cast<uint32_t>(n.index)<n.item_count));
        if(n.kind==SCO_WIDGET_TABLE)assert(n.columns && n.item_count%n.columns==0);
        Node out;out.kind=n.kind;out.flags=n.flags;out.value=n.value;out.index=n.index;out.label=n.label;out.text=n.text?n.text:"";
        for(uint32_t j=0;j<n.item_count;++j){assert(n.items && n.items[j] && std::strlen(n.items[j])<=SCO_WIDGETS_MAX_TEXT);out.items.emplace_back(n.items[j]);}
        p.nodes.emplace(n.id,std::move(out));
    }
    {const auto why=sc_offline::panel_dump::Violation(nodes,count,stride);if(!why.empty())std::fprintf(stderr,"%s: %s\n",id,why.c_str());assert(why.empty());}
    sc_offline::panel_dump::Record(id,title,nodes,count,stride);
    panels[id]=std::move(p);return SCO_OK;
}
sco_result RemovePanel(sco_plugin*,const char* id){panels.erase(id);return SCO_OK;}
sco_widgets_v1 w{sizeof(w),0,Set,RemovePanel};
sco_result Item(sco_plugin*,sco_content_kind kind,uint32_t i,sco_content_item* item) {
    if(kind!=SCO_CONTENT_LIST || i>=contentFiles.size())return SCO_NOT_FOUND;
    const auto& file=contentFiles[i];*item={};item->size=sizeof(*item);item->id=i+1;item->kind=kind;
    std::snprintf(item->pack,sizeof(item->pack),"%s",file.pack.c_str());
    std::snprintf(item->name,sizeof(item->name),"%s",file.name.c_str());return SCO_OK;
}
sco_result Read(sco_plugin*,uint64_t id,void* out,uint32_t* size) {
    if(!size || !id || id>contentFiles.size())return SCO_NOT_FOUND;
    if(id==failingReadId)return SCO_FAILED;
    const auto& bytes=contentFiles[static_cast<size_t>(id-1)].bytes;
    if(!out){*size=static_cast<uint32_t>(bytes.size());return bytes.empty()?SCO_OK:SCO_TOO_MANY;}
    if(*size<bytes.size()){*size=static_cast<uint32_t>(bytes.size());return SCO_TOO_MANY;}
    if(!bytes.empty())std::memcpy(out,bytes.data(),bytes.size());
    *size=static_cast<uint32_t>(bytes.size());return SCO_OK;
}
sco_content_v1 c{sizeof(c),0,Item,Read};
sco_result PlayerShip(uint64_t* out){*out=99;return SCO_OK;}
sco_result Seats(uint64_t ship,sc_vehicle_seat* out,uint32_t max,uint32_t* count,uint32_t* more) {
    if(ship!=99)return SCO_NOT_FOUND;
    *count=out?(std::min)(max,128u):0;*more=128-*count;
    for(uint32_t i=0;i<*count;++i){out[i]={};out[i].index=i;out[i].flags=SC_SEAT_USABLE|SC_SEAT_USABLE_KNOWN;out[i].occupant_id=occupants[i];std::snprintf(out[i].name,sizeof(out[i].name),"synthetic seat %u",i);}
    return SCO_OK;
}
sco_result Occupant(uint64_t ship,uint32_t index,uint64_t* out){if(ship!=99 || index>=128)return SCO_NOT_FOUND;*out=occupants[index];return SCO_OK;}
sco_result Seat(sco_plugin*,uint64_t actor,uint64_t ship,uint32_t index){if(ship!=99 || index>=128)return SCO_BAD_ARG;occupants[index]=actor;++seated;return SCO_OK;}
sco_result Eject(sco_plugin*,uint64_t actor){for(auto& id:occupants)if(id==actor)id=0;++ejected;return SCO_OK;}
sco_result Power(sco_plugin*,uint64_t){return SCO_OK;}
sc_vehicles_v1 v{};
sco_result Local(uint64_t* actor,uint64_t* entity){if(actor)*actor=9001;if(entity)*entity=1;return SCO_OK;} // entity id 1; actor id differs
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
// Rule 2 of docs/plugin-ui.md: no developer words in anything the player reads.
void NoInternalWords(const char* id) {
    static const char* const words[]={"archetype","owned","ticket","phase","catalog","sdk","g2","g3","payload","pagination","adapter","gamerules","retained","preview","session id"};
    const auto check=[&](const std::string& text) {
        std::string lower;for(char ch:text)lower+=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        for(const char* word:words)if(lower.find(word)!=std::string::npos){std::fprintf(stderr,"%s shows '%s' in \"%s\"\n",id,word,text.c_str());std::abort();}
    };
    for(const auto& entry:panels.at(id).nodes){check(entry.second.label);if(entry.second.kind!=SCO_WIDGET_LIST)for(const auto& item:entry.second.items)check(item);} // list rows are pack data
}
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
    std::map<std::string,std::string> parsedAliases;
    assert(sco::picker::Catalog::ParseAliasFile("synthetic_id\tÉtoile\n",127,parsedAliases));
    assert(parsedAliases.at("synthetic_id")=="Étoile");
    assert(sco::picker::MatchesEveryWordAsciiInsensitive("Étoile","Étoile"));
    assert(!sco::picker::MatchesEveryWordAsciiInsensitive("Étoile","étoile"));
    const std::string zwnjAlias="synthetic_id\t\xe0\xa4\x95\xe0\xa5\x8d\xe2\x80\x8c\xe0\xa4\xb7\n";
    const std::string zwjAlias="synthetic_id\t\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x9a\x80\n";
    assert(sco::picker::Catalog::ParseAliasFile(zwnjAlias,127,parsedAliases));
    assert(sco::picker::Catalog::ParseAliasFile(zwjAlias,127,parsedAliases));
    assert(!sco::picker::Catalog::ParseAliasFile("synthetic_id\tlabel \xe2\x80\xae\n",127,parsedAliases));
    sco::picker::Choice boundary;
    boundary.value=std::string(127,'c');boundary.pack=std::string(63,'p');
    boundary.label=boundary.value+" ["+boundary.pack+"]";
    const auto originalLabel=boundary.label;
    assert(!sco::picker::SetAliasLabel(boundary,std::string(60,'a')) && boundary.label==originalLabel);
    assert(sco::picker::SetAliasLabel(boundary,std::string(59,'a')) && boundary.label.size()==SCO_WIDGETS_MAX_TEXT);
    std::string npcCatalog;
    for(int i=0;i<300;++i)npcCatalog+="synthetic_actor_"+std::to_string(i)+"\n";
    contentFiles={
        {"ship_pack_a","lists/ships.txt","synthetic_ship_alpha\nsynthetic_ship_beta\n"},
        {"ship_pack_b","lists/ships.txt","synthetic_ship_alpha\n"},
        {"synthetic_catalog","lists/npcs.txt",npcCatalog},
        {"second_pack","lists/npcs.txt",npcCatalog},
        {"human_authored","lists/ships.aliases.txt","# exact class id<TAB>human display name\nsynthetic_ship_alpha\tSynthetic Shuttle\n"},
        {"human_authored","lists/npcs.aliases.txt","synthetic_actor_299\tSynthetic Pilot\n"},
    };
    v.size=sizeof(v);v.player_ship=PlayerShip;v.seats=Seats;v.seat_occupant=Occupant;v.seat=Seat;v.eject=Eject;v.power_on=Power;
    a.size=sizeof(a);a.local_player=Local;a.spawn_npc=Spawn;a.despawn=Despawn;s.size=sizeof(s);s.player_pose=Pose;
    sco_api api{};api.size=sizeof(api);api.major=SCO_API_MAJOR;api.minor=SCO_API_MINOR;api.has=Has;api.log=Log;api.register_command=Register;api.query_service=Query;api.subscribe=Subscribe;api.unsubscribe=Unsubscribe;api.invoke=Invoke;
    Module ship(argv[1]),crew(argv[2]),npc(argv[3]);
    assert(ship.load(&api,reinterpret_cast<sco_plugin*>(1))==SCO_OK);
    assert(crew.load(&api,reinterpret_cast<sco_plugin*>(2))==SCO_OK);
    assert(npc.load(&api,reinterpret_cast<sco_plugin*>(3))==SCO_OK);
    assert(panels.size()==4 && panels.at("spawn.vehicles").title=="Vehicles" && panels.at("crew.crew").title=="Crew" && panels.at("npc.npcs").title=="NPCs" && panels.at("spawn.asop").title=="Your own ships");
#if __has_include(<sc_asop_selection.h>)
    assert(panels.at("spawn.asop").nodes.size()==16 &&
           panels.at("spawn.asop").nodes.at(71).label=="Your fleet list isn't available right now." &&
           panels.at("spawn.asop").nodes.at(72).label=="Page 1 of 1. 0 selected.");
#else
    assert(panels.at("spawn.asop").nodes.size()==1 &&
           panels.at("spawn.asop").nodes.at(1).label=="Ships from your in-game fleet can't be used with this version.");
#endif
    for(const char* id:{"spawn.vehicles","spawn.asop","crew.crew","npc.npcs"})NoInternalWords(id);
    const auto& shipLabels=panels.at("spawn.vehicles").nodes.at(2).items;
    assert(shipLabels.size()==3);
    assert(shipLabels[0]=="Synthetic Shuttle (ship_pack_a)"); // the same name from two packs says which pack
    assert(shipLabels[1]=="synthetic ship beta");
    assert(shipLabels[2]=="Synthetic Shuttle (ship_pack_b)");
    {   // Page structure: named sections, plain labels, nothing to press before a ship is chosen.
        auto& vp=panels.at("spawn.vehicles");
        assert(vp.nodes.at(30).kind==SCO_WIDGET_COLUMN && vp.nodes.at(30).label=="Choose a ship" && vp.nodes.at(1).label=="Search ships" && vp.nodes.at(2).label=="Ships");
        assert(vp.nodes.at(19).label=="Page 1 of 1 (3 ships)");
        assert(vp.nodes.at(7).label=="Spawn ship" && (vp.nodes.at(7).flags&SCO_WIDGET_DISABLED) && vp.nodes.at(34).label=="Choose a ship first.");
        assert(vp.nodes.count(11)==0 && vp.nodes.at(37).label=="You haven't spawned any ships yet." && (vp.nodes.at(12).flags&SCO_WIDGET_DISABLED) && (vp.nodes.at(14).flags&SCO_WIDGET_DISABLED));
        assert(vp.nodes.at(12).label=="Get in" && vp.nodes.at(13).label=="Remove ship" && vp.nodes.at(14).label=="Remove all my ships");
        assert(vp.nodes.at(20).label=="Nothing yet." && vp.nodes.count(17)==0);
    }
    {   // Flight Ready is offered only with the pilot seat; otherwise it is disabled, explained and sent as false.
        auto& vp=panels.at("spawn.vehicles");
        assert(vp.nodes.at(5).kind==SCO_WIDGET_COMBO && vp.nodes.at(5).index==0 && vp.nodes.at(5).items.size()==2 && vp.nodes.at(6).label=="Start ready to fly");
        assert(vp.nodes.at(6).value==1 && !(vp.nodes.at(6).flags&SCO_WIDGET_DISABLED) && vp.nodes.count(21)==0);
        Event("spawn.vehicles",3,SCO_WIDGET_TEXT_INPUT,-1,"synthetic_ship_alpha");
        assert(!(panels.at("spawn.vehicles").nodes.at(7).flags&SCO_WIDGET_DISABLED) && panels.at("spawn.vehicles").nodes.count(34)==0);
        Event("spawn.vehicles",5,SCO_WIDGET_COMBO,1);
        auto& np=panels.at("spawn.vehicles");
        assert(np.nodes.at(5).index==1 && (np.nodes.at(6).flags&SCO_WIDGET_DISABLED) && np.nodes.at(6).value==0);
        assert(np.nodes.at(21).label=="Ready to fly needs the pilot seat.");
        Event("spawn.vehicles",7,SCO_WIDGET_BUTTON);
        assert(panels.at("spawn.vehicles").nodes.at(20).label.find("Ready to fly needs")==std::string::npos);
        Event("spawn.vehicles",5,SCO_WIDGET_COMBO,0);
        assert(panels.at("spawn.vehicles").nodes.at(6).value==1 && !(panels.at("spawn.vehicles").nodes.at(6).flags&SCO_WIDGET_DISABLED) && panels.at("spawn.vehicles").nodes.count(21)==0);
    }
    Event("spawn.vehicles",1,SCO_WIDGET_TEXT_INPUT,-1,"synthetic shuttle");
    assert(panels.at("spawn.vehicles").nodes.at(2).items.size()==2);
    assert(panels.at("spawn.vehicles").nodes.at(2).items[0]=="Synthetic Shuttle (ship_pack_a)");
    assert(panels.at("spawn.vehicles").nodes.at(2).items[1]=="Synthetic Shuttle (ship_pack_b)");
    Event("spawn.vehicles",2,SCO_WIDGET_LIST,1);
    assert(panels.at("spawn.vehicles").nodes.at(3).text=="synthetic_ship_alpha");
    Event("spawn.vehicles",1,SCO_WIDGET_TEXT_INPUT,-1,"synthetic_ship_beta");
    assert(panels.at("spawn.vehicles").nodes.at(2).items.size()==1);
    assert(panels.at("spawn.vehicles").nodes.at(2).items[0]=="synthetic ship beta");
    assert(panels.at("npc.npcs").nodes.at(2).items.size()==64);
    Event("npc.npcs",7,SCO_WIDGET_BUTTON);Event("npc.npcs",2,SCO_WIDGET_LIST,0);
    assert(panels.at("npc.npcs").nodes.at(3).text=="synthetic_actor_64");
    Event("npc.npcs",1,SCO_WIDGET_TEXT_INPUT,-1,"actor_299");
    assert(panels.at("npc.npcs").nodes.at(2).items.size()==2); // Same row in two distinct packs is retained.
    assert(panels.at("npc.npcs").nodes.at(2).items[0]=="Synthetic Pilot (synthetic_catalog)");
    assert(panels.at("npc.npcs").nodes.at(2).items[1]=="Synthetic Pilot (second_pack)");
    Event("npc.npcs",1,SCO_WIDGET_TEXT_INPUT,-1,"synthetic pilot");
    assert(panels.at("npc.npcs").nodes.at(2).items.size()==2); // Alias search keeps both source rows.
    Event("npc.npcs",2,SCO_WIDGET_LIST,1);Event("npc.npcs",5,SCO_WIDGET_BUTTON);
    assert(spawned==1 && lastNpc=="synthetic_actor_299");
    Event("npc.npcs",1,SCO_WIDGET_TEXT_INPUT,-1,"synthetic_actor_0");
    assert(panels.at("npc.npcs").nodes.at(2).items[0]=="synthetic actor 0 (synthetic_catalog)");
    Event("npc.npcs",2,SCO_WIDGET_LIST,1);Event("npc.npcs",5,SCO_WIDGET_BUTTON);
    assert(spawned==2 && lastNpc=="synthetic_actor_0"); // No alias: exact legacy identifier stays selectable.
    contentFiles.push_back({"alias_conflict_a","lists/npcs.aliases.txt","synthetic_actor_298\tFirst Human Name\n"});
    contentFiles.push_back({"alias_conflict_b","lists/npcs.aliases.txt","synthetic_actor_298\tSecond Human Name\n"});
    Event("npc.npcs",8,SCO_WIDGET_BUTTON); // two packs name this character differently: neither name is shown
    Event("npc.npcs",1,SCO_WIDGET_TEXT_INPUT,-1,"first human");
    assert(panels.at("npc.npcs").nodes.at(2).items.empty());
    Event("npc.npcs",1,SCO_WIDGET_TEXT_INPUT,-1,"synthetic_actor_298");
    assert(panels.at("npc.npcs").nodes.at(2).items.size()==2);
    assert(panels.at("npc.npcs").nodes.at(2).items[0]=="synthetic actor 298 (synthetic_catalog)");
    contentFiles.push_back({"incomplete_alias","lists/npcs.aliases.txt","synthetic_actor_297\tIncomplete Read\n"});
    failingReadId=contentFiles.size();
    Event("npc.npcs",8,SCO_WIDGET_BUTTON); // an unreadable name file is ignored as a whole
    Event("npc.npcs",1,SCO_WIDGET_TEXT_INPUT,-1,"synthetic pilot");
    assert(panels.at("npc.npcs").nodes.at(2).items.empty());
    Event("npc.npcs",1,SCO_WIDGET_TEXT_INPUT,-1,"synthetic_actor_299");
    assert(panels.at("npc.npcs").nodes.at(2).items[0]=="synthetic actor 299 (synthetic_catalog)");
    failingReadId=0;contentFiles.pop_back();
    std::string invalidAlias="synthetic_actor_297\tInvalid ";
    invalidAlias.push_back(static_cast<char>(0xc3));invalidAlias.push_back('(');invalidAlias.push_back('\n');
    contentFiles.push_back({"malformed_alias","lists/npcs.aliases.txt",invalidAlias});
    Event("npc.npcs",8,SCO_WIDGET_BUTTON); // an unreadable name file is ignored as a whole
    Event("npc.npcs",1,SCO_WIDGET_TEXT_INPUT,-1,"synthetic pilot");
    assert(panels.at("npc.npcs").nodes.at(2).items.empty());
    Event("npc.npcs",1,SCO_WIDGET_TEXT_INPUT,-1,"synthetic_actor_299");
    assert(panels.at("npc.npcs").nodes.at(2).items[0]=="synthetic actor 299 (synthetic_catalog)");
    Event("npc.npcs",9,SCO_WIDGET_COMBO,0);Event("npc.npcs",10,SCO_WIDGET_BUTTON);assert(removed==1);
    assert(panels.at("crew.crew").nodes.at(21).items.size()==96); // 32 seats a page, 3 columns
    Event("crew.crew",16,SCO_WIDGET_BUTTON);assert(panels.at("crew.crew").nodes.at(4).items[0]=="synthetic seat 32");
    Event("crew.crew",1,SCO_WIDGET_TEXT_INPUT,-1,"synthetic pilot");
    assert(panels.at("crew.crew").nodes.at(2).items.size()==2);
    Event("crew.crew",2,SCO_WIDGET_LIST,1);Event("crew.crew",4,SCO_WIDGET_COMBO,0);Event("crew.crew",18,SCO_WIDGET_BUTTON);
    assert(spawned==3 && lastNpc=="synthetic_actor_299");
    Event("crew.crew",4,SCO_WIDGET_COMBO,1);Event("crew.crew",3,SCO_WIDGET_TEXT_INPUT,-1,"synthetic_crew");Event("crew.crew",18,SCO_WIDGET_BUTTON);
    assert(spawned==4 && lastNpc=="synthetic_crew"); // Manual identifiers remain available.
    Event("crew.crew",4,SCO_WIDGET_COMBO,0);
    occupants[32]=102;Event("crew.crew",19,SCO_WIDGET_BUTTON);assert(ejected==1);
    occupants[32]=102;Event("crew.crew",20,SCO_WIDGET_BUTTON);assert(removed==2);
    occupants[32]=9999;Event("crew.crew",20,SCO_WIDGET_BUTTON);Event("crew.crew",19,SCO_WIDGET_BUTTON);assert(removed==2 && ejected==1);
    assert(panels.at("spawn.vehicles").nodes.count(16)==0 && panels.at("spawn.vehicles").nodes.count(17)==0); // the ammo toggle moved to the Creative tab; the start-place button could only refuse
    Event("spawn.vehicles",16,SCO_WIDGET_TOGGLE,-1,nullptr,1);assert(panels.at("spawn.vehicles").nodes.count(16)==0); // a forged event for it is ignored
    {const auto before=panels.at("spawn.vehicles").nodes.at(20).label;Event("spawn.vehicles",17,SCO_WIDGET_BUTTON);assert(panels.at("spawn.vehicles").nodes.at(20).label==before);}
    vehicle=false;Event("crew.crew",17,SCO_WIDGET_BUTTON);assert(panels.at("crew.crew").nodes.at(21).items.empty());
    auto fn=panels.at("npc.npcs").fn;auto ctx=panels.at("npc.npcs").ctx;sco_widget_event shortEvent{};shortEvent.size=sizeof(shortEvent)-1;fn("npc.npcs",&shortEvent,ctx);
    for(const char* id:{"spawn.vehicles","spawn.asop","crew.crew","npc.npcs"})NoInternalWords(id);
    npc.unload();crew.unload();ship.unload();assert(panels.empty());
    widgets=false;assert(npc.load(&api,reinterpret_cast<sco_plugin*>(3))==SCO_OK);assert(panels.empty());npc.unload();
    widgets=true;content=false;assert(npc.load(&api,reinterpret_cast<sco_plugin*>(3))==SCO_OK);assert(panels.at("npc.npcs").nodes.at(2).items.empty());
    Event("npc.npcs",3,SCO_WIDGET_TEXT_INPUT,-1,"manual_actor");Event("npc.npcs",5,SCO_WIDGET_BUTTON);assert(lastNpc=="manual_actor");npc.unload();assert(panels.empty());
    std::puts("PASS picker-module-workflow: 3 actual modules; catalogs/filter/paging, owned seat actions, foreign refusal, unavailable services, no creative control, no start-place button, plain labels and sections, unload/reload");
}
