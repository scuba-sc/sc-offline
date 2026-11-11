// SPDX-License-Identifier: GPL-3.0-only
// Actual module, synthetic public providers; NOT Windows/game/effects evidence.
#include <sco_api.h>
#include <sc_quantum.h>
#include <sc_gamerules.h>
#include <sco_widgets.h>
#include <sco_settings.h>
#include <sco_content.h>
#include <sc_game_spatial.h>
#include <sc_relocation.h>
#include <sc_spatial.h>
#include <sco_storage.h>
#include <array>
#include <iterator>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "quantum_test_directory.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace {
std::map<std::string,sco_command> commands;
std::vector<std::pair<sco_event_fn,void*>> ticks; // the real host allows one subscription per fn
sco_widget_event_fn event=nullptr; void* event_ctx=nullptr;
std::vector<std::string> bookmark_labels;
int32_t selected_bookmark=-1;
std::string panel_status;
sco_result panel_result=SCO_OK,request_result=SCO_OK;
bool relocation_available=true,zone_available=true;
std::vector<std::string> resolved_zones;
struct Callback { sco_widget_event_fn fn; void* ctx; };
Callback CurrentPanel() { return {event,event_ctx}; }
void Send(Callback target,uint32_t node,uint32_t kind=SCO_WIDGET_BUTTON,int32_t index=-1) {
    assert(target.fn); sco_widget_event e{sizeof(e),node,kind,index,0,nullptr};
    target.fn("quantum.travel",&e,target.ctx);
}
void Send(uint32_t node,uint32_t kind=SCO_WIDGET_BUTTON,int32_t index=-1) { Send(CurrentPanel(),node,kind,index); }
void SelectBookmark(int32_t index) { Send(3,SCO_WIDGET_LIST,index); }
bool capability=false, enabled=false, held=false, available=true;
int enables=0,holds=0,releases=0,panels=0; uint32_t duration=0;
const void* service=nullptr;
bool relocation_cap=false; int32_t allow=1; std::string data_root,last_status;
int requests=0; uint32_t reloc_state=SC_RELOCATION_PENDING; sc_relocation_request last_request{};
int Has(const char* name) { return !std::strcmp(name,"game.gamerules.read") || (capability && !std::strcmp(name,"game.quantum.control")) || (relocation_cap && !std::strcmp(name,SC_RELOCATION_NAME)); }
sco_result GetBool(sco_plugin*,const char* name,int32_t* out) { if (std::strcmp(name,"relocation")) return SCO_NOT_FOUND; *out=allow; return SCO_OK; }
sco_result GetString(sco_plugin*,const char* name,char* out,uint32_t* n) {
    if (std::strcmp(name,"data_root") || data_root.empty()) return SCO_NOT_FOUND;
    if (*n<data_root.size()+1) { *n=static_cast<uint32_t>(data_root.size()+1); return SCO_TOO_MANY; }
    std::memcpy(out,data_root.c_str(),data_root.size()+1); *n=static_cast<uint32_t>(data_root.size()+1); return SCO_OK;
}
sco_settings_v1 settings{sizeof(settings),0,GetBool,nullptr,nullptr,GetString,nullptr};
const char* curated="Stanton | Port | port_zone | 10\n";
sco_result ContentItem(sco_plugin*,sco_content_kind kind,uint32_t ordinal,sco_content_item* out) {
    assert(kind==SCO_CONTENT_LIST); if(ordinal)return SCO_NOT_FOUND;
    *out={};out->size=sizeof(*out);out->kind=kind;out->id=1;
    std::snprintf(out->pack,sizeof(out->pack),"synthetic_locations");std::snprintf(out->name,sizeof(out->name),"lists/locations.txt");return SCO_OK;
}
sco_result ContentRead(sco_plugin*,uint64_t id,void* out,uint32_t* size) {
    assert(id==1);const auto bytes=static_cast<uint32_t>(std::strlen(curated));
    if(*size<bytes){*size=bytes;return SCO_TOO_MANY;}
    std::memcpy(out,curated,bytes);*size=bytes;return SCO_OK;
}
sco_content_v1 content{sizeof(content),0,ContentItem,ContentRead};
int ZoneRecord(uint64_t id,sc_game_zone* z) {
    if (!zone_available || (id!=1 && id!=2 && id!=3 && id!=77)) return 0;
    const char* name=id==1?"zone_a":id==2?"zone_b":id==3?"root":"port_zone";
    *z={sizeof(*z),SC_GAME_ZONE_SYSTEM_UNKNOWN,5,id,id<3?3u:0u,id<3?3u:id,0,{}};
    std::snprintf(z->name,sizeof(z->name),"%s",name); return 1;
}
int FindZone(const char* name,sc_game_zone* z) {
    resolved_zones.emplace_back(name);
    for (uint64_t id:{1u,2u,3u,77u}) { sc_game_zone record{};
        if (ZoneRecord(id,&record) && !std::strcmp(name,record.name)) { *z=record; return 1; }
    }
    return 0;
}
int PlayerPose(double* p,double* q,uint64_t* zone) { p[0]=30;p[1]=p[2]=0;q[0]=q[1]=q[2]=0;q[3]=1;*zone=1;return 1; }
int PoseToZone(uint64_t,uint64_t,const double* p,const double* q,double* out,double* rot) {
    std::memcpy(out,p,3*sizeof(double));std::memcpy(rot,q,4*sizeof(double));return 1;
}
sc_spatial_v1 player{};
sc_game_spatial_v1 spatial{};
sco_result RelRequest(sco_plugin*,const sc_relocation_request* r,uint64_t* t) { ++requests; assert(r && r->size==sizeof(*r)); last_request=*r; if (request_result!=SCO_OK) return request_result; *t=9; reloc_state=SC_RELOCATION_PENDING; return SCO_OK; }
sco_result RelCancel(sco_plugin*,uint64_t) { return SCO_OK; }
sco_result RelStatus(sco_plugin*,uint64_t t,sc_relocation_status* s) { if (t!=9) return SCO_NOT_FOUND; s->state=reloc_state; s->ticket=t; s->applied=reloc_state==SC_RELOCATION_COMPLETE; return SCO_OK; }
sc_relocation_v1 relocation{sizeof(relocation),RelRequest,RelCancel,RelStatus};
void Status(sco_plugin*,const char* message) { last_status=message; }
sco_result Register(sco_plugin*,const sco_command* c) { assert(c && c->size==sizeof(*c)); commands[c->name]=*c; return SCO_OK; }
sco_result Subscribe(sco_plugin*,const char* name,sco_event_fn fn,void* ctx) {
    assert(!std::strcmp(name,"tick") && fn);
    for (const auto& t:ticks) assert(t.first!=fn);
    ticks.emplace_back(fn,ctx); return SCO_OK;
}
sco_result Unsubscribe(sco_plugin*,const char* name,sco_event_fn fn) {
    assert(!std::strcmp(name,"tick"));
    for (auto it=ticks.begin();it!=ticks.end();++it) if (it->first==fn) { ticks.erase(it); return SCO_OK; }
    assert(!"unsubscribe of an unknown tick fn"); return SCO_NOT_FOUND;
}
void Tick(uint32_t now) { const auto copy=ticks; for (const auto& t:copy) t.first("tick",&now,t.second); }
sco_result Enable(sco_plugin*,uint32_t on) { ++enables; assert(on<=1); if (on && !available) return SCO_UNAVAILABLE; enabled=on!=0; if (!on) held=false; return SCO_OK; }
sco_result Hold(sco_plugin*,uint32_t ms) { ++holds; assert(ms>=1 && ms<=1000); if (!enabled || !available) return SCO_UNAVAILABLE; held=true; duration=ms; return SCO_OK; }
sco_result Release(sco_plugin*) { ++releases; held=false; return enabled ? SCO_OK:SCO_NOT_FOUND; }
sco_result State(sco_plugin*,sc_quantum_state* s) { assert(s->size==sizeof(*s)); if (!available) return SCO_UNAVAILABLE; *s={sizeof(*s),enabled?1u:0u,held?1u:0u,SC_QUANTUM_CHARGING,0,250000,1}; return SCO_OK; }
sc_quantum_v1 q{sizeof(q),0,Enable,Hold,Release,State};
sco_result Rules(sco_plugin*,sc_gamerules_state* out) { *out={};out->size=sizeof(*out);out->valid=1;out->quantum_travel_allowed=1;out->quantum_boost_allowed=1;return SCO_OK; }
sc_gamerules_v1 rules{sizeof(rules),0,Rules,nullptr};
sco_result Panel(sco_plugin*,const char* id,const char* title,const sco_widget_node* nodes,uint32_t n,uint32_t stride,sco_widget_event_fn fn,void* ctx) {
    assert(!std::strcmp(id,"quantum.travel") && !std::strcmp(title,"Travel")); assert(stride==sizeof(*nodes));
    if (panel_result!=SCO_OK) return panel_result; // public contract retains the prior panel/callback
    bool bookmarks=false,control=false;
    for (uint32_t i=0;i<n;++i) {
        bookmarks |= nodes[i].id==3; control |= nodes[i].id==102;
        if (nodes[i].id==1) panel_status=nodes[i].label;
        if (nodes[i].id==3) {
            bookmark_labels.clear(); selected_bookmark=nodes[i].index;
            for (uint32_t j=0;j<nodes[i].item_count;++j) bookmark_labels.emplace_back(nodes[i].items[j]);
        }
    }
    assert(bookmarks && control); ++panels; event=fn; event_ctx=ctx; return SCO_OK;
}
sco_result Remove(sco_plugin*,const char* id) { assert(!std::strcmp(id,"quantum.travel")); event=nullptr; event_ctx=nullptr; return SCO_OK; }
sco_widgets_v1 widgets{};
// Minimal transactional public storage stand-in; never links host-private code.
using StoredRow=std::array<std::string,3>;
bool storage_enabled=false,transaction=false;
sco_result begin_result=SCO_OK,exec_result=SCO_OK,commit_result=SCO_OK;
unsigned rollbacks=0; size_t row_index=0;
std::vector<StoredRow> durable_rows,working_rows;
sco_result put_result=SCO_OK; std::string durable_marker,working_marker;
sco_result StorePut(sco_plugin*,const char* key,const void* data,uint32_t n) { assert(transaction&&!std::strcmp(key,"bookmarks.txt.time"));if(put_result!=SCO_OK)return put_result;working_marker.assign(static_cast<const char*>(data),n);return SCO_OK; }
sco_result StoreGet(sco_plugin*,const char* key,void* out,uint32_t* n) { assert(!std::strcmp(key,"bookmarks.txt.time"));if(durable_marker.empty())return SCO_NOT_FOUND;if(*n<durable_marker.size()){*n=static_cast<uint32_t>(durable_marker.size());return SCO_TOO_MANY;}std::memcpy(out,durable_marker.data(),durable_marker.size());*n=static_cast<uint32_t>(durable_marker.size());return SCO_OK; }
sco_result StoreBegin(sco_plugin*) { if(begin_result!=SCO_OK)return begin_result;assert(!transaction);working_rows=durable_rows;working_marker=durable_marker;transaction=true;return SCO_OK; }
sco_result StoreCommit(sco_plugin*) {assert(transaction);if(commit_result!=SCO_OK)return commit_result;durable_rows=working_rows;durable_marker=working_marker;transaction=false;return SCO_OK;}
sco_result StoreRollback(sco_plugin*) {assert(transaction);transaction=false;working_rows.clear();++rollbacks;return SCO_OK;}
sco_result StoreExec(sco_plugin*,const char* sql,const sco_sql_value* values,uint32_t n,int64_t*) {
    if(exec_result!=SCO_OK)return exec_result;
    if(!std::strncmp(sql,"CREATE TABLE",12))return SCO_OK;
    assert(transaction);
    if(!std::strcmp(sql,"DELETE FROM bookmarks")){working_rows.clear();return SCO_OK;}
    assert(!std::strncmp(sql,"INSERT INTO bookmarks",21)&&n==4&&values[0].type==SCO_SQL_INT);
    StoredRow row;for(uint32_t i=0;i<3;++i){assert(values[i+1].type==SCO_SQL_TEXT);row[i].assign(static_cast<const char*>(values[i+1].v.p),values[i+1].size);}
    working_rows.push_back(std::move(row));return SCO_OK;
}
sco_result StoreQuery(sco_plugin*,const char*,const sco_sql_value*,uint32_t,uint64_t* cursor){row_index=0;*cursor=1;return SCO_OK;}
sco_result StoreStep(sco_plugin*,uint64_t cursor){assert(cursor==1);if(row_index==durable_rows.size())return SCO_NOT_FOUND;++row_index;return SCO_OK;}
sco_result StoreColumn(sco_plugin*,uint64_t cursor,uint32_t index,sco_sql_value* value,void* out,uint32_t* n){
    assert(cursor==1&&row_index&&row_index<=durable_rows.size()&&index<3);
    const auto& text=durable_rows[row_index-1][index];*value={};value->type=SCO_SQL_TEXT;value->size=static_cast<uint32_t>(text.size());
    if(!n)return SCO_OK;
    const auto bytes=static_cast<uint32_t>(text.size()+1);
    if(*n<bytes){*n=bytes;return SCO_TOO_MANY;}std::memcpy(out,text.c_str(),bytes);*n=bytes;return SCO_OK;
}
sco_result StoreClose(sco_plugin*,uint64_t){return SCO_OK;}
sco_storage_v1 storage{};
sco_result Query(const char* name,uint32_t version,const void** out) {
    assert(version==0x10000u); *out=nullptr;
    if (!std::strcmp(name,SC_QUANTUM_SERVICE_NAME)) *out=service;
    if (!std::strcmp(name,SC_GAMERULES_SERVICE_NAME)) *out=&rules;
    if (!std::strcmp(name,SCO_WIDGETS_NAME)) *out=&widgets;
    if (!std::strcmp(name,SCO_SETTINGS_NAME)) *out=&settings;
    if (!std::strcmp(name,SCO_CONTENT_NAME)) *out=&content;
    if (!std::strcmp(name,SC_GAME_SPATIAL_NAME)) *out=&spatial;
    if (!std::strcmp(name,SC_SPATIAL_SERVICE_NAME)) *out=&player;
    if (!std::strcmp(name,SCO_STORAGE_NAME) && storage_enabled) *out=&storage;
    if (!std::strcmp(name,SC_RELOCATION_NAME) && relocation_cap && relocation_available) *out=&relocation;
    return *out ? SCO_OK:SCO_NOT_FOUND;
}
sco_result Call(const char* name,const sco_arg* args=nullptr,uint32_t n=0) { char reply[512]{}; assert(commands.count(name)); const auto& c=commands.at(name); return c.fn(args,n,c.ctx,reply,sizeof(reply)); }
std::string FileBytes(const std::filesystem::path& file) {std::ifstream in(file,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void BookmarkWorkflow(sco_result(*load)(const sco_api*,sco_plugin*),void(*unload)(),const sco_api& api,sco_plugin* self,const std::filesystem::path& root) {
    const auto file=root/"bookmarks.txt";
    const std::string first="bookmark A|Base\nspot 11 0 0 zone_a\n";
    const std::string second="bookmark B|base\nspot 22 0 0 zone_b\n";
    auto reload=[&]{unload();commands.clear();assert(load(&api,self)==SCO_OK);};
    auto seed=[&](const std::string& bytes){unload();commands.clear();std::ofstream(file,std::ios::binary)<<bytes;assert(load(&api,self)==SCO_OK);};
    auto done=[&]{reloc_state=SC_RELOCATION_COMPLETE;Tick(100);};
    auto go=[&](int32_t index,double x){SelectBookmark(index);const auto before=requests;Send(15);assert(requests==before+1&&last_request.pos[0]==x);done();};
    relocation_cap=true;zone_available=true;
    spatial.zone_record=ZoneRecord;spatial.pose_to_zone=PoseToZone;
    player.size=sizeof(player);player.player_pose=PlayerPose;
    const std::string crlf="bookmark A|Base\r\nspot 11 0 0 zone_a\r\nbookmark B|base\r\nspot 22 0 0 zone_b\r\n";
    const std::string mixed="bookmark A|Base\r\nspot 11 0 0 zone_a\nbookmark B|base\nspot 22 0 0 zone_b\r\n";
    for(const auto& bytes:{first+second,crlf,mixed}) {
        seed(bytes);assert(bookmark_labels.size()==2&&bookmark_labels[0]=="A / Base"&&bookmark_labels[1]=="B / base");
        go(0,11);go(1,22);SelectBookmark(1);Send(9);assert(bookmark_labels.size()==1);go(0,11);
    }
    // A bare CR remains data, including at EOF. Invalid poses keep their existing rejection behavior.
    seed("bookmark A|bad\nspot 1 0 0 zone_a\npose 0 0 0 0\r\nbookmark B|tail\nspot 2 0 0 zone_b\r");
    assert(bookmark_labels.size()==1&&bookmark_labels[0]=="B / tail");
    SelectBookmark(0);resolved_zones.clear();const auto controlRequests=requests;Send(15);
    assert(requests==controlRequests&&!resolved_zones.empty()&&resolved_zones.front()=="zone_b\r");
    seed("bookmark B|tail\r\nspot 2 0 0 zone_b\r\r\n");SelectBookmark(0);resolved_zones.clear();Send(15);
    assert(requests==controlRequests&&!resolved_zones.empty()&&resolved_zones.front()=="zone_b\r");
    seed("bookmark A|bare\rname\nspot 1 0 0 zone_a\n");assert(bookmark_labels.size()==1&&bookmark_labels[0]=="A / bare\rname");
    const std::string system31(31,'s'),name63(63,'n');
    seed("bookmark "+system31+"|"+name63+"\r\nspot 11 0 0 zone_a\r\n"+
         "bookmark "+system31+"s|oversize\r\nspot 11 0 0 zone_a\r\n"+
         "bookmark A|"+name63+"n\r\nspot 11 0 0 zone_a\r\n"+
         "bookmark B|last\nspot 22 0 0 zone_b");
    assert(bookmark_labels.size()==2&&bookmark_labels[0]==system31+" / "+name63&&bookmark_labels[1]=="B / last");go(1,22);
    const std::string validSpot="spot 11 0 0 zone_a\n";
    seed("bookmark A|bound\n"+validSpot+"#"+std::string(65535-validSpot.size()-2,'x')+"\r");assert(bookmark_labels.size()==1);
    seed("bookmark A|bound\n"+validSpot+"#"+std::string(65536-validSpot.size()-2,'x')+"\r");assert(bookmark_labels.empty());
    seed(first+second);assert(bookmark_labels.size()==2&&bookmark_labels[1]=="B / base");
    go(1,22); // regression: the old panel silently moved to the first same-name row
    go(0,11);
    SelectBookmark(1);resolved_zones.clear();const auto beforeInspect=requests;Send(10);
    assert(requests==beforeInspect&&!resolved_zones.empty()&&resolved_zones.front()=="zone_b");
    sco_arg name{};name.type=SCO_ARG_STRING;name.v.s="BASE";
    assert(Call("quantum.bookmark",&name,1)==SCO_OK&&last_request.pos[0]==11);done();
    resolved_zones.clear();assert(Call("quantum.inspect_bookmark",&name,1)==SCO_OK&&resolved_zones.front()=="zone_a");
    // Saving remains append-only: duplicate names and the existing schema/format survive.
    assert(Call("quantum.save_bookmark",&name,1)==SCO_OK&&bookmark_labels.size()==3&&selected_bookmark==-1);
    go(2,30);SelectBookmark(1);Send(9);
    assert(bookmark_labels.size()==2&&bookmark_labels[0]=="A / Base"&&bookmark_labels[1]=="Unknown system / BASE");
    const auto persisted=FileBytes(file);assert(persisted.find("spot 22.")==std::string::npos&&persisted.find("spot 11.")!=std::string::npos);
    reload();go(1,30);
    assert(Call("quantum.delete_bookmark",&name,1)==SCO_OK&&bookmark_labels.size()==1&&bookmark_labels[0]=="Unknown system / BASE");
    // Provider and capability refusal must never substitute another bookmark.
    seed(first+second);SelectBookmark(1);auto before=requests;relocation_cap=false;Send(15);assert(requests==before);relocation_cap=true;
    allow=0;Send(15);assert(requests==before);allow=1;
    relocation_available=false;Send(15);assert(requests==before);relocation_available=true;
    const auto savedRequest=relocation.request;relocation.request=nullptr;Send(15);assert(requests==before);relocation.request=savedRequest;
    const auto savedSize=relocation.size;relocation.size=offsetof(sc_relocation_v1,request)+sizeof(relocation.request)-1;Send(15);assert(requests==before);relocation.size=savedSize;
    zone_available=false;resolved_zones.clear();Send(10);Send(15);assert(requests==before&&!resolved_zones.empty()&&resolved_zones.front()=="zone_b");zone_available=true;
    const auto savedRecord=spatial.zone_record;spatial.zone_record=nullptr;Send(10);Send(15);assert(requests==before);spatial.zone_record=savedRecord;
    request_result=SCO_FAILED;Send(15);assert(requests==before+1&&last_request.pos[0]==22);request_result=SCO_OK;go(1,22);
    // Invalid selection indices clear selection; a later append cannot make a forged row live.
    for(int32_t index:{-1,2,127,128}){SelectBookmark(index);before=requests;resolved_zones.clear();Send(15);Send(10);Send(9);assert(requests==before&&resolved_zones.empty()&&bookmark_labels.size()==2);}
    SelectBookmark(1);const auto stale=CurrentPanel();assert(Call("quantum.delete_bookmark",&name,1)==SCO_OK&&selected_bookmark==-1);
    before=requests;Send(stale,3,SCO_WIDGET_LIST,0);Send(stale,15);Send(stale,9);Send(stale,10);assert(requests==before&&bookmark_labels.size()==1);
    Send(15);assert(requests==before); // fresh panel has no selection after collection mutation
    // Failed persistence retains both rows and the selected duplicate for an explicit retry.
    seed(first+second);SelectBookmark(1);const auto original=FileBytes(file);std::ofstream(file.string()+".new")<<"occupied";
    Send(9);assert(bookmark_labels.size()==2&&selected_bookmark==1&&FileBytes(file)==original&&panel_status.find("Delete failed")!=std::string::npos);
    std::filesystem::remove(file.string()+".new");Send(9);assert(bookmark_labels.size()==1&&bookmark_labels[0]=="A / Base");
    // A failed set_panel leaves the old callback installed under the public contract.
    // It must remain inert until publication succeeds, even after data changes.
    seed(first+second);SelectBookmark(1);const auto retained=CurrentPanel();panel_result=SCO_FAILED;
    assert(Call("quantum.delete_bookmark",&name,1)==SCO_OK);before=requests;
    Send(retained,3,SCO_WIDGET_LIST,0);Send(retained,15);Send(retained,9);Send(retained,10);assert(requests==before&&FileBytes(file).find("zone_b")!=std::string::npos);
    panel_result=SCO_OK;assert(Call("quantum.status")==SCO_OK&&bookmark_labels.size()==1&&selected_bookmark==-1);go(0,22);
    // Only the current page's actual items can be selected; removing its last row returns to page0.
    std::string many;for(unsigned i=0;i<129;++i)many+="bookmark A|same\nspot "+std::to_string(i)+" 0 0 zone_a\n";
    seed(many);Send(14);assert(bookmark_labels.size()==1);go(0,128);SelectBookmark(1);before=requests;Send(15);assert(requests==before);
    SelectBookmark(0);Send(9);assert(bookmark_labels.size()==128&&selected_bookmark==-1);go(127,127);
    const auto oldSession=CurrentPanel();reload();before=requests;Send(oldSession,3,SCO_WIDGET_LIST,0);Send(oldSession,9);Send(oldSession,15);assert(requests==before&&bookmark_labels.size()==128);
    // Public transactional storage: begin/write/commit failures preserve the selected row and durable order.
    storage.size=sizeof(storage);storage.put=StorePut;storage.get=StoreGet;storage.begin=StoreBegin;storage.commit=StoreCommit;storage.rollback=StoreRollback;
    storage.exec=StoreExec;storage.query=StoreQuery;storage.step=StoreStep;storage.column=StoreColumn;storage.close=StoreClose;
    // Failed CRLF imports leave the previous rows, marker and source intact; a retry imports both duplicates.
    durable_rows={{"Old","kept","spot 9 0 0 zone_a\n"}};durable_marker="old-marker";storage_enabled=true;
    for(int failure=0;failure<4;++failure) {
        begin_result=failure==0?SCO_FAILED:SCO_OK;exec_result=failure==1?SCO_FAILED:SCO_OK;
        put_result=failure==2?SCO_FAILED:SCO_OK;commit_result=failure==3?SCO_FAILED:SCO_OK;
        const auto previousRows=durable_rows;const auto previousMarker=durable_marker;const auto previousRollbacks=rollbacks;
        seed(crlf);assert(durable_rows==previousRows&&durable_marker==previousMarker&&FileBytes(file)==crlf&&!transaction);
        assert(rollbacks==previousRollbacks+(failure==0?0u:1u));
    }
    begin_result=exec_result=put_result=commit_result=SCO_OK;reload();
    assert(durable_rows.size()==2&&durable_rows[0][1]=="Base"&&durable_rows[1][1]=="base"&&durable_marker!="old-marker"&&FileBytes(file)==crlf);
    go(0,11);go(1,22);
    durable_rows[0][2]="spot 11 0 0 zone_a\r\npose 0 0 0 1\r\n";
    durable_rows[1][2]="spot 22 0 0 zone_b\r\npose 0 0 0 1\n";
    const auto importedMarker=durable_marker,sqlSpot=durable_rows[0][2];reload();
    assert(durable_marker==importedMarker&&durable_rows[0][2]==sqlSpot);go(0,11);go(1,22);
    std::filesystem::remove(file);reload();go(0,11);go(1,22);
    durable_marker.clear();durable_rows.clear();
    storage_enabled=true;seed(first+second);assert(durable_rows.size()==2);SelectBookmark(1);
    begin_result=SCO_UNAVAILABLE;Send(9);assert(durable_rows.size()==2&&selected_bookmark==1&&!transaction);begin_result=SCO_OK;
    auto rollbackCount=rollbacks;exec_result=SCO_FAILED;Send(9);assert(durable_rows.size()==2&&selected_bookmark==1&&rollbacks==rollbackCount+1&&!transaction);exec_result=SCO_OK;
    rollbackCount=rollbacks;commit_result=SCO_FAILED;Send(9);assert(durable_rows.size()==2&&selected_bookmark==1&&rollbacks==rollbackCount+1&&!transaction);commit_result=SCO_OK;
    Send(9);assert(durable_rows.size()==1&&durable_rows[0][1]=="Base"&&durable_rows[0][2].find("spot 11.")!=std::string::npos&&selected_bookmark==-1);
    assert(FileBytes(file)==first+second); // SQL mode never substitutes a filesystem mutation
    std::filesystem::remove(file);reload();assert(bookmark_labels.size()==1);go(0,11);
    unload();storage_enabled=false;
    std::puts("quantum bookmark rows PASS: duplicate Go/Delete/Inspect, command compatibility, append/reload, paging, stale callbacks, failed publication, storage rollback and provider refusal; synthetic only");
}

}
int main(int argc,char** argv) {
    assert(argc==2);
#ifdef _WIN32
    const auto module=LoadLibraryA(argv[1]); assert(module);
    const auto load=reinterpret_cast<sco_result(*)(const sco_api*,sco_plugin*)>(GetProcAddress(module,"sco_plugin_load"));
    const auto unload=reinterpret_cast<void(*)()>(GetProcAddress(module,"sco_plugin_unload"));
#else
    void* module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); if (!module) std::fprintf(stderr,"%s\n",dlerror()); assert(module);
    const auto load=reinterpret_cast<sco_result(*)(const sco_api*,sco_plugin*)>(dlsym(module,"sco_plugin_load"));
    const auto unload=reinterpret_cast<void(*)()>(dlsym(module,"sco_plugin_unload"));
#endif
    assert(load && unload);
    sco_api api{}; api.size=sizeof(api); api.major=SCO_API_MAJOR; api.minor=SCO_API_MINOR;
    api.has=Has; api.query_service=Query; api.register_command=Register; api.subscribe=Subscribe; api.unsubscribe=Unsubscribe; api.status=Status;
    spatial.size=sizeof(spatial); spatial.find_zone=FindZone;
    widgets.size=sizeof(widgets);widgets.set_panel=Panel;widgets.remove_panel=Remove;
    static QuantumTestDirectory directory("sco-quantum-workflow-test");
    const auto root=directory.path(); data_root=root.string();
    { std::ofstream(root/"locations.txt")<<"Raw | Ignored base | wrong_zone | 999\n"; }
    int identity=1; auto* self=reinterpret_cast<sco_plugin*>(&identity);
    assert(load(&api,self)==SCO_OK && panels && ticks.size()==1 && enables==0 && holds==0);
    for (const auto* name:{"quantum.travel","quantum.bookmark","quantum.save_bookmark","quantum.scan","quantum.status","quantum.enable","quantum.hold","quantum.release","quantum.boost","quantum.control_status"}) assert(commands.count(name));
    sco_arg on{}; on.type=SCO_ARG_BOOL; on.v.i=1;
    assert(Call("quantum.enable",&on,1)==SCO_UNAVAILABLE && enables==0);
    // game.relocation: switch, then capability, both before arguments; one tracked request; terminal state reported once.
    assert(Call("quantum.travel")==SCO_UNAVAILABLE && Call("quantum.bookmark")==SCO_UNAVAILABLE && !requests); // capability absent
    relocation_cap=true;
    assert(Call("quantum.travel")==SCO_BAD_ARG && !requests); // capability on: arguments are checked, nothing requested
    allow=0; assert(Call("quantum.travel")==SCO_UNAVAILABLE && Call("quantum.bookmark")==SCO_UNAVAILABLE && !requests); allow=1; // player switch off
    sco_arg trip[2]{}; trip[0].type=SCO_ARG_STRING; trip[0].v.s="Port"; trip[1].type=SCO_ARG_FLOAT; trip[1].v.f=99;
    assert(Call("quantum.travel",trip,2)==SCO_BAD_ARG && !requests); trip[1].v.f=500;
    trip[0].v.s="Nowhere"; assert(Call("quantum.travel",trip,2)==SCO_NOT_FOUND && !requests); trip[0].v.s="Port";
    assert(Call("quantum.travel",trip,2)==SCO_OK && requests==1 && ticks.size()==2);
    assert(last_request.zone_id==77 && last_request.pos[2]==510 && last_request.rot_xyzw[3]==1 && last_request.timeout_ms>=1 && last_request.timeout_ms<=60000);
    assert(Call("quantum.travel",trip,2)==SCO_TOO_MANY && requests==1); // one active request
    uint32_t now=100; Tick(now); assert(ticks.size()==2 && last_status.empty()); // pending: still tracked
    reloc_state=SC_RELOCATION_COMPLETE; Tick(now); assert(ticks.size()==1 && !last_status.empty()); // reported once, then untracked
    last_status.clear(); assert(Call("quantum.travel",trip,2)==SCO_OK && requests==2 && ticks.size()==2);
    reloc_state=SC_RELOCATION_FAILED; Tick(now); assert(ticks.size()==1 && !last_status.empty());
    relocation_cap=false; assert(Call("quantum.travel",trip,2)==SCO_UNAVAILABLE && requests==2); // re-checked per command
    on.v.i=2; assert(Call("quantum.enable",&on,1)==SCO_BAD_ARG); on.v.i=1;
    capability=true;
    std::array<unsigned char,8> short_table{}; const uint32_t short_size=8; std::memcpy(short_table.data(),&short_size,sizeof(short_size)); service=short_table.data();
    assert(Call("quantum.enable",&on,1)==SCO_UNAVAILABLE && enables==0);
    service=&q; q.enable=nullptr; assert(Call("quantum.enable",&on,1)==SCO_UNAVAILABLE); q.enable=Enable;
    assert(Call("quantum.enable",&on,1)==SCO_OK && enabled && enables==1);
    sco_arg ms{}; ms.type=SCO_ARG_INT; ms.v.i=0; assert(Call("quantum.hold",&ms,1)==SCO_BAD_ARG);
    ms.v.i=1001; assert(Call("quantum.hold",&ms,1)==SCO_BAD_ARG && holds==0);
    ms.v.i=1000; assert(Call("quantum.hold",&ms,1)==SCO_OK && duration==1000 && held);
    assert(Call("quantum.boost")==SCO_OK && !held);
    assert(Call("quantum.boost")==SCO_OK && held && duration==1000);
    capability=false; assert(Call("quantum.release")==SCO_OK && !held); // cancellation ignores capability
    capability=true;
    sco_widget_event e{sizeof(e),103,SCO_WIDGET_BUTTON,0,0,nullptr}; event("quantum.travel",&e,event_ctx); assert(held);
    capability=false; now=500; Tick(now); assert(!enabled && !held);
    capability=true; assert(Call("quantum.enable",&on,1)==SCO_OK);
    assert(Call("quantum.hold",&ms,1)==SCO_OK);
    relocation_cap=true; assert(Call("quantum.travel",trip,2)==SCO_OK && requests==3 && ticks.size()==2); // in flight at unload
    unload(); unload(); assert(!enabled && !held && ticks.empty() && !event); relocation_cap=false;
    // A provider release hook is NOT emulated here: ordinary unload requests cleanup.
    commands.clear(); const auto writes=enables; assert(load(&api,self)==SCO_OK && enables==writes);
    available=false; assert(Call("quantum.control_status")==SCO_UNAVAILABLE); unload();available=true;
    BookmarkWorkflow(load,unload,api,self,root);
#ifdef _WIN32
    FreeLibrary(module);
#else
    dlclose(module);
#endif
    assert(directory.cleanup());
    std::puts("quantum actual-module PASS: legacy commands/Travel, no startup writes, absent/partial/null refusal, bounded controls, capability loss, game.relocation switch/capability/request/tick tracking, widgets and unload/reload; synthetic only");
}
