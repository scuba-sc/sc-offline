// SPDX-License-Identifier: GPL-3.0-only
// Shared SDK-only consumer implementation. Each native owner has independent state/storage.
#pragma once
#include "travel_model.h"
#include "scosdk/storage.hpp"
#include "sco_settings.h"
#include "sco_ui.h"
#include "sco_widgets.h"
#ifndef TRAVEL_TELEPORT
#include "sco_content.h"
#endif
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <limits>
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
    Spot saved; bool hotkeys=false,panel=false,key_save=false,key_go=false;
    uint32_t place_page=0,mark_page=0; int32_t selected_place=-1,selected_mark=-1;
    uintptr_t panel_cookie=0;
    std::string bookmark_name,filter,last_status="Ready",plain; double altitude=1000; // plain: the page's sentence for the latest reply
#ifndef TRAVEL_TELEPORT
    std::string curated_status="Not read";
#endif
    uint64_t ticket=0; bool ticking=false; std::string moving; // one tracked game.relocation request; never persisted
} g;
// Opaque, never dereferenced; do not reuse a publication identity after reload.
uintptr_t next_panel_cookie=0;
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
// text goes to the command reply and the log; plain (default: the same text) is the sentence the page shows.
sco_result Reply(sco_result r,char* out,uint32_t n,const std::string& text,const std::string& plain={}) {
    if (out && n) std::snprintf(out,n,"%s",text.c_str());
    if (TRAVEL_FN(g.api,sco_api,log)) g.api->log(g.self,r==SCO_OK?SCO_LOG_INFO:SCO_LOG_WARN,text.c_str());
    g.plain=plain.empty()?text:plain;
    return r;
}
std::string Count(size_t n,const char* noun) { return std::to_string(n)+" "+noun+(n==1?"":"s"); }
// "System / name" when the system is known; scanned places have none, so show the name alone.
std::string Display(const std::string& system,const std::string& name) {
    return system.empty() || system=="Unknown system" ? name : system+" / "+name;
}
std::string Lower(std::string text) { for (auto& c:text) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return text; }
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
    std::vector<Bookmark> marks; std::istringstream in(text); std::string line,spots; Bookmark m; bool have=false,unterminated_cr=false;
    auto flush=[&] { if (have && marks.size()<400 && m.name.size()<=63 && m.system.size()<=31 &&
                          !(unterminated_cr && spots.size()>=65536) && ParseSpot(spots,m.spot)) marks.push_back(m);
                     spots.clear(); unterminated_cr=false; };
    while (std::getline(in,line)) {
        if (line.starts_with("bookmark ")) {
            if (!in.eof() && !line.empty() && line.back()=='\r') line.pop_back();
            flush(); if (marks.size()==400) break;
            m={}; have=true; const auto bar=line.find('|',9);
            if (bar!=std::string::npos) { m.system=line.substr(9,bar-9); m.name=line.substr(bar+1); }
            else m.name=line.substr(9);
        } else if (have) {
            spots+=line; unterminated_cr=in.eof() && !line.empty() && line.back()=='\r';
            if (!unterminated_cr) spots+='\n'; // retain the old size bound for a final unterminated line
        }
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
// Own all allocations before persistence; publishing a committed collection must
// not allocate or copy after the durable operation has succeeded.
sco_result SaveMarks(std::vector<Bookmark> next) {
    const auto r=g.store ? StoreMarks(next,{}) : WriteFile(g.root/"bookmarks.txt",MarksText(next));
    if (r==SCO_OK) {
        g.marks.swap(next);
        g.selected_mark=-1; // a copied row index must never drift after a mutation
        if (g.marks.empty()) g.mark_page=0;
        else g.mark_page=(std::min)(g.mark_page,static_cast<uint32_t>((g.marks.size()-1)/128));
    }
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
#ifndef TRAVEL_TELEPORT
sco_result LoadCuratedPlaces() {
    const auto finish=[](sco_result result,const std::string& message) {
        g.curated_status=message; return result;
    };
    const auto* content=Open<sco_content_v1>(SCO_CONTENT_NAME);
    if (!g.self || !TRAVEL_FN(content,sco_content_v1,item) || !TRAVEL_FN(content,sco_content_v1,read))
        return finish(SCO_UNAVAILABLE,"Unavailable: sco.content 1.0 item/read required; no base-file fallback");
    sco_content_item selected{}; bool found=false;
    for(uint32_t ordinal=0;;++ordinal) {
        sco_content_item item{}; item.size=sizeof(item);
        const auto result=content->item(g.self,SCO_CONTENT_LIST,ordinal,&item);
        if(result==SCO_NOT_FOUND) break;
        if(result!=SCO_OK) return finish(result,"Curated enumeration refused; no base-file fallback");
        if(ordinal==SCO_CONTENT_MAX_ITEMS) return finish(SCO_TOO_MANY,"Curated enumeration exceeds 4096 records");
        if(item.size<sizeof(item) || item.kind!=SCO_CONTENT_LIST || !item.id ||
           !std::memchr(item.name,0,sizeof(item.name)) || !std::memchr(item.pack,0,sizeof(item.pack)))
            return finish(SCO_FAILED,"Invalid copied curated-list metadata");
        if(std::strcmp(item.name,"lists/locations.txt")) continue;
        if(found) return finish(SCO_BAD_ARG,"Ambiguous lists/locations.txt: enable exactly one source pack; none selected");
        selected=item; found=true;
    }
    if(!found) return finish(SCO_NOT_FOUND,"No enabled lists/locations.txt; no base-file fallback");
    uint32_t bytes=0; const auto asked=content->read(g.self,selected.id,nullptr,&bytes);
    if(asked!=SCO_TOO_MANY && !(asked==SCO_OK && !bytes))
        return finish(asked==SCO_OK ? SCO_FAILED : asked,"Curated size query refused");
    if(bytes>SCO_CONTENT_MAX_FILE_BYTES) return finish(SCO_TOO_MANY,"Curated list exceeds 1MiB");
    std::string text(bytes,'\0'); uint32_t copied=bytes;
    if(bytes) {
        const auto result=content->read(g.self,selected.id,text.data(),&copied);
        if(result!=SCO_OK) return finish(result,"Curated copied read refused");
    }
    if(copied!=bytes) return finish(SCO_FAILED,"Curated copied byte count changed");
    if(text.find('\0')!=text.npos) return finish(SCO_BAD_ARG,"Curated list contains embedded NUL; none selected");
    // Parse atomically; first case-insensitive entity wins in original row order.
    std::vector<Destination> next; std::istringstream input(text); std::string line;
    while(std::getline(input,line)) {
        if(line.size()>512) return finish(SCO_TOO_MANY,"Curated row exceeds 512 bytes; none selected");
        if(Trim(line).empty() || Trim(line).starts_with('#')) continue;
        Destination d; std::istringstream fields(line); std::string radius;
        if(!std::getline(fields,d.system,'|') || !std::getline(fields,d.name,'|') || !std::getline(fields,d.entity,'|'))
            return finish(SCO_BAD_ARG,"Malformed curated row; none selected");
        d.system=Trim(d.system); d.name=Trim(d.name); d.entity=Trim(d.entity); std::getline(fields,radius); radius=Trim(radius);
        if(d.system.size()>31 || d.name.size()>63 || d.entity.empty() || d.entity.size()>95)
            return finish(SCO_BAD_ARG,"Invalid curated field bounds; none selected");
        try { size_t used=0; d.radius=radius.empty()?0:std::stod(radius,&used);
            if(!radius.empty() && used!=radius.size()) return finish(SCO_BAD_ARG,"Malformed curated radius; none selected");
        } catch(...) { return finish(SCO_BAD_ARG,"Malformed curated radius; none selected"); }
        if(!std::isfinite(d.radius) || d.radius<0) return finish(SCO_BAD_ARG,"Invalid curated radius; none selected");
        if(std::any_of(next.begin(),next.end(),[&](const auto& p){return Same(p.entity,d.entity);})) continue;
        if(next.size()==3000) return finish(SCO_TOO_MANY,"Curated list exceeds 3000 destinations; none selected");
        next.push_back(std::move(d));
    }
    g.places=std::move(next);
    return finish(SCO_OK,"Loaded "+std::to_string(g.places.size())+" curated destinations from lists/locations.txt");
}
#endif
sco_result LoadSaved() {
    std::string text,seen; const auto time=FileTime(g.spawn); const bool file=ReadFile(g.spawn,text); Spot old;
    const bool parsed=file && ParseSpot(text,old);
    if (!g.store) { if (parsed) g.saved=old; return SCO_OK; }
    const auto marker=g.store.Get("spawn.txt.time",seen);
    if (parsed && !time.empty() && (marker!=SCO_OK || seen!=time)) {
        sco::sdk::StorageTransaction tx(g.store); auto r=tx.Result();
        if (r==SCO_OK) r=g.store.Put("spot",std::string_view(SpotText(old)));
        if (r==SCO_OK) r=g.store.Put("spawn.txt.time",std::string_view(time));
        if (r==SCO_OK) r=tx.Commit();
        if (r==SCO_OK) g.saved=old;
        return r;
    }
    const auto r=g.store.Get("spot",text);
    if (r==SCO_OK && !ParseSpot(text,g.saved)) return SCO_BAD_ARG;
    return r==SCO_NOT_FOUND ? SCO_OK : r;
}
// Player switch: plugin.ini [settings] relocation (default true); an absent settings service keeps that default.
bool MovesAllowed() {
    const auto* st=Open<sco_settings_v1>(SCO_SETTINGS_NAME); int32_t on=1;
    if (TRAVEL_FN(st,sco_settings_v1,get_bool) && st->get_bool(g.self,"relocation",&on)==SCO_OK) return on!=0;
    return true;
}
bool RelocationOn() { return TRAVEL_FN(g.api,sco_api,has) && g.api->has(SC_RELOCATION_NAME); }
// Every move command checks the switch, then the capability, fresh, before reading its arguments.
sco_result Ready(std::string& why) {
    if (!MovesAllowed()) { why=switched_off; return SCO_UNAVAILABLE; }
    if (!RelocationOn()) { why=unavailable; return SCO_UNAVAILABLE; }
    return SCO_OK;
}
// The page's sentence for a refusal from Ready().
const char* PlainWhy(const std::string& why) { return why==switched_off ? switched_off_plain : unavailable_plain; }
bool SafePlace(const char* name) { return name && name[0] && std::strlen(name)<=95 && !std::strpbrk(name,"|\r\n"); }
void Panel();
void TravelTick(const char*,const void*,void*) noexcept;
void StopTracking() {
    if (g.ticking && TRAVEL_FN(g.api,sco_api,unsubscribe)) g.api->unsubscribe(g.self,"tick",TravelTick);
    g.ticking=false; g.ticket=0;
}
// Game thread, per tick while a request is tracked: status only reads; report the terminal state once.
void TravelTick(const char*,const void*,void*) noexcept {
    try {
        if (!g.ticket) { StopTracking(); return; }
        sc_relocation_status s{};
        const auto r=Poll(Open<sc_relocation_v1>(SC_RELOCATION_NAME),g.self,g.ticket,s);
        if (r==SCO_OK && s.state==SC_RELOCATION_PENDING) return;
        std::string text,plain; sco_result result=SCO_FAILED;
        if (r!=SCO_OK) { text="Lost track of the move to "+g.moving+" (game.relocation status unavailable); arrival not confirmed";
            plain="Lost track of the move to "+g.moving+". Check where you are."; }
        else if (s.state==SC_RELOCATION_COMPLETE) { result=SCO_OK; text="Arrived at "+g.moving; plain="Arrived at "+g.moving+"."; }
        else {
            text=std::string(s.state==SC_RELOCATION_CANCELLED?"Move cancelled":"Move failed")+" ("+ReasonText(s.reason)+"): "+g.moving+(s.applied?"; the move had started":"; not moved");
            plain="The move to "+g.moving+(s.state==SC_RELOCATION_CANCELLED?" was cancelled":" failed: "+std::string(PlainReason(s.reason)))+
                (s.applied?". You may already have started moving.":". You haven't moved.");
        }
        StopTracking(); g.moving.clear();
        Reply(result,nullptr,0,text,plain); g.last_status=g.plain;
        if (TRAVEL_FN(g.api,sco_api,status)) g.api->status(g.self,g.plain.c_str());
        Panel();
    } catch (...) { /* host callback never receives a C++ exception */ }
}
// label is how the page names the place; note is extra detail for the log only.
sco_result Move(const Pose& target,const std::string& label,char* out,uint32_t n,const std::string& note={}) {
    if (g.ticket) return Reply(SCO_TOO_MANY,out,n,"Already moving to "+g.moving+"; one move at a time","You're already travelling to "+g.moving+". Wait until you arrive.");
    if (!MovesAllowed()) return Reply(SCO_UNAVAILABLE,out,n,switched_off,switched_off_plain);
    if (!TRAVEL_FN(g.api,sco_api,subscribe) || !TRAVEL_FN(g.api,sco_api,unsubscribe)) return Reply(SCO_UNAVAILABLE,out,n,"Cannot follow a move without game ticks; not moved","Couldn't start the move. You haven't moved.");
    const auto* service=Open<sc_relocation_v1>(SC_RELOCATION_NAME);
    uint64_t ticket=0; const auto r=Relocate(g.api,service,g.self,target,ticket);
    if (r!=SCO_OK) return Reply(r,out,n,r==SCO_UNAVAILABLE?std::string(unavailable):r==SCO_TOO_MANY?"Another move is in progress; not moved":
        r==SCO_BAD_ARG?"Destination pose rejected; not moved":"game.relocation refused the move; not moved",
        r==SCO_UNAVAILABLE?std::string(unavailable_plain):r==SCO_TOO_MANY?"Another move is still in progress. Wait for it to finish.":
        r==SCO_BAD_ARG?"That destination can't be used. You haven't moved.":"The game refused the move. You haven't moved.");
    if (g.api->subscribe(g.self,"tick",TravelTick,nullptr)!=SCO_OK) {
        if (TRAVEL_FN(service,sc_relocation_v1,cancel)) service->cancel(g.self,ticket);
        return Reply(SCO_FAILED,out,n,"Cannot follow the move (tick subscription refused); request cancelled","Couldn't start the move. You haven't moved.");
    }
    g.ticking=true; g.ticket=ticket; g.moving=label;
    return Reply(SCO_OK,out,n,"Moving to "+label+(note.empty()?"":" ["+note+"]")+" (game.relocation); arrival is reported when the game confirms it",
        "Moving to "+label+". You'll see a message when you arrive.");
}
sco_result SaveSpot(const sco_arg*,uint32_t,void*,char* out,uint32_t n) {
    Spot spot; const auto r=Capture(Open<sc_spatial_v1>(SC_SPATIAL_SERVICE_NAME),Open<sc_game_spatial_v1>(SC_GAME_SPATIAL_NAME),spot);
    if (r!=SCO_OK) return Reply(r,out,n,"Cannot capture current full pose/complete ancestry; nothing saved","Couldn't read where you are, so nothing was saved.");
    const auto text=SpotText(spot);
    const auto saved=g.store ? g.store.Put("spot",std::string_view(text)) : WriteFile(g.spawn,text);
    if (saved!=SCO_OK) return Reply(saved,out,n,"Quick-save storage failed; prior spot retained","Couldn't save your spot. Your earlier saved spot is unchanged.");
    g.saved=std::move(spot);
    return Reply(SCO_OK,out,n,"Saved current spot (read-only capture; not moved)",g.key_go?"Saved this spot. Press F8 to come back to it.":"Saved this spot.");
}
sco_result GoSaved(const sco_arg*,uint32_t,void*,char* out,uint32_t n) {
    std::string why; if (Ready(why)!=SCO_OK) return Reply(SCO_UNAVAILABLE,out,n,why,PlainWhy(why));
    Restoration r; const auto found=Resolve(Open<sc_game_spatial_v1>(SC_GAME_SPATIAL_NAME),g.saved,r);
    if (found!=SCO_OK) return Reply(found,out,n,"Saved spot unavailable (none saved, unloaded/expired metadata or root-only match); not moved",
        "Can't go there. Either nothing is saved yet, or that area isn't loaded now. You haven't moved.");
    return Move(r.pose,r.partial?"the area around your saved spot":"your saved spot",out,n,std::string(r.partial?"partial, level ":"exact, level ")+std::to_string(r.level));
}
// Panel actions keep the selected row's identity, even when display names repeat.
// Name-based commands retain their historical first case-insensitive match.
size_t BookmarkIndex(const char* name) {
    const auto it=std::find_if(g.marks.begin(),g.marks.end(),[&](const auto& m){return Same(m.name,name);});
    return static_cast<size_t>(it-g.marks.begin());
}
sco_result GoBookmarkAt(size_t index,char* out,uint32_t n) {
    std::string why; if (Ready(why)!=SCO_OK) return Reply(SCO_UNAVAILABLE,out,n,why,PlainWhy(why));
    if (index>=g.marks.size()) return Reply(SCO_NOT_FOUND,out,n,"No such bookmark","Choose a saved spot first.");
    const auto bookmark=g.marks[index]; Restoration r;
    const auto found=Resolve(Open<sc_game_spatial_v1>(SC_GAME_SPATIAL_NAME),bookmark.spot,r);
    if (found!=SCO_OK) return Reply(found,out,n,"Bookmark pose unavailable (unloaded/expired metadata or root-only match); not moved",
        "Can't go to '"+bookmark.name+"' now. That area isn't loaded. You haven't moved.");
    return Move(r.pose,"saved spot '"+bookmark.name+"'",out,n,std::string(r.partial?"partial, level ":"exact, level ")+std::to_string(r.level));
}
sco_result GoBookmark(const sco_arg* a,uint32_t count,void*,char* out,uint32_t n) {
    std::string why; if (Ready(why)!=SCO_OK) return Reply(SCO_UNAVAILABLE,out,n,why,PlainWhy(why));
    if (count!=1 || !a || a[0].type!=SCO_ARG_STRING || !SafeName(a[0].v.s)) return Reply(SCO_BAD_ARG,out,n,"Bookmark name must be <=63 bytes, without separators/newlines","Spot names can be up to 63 characters and can't contain | or line breaks.");
    return GoBookmarkAt(BookmarkIndex(a[0].v.s),out,n);
}
sco_result TravelTo(const sco_arg* a,uint32_t count,void*,char* out,uint32_t n) {
    std::string why; if (Ready(why)!=SCO_OK) return Reply(SCO_UNAVAILABLE,out,n,why,PlainWhy(why));
    if (count!=2 || !a || a[0].type!=SCO_ARG_STRING || !SafePlace(a[0].v.s) || a[1].type!=SCO_ARG_FLOAT || !std::isfinite(a[1].v.f) || a[1].v.f<100 || a[1].v.f>20000)
        return Reply(SCO_BAD_ARG,out,n,"Usage: quantum.travel <destination> <altitude 100..20000 m>","Give a destination and a height between 100 and 20000 metres.");
    const auto it=std::find_if(g.places.begin(),g.places.end(),[&](const auto& p){return Same(p.name,a[0].v.s) || Same(p.entity,a[0].v.s);});
    if (it==g.places.end()) return Reply(SCO_NOT_FOUND,out,n,"No such destination; check lists/locations.txt or run quantum.scan","That destination isn't in the list. Try Find nearby places.");
    const auto* sp=Open<sc_game_spatial_v1>(SC_GAME_SPATIAL_NAME); sc_game_zone z{}; z.size=sizeof(z);
    if (!TRAVEL_FN(sp,sc_game_spatial_v1,find_zone) || !sp->find_zone(it->entity.c_str(),&z) || !RecordValid(z)) return Reply(SCO_UNAVAILABLE,out,n,"Destination zone '"+it->entity+"' is not loaded; not moved",
        it->name+" isn't loaded right now. You haven't moved.");
    // Arrival: radius + altitude along the destination zone's +Z axis, identity rotation in that frame.
    Pose target; target.zone=z.zone_id; target.pos[2]=it->radius+a[1].v.f;
    return Move(target,it->name+", "+std::to_string(static_cast<long long>(a[1].v.f))+" m up",out,n);
}
sco_result Status(const sco_arg*,uint32_t,void*,char* out,uint32_t n) {
#ifdef TRAVEL_TELEPORT
    std::string plain=g.saved.z.empty()?"No spot is saved yet.":"A spot is saved.";
#else
    std::string plain=Count(g.places.size(),"destination")+" and "+Count(g.marks.size(),"saved spot")+".";
#endif
    if (!MovesAllowed()) plain+=" Travel is turned off in this plugin's settings.";
    else if (!RelocationOn()) plain+=" Travel isn't available in this game right now.";
    if (g.ticket) plain+=" You're travelling to "+g.moving+".";
    return Reply(SCO_OK,out,n,std::string(id)+": "+std::to_string(g.marks.size())+" bookmarks, "+std::to_string(g.places.size())+" destinations; moves "+
        (MovesAllowed()?"allowed":"off in settings")+"; game.relocation "+(RelocationOn()?"on":"off")+"; "+(g.ticket?"moving to "+g.moving:std::string("no move in progress"))+
        "; system identity unknown unless explicitly provided; scan is observed ancestry only"
#ifndef TRAVEL_TELEPORT
        "; curated locations: "+g.curated_status
#endif
        ,plain);
}
sco_result ScanCommand(const sco_arg*,uint32_t,void*,char* out,uint32_t n) {
    std::vector<sc_game_zone> records; const auto r=Scan(Open<sc_game_spatial_v1>(SC_GAME_SPATIAL_NAME),records);
    if (r!=SCO_OK) return Reply(r,out,n,"Spatial scan unavailable or metadata expired; prior destination list unchanged","Couldn't look for nearby places right now. Your list is unchanged.");
    auto next=g.places; std::string appended; size_t added=0;
    for (const auto& z:records) {
        if (next.size()==3000) break;
        if (std::any_of(next.begin(),next.end(),[&](const auto& p){return Same(p.entity,z.name);})) continue;
        // Names are display-only: never infer certified system identity from OOC/root names.
        Destination d{"Unknown system",z.name,z.name,0};
        if (d.name.size()>63) d.name.resize(63);
        if (d.entity.find_first_of("|\r\n")!=std::string::npos) continue;
        next.push_back(d); ++added; appended+=d.system+" | "+d.name+" | "+d.entity+" | 0\n";
    }
    if (!appended.empty()) {
        std::string text; ReadFile(g.root/"locations_found.txt",text);
        const auto wrote=WriteFile(g.root/"locations_found.txt",text+appended);
        if (wrote!=SCO_OK) return Reply(wrote,out,n,"Observed scan could not persist; destination list unchanged","Couldn't save the places that were found. Your list is unchanged.");
    }
    g.places=std::move(next);
    return Reply(SCO_OK,out,n,"Copied "+std::to_string(records.size())+" observed zones (max 256; 64/page); not a full system scan; system identity unknown",
        "Found "+Count(records.size(),"place")+" nearby, "+std::to_string(added)+" new.");
}
sco_result SaveBookmark(const sco_arg* a,uint32_t count,void*,char* out,uint32_t n) {
    if (count!=1 || !a || a[0].type!=SCO_ARG_STRING || !SafeName(a[0].v.s)) return Reply(SCO_BAD_ARG,out,n,"Bookmark name must be <=63 bytes, without separators/newlines","Spot names can be up to 63 characters and can't contain | or line breaks.");
    if (g.marks.size()==400) return Reply(SCO_TOO_MANY,out,n,"400 bookmark limit; delete one first","You can save up to 400 spots. Delete one first.");
    Spot spot; const auto r=Capture(Open<sc_spatial_v1>(SC_SPATIAL_SERVICE_NAME),Open<sc_game_spatial_v1>(SC_GAME_SPATIAL_NAME),spot);
    if (r!=SCO_OK) return Reply(r,out,n,"Cannot capture current full pose/complete ancestry; nothing saved","Couldn't read where you are, so nothing was saved.");
    Bookmark m{"Unknown system",a[0].v.s,spot}; if (m.name.empty()) m.name=spot.z[0].name.substr(0,63);
    auto next=g.marks; next.push_back(m); const auto saved=SaveMarks(std::move(next));
    return Reply(saved,out,n,saved==SCO_OK?"Saved '"+m.name+"' (read-only capture; no relocation)":"Bookmark storage failed; in-memory bookmarks retained, durable outcome unconfirmed",
        saved==SCO_OK?"Saved '"+m.name+"'.":"Couldn't save the spot. It may be lost when you restart.");
}
sco_result DeleteBookmarkAt(size_t index,char* out,uint32_t n) {
    if (index>=g.marks.size()) return Reply(SCO_NOT_FOUND,out,n,"No such bookmark","Choose a saved spot first.");
    auto next=g.marks; next.erase(next.begin()+static_cast<std::ptrdiff_t>(index));
    const auto r=SaveMarks(std::move(next));
    return Reply(r,out,n,r==SCO_OK?"Bookmark deleted":"Delete failed; in-memory bookmarks retained, durable outcome unconfirmed",
        r==SCO_OK?"Deleted the saved spot.":"Couldn't delete that spot. It may come back when you restart.");
}
sco_result DeleteBookmark(const sco_arg* a,uint32_t count,void*,char* out,uint32_t n) {
    if (count!=1 || !a || a[0].type!=SCO_ARG_STRING || !SafeName(a[0].v.s)) return SCO_BAD_ARG;
    return DeleteBookmarkAt(BookmarkIndex(a[0].v.s),out,n);
}
sco_result InspectSpot(const Spot& spot,char* out,uint32_t n) {
    Restoration r; const auto result=Resolve(Open<sc_game_spatial_v1>(SC_GAME_SPATIAL_NAME),spot,r);
    if (result!=SCO_OK) return Reply(result,out,n,"Saved pose unavailable (unloaded/expired metadata or root-only match); not moved",
        "That spot can't be reached now. Its area isn't loaded.");
    return Reply(SCO_OK,out,n,std::string(r.partial?"Partial restoration candidate":"Exact restoration candidate")+" at saved level "+std::to_string(r.level)+"; "+(r.system_known?"provider system known":"system unknown")+"; read-only, not moved",
        r.partial?"Only the area around that spot is loaded. Going there would take you near it.":"That spot can be reached.");
}
sco_result InspectBookmarkAt(size_t index,char* out,uint32_t n) {
    if (index>=g.marks.size()) return Reply(SCO_NOT_FOUND,out,n,"No such bookmark","Choose a saved spot first.");
    const auto spot=g.marks[index].spot;
    return InspectSpot(spot,out,n);
}
sco_result InspectBookmark(const sco_arg* a,uint32_t count,void*,char* out,uint32_t n) {
#ifndef TRAVEL_TELEPORT
    if (count!=1 || !a || a[0].type!=SCO_ARG_STRING || !SafeName(a[0].v.s)) return SCO_BAD_ARG;
    return InspectBookmarkAt(BookmarkIndex(a[0].v.s),out,n);
#else
    (void)a; (void)count;
    const auto spot=g.saved;
    return InspectSpot(spot,out,n);
#endif
}
// Rows of g.places that match the search box, in list order.
std::vector<size_t> PlaceRows() {
    std::vector<size_t> rows; const auto needle=Lower(g.filter);
    for (size_t i=0;i<g.places.size();++i)
        if (needle.empty() || Lower(Display(g.places[i].system,g.places[i].name)).find(needle)!=std::string::npos) rows.push_back(i);
    return rows;
}
void Panel();
void PanelEvent(const char*,const sco_widget_event* event,void* context) noexcept {
    if (!event || event->size<sizeof(*event) || !g.api || !g.panel_cookie ||
        reinterpret_cast<uintptr_t>(context)!=g.panel_cookie) return;
    try {
        char reply[512]{}; sco_arg name{}; name.type=SCO_ARG_STRING; g.plain.clear();
        switch (event->node_id) {
        case 2: if (event->kind==SCO_WIDGET_LIST) {
            g.selected_place=-1; const auto rows=PlaceRows();
            if (event->index>=0 && event->index<128 && g.place_page*128+static_cast<uint32_t>(event->index)<rows.size())
                g.selected_place=static_cast<int32_t>(rows[g.place_page*128+static_cast<uint32_t>(event->index)]);
            } break;
        case 17: if (event->kind==SCO_WIDGET_TEXT_INPUT && event->text && std::strlen(event->text)<=63) { g.filter=Trim(event->text); g.place_page=0; g.selected_place=-1; } break;
        case 3: if (event->kind==SCO_WIDGET_LIST) {
            g.selected_mark=-1;
            if (event->index>=0 && event->index<128 && g.mark_page*128+static_cast<uint32_t>(event->index)<g.marks.size())
                g.selected_mark=static_cast<int32_t>(g.mark_page*128)+event->index;
            } break;
        case 4: if (event->kind==SCO_WIDGET_TEXT_INPUT && SafeName(event->text)) g.bookmark_name=event->text; break;
        case 5: if (event->kind==SCO_WIDGET_SLIDER && std::isfinite(event->value) && event->value>=100 && event->value<=20000) g.altitude=event->value; break;
        case 6: if (event->kind==SCO_WIDGET_BUTTON) {
#ifdef TRAVEL_TELEPORT
            GoSaved(nullptr,0,nullptr,reply,sizeof(reply));
#else
            if (g.selected_place>=0 && static_cast<size_t>(g.selected_place)<g.places.size()) {
                const std::string place=g.places[g.selected_place].entity; sco_arg pick[2]{};
                pick[0].type=SCO_ARG_STRING; pick[0].v.s=place.c_str(); pick[1].type=SCO_ARG_FLOAT; pick[1].v.f=g.altitude;
                TravelTo(pick,2,nullptr,reply,sizeof(reply));
            } else std::snprintf(reply,sizeof(reply),"Choose a destination first.");
#endif
            } break;
        case 15: if (event->kind==SCO_WIDGET_BUTTON) {
            if (g.selected_mark>=0 && static_cast<size_t>(g.selected_mark)<g.marks.size()) GoBookmarkAt(static_cast<size_t>(g.selected_mark),reply,sizeof(reply));
            else std::snprintf(reply,sizeof(reply),"Choose a saved spot first.");
            } break;
        case 16: if (event->kind==SCO_WIDGET_BUTTON) SaveSpot(nullptr,0,nullptr,reply,sizeof(reply)); break;
        case 7: if (event->kind==SCO_WIDGET_BUTTON) ScanCommand(nullptr,0,nullptr,reply,sizeof(reply)); break;
        case 8: if (event->kind==SCO_WIDGET_BUTTON) { name.v.s=g.bookmark_name.c_str(); SaveBookmark(&name,1,nullptr,reply,sizeof(reply)); } break;
        case 9: if (event->kind==SCO_WIDGET_BUTTON) {
            if (g.selected_mark>=0 && static_cast<size_t>(g.selected_mark)<g.marks.size()) DeleteBookmarkAt(static_cast<size_t>(g.selected_mark),reply,sizeof(reply));
            else std::snprintf(reply,sizeof(reply),"Choose a saved spot first.");
            } break;
        case 10: if (event->kind==SCO_WIDGET_BUTTON) { // no button on the page any more; kept for callers that still send it
#ifdef TRAVEL_TELEPORT
            InspectBookmark(nullptr,0,nullptr,reply,sizeof(reply));
#else
            if (g.selected_mark>=0 && static_cast<size_t>(g.selected_mark)<g.marks.size()) InspectBookmarkAt(static_cast<size_t>(g.selected_mark),reply,sizeof(reply));
#endif
            } break;
        case 11: if (event->kind==SCO_WIDGET_BUTTON && g.place_page) { --g.place_page; g.selected_place=-1; } break;
        case 12: if (event->kind==SCO_WIDGET_BUTTON && (g.place_page+1)*128<PlaceRows().size()) { ++g.place_page; g.selected_place=-1; } break;
        case 13: if (event->kind==SCO_WIDGET_BUTTON && g.mark_page) { --g.mark_page; g.selected_mark=-1; } break;
        case 14: if (event->kind==SCO_WIDGET_BUTTON && (g.mark_page+1)*128<g.marks.size()) { ++g.mark_page; g.selected_mark=-1; } break;
        default:
#ifdef TRAVEL_QUANTUM_CONTROLS
            sco::features::quantum::Event(event,sco::features::quantum::CallbackContext());
#endif
            break;
        }
        if (!g.plain.empty()) g.last_status=g.plain; else if (reply[0]) g.last_status=reply;
        Panel();
    } catch (...) { /* host callback never receives a C++ exception */ }
}
void Panel() {
    // set_panel failure retains the old callback. Pause it until a fresh tree is
    // accepted, so its row indices cannot address a changed collection.
    g.panel_cookie=0;
    const auto* widgets=Open<sco_widgets_v1>(SCO_WIDGETS_NAME);
    if (!TRAVEL_FN(widgets,sco_widgets_v1,set_panel) || !TRAVEL_FN(widgets,sco_widgets_v1,remove_panel)) return;
    std::vector<sco_widget_node> nodes;
    // Sections are COLUMNs (parent 0); everything else names its section or ROW as parent.
    auto add=[&](uint32_t kind,uint32_t node_id,const char* label,uint32_t parent=0) -> sco_widget_node& { sco_widget_node node{}; node.size=sizeof(node); node.kind=kind; node.id=node_id; node.parent=parent; node.index=-1; node.label=label; nodes.push_back(node); return nodes.back(); };
    const auto status=g.last_status.substr(0,255);
#ifdef TRAVEL_TELEPORT
    const bool saved=!g.saved.z.empty();
    add(SCO_WIDGET_COLUMN,20,"Save where you stand");
    add(SCO_WIDGET_LABEL,21,"Remember your position so you can come back to it later.",20);
    add(SCO_WIDGET_BUTTON,16,g.key_save?"Save this spot (F7)":"Save this spot",20);
    add(SCO_WIDGET_COLUMN,22,"Go back to it");
    add(SCO_WIDGET_LABEL,23,saved?"You have a spot saved. Saving again replaces it.":"Nothing is saved yet. Save a spot first.",22);
    auto& go=add(SCO_WIDGET_BUTTON,6,g.key_go?"Go to saved spot (F8)":"Go to saved spot",22); if (!saved) go.flags=SCO_WIDGET_DISABLED;
#else
    std::vector<std::string> places,marks; std::vector<const char*> place_items,mark_items;
    const auto rows=PlaceRows();
    if (g.place_page && g.place_page*128>=rows.size()) g.place_page=rows.empty()?0:static_cast<uint32_t>((rows.size()-1)/128);
    const size_t place_first=g.place_page*128; int32_t place_pick=-1;
    for (size_t i=place_first;i<rows.size() && places.size()<128;++i) {
        if (static_cast<int32_t>(rows[i])==g.selected_place) place_pick=static_cast<int32_t>(places.size());
        places.push_back(Display(g.places[rows[i]].system,g.places[rows[i]].name));
    }
    for (size_t i=g.mark_page*128;i<g.marks.size() && marks.size()<128;++i) marks.push_back(Display(g.marks[i].system,g.marks[i].name));
    for (const auto& text:places) place_items.push_back(text.c_str());
    for (const auto& text:marks) mark_items.push_back(text.c_str());
    const bool place_chosen=g.selected_place>=0 && static_cast<size_t>(g.selected_place)<g.places.size();
    const bool mark_chosen=g.selected_mark>=0 && static_cast<size_t>(g.selected_mark)<g.marks.size();

    add(SCO_WIDGET_COLUMN,20,"Choose a destination");
    add(SCO_WIDGET_TEXT_INPUT,17,"Search destinations",20).text=g.filter.c_str();
    if (rows.empty()) add(SCO_WIDGET_LABEL,41,g.places.empty()?"No destinations yet. Use Find nearby places to add some.":"No destination matches your search.",20);
    auto& p=add(SCO_WIDGET_LIST,2,"Destinations",20); p.items=place_items.data(); p.item_count=static_cast<uint32_t>(place_items.size()); p.index=place_pick;
    if (rows.size()>128) {
        add(SCO_WIDGET_ROW,18,"",20);
        add(SCO_WIDGET_BUTTON,11,"Previous page",18); add(SCO_WIDGET_BUTTON,12,"Next page",18);
    }
    add(SCO_WIDGET_BUTTON,7,"Find nearby places",20);

    add(SCO_WIDGET_COLUMN,21,"Set the height and go");
    auto& slider=add(SCO_WIDGET_SLIDER,5,"Arrival height (m)",21); slider.value=g.altitude; slider.minimum=100; slider.maximum=20000;
    if (!place_chosen) add(SCO_WIDGET_LABEL,42,"Choose a destination first.",21);
    auto& go=add(SCO_WIDGET_BUTTON,6,"Travel to selected destination",21); if (!place_chosen) go.flags=SCO_WIDGET_DISABLED;

    add(SCO_WIDGET_COLUMN,22,"Your saved spots");
    auto& m=add(SCO_WIDGET_LIST,3,"Saved spots",22); m.items=mark_items.data(); m.item_count=static_cast<uint32_t>(mark_items.size());
    if (g.selected_mark>=static_cast<int32_t>(g.mark_page*128) && static_cast<size_t>(g.selected_mark)<g.mark_page*128+marks.size()) m.index=g.selected_mark-static_cast<int32_t>(g.mark_page*128);
    if (g.marks.size()>128) {
        add(SCO_WIDGET_ROW,23,"",22);
        add(SCO_WIDGET_BUTTON,13,"Previous page",23); add(SCO_WIDGET_BUTTON,14,"Next page",23);
    }
    if (g.marks.empty()) add(SCO_WIDGET_LABEL,43,"Nothing saved yet. Save where you stand below.",22);
    else if (!mark_chosen) add(SCO_WIDGET_LABEL,43,"Choose a saved spot to go there or delete it.",22);
    add(SCO_WIDGET_ROW,24,"",22);
    { auto& b15=add(SCO_WIDGET_BUTTON,15,"Go to selected spot",24); if (!mark_chosen) b15.flags=SCO_WIDGET_DISABLED; }
    { auto& b9=add(SCO_WIDGET_BUTTON,9,"Delete selected spot",24); if (!mark_chosen) b9.flags=SCO_WIDGET_DISABLED; }
    add(SCO_WIDGET_ROW,25,"",22);
    add(SCO_WIDGET_TEXT_INPUT,4,"Name for the new spot (optional)",25).text=g.bookmark_name.c_str();
    add(SCO_WIDGET_BUTTON,8,"Save this spot",25);
#endif
#ifdef TRAVEL_QUANTUM_CONTROLS
    sco::features::quantum::AppendPanel(nodes);
    constexpr const char* title="Travel";
#else
    constexpr const char* title="Saved spots";
#endif
    add(SCO_WIDGET_COLUMN,30,"Status");
    add(SCO_WIDGET_LABEL,1,status.c_str(),30);
    if (next_panel_cookie==(std::numeric_limits<uintptr_t>::max)()) return;
    const auto cookie=++next_panel_cookie;
    if (widgets->set_panel(g.self,(std::string(id)+".travel").c_str(),title,nodes.data(),static_cast<uint32_t>(nodes.size()),sizeof(nodes[0]),PanelEvent,reinterpret_cast<void*>(cookie))==SCO_OK) {
        g.panel_cookie=cookie; g.panel=true;
    }
}
}
using namespace implementation;
template<sco_command_fn Fn> sco_result Guard(const sco_arg* args,uint32_t nargs,void* user,char* reply,uint32_t size) noexcept {
    if (!g.api || !g.self) return SCO_UNAVAILABLE;
    if (!reply && size) return SCO_BAD_ARG;
    try {
        g.plain.clear(); const auto r=Fn(args,nargs,user,reply,size);
        if (!g.plain.empty()) g.last_status=g.plain; else if (reply && size && reply[0]) g.last_status=reply;
        Panel(); return r;
    } catch (...) { return SCO_FAILED; }
}
inline const sco_plugin_info* Query() { return &info; }
inline sco_result Load(const sco_api* api,sco_plugin* self) {
    if (!api || !self || g.api) return SCO_BAD_ARG;
    if (!TRAVEL_FN(api,sco_api,register_command) || !TRAVEL_FN(api,sco_api,query_service) || api->major!=SCO_API_MAJOR || api->minor<SCO_API_MINOR) return SCO_UNAVAILABLE;
    g.api=api; g.self=self;
    try {
        g.root=Setting("data_root","data");
        std::string inherited;
#ifdef _MSC_VER
        { char* v=nullptr; size_t n=0; if (_dupenv_s(&v,&n,"SC_OFFLINE_SPAWN_FILE")==0 && v) inherited=v; std::free(v); }
#else
        if (const char* v=std::getenv("SC_OFFLINE_SPAWN_FILE")) inherited=v;
#endif
        g.spawn=Setting("spawn_file",!inherited.empty() ? inherited : (g.root/"spawn.txt").string());
        Storage();
#ifdef TRAVEL_TELEPORT
        const auto loaded=LoadSaved();
#else
        const auto curated=LoadCuratedPlaces();
        if(curated!=SCO_OK) Reply(curated,nullptr,0,g.curated_status,"The list of destinations could not be loaded.");
        g.last_status=curated==SCO_OK ? (g.places.empty() ? "The destination list is empty. Use Find nearby places to add some." : "Loaded "+Count(g.places.size(),"destination")+".") :
            "No destination list is available. Use Find nearby places to add some.";
        LoadPlaces(g.root/"locations_found.txt"); const auto loaded=LoadMarks();
#endif
        if (loaded!=SCO_OK) Reply(loaded,nullptr,0,"Legacy state import/read failed; prior database retained","Saved spots from an older version could not be read.");
        const sco_arg_def name{"name",SCO_ARG_STRING,0,"Name of the saved spot"};
#ifndef TRAVEL_TELEPORT
        const sco_arg_def dest[]={{"place",SCO_ARG_STRING,0,"Destination name"},{"altitude",SCO_ARG_FLOAT,0,"Height above the destination in metres (100 to 20000)"}};
#endif
        const sco_command commands[]={
#ifdef TRAVEL_TELEPORT
            {sizeof(sco_command),0,"teleport.save","Save this spot","Save where you are, to come back to it later (F7)",nullptr,nullptr,0,sizeof(sco_arg_def),Guard<SaveSpot>,nullptr},
            {sizeof(sco_command),0,"teleport.go","Go to saved spot","Go back to the spot you saved (F8). Offline only, within the same star system",nullptr,nullptr,0,sizeof(sco_arg_def),Guard<GoSaved>,nullptr},
            {sizeof(sco_command),0,"teleport.inspect","Check saved spot","Check whether the saved spot can be reached, without moving",nullptr,nullptr,0,sizeof(sco_arg_def),Guard<InspectBookmark>,nullptr},
            {sizeof(sco_command),0,"teleport.status","Saved spot status","Whether a spot is saved and whether moving is allowed",nullptr,nullptr,0,sizeof(sco_arg_def),Guard<Status>,nullptr}
#else
            {sizeof(sco_command),0,"quantum.travel","Travel","Go to a destination. Offline only, within the same star system",nullptr,dest,2,sizeof(sco_arg_def),Guard<TravelTo>,nullptr},
            {sizeof(sco_command),0,"quantum.bookmark","Go to saved spot","Go to a saved spot. Offline only, within the same star system",nullptr,&name,1,sizeof(sco_arg_def),Guard<GoBookmark>,nullptr},
            {sizeof(sco_command),0,"quantum.save_bookmark","Save this spot","Save where you are under a name",nullptr,&name,1,sizeof(sco_arg_def),Guard<SaveBookmark>,nullptr},
            {sizeof(sco_command),0,"quantum.delete_bookmark","Delete saved spot","Remove a saved spot",nullptr,&name,1,sizeof(sco_arg_def),Guard<DeleteBookmark>,nullptr},
            {sizeof(sco_command),0,"quantum.inspect_bookmark","Check saved spot","Check whether a saved spot can be reached, without moving",nullptr,&name,1,sizeof(sco_arg_def),Guard<InspectBookmark>,nullptr},
            {sizeof(sco_command),0,"quantum.scan","Find nearby places","Add the places the game has loaded around you to the destination list",nullptr,nullptr,0,sizeof(sco_arg_def),Guard<ScanCommand>,nullptr},
            {sizeof(sco_command),0,"quantum.status","Travel status","How many destinations and saved spots you have, and whether moving is allowed",nullptr,nullptr,0,sizeof(sco_arg_def),Guard<Status>,nullptr}
#endif
        };
        for (const auto& c:commands) { const auto r=api->register_command(self,&c); if (r!=SCO_OK) { g={}; return r; } }
#ifdef TRAVEL_TELEPORT
        (void)name;
        const auto* ui=Open<sco_ui_v1>(SCO_UI_NAME);
        if (TRAVEL_FN(ui,sco_ui_v1,bind_hotkey) && TRAVEL_FN(ui,sco_ui_v1,unbind_hotkey)) {
            const auto f7=ui->bind_hotkey(self,"f7","teleport.save",nullptr,0);
            const auto f8=ui->bind_hotkey(self,"f8","teleport.go",nullptr,0);
            g.key_save=f7==SCO_OK; g.key_go=f8==SCO_OK;
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
    if (g.ticking && TRAVEL_FN(g.api,sco_api,unsubscribe)) g.api->unsubscribe(g.self,"tick",TravelTick);
    } catch (...) {}
    g={}; // game.relocation cancels and forgets this owner's tickets on unload; host removes commands.
}
}
