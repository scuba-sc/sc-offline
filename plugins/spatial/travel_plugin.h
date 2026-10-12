// SPDX-License-Identifier: GPL-3.0-only
// Shared SDK-only consumer implementation. Each native owner has independent state/storage.
#pragma once
#include "travel_model.h"
#include "scosdk/storage.hpp"
#include "sco_settings.h"
#include "sco_ui.h"
#include "sco_widgets.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif
namespace sco::features::travel {
namespace implementation {
struct Bookmark { std::string system,name; Spot spot; };
struct Destination { std::string system,name,entity; double radius=0; };
struct State {
    const sco_api* api=nullptr; sco_plugin* self=nullptr;
    sco::sdk::Storage store; std::filesystem::path root="data",spawn;
    std::vector<Bookmark> marks; std::vector<Destination> places;
    Spot saved; bool hotkeys=false,panel=false;
    uint32_t place_page=0,mark_page=0; int32_t selected_place=-1,selected_mark=-1;
    std::string bookmark_name,filter,last_status="G2 REFUSE: movement unavailable"; double altitude=1000;
} g;
#ifdef TRAVEL_TELEPORT
constexpr const char* id="teleport";
#else
constexpr const char* id="quantum";
#endif
const sco_plugin_info info{sizeof(sco_plugin_info),SCO_API_MAJOR,SCO_API_MINOR,id,"1.0.0","sc-offline"};
template<class T> const T* Open(const char* name) {
    const void* out=nullptr;
    if (!TRAVEL_FN(g.api,sco_api,query_service) || g.api->query_service(name,0x10000u,&out)!=SCO_OK) return nullptr;
    return static_cast<const T*>(out);
}
sco_result Reply(sco_result r,char* out,uint32_t n,const std::string& text) {
    if (out && n) std::snprintf(out,n,"%s",text.c_str());
    if (TRAVEL_FN(g.api,sco_api,log)) g.api->log(g.self,r==SCO_OK?SCO_LOG_INFO:SCO_LOG_WARN,text.c_str());
    return r;
}
std::string Trim(std::string text) {
    const auto a=text.find_first_not_of(" \t\r\n"), b=text.find_last_not_of(" \t\r\n");
    return a==std::string::npos ? std::string{} : text.substr(a,b-a+1);
}
bool SafeName(const char* name) { return name && std::strlen(name)<=63 && !std::strpbrk(name,"|\r\n"); }
bool Same(const std::string& a,const std::string& b) {
    if (a.size()!=b.size()) return false;
    for (size_t i=0;i<a.size();++i) if (std::tolower(static_cast<unsigned char>(a[i]))!=std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}
bool ReadFile(const std::filesystem::path& path,std::string& text) {
    std::error_code ec; const auto size=std::filesystem::file_size(path,ec);
    if (ec || size>1024*1024 || std::filesystem::is_symlink(path,ec)) return false;
    std::ifstream f(path,std::ios::binary); if (!f) return false;
    text.assign(std::istreambuf_iterator<char>(f),{}); return !f.bad() && text.size()<=1024*1024;
}
std::string FileTime(const std::filesystem::path& path) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&a)) return {};
    return std::to_string((static_cast<uint64_t>(a.ftLastWriteTime.dwHighDateTime)<<32)|a.ftLastWriteTime.dwLowDateTime);
#else
    // Development fixture timestamp only. Windows preserves existing FILETIME marker encoding.
    std::error_code ec; auto time=std::filesystem::last_write_time(path,ec);
    return ec ? std::string{} : std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count());
#endif
}
sco_result WriteFile(const std::filesystem::path& path,const std::string& text) {
    std::error_code ec;
    if (std::filesystem::is_symlink(path,ec)) return SCO_BAD_ARG;
    const auto tmp=std::filesystem::path(path.string()+".new");
    ec.clear(); if (std::filesystem::exists(tmp,ec)) return SCO_FAILED;
    { std::ofstream f(tmp,std::ios::binary); if (!f || !(f<<text) || !f.flush()) return SCO_FAILED; }
#ifdef _WIN32
    if (!MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) { std::filesystem::remove(tmp,ec); return SCO_FAILED; }
#else
    std::filesystem::rename(tmp,path,ec); if (ec) { std::filesystem::remove(tmp,ec); return SCO_FAILED; }
#endif
    return SCO_OK;
}
std::string Setting(const char* name,const std::string& fallback) {
    const auto* st=Open<sco_settings_v1>(SCO_SETTINGS_NAME); char text[256]{}; uint32_t n=sizeof(text);
    if (!TRAVEL_FN(st,sco_settings_v1,get_string) || st->get_string(g.self,name,text,&n)!=SCO_OK || !n || n>sizeof(text) || !std::memchr(text,0,sizeof(text)) || !text[0]) return fallback;
    return text;
}
void Storage() {
    const auto* s=Open<sco_storage_v1>(SCO_STORAGE_NAME);
    if (!TRAVEL_FN(s,sco_storage_v1,put) || !TRAVEL_FN(s,sco_storage_v1,get) ||
        !TRAVEL_FN(s,sco_storage_v1,begin) || !TRAVEL_FN(s,sco_storage_v1,commit) || !TRAVEL_FN(s,sco_storage_v1,rollback) ||
        !TRAVEL_FN(s,sco_storage_v1,exec) || !TRAVEL_FN(s,sco_storage_v1,query) || !TRAVEL_FN(s,sco_storage_v1,step) ||
        !TRAVEL_FN(s,sco_storage_v1,column) || !TRAVEL_FN(s,sco_storage_v1,close)) return;
    // Guard the SDK wrapper's legacy size check with a complete root table/member extent above.
    g.store.Open(g.api,g.self);
    if (g.store.Table()!=s) g.store={}; // provider reopen must not substitute an unchecked table
}
constexpr const char* table="CREATE TABLE IF NOT EXISTS bookmarks(pos INTEGER PRIMARY KEY, system TEXT NOT NULL, name TEXT NOT NULL, spots TEXT NOT NULL)";
sco_result StoreMarks(const std::vector<Bookmark>& marks,const std::string& time) {
    using sco::sdk::SqlText; using sco::sdk::SqlInt;
    sco::sdk::StorageTransaction tx(g.store); auto r=tx.Result();
    if (r==SCO_OK) r=g.store.Exec(table);
    if (r==SCO_OK) r=g.store.Exec("DELETE FROM bookmarks");
    for (size_t i=0;r==SCO_OK && i<marks.size();++i) {
        const auto spots=SpotText(marks[i].spot);
        r=g.store.Exec("INSERT INTO bookmarks(pos, system, name, spots) VALUES (?, ?, ?, ?)",{SqlInt(static_cast<int64_t>(i)),SqlText(marks[i].system),SqlText(marks[i].name),SqlText(spots)});
    }
    if (r==SCO_OK && !time.empty()) r=g.store.Put("bookmarks.txt.time",std::string_view(time));
    return r==SCO_OK ? tx.Commit() : r;
}
std::string MarksText(const std::vector<Bookmark>& marks) {
    std::string text="# Saved spots from the Travel tab.\n";
    for (const auto& m:marks) text+="bookmark "+m.system+"|"+m.name+"\n"+SpotText(m.spot);
    return text;
}
std::vector<Bookmark> ParseMarks(const std::string& text) {
    std::vector<Bookmark> marks; std::istringstream in(text); std::string line,spots; Bookmark m; bool have=false;
    auto flush=[&] { if (have && marks.size()<400 && m.name.size()<=63 && m.system.size()<=31 && ParseSpot(spots,m.spot)) marks.push_back(m); spots.clear(); };
    while (std::getline(in,line)) {
        if (line.starts_with("bookmark ")) {
            flush(); if (marks.size()==400) break;
            m={}; have=true; const auto bar=line.find('|',9);
            if (bar!=std::string::npos) { m.system=line.substr(9,bar-9); m.name=line.substr(bar+1); }
            else m.name=line.substr(9);
        } else if (have) spots+=line+'\n';
    }
    flush(); return marks;
}
sco_result LoadMarks() {
    const auto path=g.root/"bookmarks.txt"; std::string text,time=FileTime(path),seen;
    const bool file=ReadFile(path,text);
    if (!g.store) { if (file) g.marks=ParseMarks(text); return SCO_OK; }
    const auto marker=g.store.Get("bookmarks.txt.time",seen);
    if (file && !time.empty() && (marker!=SCO_OK || seen!=time)) {
        const auto marks=ParseMarks(text); const auto r=StoreMarks(marks,time);
        if (r==SCO_OK) g.marks=marks;
        return r; // failed import never substitutes/writes the old DB or marker
    }
    auto r=g.store.Exec(table); if (r!=SCO_OK) return r;
    auto cursor=g.store.Query("SELECT system, name, spots FROM bookmarks ORDER BY pos"); if (!cursor) return cursor.Result();
    std::vector<Bookmark> next;
    while ((r=cursor.Next())==SCO_OK && next.size()<400) {
        Bookmark m; std::string spots;
        if (cursor.Text(0,m.system)!=SCO_OK || cursor.Text(1,m.name)!=SCO_OK || cursor.Text(2,spots)!=SCO_OK) return SCO_FAILED;
        if (m.system.size()<=31 && m.name.size()<=63 && ParseSpot(spots,m.spot)) next.push_back(m);
    }
    if (r!=SCO_NOT_FOUND && r!=SCO_OK) return r;
    g.marks=std::move(next); return SCO_OK;
}
sco_result SaveMarks(const std::vector<Bookmark>& next) {
    const auto r=g.store ? StoreMarks(next,{}) : WriteFile(g.root/"bookmarks.txt",MarksText(next));
    if (r==SCO_OK) g.marks=next;
    return r;
}
void LoadPlaces(const std::filesystem::path& path) {
    std::string text; if (!ReadFile(path,text)) return;
    std::istringstream in(text); std::string line;
    while (g.places.size()<3000 && std::getline(in,line)) {
        if (Trim(line).empty() || Trim(line).starts_with('#')) continue;
        Destination d; std::istringstream f(line); std::string radius;
        if (!std::getline(f,d.system,'|') || !std::getline(f,d.name,'|') || !std::getline(f,d.entity,'|')) continue;
        d.system=Trim(d.system); d.name=Trim(d.name); d.entity=Trim(d.entity); std::getline(f,radius);
        if (d.system.size()>31 || d.name.size()>63 || d.entity.empty() || d.entity.size()>95) continue;
        try { d.radius=radius.empty()?0:std::stod(radius); } catch (...) { continue; }
        if (!std::isfinite(d.radius) || d.radius<0) continue;
        if (std::none_of(g.places.begin(),g.places.end(),[&](const auto& p){return Same(p.entity,d.entity);})) g.places.push_back(d);
    }
}
sco_result LoadSaved() {
    std::string text,seen; const auto time=FileTime(g.spawn); const bool file=ReadFile(g.spawn,text); Spot old;
    const bool parsed=file && ParseSpot(text,old);
    if (!g.store) { if (parsed) g.saved=old; return SCO_OK; }
    const auto marker=g.store.Get("spawn.txt.time",seen);
    if (parsed && !time.empty() && (marker!=SCO_OK || seen!=time)) {
        sco::sdk::StorageTransaction tx(g.store); auto r=tx.Result();
        if (r==SCO_OK) r=g.store.Put("spot",std::string_view(SpotText(old)));
        if (r==SCO_OK) r=g.store.Put("spawn.txt.time",std::string_view(time));
        if (r==SCO_OK) r=tx.Commit(); if (r==SCO_OK) g.saved=old;
        return r;
    }
    const auto r=g.store.Get("spot",text);
    if (r==SCO_OK && !ParseSpot(text,g.saved)) return SCO_BAD_ARG;
    return r==SCO_NOT_FOUND ? SCO_OK : r;
}
sco_result Refuse(const sco_arg*,uint32_t,void*,char* out,uint32_t n) { return Reply(Relocate(),out,n,refusal); }
sco_result Status(const sco_arg*,uint32_t,void*,char* out,uint32_t n) {
    return Reply(SCO_OK,out,n,std::string(id)+": "+std::to_string(g.marks.size())+" bookmarks, "+std::to_string(g.places.size())+" destinations; G2 REFUSE; system identity unknown unless explicitly provided; scan is observed ancestry only");
}
sco_result ScanCommand(const sco_arg*,uint32_t,void*,char* out,uint32_t n) {
    std::vector<sc_game_zone> records; const auto r=Scan(Open<sc_game_spatial_v1>(SC_GAME_SPATIAL_NAME),records);
    if (r!=SCO_OK) return Reply(r,out,n,"Spatial scan unavailable or metadata expired; prior destination list unchanged");
    auto next=g.places; std::string appended;
    for (const auto& z:records) {
        if (next.size()==3000) break;
        if (std::any_of(next.begin(),next.end(),[&](const auto& p){return Same(p.entity,z.name);})) continue;
        // Names are display-only: never infer certified system identity from OOC/root names.
        Destination d{"Unknown system",z.name,z.name,0};
        if (d.name.size()>63) d.name.resize(63);
        if (d.entity.find_first_of("|\r\n")!=std::string::npos) continue;
        next.push_back(d); appended+=d.system+" | "+d.name+" | "+d.entity+" | 0\n";
    }
    if (!appended.empty()) {
        std::string text; ReadFile(g.root/"locations_found.txt",text);
        const auto wrote=WriteFile(g.root/"locations_found.txt",text+appended);
        if (wrote!=SCO_OK) return Reply(wrote,out,n,"Observed scan could not persist; destination list unchanged");
    }
    g.places=std::move(next);
    return Reply(SCO_OK,out,n,"Copied "+std::to_string(records.size())+" observed zones (max 256; 64/page); not a full system scan; system identity unknown");
}
sco_result SaveBookmark(const sco_arg* a,uint32_t count,void*,char* out,uint32_t n) {
    if (count!=1 || !a || a[0].type!=SCO_ARG_STRING || !SafeName(a[0].v.s)) return Reply(SCO_BAD_ARG,out,n,"Bookmark name must be <=63 bytes, without separators/newlines");
    if (g.marks.size()==400) return Reply(SCO_TOO_MANY,out,n,"400 bookmark limit; delete one first");
    Spot spot; const auto r=Capture(Open<sc_spatial_v1>(SC_SPATIAL_SERVICE_NAME),Open<sc_game_spatial_v1>(SC_GAME_SPATIAL_NAME),spot);
    if (r!=SCO_OK) return Reply(r,out,n,"Cannot capture current full pose/complete ancestry; nothing saved");
    Bookmark m{"Unknown system",a[0].v.s,spot}; if (m.name.empty()) m.name=spot.z[0].name.substr(0,63);
    auto next=g.marks; next.push_back(m); const auto saved=SaveMarks(next);
    return Reply(saved,out,n,saved==SCO_OK?"Saved '"+m.name+"' (read-only capture; no relocation)":"Bookmark storage failed; prior state retained");
}
sco_result DeleteBookmark(const sco_arg* a,uint32_t count,void*,char* out,uint32_t n) {
    if (count!=1 || !a || a[0].type!=SCO_ARG_STRING || !SafeName(a[0].v.s)) return SCO_BAD_ARG;
    auto next=g.marks; const auto it=std::find_if(next.begin(),next.end(),[&](const auto& m){return Same(m.name,a[0].v.s);});
    if (it==next.end()) return Reply(SCO_NOT_FOUND,out,n,"No such bookmark");
    next.erase(it); const auto r=SaveMarks(next); return Reply(r,out,n,r==SCO_OK?"Bookmark deleted":"Delete failed; prior state retained");
}
sco_result InspectBookmark(const sco_arg* a,uint32_t count,void*,char* out,uint32_t n) {
    const Spot* spot=&g.saved;
#ifndef TRAVEL_TELEPORT
    if (count!=1 || !a || a[0].type!=SCO_ARG_STRING || !SafeName(a[0].v.s)) return SCO_BAD_ARG;
    const auto it=std::find_if(g.marks.begin(),g.marks.end(),[&](const auto& m){return Same(m.name,a[0].v.s);});
    if (it==g.marks.end()) return Reply(SCO_NOT_FOUND,out,n,"No such bookmark"); spot=&it->spot;
#else
    (void)a; (void)count;
#endif
    Restoration r; const auto result=Resolve(Open<sc_game_spatial_v1>(SC_GAME_SPATIAL_NAME),*spot,r);
    if (result!=SCO_OK) return Reply(result,out,n,"Saved pose unavailable (unloaded/expired metadata or root-only match); not moved");
    return Reply(SCO_OK,out,n,std::string(r.partial?"Partial restoration candidate":"Exact restoration candidate")+" at saved level "+std::to_string(r.level)+"; "+(r.system_known?"provider system known":"system unknown")+"; read-only, not moved");
}
void Panel();
void PanelEvent(const char*,const sco_widget_event* event,void*) noexcept {
    if (!event || event->size<sizeof(*event) || !g.api) return;
    try {
        char reply[512]{}; sco_arg name{}; name.type=SCO_ARG_STRING;
        switch (event->node_id) {
        case 2: if (event->kind==SCO_WIDGET_LIST && event->index>=0 && event->index<128) g.selected_place=static_cast<int32_t>(g.place_page*128)+event->index; break;
        case 3: if (event->kind==SCO_WIDGET_LIST && event->index>=0 && event->index<128) g.selected_mark=static_cast<int32_t>(g.mark_page*128)+event->index; break;
        case 4: if (event->kind==SCO_WIDGET_TEXT_INPUT && SafeName(event->text)) g.bookmark_name=event->text; break;
        case 5: if (event->kind==SCO_WIDGET_SLIDER && std::isfinite(event->value) && event->value>=100 && event->value<=20000) g.altitude=event->value; break;
        case 6: if (event->kind==SCO_WIDGET_BUTTON) Refuse(nullptr,0,nullptr,reply,sizeof(reply)); break;
        case 7: if (event->kind==SCO_WIDGET_BUTTON) ScanCommand(nullptr,0,nullptr,reply,sizeof(reply)); break;
        case 8: if (event->kind==SCO_WIDGET_BUTTON) { name.v.s=g.bookmark_name.c_str(); SaveBookmark(&name,1,nullptr,reply,sizeof(reply)); } break;
        case 9: if (event->kind==SCO_WIDGET_BUTTON && g.selected_mark>=0 && static_cast<size_t>(g.selected_mark)<g.marks.size()) { name.v.s=g.marks[g.selected_mark].name.c_str(); const std::string stable=name.v.s; name.v.s=stable.c_str(); DeleteBookmark(&name,1,nullptr,reply,sizeof(reply)); g.selected_mark=-1; } break;
        case 10: if (event->kind==SCO_WIDGET_BUTTON) {
#ifdef TRAVEL_TELEPORT
            InspectBookmark(nullptr,0,nullptr,reply,sizeof(reply));
#else
            if (g.selected_mark>=0 && static_cast<size_t>(g.selected_mark)<g.marks.size()) { name.v.s=g.marks[g.selected_mark].name.c_str(); InspectBookmark(&name,1,nullptr,reply,sizeof(reply)); }
#endif
            } break;
        case 11: if (event->kind==SCO_WIDGET_BUTTON && g.place_page) { --g.place_page; g.selected_place=-1; } break;
        case 12: if (event->kind==SCO_WIDGET_BUTTON && (g.place_page+1)*128<g.places.size()) { ++g.place_page; g.selected_place=-1; } break;
        case 13: if (event->kind==SCO_WIDGET_BUTTON && g.mark_page) { --g.mark_page; g.selected_mark=-1; } break;
        case 14: if (event->kind==SCO_WIDGET_BUTTON && (g.mark_page+1)*128<g.marks.size()) { ++g.mark_page; g.selected_mark=-1; } break;
        default: break;
        }
        if (reply[0]) g.last_status=reply;
        Panel();
    } catch (...) { /* host callback never receives a C++ exception */ }
}
void Panel() {
    const auto* widgets=Open<sco_widgets_v1>(SCO_WIDGETS_NAME);
    if (!TRAVEL_FN(widgets,sco_widgets_v1,set_panel) || !TRAVEL_FN(widgets,sco_widgets_v1,remove_panel)) return;
    std::vector<sco_widget_node> nodes;
    auto add=[&](uint32_t kind,uint32_t node_id,const char* label) -> sco_widget_node& { sco_widget_node node{}; node.size=sizeof(node); node.kind=kind; node.id=node_id; node.index=-1; node.label=label; nodes.push_back(node); return nodes.back(); };
    std::vector<std::string> places,marks; std::vector<const char*> place_items,mark_items;
    for (size_t i=g.place_page*128;i<g.places.size() && places.size()<128;++i) places.push_back(g.places[i].system+" / "+g.places[i].name);
    for (size_t i=g.mark_page*128;i<g.marks.size() && marks.size()<128;++i) marks.push_back(g.marks[i].system+" / "+g.marks[i].name);
    for (const auto& text:places) place_items.push_back(text.c_str()); for (const auto& text:marks) mark_items.push_back(text.c_str());
    const auto status=g.last_status.substr(0,255); add(SCO_WIDGET_LABEL,1,status.c_str());
#ifdef TRAVEL_TELEPORT
    add(SCO_WIDGET_BUTTON,6,"F7/F8 unavailable: G2 REFUSE"); add(SCO_WIDGET_BUTTON,10,"Inspect saved pose (read-only)");
#else
    auto& p=add(SCO_WIDGET_LIST,2,"Destinations (system / name; 128 per page)"); p.items=place_items.data(); p.item_count=static_cast<uint32_t>(place_items.size());
    if (g.selected_place>=static_cast<int32_t>(g.place_page*128) && static_cast<size_t>(g.selected_place)<g.place_page*128+places.size()) p.index=g.selected_place-static_cast<int32_t>(g.place_page*128);
    add(SCO_WIDGET_BUTTON,11,"Previous destinations"); add(SCO_WIDGET_BUTTON,12,"Next destinations");
    auto& m=add(SCO_WIDGET_LIST,3,"Bookmarks (system / name; 128 per page)"); m.items=mark_items.data(); m.item_count=static_cast<uint32_t>(mark_items.size());
    if (g.selected_mark>=static_cast<int32_t>(g.mark_page*128) && static_cast<size_t>(g.selected_mark)<g.mark_page*128+marks.size()) m.index=g.selected_mark-static_cast<int32_t>(g.mark_page*128);
    add(SCO_WIDGET_BUTTON,13,"Previous bookmarks"); add(SCO_WIDGET_BUTTON,14,"Next bookmarks");
    add(SCO_WIDGET_TEXT_INPUT,4,"Bookmark name").text=g.bookmark_name.c_str();
    auto& slider=add(SCO_WIDGET_SLIDER,5,"Arrival altitude (movement unavailable)"); slider.value=g.altitude; slider.minimum=100; slider.maximum=20000;
    add(SCO_WIDGET_BUTTON,6,"Travel / go to bookmark unavailable: G2 REFUSE");
    add(SCO_WIDGET_BUTTON,7,"Scan observed zones (not a full system scan)");
    add(SCO_WIDGET_BUTTON,8,"Save current bookmark (read-only capture)");
    add(SCO_WIDGET_BUTTON,9,"Delete selected bookmark"); add(SCO_WIDGET_BUTTON,10,"Inspect selected bookmark (read-only)");
#endif
    if (widgets->set_panel(g.self,(std::string(id)+".travel").c_str(),id,nodes.data(),static_cast<uint32_t>(nodes.size()),sizeof(nodes[0]),PanelEvent,nullptr)==SCO_OK) g.panel=true;
}
}
using namespace implementation;
template<sco_command_fn Fn> sco_result Guard(const sco_arg* args,uint32_t nargs,void* user,char* reply,uint32_t size) noexcept {
    if (!g.api || !g.self) return SCO_UNAVAILABLE;
    if (!reply && size) return SCO_BAD_ARG;
    try { const auto r=Fn(args,nargs,user,reply,size); if (reply && size && reply[0]) g.last_status=reply; Panel(); return r; } catch (...) { return SCO_FAILED; }
}
inline const sco_plugin_info* Query() { return &info; }
inline sco_result Load(const sco_api* api,sco_plugin* self) {
    if (!api || !self || g.api) return SCO_BAD_ARG;
    if (api->major!=SCO_API_MAJOR || api->minor<SCO_API_MINOR || !TRAVEL_FN(api,sco_api,register_command) || !TRAVEL_FN(api,sco_api,query_service)) return SCO_UNAVAILABLE;
    g.api=api; g.self=self;
    try {
        g.root=Setting("data_root","data");
        const char* inherited=std::getenv("SC_OFFLINE_SPAWN_FILE");
        g.spawn=Setting("spawn_file",inherited && *inherited ? inherited : (g.root/"spawn.txt").string());
        Storage();
#ifdef TRAVEL_TELEPORT
        const auto loaded=LoadSaved();
#else
        LoadPlaces(g.root/"locations.txt"); LoadPlaces(g.root/"locations_found.txt"); const auto loaded=LoadMarks();
#endif
        if (loaded!=SCO_OK) Reply(loaded,nullptr,0,"Legacy state import/read failed; prior database retained");
        const sco_arg_def name{"name",SCO_ARG_STRING,0,"Legacy display name"};
#ifndef TRAVEL_TELEPORT
        const sco_arg_def dest[]={{"place",SCO_ARG_STRING,0,"Destination display name"},{"altitude",SCO_ARG_FLOAT,0,"Metres above surface (100..20000)"}};
#endif
        const sco_command commands[]={
#ifdef TRAVEL_TELEPORT
            {sizeof(sco_command),0,"teleport.save","Quick save unavailable","G2 consumer held in REFUSE mode",nullptr,nullptr,0,sizeof(sco_arg_def),Guard<Refuse>,nullptr},
            {sizeof(sco_command),0,"teleport.go","Relocation unavailable",refusal,nullptr,nullptr,0,sizeof(sco_arg_def),Guard<Refuse>,nullptr},
            {sizeof(sco_command),0,"teleport.inspect","Inspect saved pose","Read-only exact/partial restoration candidate",nullptr,nullptr,0,sizeof(sco_arg_def),Guard<InspectBookmark>,nullptr},
            {sizeof(sco_command),0,"teleport.status","Teleport state","Saved data retained; mutations unavailable",nullptr,nullptr,0,sizeof(sco_arg_def),Guard<Status>,nullptr}
#else
            {sizeof(sco_command),0,"quantum.travel","Relocation unavailable",refusal,nullptr,dest,2,sizeof(sco_arg_def),Guard<Refuse>,nullptr},
            {sizeof(sco_command),0,"quantum.bookmark","Relocation unavailable",refusal,nullptr,&name,1,sizeof(sco_arg_def),Guard<Refuse>,nullptr},
            {sizeof(sco_command),0,"quantum.save_bookmark","Save bookmark","Read-only full pose capture with bounded ancestry",nullptr,&name,1,sizeof(sco_arg_def),Guard<SaveBookmark>,nullptr},
            {sizeof(sco_command),0,"quantum.delete_bookmark","Delete bookmark","Retains legacy quantum DB schema",nullptr,&name,1,sizeof(sco_arg_def),Guard<DeleteBookmark>,nullptr},
            {sizeof(sco_command),0,"quantum.inspect_bookmark","Inspect bookmark","Read-only partial restoration candidate; no mutation",nullptr,&name,1,sizeof(sco_arg_def),Guard<InspectBookmark>,nullptr},
            {sizeof(sco_command),0,"quantum.scan","Observed zones","Bounded observation pages; not an exhaustive streamed-system scan",nullptr,nullptr,0,sizeof(sco_arg_def),Guard<ScanCommand>,nullptr},
            {sizeof(sco_command),0,"quantum.status","Travel state","Destination/bookmark counts and refusal status",nullptr,nullptr,0,sizeof(sco_arg_def),Guard<Status>,nullptr}
#endif
        };
        for (const auto& c:commands) { const auto r=api->register_command(self,&c); if (r!=SCO_OK) { g={}; return r; } }
#ifdef TRAVEL_TELEPORT
        (void)name;
        const auto* ui=Open<sco_ui_v1>(SCO_UI_NAME);
        if (TRAVEL_FN(ui,sco_ui_v1,bind_hotkey) && TRAVEL_FN(ui,sco_ui_v1,unbind_hotkey)) {
            const auto f7=ui->bind_hotkey(self,"f7","teleport.save",nullptr,0);
            const auto f8=ui->bind_hotkey(self,"f8","teleport.go",nullptr,0);
            g.hotkeys=f7==SCO_OK || f8==SCO_OK;
        }
#endif
        Panel();
        return SCO_OK;
    } catch (...) { g={}; return SCO_FAILED; }
}
inline void Unload() noexcept {
    try {
    if (g.panel) {
        const auto* widgets=Open<sco_widgets_v1>(SCO_WIDGETS_NAME);
        if (TRAVEL_FN(widgets,sco_widgets_v1,remove_panel)) widgets->remove_panel(g.self,(std::string(id)+".travel").c_str());
    }
    if (g.hotkeys) {
        const auto* ui=Open<sco_ui_v1>(SCO_UI_NAME);
        if (TRAVEL_FN(ui,sco_ui_v1,unbind_hotkey)) { ui->unbind_hotkey(g.self,"f7"); ui->unbind_hotkey(g.self,"f8"); }
    }
    } catch (...) {}
    g={}; // No subscription, worker, provider mutation or shutdown calls; host removes commands.
}
}
