// SPDX-License-Identifier: GPL-3.0-only
// Synthetic public-service workflow; no renderer, host implementation or game.
#include "loadout.h"
#include "sco_content.h"
#include "sc_loadout.h"
#include "sco_widgets.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {
struct File { const char* name; std::string bytes; };
struct Panel {
    sco_widget_event_fn event = nullptr;
    void* context = nullptr;
    unsigned accepted = 0;
    std::map<uint32_t,uint32_t> combos, flags;
    std::vector<std::string> gear_labels, outfit_labels;
    int outfit_index=-1; std::string outfit_filter;
};
std::vector<File> files;
Panel player, sq42;
unsigned rejected = 0, removed = 0, unsubscribed = 0;
bool fail_sq=false; sco_command_fn reload_command=nullptr;
sco_content_v1 content{};
sco_widgets_v1 widgets{};
sc_loadout_v1_2 loadout{};
bool async_cap=true, provider=true; uint32_t supported=SC_LOADOUT_VERSION_1_2;
unsigned submits=0, cancels=0; std::vector<sc_loadout_item> captured;
sco_result Submit(sco_plugin*,const sc_loadout_request* request,uint64_t* job) {
    ++submits; captured.assign(request->items,request->items+request->count); *job=42; return SCO_OK;
}
sco_result Status(sco_plugin*,uint64_t,sc_loadout_job_report*) { return SCO_UNAVAILABLE; }
sco_result Cancel(sco_plugin*,uint64_t job) { (void)job; ++cancels; return SCO_OK; }


void Require(bool condition,const char* message) {
    if (!condition) { std::fprintf(stderr,"loadout widgets: %s\n",message); std::exit(1); }
}
sco_result Item(sco_plugin*,sco_content_kind kind,uint32_t ordinal,sco_content_item* out) {
    if (kind!=SCO_CONTENT_LIST || !out || out->size<sizeof(*out)) return SCO_BAD_ARG;
    if (ordinal>=files.size()) return SCO_NOT_FOUND;
    *out={}; out->size=sizeof(*out); out->kind=kind; out->id=ordinal+1;
    std::snprintf(out->pack,sizeof(out->pack),"fixture");
    std::snprintf(out->name,sizeof(out->name),"%s",files[ordinal].name);
    return SCO_OK;
}
sco_result Read(sco_plugin*,uint64_t id,void* out,uint32_t* size) {
    if (!size || (!out && *size)) return SCO_BAD_ARG;
    if (!id || id>files.size()) return SCO_NOT_FOUND;
    const auto& bytes=files[static_cast<size_t>(id-1)].bytes;
    if (*size<bytes.size()) { *size=static_cast<uint32_t>(bytes.size()); return SCO_TOO_MANY; }
    if (!bytes.empty()) std::memcpy(out,bytes.data(),bytes.size());
    *size=static_cast<uint32_t>(bytes.size()); return SCO_OK;
}
sco_result SetPanel(sco_plugin*,const char* id,const char*,const sco_widget_node* nodes,
                    uint32_t count,uint32_t stride,sco_widget_event_fn event,void* context) {
    Require(nodes && count && stride==sizeof(sco_widget_node),"invalid panel extent");
    std::map<uint32_t,uint32_t> combos, flags; std::vector<std::string> gear_labels, outfit_labels;
    int outfit_index=-1; std::string outfit_filter;
    for (uint32_t i=0;i<count;++i) {
        const auto& node=nodes[i];
        Require(node.size==sizeof(node),"invalid node extent");
        flags[node.id]=node.flags;
        if(node.id==101) { outfit_index=node.index; for(uint32_t j=0;j<node.item_count;++j) outfit_labels.emplace_back(node.items[j]); }
        if(node.id==135 && node.text) outfit_filter=node.text;
        if(node.id==2) for(uint32_t j=0;j<node.item_count;++j) gear_labels.emplace_back(node.items[j]);
        // sco.widgets requires a NULL array exactly when the item count is zero.
        // A rejected replacement must preserve the previous panel and callback.
        if ((node.item_count && !node.items) || (!node.item_count && node.items)) {
            ++rejected; return SCO_BAD_ARG;
        }
        if (node.kind==SCO_WIDGET_COMBO) {
            Require(node.index>=-1 && (node.index<0 || static_cast<uint32_t>(node.index)<node.item_count),"invalid combo selection");
            for (uint32_t j=0;j<node.item_count;++j) Require(node.items[j]!=nullptr,"missing combo option");
            combos[node.id]=node.item_count;
        }
    }
    Require(!std::strcmp(id,"loadout.player") || !std::strcmp(id,"loadout.sq42"),"unexpected panel id");
    if(fail_sq && !std::strcmp(id,"loadout.sq42")) return SCO_FAILED;
    auto& panel=!std::strcmp(id,"loadout.player") ? player : sq42;
    panel.event=event; panel.context=context; ++panel.accepted; panel.combos=std::move(combos); panel.flags=std::move(flags); panel.gear_labels=std::move(gear_labels); panel.outfit_labels=std::move(outfit_labels); panel.outfit_index=outfit_index; panel.outfit_filter=std::move(outfit_filter);
    return SCO_OK;
}
sco_result RemovePanel(sco_plugin*,const char*) { ++removed; return SCO_OK; }
sco_result Query(const char* name,uint32_t version,const void** out) {
    *out=nullptr;
    if (!std::strcmp(name,SC_LOADOUT_SERVICE_NAME)) { if(provider && version==SC_LOADOUT_VERSION_1_2 && version<=supported) *out=&loadout; return *out ? SCO_OK : SCO_NOT_FOUND; }
    Require(version==0x10000u,"consumer changed service version");
    if (!std::strcmp(name,SCO_CONTENT_NAME)) *out=&content;
    if (!std::strcmp(name,SCO_WIDGETS_NAME)) *out=&widgets;
    return *out ? SCO_OK : SCO_NOT_FOUND;
}
sco_result Register(sco_plugin*,const sco_command* command) { if (!std::strcmp(command->name,"loadout.reload_lists")) reload_command=command->fn; return SCO_OK; }
sco_result Subscribe(sco_plugin*,const char*,sco_event_fn,void*) { return SCO_OK; }
sco_result Unsubscribe(sco_plugin*,const char*,sco_event_fn) { ++unsubscribed; return SCO_OK; }
int Has(const char* name) { return async_cap && !std::strcmp(name,"game.loadout.async"); }
void Send(const char* id,Panel& panel,uint32_t node,uint32_t kind,const char* text=nullptr) {
    Require(panel.event!=nullptr,"missing panel callback");
    sco_widget_event event{}; event.size=sizeof(event); event.node_id=node; event.kind=kind; event.text=text;
    panel.event(id,&event,panel.context);
}
void Select(const char* id,Panel& panel,uint32_t node,int index) {
    sco_widget_event event{}; event.size=sizeof(event); event.node_id=node; event.kind=SCO_WIDGET_COMBO; event.index=index;
    panel.event(id,&event,panel.context);
}
void Workflow(bool populated) {
    files={{"lists/items.txt","[helmet]\nFixtureHelmet\n[primary]\nFixtureGun\n"}};
    if (populated) {
        files.push_back({"lists/outfits.txt","[Fixture outfit]\nArmor_Helmet FixtureHelmet\n[Other FLIGHT]\nArmor_Helmet OtherHelmet\n[Flight suit]\nArmor_Helmet FlightHelmet\n"});
        files.push_back({"lists/buildables.txt","[sq42]\nFixtureObject\n"});
    }
    player={}; sq42={}; rejected=0; removed=0; unsubscribed=0; fail_sq=false; reload_command=nullptr; submits=0; cancels=0; captured.clear(); async_cap=provider=true; supported=SC_LOADOUT_VERSION_1_2; loadout.size=sizeof(loadout); loadout.submit=Submit; loadout.status=Status; loadout.cancel=Cancel;
    sco_api api{}; api.size=sizeof(api); api.major=SCO_API_MAJOR; api.minor=SCO_API_MINOR;
    api.has=Has; api.register_command=Register; api.query_service=Query;
    api.subscribe=Subscribe; api.unsubscribe=Unsubscribe;
    unsigned char owner=0; auto* self=reinterpret_cast<sco_plugin*>(&owner);
    Require(sco::features::loadout::Load(&api,self)==SCO_OK,"load failed");
    Require(!rejected && player.accepted==1 && sq42.accepted==1,"initial panels rejected");
    Require(player.combos.at(2)==2 && sq42.combos.empty(),"initial picker/confirmation mismatch");
    Require(player.gear_labels.size()==2 && player.gear_labels[1].find("[requires 1.1]")!=std::string::npos,"weapon label reflects synchronous version requirement");
    async_cap=false; Send("loadout.player",player,1,SCO_WIDGET_TEXT_INPUT,"");
    Require(player.flags.at(27)&SCO_WIDGET_DISABLED,"capability disables submission");
    Send("loadout.player",player,27,SCO_WIDGET_BUTTON); Require(submits==0,"forged unavailable submission refused");
    async_cap=true; provider=false; Send("loadout.player",player,1,SCO_WIDGET_TEXT_INPUT,"");
    Require(player.flags.at(27)&SCO_WIDGET_DISABLED,"missing provider disables submission");
    provider=true; supported=SC_LOADOUT_VERSION_1_1; Send("loadout.player",player,1,SCO_WIDGET_TEXT_INPUT,"");
    Require(player.flags.at(27)&SCO_WIDGET_DISABLED,"older provider disables submission");
    supported=SC_LOADOUT_VERSION_1_2; loadout.size=offsetof(sc_loadout_v1_2,cancel); Send("loadout.player",player,1,SCO_WIDGET_TEXT_INPUT,"");
    Require(player.flags.at(27)&SCO_WIDGET_DISABLED,"short provider disables submission");
    loadout.size=sizeof(loadout); loadout.status=nullptr; Send("loadout.player",player,1,SCO_WIDGET_TEXT_INPUT,"");
    Require(player.flags.at(27)&SCO_WIDGET_DISABLED,"null member disables submission");
    loadout.status=Status; Send("loadout.player",player,1,SCO_WIDGET_TEXT_INPUT,"");
    Require(!(player.flags.at(27)&SCO_WIDGET_DISABLED),"available provider enables submission");
    auto old=sq42; Send("loadout.sq42",sq42,106,SCO_WIDGET_BUTTON);
    Require(submits==0,"spoiler gate refused outfit submission");
    Send("loadout.sq42",sq42,100,SCO_WIDGET_BUTTON);
    Send("loadout.sq42",old,100,SCO_WIDGET_BUTTON);
    Require(sq42.accepted==old.accepted+1,"stale SQ42 callback ignored");
    Require(!rejected && sq42.accepted==old.accepted+1,"SQ42 panel rejected after spoiler confirmation");
    Require(sq42.combos.at(101)==(populated ? 3u : 0u),"outfit picker count mismatch");
    Require(sq42.combos.at(130)==(populated ? 1u : 0u),"build picker count mismatch");
    Send("loadout.player",player,1,SCO_WIDGET_TEXT_INPUT,"no matching gear");
    Require(!rejected && player.combos.at(2)==0,"empty filtered picker rejected");
    Send("loadout.player",player,1,SCO_WIDGET_TEXT_INPUT,"");
    Require(!rejected && player.combos.at(2)==2,"restored gear picker rejected");
    Select("loadout.player",player,2,0);
    Send("loadout.player",player,27,SCO_WIDGET_BUTTON);
    Require(submits==1 && captured.size()==1 && captured[0].slot==SC_LOADOUT_SLOT_HEAD && !std::strcmp(captured[0].item_class,"FixtureHelmet"),"selected gear submitted");
    Require((player.flags.at(6)&SCO_WIDGET_DISABLED) && (player.flags.at(27)&SCO_WIDGET_DISABLED) && (sq42.flags.at(104)&SCO_WIDGET_DISABLED),"retained job disables sync and async controls");
    Send("loadout.player",player,27,SCO_WIDGET_BUTTON);
    Require(submits==1,"retained job refuses second submission");
    Send("loadout.player",player,24,SCO_WIDGET_BUTTON);
    Require(cancels==1,"player releases job");
    if(populated) {
        Send("loadout.sq42",sq42,135,SCO_WIDGET_TEXT_INPUT,"fLiGhT");
        Require(sq42.outfit_labels==std::vector<std::string>({"Other FLIGHT","Flight suit"}),"case-insensitive outfit filter");
        auto stale=sq42; Select("loadout.sq42",sq42,101,1);
        Require(sq42.outfit_index==1,"filtered source selection retained");
        Select("loadout.sq42",sq42,101,-1);
        Require(sq42.outfit_index==-1 && (sq42.flags.at(102)&SCO_WIDGET_DISABLED) && (sq42.flags.at(106)&SCO_WIDGET_DISABLED),"deselection disables outfit actions");
        Send("loadout.sq42",sq42,102,SCO_WIDGET_BUTTON); Send("loadout.sq42",sq42,106,SCO_WIDGET_BUTTON);
        Require(submits==1,"forged deselected actions refused");
        Select("loadout.sq42",sq42,101,1);
        Select("loadout.sq42",stale,101,0);
        Require(sq42.outfit_index==1,"stale filtered callback ignored");
        Send("loadout.sq42",sq42,135,SCO_WIDGET_TEXT_INPUT,"none");
        Require(sq42.combos.at(101)==0 && sq42.outfit_index==-1 && (sq42.flags.at(102)&SCO_WIDGET_DISABLED) && (sq42.flags.at(106)&SCO_WIDGET_DISABLED),"hidden selection disables apply");
        Send("loadout.sq42",sq42,106,SCO_WIDGET_BUTTON); Send("loadout.sq42",sq42,102,SCO_WIDGET_BUTTON);
        Require(submits==1,"forged hidden selection refused");
        Send("loadout.sq42",sq42,135,SCO_WIDGET_TEXT_INPUT,"FLIGHT");
        Require(sq42.outfit_index==1,"source selection survives filter changes");
        std::string oversized(64,'x'); Send("loadout.sq42",sq42,135,SCO_WIDGET_TEXT_INPUT,oversized.c_str());
        Send("loadout.sq42",sq42,135,SCO_WIDGET_TEXT_INPUT,"bad\ntext");
        Require(sq42.outfit_filter=="FLIGHT" && sq42.outfit_index==1,"invalid search rejected");
        Send("loadout.sq42",sq42,135,SCO_WIDGET_TEXT_INPUT,std::string(63,'x').c_str());
        Require(sq42.outfit_filter.size()==63 && sq42.combos.at(101)==0,"63 character search accepted");
        Send("loadout.sq42",sq42,135,SCO_WIDGET_TEXT_INPUT,"flight");
        auto before_failure=sq42; fail_sq=true;
        Send("loadout.sq42",sq42,135,SCO_WIDGET_TEXT_INPUT,"other");
        Select("loadout.sq42",before_failure,101,0); Send("loadout.sq42",before_failure,106,SCO_WIDGET_BUTTON);
        Require(submits==1 && sq42.accepted==before_failure.accepted,"failed replacement invalidates old callbacks");
        fail_sq=false; Send("loadout.player",player,1,SCO_WIDGET_TEXT_INPUT,"");
        Require(sq42.outfit_labels==std::vector<std::string>({"Other FLIGHT"}) && sq42.outfit_index==-1,"failed replacement does not publish mapping");
        Send("loadout.sq42",sq42,135,SCO_WIDGET_TEXT_INPUT,"flight");
        Require(sq42.outfit_index==1,"failed replacement preserves source selection");
        Send("loadout.sq42",sq42,106,SCO_WIDGET_BUTTON);
        Require(submits==2 && captured.size()==1 && !std::strcmp(captured[0].item_class,"FlightHelmet"),"filtered non-first source submitted");
        Send("loadout.sq42",sq42,108,SCO_WIDGET_BUTTON); Require(cancels==2,"filtered job released");
        Require(reload_command!=nullptr,"reload command registered");
        auto before_reload=sq42;
        files[1].bytes="[New flight]\nArmor_Helmet NewHelmet\n";
        Require(reload_command(nullptr,0,nullptr,nullptr,0)==SCO_OK,"reload succeeded");
        Select("loadout.sq42",before_reload,101,0); Send("loadout.sq42",before_reload,106,SCO_WIDGET_BUTTON);
        Require(submits==2,"reload invalidates published callbacks");
        Send("loadout.player",player,1,SCO_WIDGET_TEXT_INPUT,"");
        Require(sq42.outfit_labels==std::vector<std::string>({"New flight"}) && sq42.outfit_index==-1,"reload clears source selection");
        Select("loadout.sq42",sq42,101,0);
        async_cap=false; Send("loadout.sq42",sq42,105,SCO_WIDGET_BUTTON);
        Require(sq42.flags.at(106)&SCO_WIDGET_DISABLED,"outfit submission disabled without capability");
        Send("loadout.sq42",sq42,106,SCO_WIDGET_BUTTON); Require(submits==2,"forged unavailable outfit refused");
        async_cap=true; Send("loadout.sq42",sq42,105,SCO_WIDGET_BUTTON); Send("loadout.sq42",sq42,106,SCO_WIDGET_BUTTON);
        Require(submits==3 && captured.size()==1 && captured[0].slot==SC_LOADOUT_SLOT_HEAD && !std::strcmp(captured[0].item_class,"NewHelmet"),"selected outfit submitted");
        Send("loadout.sq42",sq42,108,SCO_WIDGET_BUTTON); Require(cancels==3,"SQ42 releases job");
        auto before_bad_reload=sq42; files[1].bytes="[broken]\nnot a valid piece\n";
        Require(reload_command(nullptr,0,nullptr,nullptr,0)!=SCO_OK,"malformed reload refused");
        Send("loadout.sq42",before_bad_reload,106,SCO_WIDGET_BUTTON); Require(submits==3,"failed reload invalidates callbacks");
        Send("loadout.player",player,1,SCO_WIDGET_TEXT_INPUT,"");
        Require(sq42.combos.at(101)==0 && (sq42.flags.at(106)&SCO_WIDGET_DISABLED),"failed reload clears picker");
    }
    auto before_unload=sq42; sco::features::loadout::Unload(); sco::features::loadout::Unload();
    Send("loadout.sq42",before_unload,106,SCO_WIDGET_BUTTON); Require(submits==(populated ? 3u : 1u),"unloaded callback ignored");
    Require(removed==2 && unsubscribed==1,"unload cleanup mismatch");
}
}
int main() {
    content.size=sizeof(content); content.item=Item; content.read=Read;
    loadout.size=sizeof(loadout); loadout.submit=Submit; loadout.status=Status; loadout.cancel=Cancel;
    widgets.size=sizeof(widgets); widgets.set_panel=SetPanel; widgets.remove_panel=RemovePanel;
    Workflow(false); Workflow(true);
    std::puts("loadout widget workflow: PASS (synthetic public services; not run in game)");
}
