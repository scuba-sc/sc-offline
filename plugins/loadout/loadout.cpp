// SPDX-License-Identifier: GPL-3.0-only
// Product policy only: copied pack lists, typed equip requests and read-only SQ42 observations.
#include "loadout.h"
#include "sc_loadout.h"
#include "sc_cvars.h"
#include "sc_creative.h"
#include "sco_content.h"
#include "sco_widgets.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace sco::features::loadout {
namespace {
#define HAS(t, type, m) ((t) && (t)->size >= offsetof(type, m) + sizeof((t)->m) && (t)->m)
struct Piece { std::string port, name; uint32_t slot = 0; };
struct Outfit { std::string name; std::vector<Piece> pieces; };
struct Lists { std::vector<Piece> items; std::vector<Outfit> outfits; };
struct State {
    const sco_api* api = nullptr; sco_plugin* self = nullptr;
    Lists lists; bool lists_known = false, spoilers = false, clocked = false;
    uint32_t last_read = 0, page = 0; int32_t outfit = -1, ship = 0, build = -1;
    int cvars[4]{-1,-1,-1,-1};
    std::string filter, equip, visor; std::vector<std::string> buildables;
    double height = 30, ahead = 8, speed = 30; bool sit = true;
    char status[2048]{};
} g;
const sco_plugin_info info{sizeof(sco_plugin_info),SCO_API_MAJOR,SCO_API_MINOR,"loadout","1.0.0","sc-offline"};
const char* const cvar_names[]{"i_target_selector.targeting2_enabled","pl_lensdisplay.minimap_enabled",
    "pl_lensdisplay.greebles_enabled","g_squadron_frontend"};
const char* const cvar_labels[]{"SQ42 auto targeting","Visor mini-map","Visor greebles","SQ42 menus (experimental)"};
// Existing product reference names/labels, not fixtures or extracted assets.
struct Ship { const char* label; const char* cls; double height; bool board; };
const Ship ships[]{
    {"Idris-P (the Stanton's class)","AEGS_Idris_P",30,true},
    {"Gladius (SQ42 fighter)","AEGS_Gladius",30,true},
    {"Retaliator (has an S42 HUD)","AEGS_Retaliator",30,true},
    {"Starfarer (ch 5, 7, 9)","MISC_Starfarer",30,true},
    {"Avenger Stalker (S42 wreck)","AEGS_Avenger_Stalker",30,true},
    {"Hornet (Cal Mason's ship)","ANVL_Hornet_F7C",30,true},
    {"Vanduul Blade (AI)","VNCL_Blade_PU_AI_VAN",300,false},
    {"Vanduul Scythe (AI)","VNCL_Scythe_PU_AI_VAN",300,false},
    {"Vanduul Glaive (AI)","VNCL_Glaive_PU_AI_VAN",300,false},
    {"Vanduul Stinger (AI)","VNCL_Stinger_PU_AI_VAN",300,false},
    {"Bengal (UEE)","RSI_Bengal_PU_AI_UEE",1500,false},
};
template<class T> const T* Open(const char* name) {
    const void* table = nullptr;
    if (!HAS(g.api,sco_api,query_service) || g.api->query_service(name,0x10000u,&table) != SCO_OK) return nullptr;
    return static_cast<const T*>(table);
}
bool Cap(const char* name) { return HAS(g.api,sco_api,has) && g.api->has(name); }
sco_result Answer(sco_result result,char* reply,uint32_t size,const char* message) {
    std::snprintf(g.status,sizeof(g.status),"%s",message);
    if (reply && size) std::snprintf(reply,size,"%s",g.status);
    if (HAS(g.api,sco_api,log)) g.api->log(g.self,result == SCO_OK ? SCO_LOG_INFO : SCO_LOG_WARN,g.status);
    return result;
}
std::string Trim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t\r");
    if (first == s.npos) return {};
    return std::string(s.substr(first,s.find_last_not_of(" \t\r")-first+1));
}
bool Same(std::string_view a,std::string_view b) {
    if (a.size()!=b.size()) return false;
    for (size_t i=0;i<a.size();++i) {
        const auto lower=[](unsigned char c) { return c>='A' && c<='Z' ? c+('a'-'A') : c; };
        if (lower(static_cast<unsigned char>(a[i]))!=lower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}
bool Class(std::string_view name) {
    if (name.empty() || name.size()>=SC_LOADOUT_ITEM_NAME_BYTES) return false;
    for (unsigned char c:name) if (!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_')) return false;
    return true;
}
uint32_t Slot(std::string_view name,bool outfit) {
    struct Map { const char* item; const char* port; uint32_t slot; };
    // These are logical consumer selections only; the game pack must verify actual engine mapping.
    const Map map[]{ {"undersuit","Armor_Undersuit",SC_LOADOUT_SLOT_UNDERSUIT},
        {"helmet","Armor_Helmet",SC_LOADOUT_SLOT_HEAD},{"torso","Armor_Torso",SC_LOADOUT_SLOT_TORSO},
        {"arms","Armor_Arms",SC_LOADOUT_SLOT_ARMS},{"legs","Armor_Legs",SC_LOADOUT_SLOT_LEGS},
        {"backpack","backpack",SC_LOADOUT_SLOT_BACKPACK},{"visor","Lens_ItemPort",SC_LOADOUT_SLOT_VISOR} };
    for (const auto& m:map) if (Same(name,outfit ? m.port : m.item)) return m.slot;
    // Never invent G6 extension slots for weapons, clothing, body, head parts or material variants.
    return 0;
}
sco_result Parse(std::string_view bytes,int kind,Lists& lists,std::vector<std::string>& buildables) {
    if (bytes.find('\0')!=bytes.npos) return SCO_BAD_ARG;
    std::string section; Outfit* outfit=nullptr;
    while (!bytes.empty()) {
        const auto end=bytes.find('\n'); const auto raw=bytes.substr(0,end);
        bytes=end==bytes.npos ? std::string_view{} : bytes.substr(end+1);
        const auto line=Trim(raw.substr(0,raw.find('#')));
        if (line.empty()) continue;
        if (line.size()>255) return SCO_TOO_MANY;
        if (line.front()=='[') {
            if (line.size()<3 || line.back()!=']') return SCO_BAD_ARG;
            section=Trim(std::string_view(line).substr(1,line.size()-2));
            if (section.empty() || section.size()>127) return SCO_BAD_ARG;
            if (kind==1) {
                if (lists.outfits.size()>=256) return SCO_TOO_MANY;
                lists.outfits.push_back({section,{}}); outfit=&lists.outfits.back();
            }
            continue;
        }
        if (section.empty()) return SCO_BAD_ARG;
        if (kind==0) {
            if (!Class(line)) return SCO_BAD_ARG;
            if (lists.items.size()>=3000) return SCO_TOO_MANY;
            lists.items.push_back({section,line,Slot(section,false)});
        } else if (kind==1) {
            const auto sep=line.find_first_of(" \t");
            if (!outfit || sep==line.npos) return SCO_BAD_ARG;
            const auto port=line.substr(0,sep),name=Trim(std::string_view(line).substr(sep));
            if (!Class(port) || !Class(name)) return SCO_BAD_ARG;
            if (outfit->pieces.size()>=32) return SCO_TOO_MANY;
            outfit->pieces.push_back({port,name,Slot(port,true)});
        } else if (Same(section,"sq42")) {
            // buildables uses a separate retained list format; first name before a pipe is its command key.
            const auto name=Trim(std::string_view(line).substr(0,line.find('|')));
            if (buildables.size()>=256) return SCO_TOO_MANY;
            if (!name.empty()) buildables.push_back(name);
        }
    }
    return SCO_OK;
}
sco_result ReadLists() {
    const auto* content=Open<sco_content_v1>(SCO_CONTENT_NAME);
    if (!HAS(content,sco_content_v1,item) || !HAS(content,sco_content_v1,read)) return SCO_UNAVAILABLE;
    Lists lists; std::vector<std::string> buildables; uint32_t total=0; bool found=false;
    for (uint32_t ordinal=0;ordinal<=SCO_CONTENT_MAX_ITEMS;++ordinal) {
        sco_content_item item{}; item.size=sizeof(item);
        const auto r=content->item(g.self,SCO_CONTENT_LIST,ordinal,&item);
        if (r==SCO_NOT_FOUND) {
            if (!found) return SCO_NOT_FOUND;
            g.lists=std::move(lists); g.buildables=std::move(buildables); g.lists_known=true;
            g.outfit=-1; g.build=-1; g.page=0; return SCO_OK;
        }
        if (r!=SCO_OK) return r;
        if (ordinal==SCO_CONTENT_MAX_ITEMS || !std::memchr(item.name,0,sizeof(item.name))) return SCO_FAILED;
        const int kind=Same(item.name,"lists/items.txt") ? 0 : Same(item.name,"lists/outfits.txt") ? 1 : Same(item.name,"lists/buildables.txt") ? 2 : -1;
        if (kind<0) continue;
        uint32_t needed=0; const auto asked=content->read(g.self,item.id,nullptr,&needed);
        if (asked!=SCO_TOO_MANY && !(asked==SCO_OK && needed==0)) return asked==SCO_OK ? SCO_FAILED : asked;
        if (needed>SCO_CONTENT_MAX_FILE_BYTES || total>SCO_CONTENT_MAX_FILE_BYTES-needed) return SCO_TOO_MANY;
        total+=needed;
        std::string bytes(needed,'\0'); uint32_t count=needed;
        if (needed) { const auto read=content->read(g.self,item.id,bytes.data(),&count); if (read!=SCO_OK) return read; }
        if (count!=needed) return SCO_FAILED;
        const auto parsed=Parse(bytes,kind,lists,buildables); if (parsed!=SCO_OK) return parsed;
        found=true;
    }
    return SCO_TOO_MANY;
}
sco_result Ready(char* out,uint32_t size) {
    if (g.lists_known) return SCO_OK;
    const auto r=ReadLists();
    if (r!=SCO_OK) Answer(r,out,size,"Loadout lists unavailable/refused; install enabled packs with lists/items.txt and lists/outfits.txt. No private-file fallback.");
    return r;
}
sco_result Validate(const sco_arg* args,uint32_t n,uint32_t expected,char* out,uint32_t size) {
    if (!g.api || !g.self) return SCO_UNAVAILABLE;
    if ((!out && size) || n!=expected || (expected && !args)) return SCO_BAD_ARG;
    return SCO_OK;
}
bool String(const sco_arg& arg,size_t max) { return arg.type==SCO_ARG_STRING && arg.v.s && std::strlen(arg.v.s)<=max; }
const char* Status(uint32_t s) {
    switch(s) { case SC_LOADOUT_APPLIED:return "applied"; case SC_LOADOUT_UNSUPPORTED:return "unsupported";
        case SC_LOADOUT_ITEM_MISSING:return "item-missing"; case SC_LOADOUT_FAILED:return "failed";
        case SC_LOADOUT_UNAVAILABLE:return "unavailable"; default:return "invalid"; }
}
sco_result Apply(const std::vector<Piece>& pieces,char* out,uint32_t size) {
    if (pieces.empty()) return Answer(SCO_BAD_ARG,out,size,"No selected pieces; unspecified slots are unchanged (not cleared/defaulted).");
    if (pieces.size()>SC_LOADOUT_MAX_ITEMS) return Answer(SCO_TOO_MANY,out,size,"More than 16 pieces: entire request refused before equip; no split/retry.");
    sc_loadout_item items[SC_LOADOUT_MAX_ITEMS]{};
    for (size_t i=0;i<pieces.size();++i) {
        const auto& p=pieces[i];
        if (!p.slot) {
            const auto message="Unsupported legacy slot/port '"+p.port+"': whole selection refused; clothing/head parts/weapons need approved G6 extension.";
            return Answer(SCO_UNAVAILABLE,out,size,message.c_str());
        }
        if (!Class(p.name)) return SCO_BAD_ARG;
        for (size_t j=0;j<i;++j) if (pieces[j].slot==p.slot) return Answer(SCO_BAD_ARG,out,size,"Duplicate logical slot: entire selection refused before equip.");
        items[i].slot=p.slot; std::memcpy(items[i].item_class,p.name.c_str(),p.name.size()+1);
    }
    const auto* loadout=Open<sc_loadout_v1>(SC_LOADOUT_SERVICE_NAME);
    if (!Cap("game.loadout.apply") || !HAS(loadout,sc_loadout_v1,apply)) return Answer(SCO_UNAVAILABLE,out,size,"Typed game.loadout.apply unavailable; no XML/private equip fallback.");
    sc_loadout_request request{sizeof(request),static_cast<uint32_t>(pieces.size()),sizeof(sc_loadout_item),0,items};
    sc_loadout_report report{}; report.size=sizeof(report);
    const auto result=loadout->apply(g.self,&request,&report);
    if (result!=SCO_OK && result!=SCO_FAILED && result!=SCO_UNAVAILABLE) return Answer(result,out,size,"Loadout provider rejected batch; report not interpreted, no retry.");
    if (report.size<sizeof(report) || report.count!=request.count || report.applied>report.count) return Answer(SCO_FAILED,out,size,"Invalid loadout report; state unknown, no retry.");
    uint32_t applied=0;
    for (uint32_t i=0;i<report.count;++i) {
        if (report.slots[i].slot!=items[i].slot || report.slots[i].status>SC_LOADOUT_UNAVAILABLE) return Answer(SCO_FAILED,out,size,"Invalid slot report; state unknown, no retry.");
        applied+=report.slots[i].status==SC_LOADOUT_APPLIED;
    }
    if (applied!=report.applied || (result==SCO_OK && applied!=report.count) || (result==SCO_UNAVAILABLE && applied)) return Answer(SCO_FAILED,out,size,"Inconsistent loadout report; state unknown, no retry.");
    std::string message="applied="+std::to_string(applied)+"/"+std::to_string(report.count)+"; no rollback/automatic retry";
    for (uint32_t i=0;i<report.count;++i) message+="; "+pieces[i].port+"="+Status(report.slots[i].status);
    return Answer(result,out,size,message.c_str());
}
sco_result Equip(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    auto r=Validate(args,n,1,out,size); if (r!=SCO_OK) return r;
    if (!String(args[0],1023) || !*args[0].v.s) return SCO_BAD_ARG;
    r=Ready(out,size); if (r!=SCO_OK) return r;
    std::string_view names=args[0].v.s; std::vector<Piece> pieces;
    while (!names.empty()) {
        const auto start=names.find_first_not_of(" ,\t"); if (start==names.npos) break;
        names.remove_prefix(start); const auto end=names.find_first_of(" ,\t"); const auto name=names.substr(0,end);
        names=end==names.npos ? std::string_view{} : names.substr(end);
        const Piece* match=nullptr;
        for (const auto& p:g.lists.items) if (Same(name,p.name)) {
            if (match && !Same(match->port,p.port)) return Answer(SCO_BAD_ARG,out,size,"Item appears in multiple slots; ambiguous selection refused.");
            if (!match) match=&p;
        }
        if (!match) return Answer(SCO_NOT_FOUND,out,size,("Item not in copied items.txt: "+std::string(name)).c_str());
        if (pieces.size()==SC_LOADOUT_MAX_ITEMS) return Answer(SCO_TOO_MANY,out,size,"Too many selected items; no equip requested.");
        pieces.push_back(*match);
    }
    return Apply(pieces,out,size);
}
sco_result Wear(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    auto r=Validate(args,n,1,out,size); if (r!=SCO_OK) return r;
    if (!String(args[0],127) || !*args[0].v.s) return SCO_BAD_ARG;
    if (!g.spoilers) return Answer(SCO_UNAVAILABLE,out,size,"SQ42 spoiler confirmation required: loadout.spoilers true (session-only).");
    r=Ready(out,size); if (r!=SCO_OK) return r;
    const Outfit* found=nullptr;
    for (const auto& outfit:g.lists.outfits) if (Same(outfit.name,args[0].v.s)) {
        if (found) return Answer(SCO_BAD_ARG,out,size,"Duplicate outfit name across packs; ambiguous selection refused.");
        found=&outfit;
    }
    if (!found) return Answer(SCO_NOT_FOUND,out,size,"Outfit not in copied outfits.txt.");
    return Apply(found->pieces,out,size);
}
sco_result Spoilers(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    const auto r=Validate(args,n,1,out,size); if (r!=SCO_OK) return r;
    if (args[0].type!=SCO_ARG_BOOL || (args[0].v.i!=0 && args[0].v.i!=1)) return SCO_BAD_ARG;
    g.spoilers=args[0].v.i!=0;
    for (auto& value:g.cvars) value=-1;
    return Answer(SCO_OK,out,size,g.spoilers ? "SQ42 spoilers confirmed for this loaded session." : "SQ42 spoiler confirmation cleared.");
}
sco_result Visor(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    const auto r=Validate(args,n,1,out,size); if (r!=SCO_OK) return r;
    if (!String(args[0],95) || !Class(args[0].v.s)) return SCO_BAD_ARG;
    if (!g.spoilers) return Answer(SCO_UNAVAILABLE,out,size,"SQ42 spoiler confirmation required.");
    return Apply({{"visor",args[0].v.s,SC_LOADOUT_SLOT_VISOR}},out,size);
}
void ReadCvars() {
    const auto* cv=Open<sc_cvars_v1>(SC_CVARS_NAME);
    for (uint32_t i=0;i<4;++i) {
        g.cvars[i]=-1; uint32_t value=2;
        if (Cap("game.cvars.read") && HAS(cv,sc_cvars_v1,get_bool) && cv->get_bool(g.self,cvar_names[i],SC_CVAR_BOOL,&value)==SCO_OK && value<=1)
            g.cvars[i]=static_cast<int>(value);
    }
}
sco_result Cvars(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    const auto r=Validate(args,n,0,out,size); if (r!=SCO_OK) return r;
    ReadCvars(); std::string message="SQ42 READ-ONLY (writes not approved)"; bool unknown=false;
    for (uint32_t i=0;i<4;++i) { unknown|=g.cvars[i]<0; message+="; "+std::string(cvar_names[i])+"="+(g.cvars[i]<0 ? "unknown/unavailable" : g.cvars[i] ? "on" : "off"); }
    return Answer(unknown ? SCO_UNAVAILABLE : SCO_OK,out,size,message.c_str());
}
sco_result Refuse(const sco_arg*,uint32_t,void*,char* out,uint32_t size) {
    if ((!out && size) || !g.api || !g.self) return SCO_BAD_ARG;
    return Answer(SCO_UNAVAILABLE,out,size,"SQ42 cvar writes and arbitrary console execution are NOT approved; read-only, no private fallback/USER.cfg changes.");
}
sco_result Reload(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    const auto r=Validate(args,n,0,out,size); if (r!=SCO_OK) return r;
    const auto read=ReadLists();
    // Explicit failure invalidates old picker state: never pretend an old snapshot is a current read.
    if (read!=SCO_OK) { g.lists={}; g.buildables.clear(); g.lists_known=false; g.outfit=-1; g.build=-1; }
    const auto message=read==SCO_OK ? "Copied lists loaded: items="+std::to_string(g.lists.items.size())+", outfits="+std::to_string(g.lists.outfits.size()) : "Copied lists unavailable/refused; no private-file fallback.";
    return Answer(read,out,size,message.c_str());
}
sco_result Items(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    auto r=Validate(args,n,1,out,size); if (r!=SCO_OK) return r;
    if (!String(args[0],63)) return SCO_BAD_ARG;
    r=Ready(out,size); if (r!=SCO_OK) return r;
    std::string message="Copied gear list (first 12 matches; use UI filter/pages)"; uint32_t count=0;
    for (const auto& p:g.lists.items) if (p.name.find(args[0].v.s)!=p.name.npos) {
        if (count++==12) break;
        message+="; "+p.port+":"+p.name+(p.slot ? "" : " [unsupported]");
    }
    return Answer(SCO_OK,out,size,message.c_str());
}
void ForwardReply(sco_result result,const char* reply,void*) {
    if (!g.api) return;
    Answer(result,nullptr,0,reply && *reply ? reply : "External command refused/unavailable; no fallback.");
}
sco_result Forward(const char* name,const sco_arg* args,uint32_t n,char* out,uint32_t size) {
    if (!HAS(g.api,sco_api,invoke)) return Answer(SCO_UNAVAILABLE,out,size,"External command invocation unavailable.");
    const auto r=g.api->invoke(g.self,name,args,n,ForwardReply,nullptr);
    // invoke is queued; successful submission is not gameplay success.
    return Answer(r,out,size,r==SCO_OK ? "External command queued; completion is reported separately, no host fallback." : "External command absent/refused; no host fallback.");
}
sco_result Spawn(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    const auto r=Validate(args,n,4,out,size); if (r!=SCO_OK) return r;
    if (!g.spoilers) return Answer(SCO_UNAVAILABLE,out,size,"SQ42 spoiler confirmation required.");
    if (!String(args[0],63) || !Class(args[0].v.s) || args[1].type!=SCO_ARG_FLOAT || !std::isfinite(args[1].v.f) || args[1].v.f<0 || args[1].v.f>10000 ||
        !String(args[2],63) || args[3].type!=SCO_ARG_BOOL || (args[3].v.i!=0 && args[3].v.i!=1)) return SCO_BAD_ARG;
    return Forward("spawn.ship_options",args,n,out,size);
}
sco_result Build(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    const auto r=Validate(args,n,2,out,size); if (r!=SCO_OK) return r;
    if (!g.spoilers) return Answer(SCO_UNAVAILABLE,out,size,"SQ42 spoiler confirmation required.");
    if (!String(args[0],127) || !*args[0].v.s || args[1].type!=SCO_ARG_FLOAT || !std::isfinite(args[1].v.f) || args[1].v.f<0 || args[1].v.f>100) return SCO_BAD_ARG;
    return Forward("build.place",args,n,out,size);
}
void Publish();
sco_arg Text(const char* text) { sco_arg a{}; a.type=SCO_ARG_STRING; a.v.s=text; return a; }
sco_arg Number(double value) { sco_arg a{}; a.type=SCO_ARG_FLOAT; a.v.f=value; return a; }
sco_arg Boolean(bool value) { sco_arg a{}; a.type=SCO_ARG_BOOL; a.v.i=value ? 1 : 0; return a; }
std::vector<size_t> Filtered() {
    std::vector<size_t> matches;
    for (size_t i=0;i<g.lists.items.size();++i) {
        const auto& p=g.lists.items[i];
        if (g.filter.empty() || p.name.find(g.filter)!=p.name.npos || p.port.find(g.filter)!=p.port.npos) matches.push_back(i);
    }
    return matches;
}
void Event(const char* panel,const sco_widget_event* e,void*) {
    if (!g.api || !panel || !e || e->size<sizeof(*e)) return;
    if (std::strcmp(panel,"loadout.player") && std::strcmp(panel,"loadout.sq42")) return;
    const bool sq=!std::strcmp(panel,"loadout.sq42");
    if (sq && !g.spoilers && e->node_id!=100) return;
    auto string=[&](std::string& target,size_t limit) {
        if (e->kind==SCO_WIDGET_TEXT_INPUT && e->text && std::strlen(e->text)<=limit) target=e->text;
    };
    if (!sq) switch(e->node_id) {
        case 1: string(g.filter,63); g.page=0; break;
        case 2: if (e->kind==SCO_WIDGET_COMBO && e->index>=0 && e->index<64) {
            const auto matches=Filtered(); const auto index=static_cast<size_t>(g.page)*64+static_cast<size_t>(e->index);
            if (index<matches.size()) {
                const auto& p=g.lists.items[matches[index]];
                if (g.equip.size()+p.name.size()+1<=1023) { if (!g.equip.empty()) g.equip+=','; g.equip+=p.name; }
            }
        } break;
        case 3: if (e->kind==SCO_WIDGET_BUTTON && g.page) --g.page; break;
        case 4: if (e->kind==SCO_WIDGET_BUTTON && (g.page+1)*64<Filtered().size()) ++g.page; break;
        case 5: string(g.equip,1023); break;
        case 6: if (e->kind==SCO_WIDGET_BUTTON) { auto a=Text(g.equip.c_str()); Equip(&a,1,nullptr,nullptr,0); } break;
        case 7: if (e->kind==SCO_WIDGET_BUTTON) { g.equip.clear(); } break;
        case 8: if (e->kind==SCO_WIDGET_BUTTON) Reload(nullptr,0,nullptr,nullptr,0); break;
        case 10: case 11: case 12: if (e->kind==SCO_WIDGET_TOGGLE && (e->value==0 || e->value==1)) {
            auto a=Boolean(e->value!=0); Forward(e->node_id==10 ? "creative.noclip" : e->node_id==11 ? "creative.god" : "creative.ammo",&a,1,nullptr,0);
        } break;
        case 13: if (e->kind==SCO_WIDGET_SLIDER && std::isfinite(e->value) && e->value>=1 && e->value<=500) {
            g.speed=e->value; auto a=Number(g.speed); Forward("creative.noclip_speed",&a,1,nullptr,0);
        } break;
        default: break;
    }
    else switch(e->node_id) {
        case 100: if (e->kind==SCO_WIDGET_BUTTON) { g.spoilers=true; ReadCvars(); } break;
        case 101: if (e->kind==SCO_WIDGET_COMBO && e->index>=0 && static_cast<size_t>(e->index)<g.lists.outfits.size()) g.outfit=e->index; break;
        case 102: if (e->kind==SCO_WIDGET_BUTTON && g.outfit>=0 && static_cast<size_t>(g.outfit)<g.lists.outfits.size()) {
            auto a=Text(g.lists.outfits[static_cast<size_t>(g.outfit)].name.c_str()); Wear(&a,1,nullptr,nullptr,0);
        } break;
        case 103: string(g.visor,95); break;
        case 104: if (e->kind==SCO_WIDGET_BUTTON) { auto a=Text(g.visor.c_str()); Visor(&a,1,nullptr,nullptr,0); } break;
        case 105: if (e->kind==SCO_WIDGET_BUTTON) Cvars(nullptr,0,nullptr,nullptr,0); break;
        case 110: case 111: case 112: case 113: Refuse(nullptr,0,nullptr,nullptr,0); break;
        case 120: if (e->kind==SCO_WIDGET_COMBO && e->index>=0 && static_cast<size_t>(e->index)<std::size(ships)) g.ship=e->index; break;
        case 121: if (e->kind==SCO_WIDGET_SLIDER && std::isfinite(e->value) && e->value>=0 && e->value<=500) g.height=e->value; break;
        case 122: if (e->kind==SCO_WIDGET_TOGGLE && (e->value==0 || e->value==1)) g.sit=e->value!=0; break;
        case 123: if (e->kind==SCO_WIDGET_BUTTON) {
            const auto& ship=ships[g.ship]; const bool sit=ship.board && g.sit;
            sco_arg a[]{Text(ship.cls),Number(ship.board ? g.height : ship.height),Text(sit ? "pilot" : "none"),Boolean(sit)};
            Spawn(a,4,nullptr,nullptr,0);
        } break;
        case 130: if (e->kind==SCO_WIDGET_COMBO && e->index>=0 && static_cast<size_t>(e->index)<g.buildables.size()) g.build=e->index; break;
        case 131: if (e->kind==SCO_WIDGET_SLIDER && std::isfinite(e->value) && e->value>=0 && e->value<=100) g.ahead=e->value; break;
        case 132: if (e->kind==SCO_WIDGET_BUTTON && g.build>=0 && static_cast<size_t>(g.build)<g.buildables.size()) {
            sco_arg a[]{Text(g.buildables[static_cast<size_t>(g.build)].c_str()),Number(g.ahead)}; Build(a,2,nullptr,nullptr,0);
        } break;
        default: break;
    }
    Publish();
}
void Publish() {
    const auto* w=Open<sco_widgets_v1>(SCO_WIDGETS_NAME);
    if (!HAS(w,sco_widgets_v1,set_panel)) return; // command-first when optional renderer-neutral UI is absent
    std::vector<sco_widget_node> nodes; std::vector<std::string> text;
    text.reserve(90); // node string pointers survive construction, then set_panel copies them
    auto add=[&](uint32_t kind,uint32_t id,const char* label,uint32_t flags=0) -> sco_widget_node& {
        sco_widget_node n{}; n.size=sizeof(n); n.kind=kind; n.id=id; n.label=label; n.flags=flags; nodes.push_back(n); return nodes.back();
    };
    const auto matches=Filtered(); if (g.page*64>=matches.size()) g.page=0;
    std::vector<const char*> options;
    for (size_t i=static_cast<size_t>(g.page)*64;i<matches.size() && options.size()<64;++i) {
        const auto& p=g.lists.items[matches[i]]; text.push_back(p.port+": "+p.name+(p.slot ? "" : " [unsupported]")); options.push_back(text.back().c_str());
    }
    add(SCO_WIDGET_TEXT_INPUT,1,"Search gear (case-sensitive)").text=g.filter.c_str();
    auto& picker=add(SCO_WIDGET_COMBO,2,"Gear list (64 per page)"); picker.items=options.data(); picker.item_count=static_cast<uint32_t>(options.size()); picker.index=-1;
    add(SCO_WIDGET_BUTTON,3,"Previous page",g.page ? 0u : SCO_WIDGET_DISABLED);
    add(SCO_WIDGET_BUTTON,4,"Next page",(g.page+1)*64<matches.size() ? 0u : SCO_WIDGET_DISABLED);
    // widgets text fields are bounded to 255 bytes; commands support the full 1023-byte multi-slot request.
    add(SCO_WIDGET_TEXT_INPUT,5,"Selected classes (commas/spaces)").text=g.equip.size()<=255 ? g.equip.c_str() : "Selection exceeds UI field; use loadout.equip command or clear";
    add(SCO_WIDGET_BUTTON,6,"Equip selected gear"); add(SCO_WIDGET_BUTTON,7,"Clear selection"); add(SCO_WIDGET_BUTTON,8,"Reload copied lists");
    add(SCO_WIDGET_LABEL,9,"Unspecified slots stay unchanged. Unsupported legacy slots refuse the whole selection. No defaults/XML fallback.");
    const auto* creative=Open<sc_creative_v1>(SC_CREATIVE_SERVICE_NAME);
    uint32_t creative_flags=0;
    const bool creative_known=HAS(creative,sc_creative_v1,state) && creative->state(g.self,&creative_flags)==SCO_OK;
    add(SCO_WIDGET_TOGGLE,10,creative_known ? "Noclip (provider requested state)" : "Noclip [unknown]",creative_known && Cap("game.creative.fly") ? 0u : SCO_WIDGET_DISABLED).value=(creative_flags & SC_CREATIVE_FLY) ? 1 : 0;
    add(SCO_WIDGET_TOGGLE,11,creative_known ? "God mode (provider requested state)" : "God mode [unknown]",creative_known && Cap("game.creative.god_mode") ? 0u : SCO_WIDGET_DISABLED).value=(creative_flags & SC_CREATIVE_GOD_MODE) ? 1 : 0;
    add(SCO_WIDGET_TOGGLE,12,creative_known ? "Infinite ammo (provider requested state)" : "Infinite ammo [unknown]",creative_known && Cap("game.creative.ammo") ? 0u : SCO_WIDGET_DISABLED).value=(creative_flags & SC_CREATIVE_INFINITE_AMMO) ? 1 : 0;
    auto& speed=add(SCO_WIDGET_SLIDER,13,"Noclip speed"); speed.value=g.speed; speed.minimum=1; speed.maximum=500;
    char status[256]{}; std::snprintf(status,sizeof(status),"%.255s",g.status); add(SCO_WIDGET_LABEL,14,status);
    (void)w->set_panel(g.self,"loadout.player","Player",nodes.data(),static_cast<uint32_t>(nodes.size()),sizeof(sco_widget_node),Event,nullptr);
    nodes.clear(); options.clear(); text.clear();
    if (!g.spoilers) { add(SCO_WIDGET_LABEL,99,"This tab may contain Squadron 42 spoilers. Confirmation is session-only."); add(SCO_WIDGET_BUTTON,100,"OK: show SQ42 spoilers"); }
    else {
        for (const auto& o:g.lists.outfits) options.push_back(o.name.c_str());
        auto& outfit=add(SCO_WIDGET_COMBO,101,"SQ42 outfit"); outfit.items=options.data(); outfit.item_count=static_cast<uint32_t>(options.size()); outfit.index=g.outfit;
        add(SCO_WIDGET_BUTTON,102,"Wear SQ42 outfit",g.outfit>=0 ? 0u : SCO_WIDGET_DISABLED);
        add(SCO_WIDGET_TEXT_INPUT,103,"Visor item class (no implicit head replacement)").text=g.visor.c_str();
        add(SCO_WIDGET_BUTTON,104,"Equip visor"); add(SCO_WIDGET_BUTTON,105,"Refresh real SQ42 values");
        for (uint32_t i=0;i<4;++i) {
            text.push_back(std::string(cvar_labels[i])+(g.cvars[i]<0 ? " [unknown/unavailable]" : " [read-only]"));
            auto& setting=add(SCO_WIDGET_TOGGLE,110+i,text.back().c_str(),SCO_WIDGET_DISABLED); setting.value=g.cvars[i]>0 ? 1 : 0;
        }
        add(SCO_WIDGET_LABEL,114,"Cvar writes and arbitrary console execution are not approved. Unknown is not off; no USER.cfg changes.");
        const char* ship_labels[std::size(ships)]{}; for (size_t i=0;i<std::size(ships);++i) ship_labels[i]=ships[i].label;
        auto& ship=add(SCO_WIDGET_COMBO,120,"SQ42 ships"); ship.items=ship_labels; ship.item_count=static_cast<uint32_t>(std::size(ships)); ship.index=g.ship;
        auto& height=add(SCO_WIDGET_SLIDER,121,"Height above me (m)"); height.value=g.height; height.minimum=0; height.maximum=500;
        add(SCO_WIDGET_TOGGLE,122,"Pilot seat and Flight Ready").value=g.sit ? 1 : 0;
        add(SCO_WIDGET_BUTTON,123,"Spawn via external spawn.ship_options");
        std::vector<const char*> builds; for (const auto& name:g.buildables) builds.push_back(name.c_str());
        auto& build=add(SCO_WIDGET_COMBO,130,"SQ42 buildables"); build.items=builds.data(); build.item_count=static_cast<uint32_t>(builds.size()); build.index=g.build;
        auto& ahead=add(SCO_WIDGET_SLIDER,131,"Metres ahead (0 = feet)"); ahead.value=g.ahead; ahead.minimum=0; ahead.maximum=100;
        add(SCO_WIDGET_BUTTON,132,"Place via build.place",g.build>=0 ? 0u : SCO_WIDGET_DISABLED);
        add(SCO_WIDGET_LABEL,133,"Enemy-wing orchestration remains deferred. Missing spawn/build commands refuse; no private fallback.");
        add(SCO_WIDGET_LABEL,134,status);
        (void)w->set_panel(g.self,"loadout.sq42","Squadron 42",nodes.data(),static_cast<uint32_t>(nodes.size()),sizeof(sco_widget_node),Event,nullptr);
        return;
    }
    (void)w->set_panel(g.self,"loadout.sq42","Squadron 42",nodes.data(),static_cast<uint32_t>(nodes.size()),sizeof(sco_widget_node),Event,nullptr);
}
void Tick(const char*,const void* data,void*) {
    if (!g.api || !data) return;
    uint32_t now=0; std::memcpy(&now,data,sizeof(now));
    if (g.clocked && static_cast<uint32_t>(now-g.last_read)<200) return;
    g.clocked=true; g.last_read=now;
    if (g.spoilers) ReadCvars();
    Publish();
}
sco_result Command(const char* name,sco_command_fn fn,const sco_arg_def* args,uint32_t count) {
    sco_command c{}; c.size=sizeof(c); c.name=name; c.title=name; c.help=name;
    c.args=args; c.nargs=count; c.arg_def_size=sizeof(sco_arg_def); c.fn=fn;
    // No capability gate: commands provide precise missing-service/refusal replies themselves.
    return g.api->register_command(g.self,&c);
}
sco_arg_def Arg(const char* name,sco_arg_type type) { sco_arg_def a{}; a.name=name; a.type=type; return a; }
}
const sco_plugin_info* Query() { return &info; }
sco_result Load(const sco_api* api,sco_plugin* self) {
    if (g.api || !self || !HAS(api,sco_api,register_command) || !HAS(api,sco_api,query_service) || !HAS(api,sco_api,subscribe) ||
        !HAS(api,sco_api,unsubscribe) || !HAS(api,sco_api,has)) return SCO_BAD_ARG;
    g=State{}; g.api=api; g.self=self;
    const auto items=Arg("items",SCO_ARG_STRING),outfit=Arg("outfit",SCO_ARG_STRING),spoilers=Arg("confirmed",SCO_ARG_BOOL),visor=Arg("class",SCO_ARG_STRING),filter=Arg("filter",SCO_ARG_STRING);
    const sco_arg_def spawn[]{Arg("class",SCO_ARG_STRING),Arg("height",SCO_ARG_FLOAT),Arg("seat",SCO_ARG_STRING),Arg("power",SCO_ARG_BOOL)};
    const sco_arg_def build[]{Arg("object",SCO_ARG_STRING),Arg("ahead",SCO_ARG_FLOAT)};
    const sco_arg_def write[]{Arg("name",SCO_ARG_STRING),Arg("on",SCO_ARG_BOOL)};
    struct Registration { const char* name; sco_command_fn fn; const sco_arg_def* args; uint32_t count; };
    const Registration commands[]{ {"loadout.equip",Equip,&items,1},{"loadout.wear",Wear,&outfit,1},{"loadout.spoilers",Spoilers,&spoilers,1},
        {"loadout.visor",Visor,&visor,1},{"loadout.items",Items,&filter,1},{"loadout.reload_lists",Reload,nullptr,0},
        {"loadout.cvars",Cvars,nullptr,0},{"loadout.cvar_set",Refuse,write,2},{"loadout.console",Refuse,&items,1},
        {"loadout.spawn",Spawn,spawn,4},{"loadout.build",Build,build,2} };
    for (const auto& c:commands) { const auto r=Command(c.name,c.fn,c.args,c.count); if (r!=SCO_OK) { g=State{}; return r; } }
    const auto r=api->subscribe(self,"tick",Tick,nullptr); if (r!=SCO_OK) { g=State{}; return r; }
    const auto lists=ReadLists();
    Answer(lists==SCO_OK ? SCO_OK : SCO_UNAVAILABLE,nullptr,0,lists==SCO_OK ? "Loadout copied lists ready; typed equip availability is independent." : "Loadout copied lists unavailable; no private fallback. Use loadout.reload_lists after installing packs and restarting.");
    Publish(); return SCO_OK;
}
void Unload() {
    if (!g.api) return;
    const auto* w=Open<sco_widgets_v1>(SCO_WIDGETS_NAME);
    if (HAS(w,sco_widgets_v1,remove_panel)) { (void)w->remove_panel(g.self,"loadout.player"); (void)w->remove_panel(g.self,"loadout.sq42"); }
    if (HAS(g.api,sco_api,unsubscribe)) (void)g.api->unsubscribe(g.self,"tick",Tick);
    // Equip is player state, not entity ownership; ordinary unload does not undo equipped gear.
    g=State{};
}
}
