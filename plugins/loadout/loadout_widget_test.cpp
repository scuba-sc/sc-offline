// SPDX-License-Identifier: GPL-3.0-only
// Synthetic public-service workflow; no renderer, host implementation or game.
#include "loadout.h"
#include "../../tools/panel_dump.h"
#include "sco_content.h"
#include "sc_loadout.h"
#include "sco_widgets.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {
struct File {
    std::string pack, name, bytes;
    uint64_t id=0;
    bool fail_copy=false, bad_pack=false, bad_record=false;
};
struct Panel {
    sco_widget_event_fn event = nullptr;
    void* context = nullptr;
    unsigned accepted = 0;
    std::map<uint32_t,uint32_t> combos, flags;
    std::vector<std::string> gear_labels, outfit_labels, ship_labels, words;
    int outfit_index=-1, ship_index=-1, build_index=-1; std::string outfit_filter, ship_filter, status, job;
};
std::vector<File> files;
Panel player, sq42;
unsigned rejected = 0, removed = 0, unsubscribed = 0;
bool fail_sq=false; sco_command_fn reload_command=nullptr, items_command=nullptr, wear_command=nullptr; sco_event_fn tick_event=nullptr;
bool spawn_listed=true, build_listed=true; sco_command spawn_command{}, build_command{};
uint32_t tick_time=0;
sco_content_v1 content{};
sco_widgets_v1 widgets{};
sc_loadout_v1_2 loadout{};
bool async_cap=true, provider=true, outfit_cap=false; uint32_t supported=SC_LOADOUT_VERSION_1_2;
unsigned spawns=0; std::string spawn_class, spawn_seat; double spawn_height=0; int64_t spawn_ready=0;
unsigned submits=0, cancels=0; std::vector<sc_loadout_item> captured;
sco_result Submit(sco_plugin*,const sc_loadout_request* request,uint64_t* job) {
    ++submits; captured.assign(request->items,request->items+request->count); *job=42; return SCO_OK;
}
int places=0; sco_result place_result=SCO_OK; const char* place_reply="";
sco_result Invoke(sco_plugin*,const char* name,const sco_arg* args,uint32_t count,sco_invoke_done done,void* context) {
    if (!std::strcmp(name,"build.place")) { ++places; if (done) done(place_result,place_reply,context); return place_result; }
    if (std::strcmp(name,"spawn.ship_options") || count!=4 || args[0].type!=SCO_ARG_STRING || args[1].type!=SCO_ARG_FLOAT || args[2].type!=SCO_ARG_STRING || args[3].type!=SCO_ARG_BOOL) std::abort();
    ++spawns; spawn_class=args[0].v.s; spawn_height=args[1].v.f; spawn_seat=args[2].v.s; spawn_ready=args[3].v.i; return SCO_OK;
}
uint32_t ListCommands(const sco_command** out,uint32_t max) {
    uint32_t count=0;
    spawn_command.name="spawn.ship_options"; build_command.name="build.place";
    if (spawn_listed && count<max) out[count++]=&spawn_command;
    if (build_listed && count<max) out[count++]=&build_command;
    return count;
}
sco_result Status(sco_plugin*,uint64_t,sc_loadout_job_report*) { return SCO_UNAVAILABLE; }
sco_result Cancel(sco_plugin*,uint64_t job) { (void)job; ++cancels; return SCO_OK; }


void Require(bool condition,const char* message) {
    if (!condition) { std::fprintf(stderr,"loadout widgets: %s\n",message); std::exit(1); }
}
sco_result Item(sco_plugin*,sco_content_kind kind,uint32_t ordinal,sco_content_item* out) {
    if (kind!=SCO_CONTENT_LIST || !out || out->size<sizeof(*out)) return SCO_BAD_ARG;
    if (ordinal>=files.size()) return SCO_NOT_FOUND;
    const auto& file=files[ordinal];
    *out={}; out->size=sizeof(*out); out->kind=kind; out->id=file.id;
    std::snprintf(out->pack,sizeof(out->pack),"%s",file.pack.c_str());
    std::snprintf(out->name,sizeof(out->name),"%s",file.name.c_str());
    if(file.bad_pack) std::memset(out->pack,'x',sizeof(out->pack));
    if(file.bad_record) out->size=sizeof(*out)-1;
    return SCO_OK;
}
sco_result Read(sco_plugin*,uint64_t id,void* out,uint32_t* size) {
    if (!size || (!out && *size)) return SCO_BAD_ARG;
    const auto found=std::find_if(files.begin(),files.end(),[id](const File& file) { return file.id==id; });
    if (found==files.end()) return SCO_NOT_FOUND;
    const auto& file=*found;
    const auto& bytes=file.bytes;
    if(out && file.fail_copy) return SCO_FAILED;
    if (*size<bytes.size()) { *size=static_cast<uint32_t>(bytes.size()); return SCO_TOO_MANY; }
    if (!bytes.empty()) std::memcpy(out,bytes.data(),bytes.size());
    *size=static_cast<uint32_t>(bytes.size()); return SCO_OK;
}
sco_result SetPanel(sco_plugin*,const char* id,const char* title,const sco_widget_node* nodes,
                    uint32_t count,uint32_t stride,sco_widget_event_fn event,void* context) {
    Require(nodes && count && stride==sizeof(sco_widget_node),"invalid panel extent");
    { const auto why=sc_offline::panel_dump::Violation(nodes,count,stride); if(!why.empty()) std::printf("%s: %s\n",id,why.c_str());
      Require(why.empty(),"panel follows the sco.widgets tree rules"); sc_offline::panel_dump::Record(id,title,nodes,count,stride); }
    std::map<uint32_t,uint32_t> combos, flags; std::vector<std::string> gear_labels, outfit_labels, ship_labels, words;
    int outfit_index=-1, ship_index=-1, build_index=-1; std::string outfit_filter, ship_filter, status, job;
    for (uint32_t i=0;i<count;++i) {
        const auto& node=nodes[i];
        Require(node.size==sizeof(node),"invalid node extent");
        flags[node.id]=node.flags;
        if(node.label) words.emplace_back(node.label);
        for(uint32_t j=0;j<node.item_count && node.items;++j) if(node.items[j]) words.emplace_back(node.items[j]);
        if(node.id==26 && node.label) job=node.label;
        if(node.id==101) { outfit_index=node.index; for(uint32_t j=0;j<node.item_count;++j) outfit_labels.emplace_back(node.items[j]); }
        if(node.id==135 && node.text) outfit_filter=node.text;
        if(node.id==14 && node.label) status=node.label;
        if(node.id==120) { ship_index=node.index; for(uint32_t j=0;j<node.item_count;++j) ship_labels.emplace_back(node.items[j]); }
        if(node.id==136 && node.text) ship_filter=node.text;
        if(node.id==130) build_index=node.index;
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
    panel.event=event; panel.context=context; ++panel.accepted; panel.combos=std::move(combos); panel.flags=std::move(flags); panel.gear_labels=std::move(gear_labels); panel.outfit_labels=std::move(outfit_labels); panel.outfit_index=outfit_index; panel.outfit_filter=std::move(outfit_filter); panel.ship_labels=std::move(ship_labels); panel.ship_index=ship_index; panel.ship_filter=std::move(ship_filter); panel.status=std::move(status); panel.job=std::move(job); panel.words=std::move(words);
    panel.build_index=build_index;
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
sco_result Register(sco_plugin*,const sco_command* command) {
    if (!std::strcmp(command->name,"loadout.reload_lists")) reload_command=command->fn;
    if (!std::strcmp(command->name,"loadout.items")) items_command=command->fn;
    if (!std::strcmp(command->name,"loadout.wear")) wear_command=command->fn;
    return SCO_OK;
}
sco_result Subscribe(sco_plugin*,const char* name,sco_event_fn callback,void*) {
    if (!std::strcmp(name,"tick")) tick_event=callback;
    return SCO_OK;
}
sco_result Unsubscribe(sco_plugin*,const char*,sco_event_fn) { ++unsubscribed; return SCO_OK; }
int Has(const char* name) {
    return (async_cap && !std::strcmp(name,"game.loadout.async")) || (outfit_cap && !std::strcmp(name,"game.loadout.outfit"));
}
void Send(const char* id,Panel& panel,uint32_t node,uint32_t kind,const char* text=nullptr) {
    Require(panel.event!=nullptr,"missing panel callback");
    sco_widget_event event{}; event.size=sizeof(event); event.node_id=node; event.kind=kind; event.text=text;
    panel.event(id,&event,panel.context);
}
void Select(const char* id,Panel& panel,uint32_t node,int index) {
    sco_widget_event event{}; event.size=sizeof(event); event.node_id=node; event.kind=SCO_WIDGET_COMBO; event.index=index;
    panel.event(id,&event,panel.context);
}
void Value(const char* id,Panel& panel,uint32_t node,uint32_t kind,double value) {
    sco_widget_event event{}; event.size=sizeof(event); event.node_id=node; event.kind=kind; event.value=value;
    panel.event(id,&event,panel.context);
}
void TickNow() {
    Require(tick_event!=nullptr,"missing tick callback");
    tick_time+=250;
    tick_event("tick",&tick_time,nullptr);
}
// Nothing a player reads on either page may carry developer words: versions, ids, job or slot plumbing.
void ReadableOnly(const Panel& panel,const char* page) {
    static const char* const forbidden[]{"job","slot","(1.","requires","archetype","build.place","spawn.ship","orchestration","sdk",
        "category %","unavailable]","unknown/","loadout 1","ascii","retained","payload","[","_"};
    for (const auto& word:panel.words) {
        std::string lower=word; for (auto& c:lower) if (c>='A' && c<='Z') c=static_cast<char>(c+('a'-'A'));
        for (const char* bad:forbidden)
            if (lower.find(bad)!=std::string::npos) { std::fprintf(stderr,"loadout widgets: %s shows '%s' (%s)\n",page,word.c_str(),bad); std::exit(1); }
    }
}
void Workflow(bool populated) {
    files={{"fixture_armory","lists/items.txt","[helmet]\nFixtureHelmet\n[primary]\nFixtureGun\n"}};
    if (populated) {
        files.push_back({"sq42_catalog","lists/outfits.txt","[Fixture outfit]\nArmor_Helmet FixtureHelmet\n[Other FLIGHT]\nArmor_Helmet OtherHelmet\n[Flight suit]\nArmor_Helmet FlightHelmet\n"});
        files.push_back({"build_catalog","lists/buildables.txt","[sq42]\nFixtureObject\n"});
    }
    // Content IDs are process-unique opaque handles, not ordinal-derived indexes.
    files[0].id=0xF001000000000123ULL;
    if (populated) {
        files[1].id=0x1029384756ABCDEFULL;
        files[2].id=0x8ABCDE0012345678ULL;
    }
    player={}; sq42={}; rejected=0; removed=0; unsubscribed=0; fail_sq=false; reload_command=nullptr; items_command=nullptr; wear_command=nullptr; tick_event=nullptr; tick_time=0; submits=0; cancels=0; captured.clear(); async_cap=provider=true; supported=SC_LOADOUT_VERSION_1_2; loadout.size=sizeof(loadout); loadout.submit=Submit; loadout.status=Status; loadout.cancel=Cancel;
    sco_api api{}; api.size=sizeof(api); api.major=SCO_API_MAJOR; api.minor=SCO_API_MINOR;
    api.has=Has; api.register_command=Register; api.query_service=Query; api.invoke=Invoke; api.list_commands=ListCommands; spawns=0; spawn_listed=build_listed=true;
    api.subscribe=Subscribe; api.unsubscribe=Unsubscribe;
    unsigned char owner=0; auto* self=reinterpret_cast<sco_plugin*>(&owner);
    Require(sco::features::loadout::Load(&api,self)==SCO_OK,"load failed");
    Require(!rejected && player.accepted==1 && sq42.accepted==1,"initial panels rejected");
    Require(items_command!=nullptr && reload_command!=nullptr && wear_command!=nullptr,"list commands registered");
    Require(player.combos.at(2)==2 && sq42.combos.empty(),"initial picker/confirmation mismatch");
    Require(player.gear_labels==std::vector<std::string>({"Helmet: Fixture Helmet","Primary: Fixture Gun"}),"gear rows read as a slot heading and a plain name");
    Require(player.status.empty() && !player.flags.count(25),"a healthy start shows no status and no unavailable notice");
    Require(player.flags.count(300) && player.flags.count(310) && player.flags.count(320),"player page is grouped into labelled sections");
    ReadableOnly(player,"player");
    // The creative controls (node ids 10 to 13) live only on the Creative tab: the Player panel publishes none
    // of them, and a forged event for those ids reaches no command (Invoke aborts on any name but spawn.ship_options).
    Require(!player.flags.count(10) && !player.flags.count(11) && !player.flags.count(12) && !player.flags.count(13),
            "Player panel carries no creative controls");
    Send("loadout.player",player,10,SCO_WIDGET_TOGGLE); Send("loadout.player",player,11,SCO_WIDGET_TOGGLE);
    Send("loadout.player",player,12,SCO_WIDGET_TOGGLE); Value("loadout.player",player,13,SCO_WIDGET_SLIDER,100);
    Require(spawns==0 && !rejected,"forged creative events are ignored");
    sco_arg item_filter{}; item_filter.type=SCO_ARG_STRING; item_filter.v.s="Fixture";
    char item_reply[1024]{};
    Require(items_command(&item_filter,1,nullptr,item_reply,sizeof(item_reply))==SCO_OK &&
            std::strstr(item_reply,"fixture_armory/lists/items.txt"),"item command reports deterministic copied source");
    const auto original_pack=files[0].pack; files[0].pack="changed_provider_buffer";
    std::memset(item_reply,0,sizeof(item_reply));
    Require(items_command(&item_filter,1,nullptr,item_reply,sizeof(item_reply))==SCO_OK &&
            std::strstr(item_reply,"fixture_armory/lists/items.txt") &&
            !std::strstr(item_reply,"changed_provider_buffer"),"source identity is owned after provider buffer changes");
    files[0].pack=original_pack;
    const auto original_bytes=files[0].bytes; files[0].bytes="[helmet]\nProviderMutation\n";
    std::memset(item_reply,0,sizeof(item_reply));
    Require(items_command(&item_filter,1,nullptr,item_reply,sizeof(item_reply))==SCO_OK &&
            std::strstr(item_reply,"helmet:FixtureHelmet") && !std::strstr(item_reply,"ProviderMutation"),
            "parsed list content remains owned after provider backing bytes mutate");
    Require(player.gear_labels.size()==2 && player.gear_labels[0].find("Fixture Helmet")!=std::string::npos,
            "published picker rows remain owned after provider backing bytes mutate");
    files[0].bytes=original_bytes;
    Require(player.flags.at(27)&SCO_WIDGET_DISABLED,"nothing selected disables Equip");
    Select("loadout.player",player,2,0);
    Require(!(player.flags.at(27)&SCO_WIDGET_DISABLED) && player.flags.count(26) && !player.flags.count(25),"a selection enables Equip");
    async_cap=false; Send("loadout.player",player,1,SCO_WIDGET_TEXT_INPUT,"");
    Require(player.flags.count(25) && !player.flags.count(1) && !player.flags.count(2) && !player.flags.count(27) && !player.flags.count(221),"capability disables submission");
    Require(player.flags.count(25) && !player.flags.count(26) && !player.flags.count(24),"without the capability the page says so and hides the progress controls");
    ReadableOnly(player,"player (unavailable)");
    Send("loadout.player",player,27,SCO_WIDGET_BUTTON); Require(submits==0,"forged unavailable submission refused");
    Require(player.status=="Changing gear isn't available yet.","refusal reaches the panel as a plain sentence");
    async_cap=true; provider=false; Send("loadout.player",player,1,SCO_WIDGET_TEXT_INPUT,"");
    Require(player.flags.count(25) && !player.flags.count(1) && !player.flags.count(2) && !player.flags.count(27) && !player.flags.count(221),"missing provider disables submission");
    provider=true; supported=SC_LOADOUT_VERSION_1_1; Send("loadout.player",player,1,SCO_WIDGET_TEXT_INPUT,"");
    Require(player.flags.count(25) && !player.flags.count(1) && !player.flags.count(2) && !player.flags.count(27) && !player.flags.count(221),"older provider disables submission");
    supported=SC_LOADOUT_VERSION_1_2; loadout.size=offsetof(sc_loadout_v1_2,cancel); Send("loadout.player",player,1,SCO_WIDGET_TEXT_INPUT,"");
    Require(player.flags.count(25) && !player.flags.count(1) && !player.flags.count(2) && !player.flags.count(27) && !player.flags.count(221),"short provider disables submission");
    loadout.size=sizeof(loadout); loadout.status=nullptr; Send("loadout.player",player,1,SCO_WIDGET_TEXT_INPUT,"");
    Require(player.flags.count(25) && !player.flags.count(1) && !player.flags.count(2) && !player.flags.count(27) && !player.flags.count(221),"null member disables submission");
    loadout.status=Status; Send("loadout.player",player,1,SCO_WIDGET_TEXT_INPUT,"");
    Require(!(player.flags.at(27)&SCO_WIDGET_DISABLED),"available provider enables submission");
    Send("loadout.player",player,7,SCO_WIDGET_BUTTON);
    Require((player.flags.at(27)&SCO_WIDGET_DISABLED) && (player.flags.at(7)&SCO_WIDGET_DISABLED),"clearing the selection disables Equip and Clear");
    auto old=sq42; Send("loadout.sq42",sq42,106,SCO_WIDGET_BUTTON);
    Require(submits==0,"spoiler gate refused outfit submission");
    Send("loadout.sq42",sq42,100,SCO_WIDGET_BUTTON);
    Send("loadout.sq42",old,100,SCO_WIDGET_BUTTON);
    Require(sq42.accepted==old.accepted+1,"stale SQ42 callback ignored");
    Require(!rejected && sq42.accepted==old.accepted+1,"SQ42 panel rejected after spoiler confirmation");
    Require(sq42.combos.at(101)==(populated ? 3u : 0u),"outfit picker count mismatch");
    Require(sq42.flags.count(114) && !sq42.flags.count(110) && !sq42.flags.count(111) && !sq42.flags.count(112) && !sq42.flags.count(113),
            "unreadable game settings are one plain line, not disabled toggles");
    Require(sq42.flags.count(400) && sq42.flags.count(410) && sq42.flags.count(420) && sq42.flags.count(430) && !sq42.flags.count(115),
            "Squadron 42 page is grouped into Outfit, Game settings, Ships and Objects");
    Require(!sq42.flags.count(103) && !sq42.flags.count(104) && !sq42.flags.count(102) && !sq42.flags.count(133),"refusal-only visor and developer lines are gone");
    ReadableOnly(sq42,"squadron 42");
    Require(sq42.combos.at(130)==(populated ? 1u : 0u),"build picker count mismatch");
    if(!populated) {
        sco_arg outfit_name{}; outfit_name.type=SCO_ARG_STRING; outfit_name.v.s="Missing outfit";
        char outfit_reply[512]{};
        Require(wear_command(&outfit_name,1,nullptr,outfit_reply,sizeof(outfit_reply))==SCO_NOT_FOUND &&
                std::strstr(outfit_reply,"No enabled data pack supplies lists/outfits.txt"),
                "an absent outfit section is explicitly unavailable without fallback");
    }
    Require(sq42.combos.at(120)==11 && sq42.ship_index==-1 && (sq42.flags.at(123)&SCO_WIDGET_DISABLED),"no default ship; spawn disabled until chosen");
    Send("loadout.sq42",sq42,123,SCO_WIDGET_BUTTON);
    Require(spawns==0,"unchosen ship spawns nothing");
    Require(sq42.build_index==-1 && (sq42.flags.at(132)&SCO_WIDGET_DISABLED),"no default buildable; place disabled until chosen");
    Select("loadout.sq42",sq42,120,0);
    Require(sq42.ship_index==0 && !(sq42.flags.at(123)&SCO_WIDGET_DISABLED),"chosen ship enables spawn");
    Send("loadout.sq42",sq42,123,SCO_WIDGET_BUTTON);
    Require(spawns==1 && spawn_class=="AEGS_Idris_P" && spawn_height==30 && spawn_seat=="pilot" && spawn_ready==1,"default spawn arguments");
    Value("loadout.sq42",sq42,121,SCO_WIDGET_SLIDER,77);
    Value("loadout.sq42",sq42,122,SCO_WIDGET_TOGGLE,0);
    Send("loadout.sq42",sq42,123,SCO_WIDGET_BUTTON);
    Require(spawns==2 && spawn_class=="AEGS_Idris_P" && spawn_height==77 && spawn_seat=="none" && spawn_ready==0,"boarding options forwarded");
    Send("loadout.sq42",sq42,136,SCO_WIDGET_TEXT_INPUT,"vAnDuUl");
    Require(spawns==2 && sq42.ship_labels.size()==4 && sq42.ship_index==-1 && (sq42.flags.at(123)&SCO_WIDGET_DISABLED),"filter hides selection without spawning");
    auto stale_ship=sq42; Select("loadout.sq42",sq42,120,1);
    Send("loadout.sq42",sq42,123,SCO_WIDGET_BUTTON);
    Require(spawns==3 && spawn_class=="VNCL_Scythe_PU_AI_VAN" && spawn_height==300 && spawn_seat=="none" && spawn_ready==0,"filtered source spawn arguments");
    Select("loadout.sq42",stale_ship,120,0); Send("loadout.sq42",stale_ship,123,SCO_WIDGET_BUTTON);
    Require(spawns==3 && sq42.ship_index==1,"stale ship callback refused");
    Select("loadout.sq42",sq42,120,-1); Send("loadout.sq42",sq42,123,SCO_WIDGET_BUTTON);
    Require(spawns==3 && sq42.ship_index==-1 && (sq42.flags.at(123)&SCO_WIDGET_DISABLED),"deselected ship refused");
    Select("loadout.sq42",sq42,120,1);
    Send("loadout.sq42",sq42,136,SCO_WIDGET_TEXT_INPUT,"no match"); Send("loadout.sq42",sq42,123,SCO_WIDGET_BUTTON);
    Require(spawns==3 && sq42.combos.at(120)==0 && sq42.ship_index==-1 && (sq42.flags.at(123)&SCO_WIDGET_DISABLED),"empty ship filter refused");
    Send("loadout.sq42",sq42,136,SCO_WIDGET_TEXT_INPUT,"VANDUUL");
    Require(sq42.ship_index==1,"hidden ship selection retained");
    Send("loadout.sq42",sq42,136,SCO_WIDGET_TEXT_INPUT,std::string(64,'x').c_str()); Send("loadout.sq42",sq42,136,SCO_WIDGET_TEXT_INPUT,"bad\ntext");
    Require(sq42.ship_filter=="VANDUUL" && sq42.ship_index==1,"invalid ship searches rejected");
    Send("loadout.sq42",sq42,136,SCO_WIDGET_TEXT_INPUT,std::string(63,'x').c_str());
    Require(sq42.ship_filter.size()==63 && sq42.combos.at(120)==0,"bounded ship search accepted");
    Send("loadout.sq42",sq42,136,SCO_WIDGET_TEXT_INPUT,"vanduul");
    auto failed_ship=sq42; fail_sq=true; Send("loadout.sq42",sq42,136,SCO_WIDGET_TEXT_INPUT,"bengal");
    Select("loadout.sq42",failed_ship,120,0); Send("loadout.sq42",failed_ship,123,SCO_WIDGET_BUTTON);
    Require(spawns==3 && sq42.accepted==failed_ship.accepted,"failed ship publication refuses old callback");
    fail_sq=false; Send("loadout.player",player,1,SCO_WIDGET_TEXT_INPUT,"");
    Require(sq42.ship_labels.size()==1 && sq42.ship_index==-1,"retry publishes new ship mapping");
    Select("loadout.sq42",sq42,120,0); Send("loadout.sq42",sq42,123,SCO_WIDGET_BUTTON);
    Require(spawns==4 && spawn_class=="RSI_Bengal_PU_AI_UEE" && spawn_height==1500 && spawn_seat=="none" && spawn_ready==0,"retry uses accepted mapping");
    auto before_ship_reload=sq42; Require(reload_command(nullptr,0,nullptr,nullptr,0)==SCO_OK,"ship reload succeeds");
    Send("loadout.sq42",before_ship_reload,123,SCO_WIDGET_BUTTON); Require(spawns==4,"reload invalidates ship callback");
    TickNow();

    Send("loadout.player",player,1,SCO_WIDGET_TEXT_INPUT,"no matching gear");
    Require(!rejected && player.combos.at(2)==0,"empty filtered picker rejected");
    Send("loadout.player",player,1,SCO_WIDGET_TEXT_INPUT,"");
    Require(!rejected && player.combos.at(2)==2,"restored gear picker rejected");
    Select("loadout.player",player,2,0);
    Send("loadout.player",player,27,SCO_WIDGET_BUTTON);
    Require(submits==1 && captured.size()==1 && captured[0].slot==SC_LOADOUT_SLOT_HEAD && !std::strcmp(captured[0].item_class,"FixtureHelmet"),"selected gear submitted");
    Require((player.flags.at(27)&SCO_WIDGET_DISABLED) && (player.flags.at(221)&SCO_WIDGET_DISABLED) && (sq42.flags.at(106)&SCO_WIDGET_DISABLED),"retained job disables new equip requests");
    Require(!(player.flags.at(24)&SCO_WIDGET_DISABLED) && player.job=="Equipping: waiting for the game." && player.status=="Equipping started. Progress shows below.","job progress and cancel are plain and enabled");
    ReadableOnly(player,"player (job)");
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
        Require(sq42.outfit_index==-1 && (sq42.flags.at(106)&SCO_WIDGET_DISABLED),"deselection disables outfit actions");
        Send("loadout.sq42",sq42,106,SCO_WIDGET_BUTTON);
        Require(submits==1,"forged deselected actions refused");
        Select("loadout.sq42",sq42,101,1);
        Select("loadout.sq42",stale,101,0);
        Require(sq42.outfit_index==1,"stale filtered callback ignored");
        Send("loadout.sq42",sq42,135,SCO_WIDGET_TEXT_INPUT,"none");
        Require(sq42.combos.at(101)==0 && sq42.outfit_index==-1 && (sq42.flags.at(106)&SCO_WIDGET_DISABLED),"hidden selection disables apply");
        Send("loadout.sq42",sq42,106,SCO_WIDGET_BUTTON);
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
        files[1].pack="new_sq42_catalog";
        char reload_reply[1024]{};
        Require(reload_command(nullptr,0,nullptr,reload_reply,sizeof(reload_reply))==SCO_OK,"reload succeeded");
        Require(std::strstr(reload_reply,"fixture_armory/lists/items.txt") &&
                std::strstr(reload_reply,"new_sq42_catalog/lists/outfits.txt") &&
                std::strstr(reload_reply,"build_catalog/lists/buildables.txt"),
                "reload publishes a coherent generation with independent copied source identities");
        Select("loadout.sq42",before_reload,101,0); Send("loadout.sq42",before_reload,106,SCO_WIDGET_BUTTON);
        Require(submits==2,"reload invalidates published callbacks");
        TickNow();
        Require(sq42.outfit_labels==std::vector<std::string>({"New flight"}) && sq42.outfit_index==-1,"reload clears source selection");
        Select("loadout.sq42",before_reload,101,0); Send("loadout.sq42",before_reload,106,SCO_WIDGET_BUTTON);
        Require(submits==2 && sq42.outfit_index==-1,"old callback remains stale after the new generation is published");
        Select("loadout.sq42",sq42,101,0);
        async_cap=false; TickNow();
        Require(sq42.flags.count(115) && !sq42.flags.count(135) && !sq42.flags.count(101) && !sq42.flags.count(106) && !sq42.flags.count(137) && !sq42.flags.count(138) && sq42.flags.count(123) && sq42.flags.count(132),
                "without the capability the outfit section is one plain notice and Ships and Objects stay");
        Send("loadout.sq42",sq42,106,SCO_WIDGET_BUTTON); Require(submits==2,"forged unavailable outfit refused");
        Select("loadout.sq42",sq42,101,0); Send("loadout.sq42",sq42,137,SCO_WIDGET_BUTTON); Send("loadout.sq42",sq42,138,SCO_WIDGET_BUTTON); Send("loadout.sq42",sq42,135,SCO_WIDGET_TEXT_INPUT,"");
        Require(submits==2,"forged events for hidden outfit controls start nothing");
        async_cap=true; TickNow(); Send("loadout.sq42",sq42,106,SCO_WIDGET_BUTTON);
        Require(submits==3 && captured.size()==1 && captured[0].slot==SC_LOADOUT_SLOT_HEAD && !std::strcmp(captured[0].item_class,"NewHelmet"),"selected outfit submitted");
        async_cap=false; TickNow();
        Require(sq42.flags.count(115) && sq42.flags.count(107) && sq42.flags.count(108) && !(sq42.flags.at(108)&SCO_WIDGET_DISABLED) && (sq42.flags.at(106)&SCO_WIDGET_DISABLED),
                "async off with a job running keeps progress and Cancel, and the wear button is disabled");
        async_cap=true; TickNow();
        Send("loadout.sq42",sq42,108,SCO_WIDGET_BUTTON); Require(cancels==3,"SQ42 releases job");
        auto before_bad_reload=sq42; files[1].bytes="[broken]\nnot a valid piece\n";
        char failure_reply[1024]{};
        Require(reload_command(nullptr,0,nullptr,failure_reply,sizeof(failure_reply))==SCO_BAD_ARG &&
                std::strstr(failure_reply,"malformed or incomplete"),"malformed list refuses the whole reload");
        Send("loadout.sq42",before_bad_reload,106,SCO_WIDGET_BUTTON); Require(submits==3,"failed reload invalidates callbacks");
        TickNow();
        Require(player.gear_labels.empty() && sq42.combos.at(101)==0 &&
                (sq42.flags.at(106)&SCO_WIDGET_DISABLED),"failed reload clears every published content section");

        files[1].bytes="[Replacement outfit]\nArmor_Helmet ReplacementHelmet\n";
        files.push_back({"duplicate_catalog","lists/outfits.txt","[Injected outfit]\nArmor_Helmet InjectedHelmet\n"});
        std::memset(failure_reply,0,sizeof(failure_reply));
        Require(reload_command(nullptr,0,nullptr,failure_reply,sizeof(failure_reply))==SCO_BAD_ARG &&
                std::strstr(failure_reply,"Ambiguous copied list source"),"duplicate path across packs refuses the whole snapshot");
        TickNow();
        Require(player.gear_labels.empty() && sq42.outfit_labels.empty() && sq42.combos.at(130)==0,
                "duplicate source does not publish an arbitrary winner or partial lists");
        files.pop_back();

        files[2].id=files[0].id;
        std::memset(failure_reply,0,sizeof(failure_reply));
        Require(reload_command(nullptr,0,nullptr,failure_reply,sizeof(failure_reply))==SCO_FAILED &&
                std::strstr(failure_reply,"reused an item identity"),
                "process-duplicate opaque IDs refuse the whole snapshot");
        TickNow();
        Require(player.gear_labels.empty() && sq42.outfit_labels.empty() && sq42.combos.at(130)==0,
                "duplicate opaque IDs publish no partial source sections");
        files[2].id=0x8ABCDE0012345678ULL;

        files[0].bad_pack=true;
        Require(reload_command(nullptr,0,nullptr,failure_reply,sizeof(failure_reply))==SCO_FAILED,
                "invalid source identity refuses the whole snapshot");
        files[0].bad_pack=false;
        files[1].fail_copy=true;
        Require(reload_command(nullptr,0,nullptr,failure_reply,sizeof(failure_reply))==SCO_FAILED,
                "a later content read failure refuses earlier parsed sections too");
        files[1].fail_copy=false;
        files[0].bad_record=true;
        std::memset(failure_reply,0,sizeof(failure_reply));
        Require(reload_command(nullptr,0,nullptr,failure_reply,sizeof(failure_reply))==SCO_FAILED &&
                std::strstr(failure_reply,"invalid list record"),
                "incorrect public record extent refuses the whole snapshot");
        files[0].bad_record=false;
        TickNow();
        Require(player.gear_labels.empty() && sq42.outfit_labels.empty() && sq42.combos.at(101)==0,
                "failed copied-content reads never leave a partial snapshot visible");
    }
    auto before_unload=sq42; sco::features::loadout::Unload(); sco::features::loadout::Unload();
    Send("loadout.sq42",before_unload,123,SCO_WIDGET_BUTTON); Require(spawns==4,"unloaded ship callback ignored");
    Send("loadout.sq42",before_unload,106,SCO_WIDGET_BUTTON); Require(submits==(populated ? 3u : 1u),"unloaded callback ignored");
    Require(removed==2 && unsubscribed==1,"unload cleanup mismatch");
}
// Ships and objects are other features' commands: without them the page says so instead of offering a button that refuses.
void MissingFeatures() {
    files={{"fixture_armory","lists/items.txt","[helmet]\nFixtureHelmet\n"}};
    files.push_back({"build_catalog","lists/buildables.txt","[sq42]\nNPC_Archetypes-Male-Human-MissionGivers-MilesEckhart\n"});
    files[0].id=1; files[1].id=2;
    player={}; sq42={}; rejected=0; removed=0; unsubscribed=0; fail_sq=false; reload_command=nullptr; items_command=nullptr; wear_command=nullptr; tick_event=nullptr; tick_time=0; async_cap=provider=true; supported=SC_LOADOUT_VERSION_1_2;
    sco_api api{}; api.size=sizeof(api); api.major=SCO_API_MAJOR; api.minor=SCO_API_MINOR;
    api.has=Has; api.register_command=Register; api.query_service=Query; api.invoke=Invoke; api.list_commands=ListCommands;
    api.subscribe=Subscribe; api.unsubscribe=Unsubscribe;
    unsigned char owner=0; auto* self=reinterpret_cast<sco_plugin*>(&owner);
    spawn_listed=true; build_listed=true;
    Require(sco::features::loadout::Load(&api,self)==SCO_OK,"missing-feature load failed");
    Send("loadout.sq42",sq42,100,SCO_WIDGET_BUTTON);
    Require(sq42.flags.count(123) && sq42.flags.count(132),"present features keep their buttons");
    // build.place answers in technical words (its own page shows a plain sentence); this page must say the outcome plainly.
    places=0; place_result=SCO_UNAVAILABLE; place_reply="Build ground placement unavailable: game.world missing/disabled; no private fallback";
    Select("loadout.sq42",sq42,130,0); Send("loadout.sq42",sq42,132,SCO_WIDGET_BUTTON);
    { bool plain=false, technical=false; for (const auto& word:sq42.words) { plain|=word=="Couldn't place that object here. Try somewhere else."; technical|=word.find("game.world")!=std::string::npos; }
      Require(places==1 && plain && !technical,"a refused object placement shows a plain sentence, not build's technical reply"); }
    place_result=SCO_OK; place_reply="Owned placement initiated; streaming unconfirmed"; Send("loadout.sq42",sq42,132,SCO_WIDGET_BUTTON);
    { bool plain=false; for (const auto& word:sq42.words) plain|=word=="Placed the object. It can take a moment to appear.";
      Require(places==2 && plain,"an accepted object placement shows a plain sentence"); }
    spawn_listed=false; TickNow();
    Require(!sq42.flags.count(123) && !sq42.flags.count(120) && sq42.flags.count(124) && sq42.flags.count(132),"missing ship feature replaces the ship controls with a plain line");
    build_listed=false; TickNow();
    Require(!sq42.flags.count(132) && !sq42.flags.count(130) && sq42.flags.count(141),"missing build feature replaces the object controls with a plain line");
    ReadableOnly(sq42,"squadron 42 (features missing)");
    spawn_listed=build_listed=true; TickNow();
    Require(sq42.flags.count(132) && sq42.combos.at(130)==1,"features back: controls return");
    Require(std::find(sq42.words.begin(),sq42.words.end(),"Male Human Mission Givers Miles Eckhart")!=sq42.words.end(),"raw object names are cleaned for reading");
    sco::features::loadout::Unload();
}
// Long SQ42 outfit and buildable lists are paged 64 rows at a time (ids 137/138 and 139/140).
std::string NumberedLines(const char* section,const char* prefix,unsigned count,bool outfits) {
    std::string text=section;
    for (unsigned i=0;i<count;++i) {
        text+=outfits ? std::string("Armor_Helmet ")+prefix+std::to_string(i)+"\n" : std::string(prefix)+std::to_string(i)+"\n";
    }
    return text;
}
void Paging(unsigned outfits,unsigned builds) {
    files={{"fixture_armory","lists/items.txt","[helmet]\nFixtureHelmet\n"}};
    std::string outfit_text;
    for (unsigned i=0;i<outfits;++i) outfit_text+="[Outfit "+std::to_string(i)+"]\nArmor_Helmet Helmet"+std::to_string(i)+"\n";
    files.push_back({"sq42_catalog","lists/outfits.txt",outfit_text});
    files.push_back({"build_catalog","lists/buildables.txt",NumberedLines("[sq42]\n","Object",builds,false)});
    files[0].id=1; files[1].id=2; files[2].id=3;
    player={}; sq42={}; rejected=0; removed=0; unsubscribed=0; fail_sq=false; reload_command=nullptr; items_command=nullptr; wear_command=nullptr; tick_event=nullptr; tick_time=0; async_cap=provider=true; supported=SC_LOADOUT_VERSION_1_2;
    sco_api api{}; api.size=sizeof(api); api.major=SCO_API_MAJOR; api.minor=SCO_API_MINOR;
    api.has=Has; api.register_command=Register; api.query_service=Query; api.invoke=Invoke; api.list_commands=ListCommands; spawn_listed=build_listed=true;
    api.subscribe=Subscribe; api.unsubscribe=Unsubscribe;
    unsigned char owner=0; auto* self=reinterpret_cast<sco_plugin*>(&owner);
    Require(sco::features::loadout::Load(&api,self)==SCO_OK,"paging load failed");
    Send("loadout.sq42",sq42,100,SCO_WIDGET_BUTTON);
    const auto disabled=[&](uint32_t id) { return (sq42.flags.at(id)&SCO_WIDGET_DISABLED)!=0; };
    // Outfits: 64 fit one page; the 65th is on a second page.
    Require(sq42.combos.at(101)==std::min(outfits,64u),"first outfit page holds at most 64 rows");
    Require(disabled(137),"no previous outfit page on the first page");
    Require(disabled(138)==(outfits<=64),"next outfit page exists only beyond 64 outfits");
    Select("loadout.sq42",sq42,101,0);
    Require(sq42.outfit_index==0 && !disabled(106),"first outfit selected and wearable");
    if (outfits>64) {
        Send("loadout.sq42",sq42,138,SCO_WIDGET_BUTTON);
        Require(sq42.combos.at(101)==outfits-64,"second outfit page shows the remainder");
        Require(sq42.outfit_index==-1 && disabled(106),"selection off the page disables Wear");
        Require(!disabled(137) && disabled(138),"previous enabled and next disabled on the last outfit page");
        Send("loadout.sq42",sq42,138,SCO_WIDGET_BUTTON); // already last: a forged Next stays put
        Require(sq42.combos.at(101)==outfits-64 && !disabled(137),"a forged Next past the last page is ignored");
        Send("loadout.sq42",sq42,137,SCO_WIDGET_BUTTON);
        Require(sq42.combos.at(101)==64 && sq42.outfit_index==0 && !disabled(106),"previous returns to the selected outfit");
    }
    // Buildables.
    Require(sq42.combos.at(130)==std::min(builds,64u),"first buildable page holds at most 64 rows");
    Require(disabled(139),"no previous buildable page on the first page");
    Require(disabled(140)==(builds<=64),"next buildable page exists only beyond 64 buildables");
    Select("loadout.sq42",sq42,130,0);
    Require(sq42.build_index==0 && !disabled(132),"first buildable selected and placeable");
    if (builds>64) {
        Send("loadout.sq42",sq42,140,SCO_WIDGET_BUTTON);
        Require(sq42.combos.at(130)==builds-64,"second buildable page shows the remainder");
        Require(sq42.build_index==-1 && disabled(132),"selection off the page disables Place");
        Require(!disabled(139) && disabled(140),"previous enabled and next disabled on the last buildable page");
        Send("loadout.sq42",sq42,139,SCO_WIDGET_BUTTON);
        Require(sq42.combos.at(130)==64 && sq42.build_index==0 && !disabled(132),"previous returns to the selected buildable");
    }
    sco::features::loadout::Unload();
}
// Outfits that use a port Core has no slot for (clothing, hats, faces) can never be worn. The page must not
// offer them, must say so in plain words, and every outfit it does offer must really be submittable.
void StartSq42(const std::string& outfits,bool outfit=false) {
    files={{"fixture_armory","lists/items.txt","[helmet]\nFixtureHelmet\n"},{"sq42_catalog","lists/outfits.txt",outfits}};
    files[0].id=1; files[1].id=2;
    player={}; sq42={}; rejected=0; removed=0; unsubscribed=0; fail_sq=false; reload_command=nullptr; items_command=nullptr; wear_command=nullptr; tick_event=nullptr; tick_time=0; submits=0; cancels=0; captured.clear(); async_cap=provider=true; outfit_cap=outfit; supported=SC_LOADOUT_VERSION_1_2;
    static sco_api api{}; api={};   // the plugin keeps the pointer until Unload
    api.size=sizeof(api); api.major=SCO_API_MAJOR; api.minor=SCO_API_MINOR;
    api.has=Has; api.register_command=Register; api.query_service=Query; api.invoke=Invoke; api.list_commands=ListCommands; spawn_listed=build_listed=true;
    api.subscribe=Subscribe; api.unsubscribe=Unsubscribe;
    static unsigned char owner=0; auto* self=reinterpret_cast<sco_plugin*>(&owner);
    Require(sco::features::loadout::Load(&api,self)==SCO_OK,"outfit load failed");
    Send("loadout.sq42",sq42,100,SCO_WIDGET_BUTTON);
}
bool Said(const Panel& panel,const std::string& text) { return std::find(panel.words.begin(),panel.words.end(),text)!=panel.words.end(); }
void UnwearableOutfits() {
    // A mix: only the armour outfit is offered; the clothing, hat and face outfits are not.
    StartSq42("[Clothes]\nClothing_Torso_1 FixtureJacket\n[Armour]\nArmor_Helmet FixtureHelmet\nArmor_Torso FixtureTorso\n"
              "[Hat]\nArmor_Helmet FixtureHelmet\nHat_ItemPort FixtureHat\n[Face]\nHead_ItemPort FixtureHead\n[Twice]\nArmor_Helmet A\nArmor_Helmet B\n");
    Require(sq42.outfit_labels==std::vector<std::string>({"Armour"}),"only outfits whose pieces all have a slot are offered");
    Require(Said(sq42,"4 outfits aren't shown because they use parts the game can't change yet."),"hidden outfits are explained in plain words");
    ReadableOnly(sq42,"squadron 42 (hidden outfits)");
    Send("loadout.sq42",sq42,135,SCO_WIDGET_TEXT_INPUT,"clothes");
    Require(sq42.combos.at(101)==0,"search cannot bring a hidden outfit back");
    Send("loadout.sq42",sq42,135,SCO_WIDGET_TEXT_INPUT,"");
    // The same refusal by command says so plainly and sends nothing.
    sco_arg name{}; name.type=SCO_ARG_STRING; name.v.s="Clothes"; char reply[512]{};
    Require(wear_command(&name,1,nullptr,reply,sizeof(reply))==SCO_UNAVAILABLE && submits==0,"an unwearable outfit by name sends nothing");
    Require(std::strstr(reply,"nothing sent"),"the refusal reply says nothing was sent");
    sco::features::loadout::Unload();
    // Nothing wearable at all: no picker or Wear button that could only fail, one plain sentence instead.
    StartSq42("[Clothes]\nClothing_Torso_1 FixtureJacket\n[Face]\nHead_ItemPort FixtureHead\n");
    Require(!sq42.flags.count(101) && !sq42.flags.count(106) && !sq42.flags.count(135),"no outfit controls when none can be worn");
    Require(Said(sq42,"None of these outfits can be worn yet. They use parts the game can't change yet."),"the page says why there is no outfit picker");
    ReadableOnly(sq42,"squadron 42 (no wearable outfit)");
    sco::features::loadout::Unload();
    // With async off the section still says only that it is not turned on.
    StartSq42("[Armour]\nArmor_Helmet FixtureHelmet\n");
    async_cap=false; TickNow();
    Require(!sq42.flags.count(106) && Said(sq42,"Changing outfits isn't turned on."),"async off: one plain sentence, no Wear button");
    sco::features::loadout::Unload();
}
// Core 2.4.1 gives clothing, hat, head accessory, head horn and jewellery a slot (22 to 31) while game.loadout.outfit is
// on. It has no slot for hair, hair colour, eyebrows, head, body, eyes or teeth, so an outfit using one of those is never
// offered and is refused whole by name; the "left out" count is kept for those.
void ClothingOutfits() {
    const std::string list="[Armour]\nArmor_Helmet FixtureHelmet\n[Clothes]\nClothing_Torso_1 FixtureJacket\nClothing_Legs FixturePants\nClothing_Feet FixtureBoots\n"
        "[Hat]\nHat_ItemPort FixtureHat\nJewellery_ItemPort FixtureRing\n[Horns]\nHead_Accessory_ItemPort FixtureCap\nHead_Horn_ItemPort FixtureHorn\n"
        "[Hair]\nHair_ItemPort FixtureHair\nEyebrow_ItemPort FixtureBrow\n[HairColour]\nHair_Color FixtureDye\n"
        "[Face]\nHead_ItemPort FixtureHead\nClothing_Torso_1 FixtureJacket\n[Twice]\nClothing_Legs A\nClothing_Legs B\n";
    // Capability off: only the armour outfit.
    StartSq42(list);
    Require(sq42.outfit_labels==std::vector<std::string>({"Armour"}),"outfit capability off: clothing outfits stay out");
    Require(Said(sq42,"7 outfits aren't shown because they use parts the game can't change yet."),"capability off: seven left out");
    sco::features::loadout::Unload();
    // Outfit capability on: clothing, hat, head accessory, horn and jewellery are wearable; hair and eyebrows never are.
    StartSq42(list,true);
    Require(sq42.outfit_labels==std::vector<std::string>({"Armour","Clothes","Hat","Horns"}),"outfit capability on: outfits whose parts all have a slot are offered");
    Require(Said(sq42,"4 outfits aren't shown because they use parts the game can't change yet."),"hair, hair colour, face and duplicate outfits are left out");
    ReadableOnly(sq42,"squadron 42 (clothing outfits)");
    Select("loadout.sq42",sq42,101,1); // Clothes
    const auto before=submits; Send("loadout.sq42",sq42,106,SCO_WIDGET_BUTTON);
    Require(submits==before+1 && captured.size()==3,"a clothing outfit is submitted");
    Require(captured[0].slot==23 && captured[1].slot==25 && captured[2].slot==26 &&
            !std::strcmp(captured[0].item_class,"FixtureJacket"),"clothing pieces go to the Core outfit slots");
    Send("loadout.sq42",sq42,108,SCO_WIDGET_BUTTON);
    Select("loadout.sq42",sq42,101,2); // Hat
    Send("loadout.sq42",sq42,106,SCO_WIDGET_BUTTON);
    Require(captured.size()==2 && captured[0].slot==28 && captured[1].slot==31,"hat and jewellery slots");
    Send("loadout.sq42",sq42,108,SCO_WIDGET_BUTTON);
    Select("loadout.sq42",sq42,101,3); // Horns
    Send("loadout.sq42",sq42,106,SCO_WIDGET_BUTTON);
    Require(captured.size()==2 && captured[0].slot==29 && captured[1].slot==30 &&
            !std::strcmp(captured[0].item_class,"FixtureCap") && !std::strcmp(captured[1].item_class,"FixtureHorn"),"head accessory and head horn use slots 29 and 30");
    Send("loadout.sq42",sq42,108,SCO_WIDGET_BUTTON);
    // Hair, hair colour and eyebrows have no slot: by name the whole outfit is refused in plain words and nothing is sent.
    sco_arg hair{}; hair.type=SCO_ARG_STRING; hair.v.s="Hair"; char hair_reply[512]{};
    auto sent=submits;
    Require(wear_command(&hair,1,nullptr,hair_reply,sizeof(hair_reply))==SCO_UNAVAILABLE && submits==sent,"an outfit with hair sends nothing");
    Require(std::strstr(hair_reply,"Hair_ItemPort") && std::strstr(hair_reply,"nothing sent"),"the hair refusal names the port and says nothing was sent");
    TickNow();
    Require(Said(sq42,"This outfit includes hair, and the game has nowhere to put it yet. Nothing was changed."),"the hair refusal is a plain sentence naming the part");
    sco_arg dye{}; dye.type=SCO_ARG_STRING; dye.v.s="HairColour"; char dye_reply[512]{};
    Require(wear_command(&dye,1,nullptr,dye_reply,sizeof(dye_reply))==SCO_UNAVAILABLE && submits==sent,"an outfit with hair colour sends nothing");
    TickNow();
    Require(Said(sq42,"This outfit includes hair colour, and the game has nowhere to put it yet. Nothing was changed."),"the hair colour refusal names the part");
    // The capability is revoked after the page was drawn: the request is refused, nothing is sent.
    outfit_cap=false;
    sco_arg name{}; name.type=SCO_ARG_STRING; name.v.s="Clothes"; char reply[512]{};
    sent=submits;
    Require(wear_command(&name,1,nullptr,reply,sizeof(reply))==SCO_UNAVAILABLE && submits==sent,"capability off: a clothing outfit by name sends nothing");
    TickNow();
    Require(Said(sq42,"Changing outfits isn't turned on."),"capability off: the refusal says outfits are not turned on");
    outfit_cap=true;
    sco::features::loadout::Unload();
    // A capability name Core 2.4.1 does not have changes nothing.
    StartSq42(list,false);
    Require(sq42.outfit_labels==std::vector<std::string>({"Armour"}),"without the outfit capability only armour is offered");
    sco::features::loadout::Unload();
}
// The shipped data/outfits.txt, parsed here without the plugin: every outfit the page offers can be submitted.
void ShippedOutfits(const char* path) {
    std::ifstream file(path,std::ios::binary); Require(bool(file),"shipped outfits file readable");
    const std::string bytes((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    const std::set<std::string> mapped{"Armor_Undersuit","Armor_Helmet","Armor_Torso","Armor_Arms","Armor_Legs","backpack","Lens_ItemPort"};
    struct Entry { std::string name; std::vector<std::string> ports; };
    std::vector<Entry> all; std::string line;
    for (size_t pos=0;pos<=bytes.size();) {
        const auto end=bytes.find('\n',pos); line=bytes.substr(pos,end==std::string::npos ? std::string::npos : end-pos);
        pos=end==std::string::npos ? bytes.size()+1 : end+1;
        line=line.substr(0,line.find('#')); while (!line.empty() && (line.back()=='\r' || line.back()==' ')) line.pop_back();
        if (line.empty()) continue;
        if (line[0]=='[') all.push_back({line.substr(1,line.size()-2),{}}); else all.back().ports.push_back(line.substr(0,line.find_first_of(" \t")));
    }
    std::vector<std::string> expected;
    for (const auto& e:all) if (std::all_of(e.ports.begin(),e.ports.end(),[&](const std::string& p) { return mapped.count(p)!=0; })) expected.push_back(e.name);
    Require(all.size()>expected.size() && !expected.empty(),"shipped data has both wearable and unwearable outfits");
    StartSq42(bytes);
    Require(sq42.outfit_labels.size()==expected.size(),"the page offers exactly the outfits whose parts all have a slot");
    Require(Said(sq42,std::to_string(all.size()-expected.size())+" outfits aren't shown because they use parts the game can't change yet."),"hidden shipped outfits are explained");
    ReadableOnly(sq42,"squadron 42 (shipped outfits)");
    for (size_t i=0;i<sq42.outfit_labels.size();++i) {
        Select("loadout.sq42",sq42,101,static_cast<int>(i));
        Require(!(sq42.flags.at(106)&SCO_WIDGET_DISABLED),"an offered outfit enables Wear");
        const auto before=submits; Send("loadout.sq42",sq42,106,SCO_WIDGET_BUTTON);
        Require(submits==before+1,"every outfit the page offers is accepted by the equip route, none can only fail");
        Send("loadout.sq42",sq42,108,SCO_WIDGET_BUTTON); Require(cancels==static_cast<unsigned>(i+1),"released for the next outfit");
    }
    sco::features::loadout::Unload();
    // With the outfit capability on, the offered set grows to outfits whose ports are all armour or clothing-class.
    std::set<std::string> wider=mapped;
    for (const char* p:{"Clothing_Torso_0","Clothing_Torso_1","Clothing_Torso2","Clothing_Legs","Clothing_Feet","Clothing_Hands","Hat_ItemPort",
                        "Head_Accessory_ItemPort","Head_Horn_ItemPort","Jewellery_ItemPort"}) wider.insert(p);
    size_t grown=0;
    for (const auto& e:all) {
        if (!std::all_of(e.ports.begin(),e.ports.end(),[&](const std::string& p) { return wider.count(p)!=0; })) continue;
        std::set<std::string> distinct(e.ports.begin(),e.ports.end());
        if (distinct.size()==e.ports.size() && e.ports.size()<=16) ++grown;   // same rule the plugin applies: no shared slot, one request
    }
    Require(grown>expected.size(),"shipped data has outfits that only the new slots make wearable");
    StartSq42(bytes,true);
    Require(sq42.outfit_labels.size()==grown,"with the outfit capability on the page offers every outfit whose parts all have a slot");
    Require(Said(sq42,std::to_string(all.size()-grown)+" outfits aren't shown because they use parts the game can't change yet."),"the remainder is still counted as left out");
    for (size_t i=0;i<sq42.outfit_labels.size();++i) {
        Select("loadout.sq42",sq42,101,static_cast<int>(i));
        const auto before=submits; Send("loadout.sq42",sq42,106,SCO_WIDGET_BUTTON);
        Require(submits==before+1,"every outfit offered with the new slots is accepted by the equip route");
        Send("loadout.sq42",sq42,108,SCO_WIDGET_BUTTON);
    }
    sco::features::loadout::Unload();
}
}
int main(int argc,char** argv) {
    content.size=sizeof(content); content.item=Item; content.read=Read;
    loadout.size=sizeof(loadout); loadout.submit=Submit; loadout.status=Status; loadout.cancel=Cancel;
    widgets.size=sizeof(widgets); widgets.set_panel=SetPanel; widgets.remove_panel=RemovePanel;
    Workflow(false); Workflow(true);
    Paging(65,64); Paging(64,65); Paging(65,65);
    MissingFeatures();
    UnwearableOutfits();
    ClothingOutfits();
    if (argc>1) ShippedOutfits(argv[1]);
    std::puts("loadout widget workflow: PASS (synthetic public services; not run in game)");
}
