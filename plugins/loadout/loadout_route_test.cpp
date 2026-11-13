// SPDX-License-Identifier: GPL-3.0-only
// Actual consumer entry points and copied public RC1 records; synthetic providers only.
#include "loadout.h"
#include "sc_loadout.h"
#include "sco_widgets.h"
#include "sco_content.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {
unsigned checks=0, applies=0, submits=0, statuses=0, cancels=0;
sco_plugin* owner=nullptr;
std::map<std::string,sco_command_fn> commands;
sco_widget_event_fn panel_event=nullptr;
void* panel_context=nullptr;
std::map<uint32_t,sco_widget_node> player;
std::string copied_status;
bool apply_cap=true, weapons_cap=true, async_cap=true, present=true;
uint32_t supported=SC_LOADOUT_VERSION_1_2, queried=0;
sc_loadout_v1_2 service{};
const void* publication=&service;
std::vector<sc_loadout_item> captured;
sco_result apply_result=SCO_OK, submit_result=SCO_OK, status_result=SCO_OK, cancel_result=SCO_OK;
uint64_t submitted_id=42;
sc_loadout_job_report progress{};
int corrupt=0; bool reenter_submit=false, reenter_cancel=false, unload_on_lookup=false; sco_result nested=SCO_OK, nested_cancel=SCO_OK;
char reply[2048]{};
std::string list_items, list_outfits;
sco_content_v1 content{};
sco_result Item(sco_plugin*,sco_content_kind,uint32_t ordinal,sco_content_item* item) {
    if(ordinal>=2) return SCO_NOT_FOUND;
    *item={}; item->size=sizeof(*item); item->id=ordinal+1;
    std::snprintf(item->name,sizeof(item->name),"%s",ordinal ? "lists/outfits.txt" : "lists/items.txt"); return SCO_OK;
}
sco_result Read(sco_plugin*,uint64_t id,void* out,uint32_t* size) {
    const auto& bytes=id==1 ? list_items : list_outfits;
    if(*size<bytes.size()) { *size=static_cast<uint32_t>(bytes.size()); return SCO_TOO_MANY; }
    if(!bytes.empty()) std::memcpy(out,bytes.data(),bytes.size());
    *size=static_cast<uint32_t>(bytes.size()); return SCO_OK;
}
void Require(bool ok,const char* message) {
    ++checks; if (!ok) { std::fprintf(stderr,"loadout route: %s (%s)\n",message,reply); std::exit(1); }
}
void Capture(sco_plugin* self,const sc_loadout_request* request) {
    Require(self==owner,"live owner forwarded");
    Require(request && request->size==sizeof(*request) && !request->_pad,"request extent/reserved");
    Require(request->count && request->count<=16 && request->item_stride==sizeof(sc_loadout_item),"request array bounds");
    captured.assign(request->items,request->items+request->count);
    for (const auto& item:captured) Require(!item._pad && std::memchr(item.item_class,0,sizeof(item.item_class)),"copied fixed item");
}
sc_loadout_report Report() {
    sc_loadout_report r{}; r.size=sizeof(r); r.count=static_cast<uint32_t>(captured.size());
    for (uint32_t i=0;i<r.count;++i) r.slots[i]={captured[i].slot,SC_LOADOUT_APPLIED};
    r.applied=r.count; return r;
}
sco_result Apply(sco_plugin* self,const sc_loadout_request* request,sc_loadout_report* out) {
    ++applies; Capture(self,request); Require(out && out->size==sizeof(*out),"report caller extent");
    *out=Report();
    if (apply_result!=SCO_OK) { out->slots[0].status=SC_LOADOUT_UNAVAILABLE; --out->applied; }
    switch(corrupt) {
        case 1: --out->size; break;
        case 2: ++out->count; break;
        case 3: ++out->slots[0].slot; break;
        case 4: out->slots[0].status=static_cast<sc_loadout_status>(9); break;
        case 5: ++out->applied; break;
        case 6: out->slots[0].status=SC_LOADOUT_FAILED; break;
        default: break;
    }
    return apply_result;
}
sco_result Submit(sco_plugin* self,const sc_loadout_request* request,uint64_t* id) {
    ++submits; Capture(self,request); Require(id && !*id,"submit output initialized");
    if(reenter_submit) { sco_arg a{}; a.type=SCO_ARG_STRING; a.v.s="HelmetA"; nested=commands.at("loadout.submit_gear")(&a,1,nullptr,nullptr,0); }
    *id=submitted_id; return submit_result;
}
sco_result Status(sco_plugin* self,uint64_t id,sc_loadout_job_report* out) {
    ++statuses; Require(self==owner && id==42,"status owner/job");
    Require(out && out->size==sizeof(*out) && out->report.size==sizeof(out->report),"status output extent");
    if (status_result==SCO_OK) *out=progress;
    return status_result;
}
sco_result Cancel(sco_plugin* self,uint64_t id) {
    ++cancels; Require(self==owner && id==42,"cancel owner/job");
    if(reenter_cancel) {
        nested_cancel=commands.at("loadout.cancel")(nullptr,0,nullptr,nullptr,0);
        sco_arg a{}; a.type=SCO_ARG_STRING; a.v.s="primary=Replacement";
        nested=commands.at("loadout.submit")(&a,1,nullptr,nullptr,0);
    }
    return cancel_result;
}
sco_result Query(const char* name,uint32_t version,const void** out) {
    *out=nullptr;
    if (!std::strcmp(name,SC_LOADOUT_SERVICE_NAME)) {
        queried=version;
        if (unload_on_lookup) { unload_on_lookup=false; sco::features::loadout::Unload(); }
        if (present && version<=supported) { *out=publication; return SCO_OK; }
    }
    return SCO_NOT_FOUND;
}
int Has(const char* name) {
    return !std::strcmp(name,"game.loadout.apply") ? apply_cap : !std::strcmp(name,"game.loadout.weapons") ? weapons_cap :
        !std::strcmp(name,"game.loadout.async") && async_cap;
}
sco_result Register(sco_plugin* self,const sco_command* command) {
    Require(self==owner,"registration owner"); commands[command->name]=command->fn; return SCO_OK;
}
sco_result Subscribe(sco_plugin*,const char*,sco_event_fn,void*) { return SCO_OK; }
sco_result Unsubscribe(sco_plugin*,const char*,sco_event_fn) { return SCO_OK; }
sco_result SetPanel(sco_plugin*,const char* id,const char*,const sco_widget_node* nodes,uint32_t count,
                    uint32_t stride,sco_widget_event_fn event,void* context) {
    Require(stride==sizeof(*nodes),"widget stride");
    if (!std::strcmp(id,"loadout.player")) {
        player.clear(); for (uint32_t i=0;i<count;++i) {
            player[nodes[i].id]=nodes[i];
            if (nodes[i].id==14) copied_status=nodes[i].label ? nodes[i].label : "";
        }
        panel_event=event;
        panel_context=context;
    }
    return SCO_OK;
}
sco_result WidgetsQuery(const char* name,uint32_t version,const void** out) {
    static sco_widgets_v1 widgets{};
    widgets.size=sizeof(widgets); widgets.set_panel=SetPanel;
    if (!std::strcmp(name,SCO_CONTENT_NAME)) { *out=&content; return SCO_OK; }
    if (!std::strcmp(name,SCO_WIDGETS_NAME)) { Require(version==0x10000u,"widgets prefix version"); *out=&widgets; return SCO_OK; }
    return Query(name,version,out);
}
sco_result Call(const char* name,const char* text=nullptr) {
    Require(commands.count(name)!=0,"requested command registered"); reply[0]=0;
    sco_arg arg{}; arg.type=SCO_ARG_STRING; arg.v.s=text;
    return commands.at(name)(text ? &arg : nullptr,text ? 1 : 0,nullptr,reply,sizeof(reply));
}
void Reset() {
    sco::features::loadout::Unload(); commands.clear();
    list_items="[helmet]\nHelmetA\n[torso]\nTorsoA\n[ammo]\nAmmoA\n[primary]\nWeaponA\n[sidearm]\nSidearmA\n[helmet]\nDefault\nNone\n[torso]\nHelmetA\n"; list_outfits="[Good]\nArmor_Torso TorsoA\nArmor_Helmet HelmetA\n[Bad]\nAmmo AmmoA\n[Duplicate]\nArmor_Helmet HelmetA\nArmor_Helmet HelmetB\n"; content.size=sizeof(content); content.item=Item; content.read=Read;
    service={}; service.size=sizeof(service); service.apply=Apply; service.submit=Submit; service.status=Status; service.cancel=Cancel;
    publication=&service; supported=SC_LOADOUT_VERSION_1_2; present=apply_cap=weapons_cap=async_cap=true;
    apply_result=submit_result=status_result=cancel_result=SCO_OK; submitted_id=42; corrupt=0; reenter_submit=false; reenter_cancel=false; unload_on_lookup=false;
    applies=submits=statuses=cancels=0; captured.clear(); copied_status.clear(); reply[0]=0;
    static unsigned char token=0; owner=reinterpret_cast<sco_plugin*>(&token);
    static sco_api api{}; api={}; api.size=sizeof(api); api.major=SCO_API_MAJOR; api.minor=SCO_API_MINOR;
    api.register_command=Register; api.query_service=WidgetsQuery; api.has=Has; api.subscribe=Subscribe; api.unsubscribe=Unsubscribe;
    Require(sco::features::loadout::Load(&api,owner)==SCO_OK,"load consumer");
}
void Send(uint32_t node,uint32_t kind,const char* text=nullptr) {
    Require(panel_event!=nullptr,"player callback");
    sco_widget_event event{}; event.size=sizeof(event); event.node_id=node; event.kind=kind; event.text=text;
    panel_event("loadout.player",&event,panel_context);
}
void SetProgress(sc_loadout_job_state state,uint32_t completed) {
    progress={}; progress.size=sizeof(progress); progress.state=state; progress.completed=completed;
    progress.report=Report(); progress.report.applied=completed;
    for (uint32_t i=completed;i<progress.report.count;++i) progress.report.slots[i].status=SC_LOADOUT_UNAVAILABLE;
}
void Sync() {
    Reset();
    Require(Call("loadout.slots","primary_optic=FixtureOptic,primary=FixtureWeapon")==SCO_OK,"explicit slots apply");
    Require(queried==SC_LOADOUT_VERSION_1_1 && captured.size()==2 && captured[0].slot==8 && captured[1].slot==10,"public version, parent precedes attachment");
    Require(!std::strcmp(captured[1].item_class,"FixtureOptic"),"class copied exactly");
    const char* names[]{"primary","sidearm","primary_optic","primary_barrel","primary_underbarrel","sidearm_optic","sidearm_barrel","sidearm_underbarrel",
        "primary_tool_module","primary_tool_canister","primary_tool_battery","sidearm_tool_module","sidearm_tool_canister","sidearm_tool_battery"};
    std::string all;
    for (const auto* name:names) { if (!all.empty()) all+=','; all+=std::string(name)+"=FixtureClass"; }
    Require(Call("loadout.slots",all.c_str())==SCO_OK && captured.size()==14,"all public weapon/tool slots");
    for (uint32_t i=0;i<14;++i) Require(captured[i].slot==8+i,"canonical public slot number");
    const unsigned before=applies;
    for (const char* bad:{"","primary=","primary=None","primary=none","primary=Fixture,primary=Other","unknown=Fixture","Armor_Helmet=Fixture","primary=Bad-Class",
            "primary=../Class","primary=Fixture,","primary=Fixture,,sidearm=Other","primary=Fixture=Other","Primary=Fixture","primary=Cl\xC3\xA4ss"})
        Require(Call("loadout.slots",bad)==SCO_BAD_ARG,"invalid/ambiguous input refused");
    Require(applies==before,"invalid inputs make no calls");
    Require(Call("loadout.slots",std::string(2048,'x').c_str())==SCO_BAD_ARG,"command byte bound");
    Require(commands.at("loadout.slots")(nullptr,1,nullptr,reply,sizeof(reply))==SCO_BAD_ARG,"missing argument array");
    sco_arg bad{}; bad.type=SCO_ARG_INT;
    Require(commands.at("loadout.slots")(&bad,1,nullptr,reply,sizeof(reply))==SCO_BAD_ARG,"wrong argument type");
    Require(commands.at("loadout.slots")(&bad,2,nullptr,reply,sizeof(reply))==SCO_BAD_ARG,"wrong argument count");
    Require(commands.at("loadout.slots")(&bad,1,nullptr,nullptr,1)==SCO_BAD_ARG,"invalid reply extent");
    Require(Call("loadout.slots",("primary="+std::string(95,'A')).c_str())==SCO_OK,"95 byte class");
    Require(Call("loadout.slots",("primary="+std::string(96,'A')).c_str())==SCO_BAD_ARG,"96 byte class refused");
    supported=SC_LOADOUT_VERSION_1_0;
    Require(Call("loadout.slots","primary=Fixture")==SCO_UNAVAILABLE && queried==SC_LOADOUT_VERSION_1_1,"old host no prefix fallback");
    supported=SC_LOADOUT_VERSION_1_2;
    for (int capability=0;capability<2;++capability) {
        apply_cap=capability!=0; weapons_cap=capability==0;
        Require(Call("loadout.slots","primary=Fixture")==SCO_UNAVAILABLE,"required capability refusal");
    }
    apply_cap=weapons_cap=true; present=false;
    Require(Call("loadout.slots","primary=Fixture")==SCO_UNAVAILABLE,"provider absent"); present=true;
    for (uint32_t bytes=4;bytes<offsetof(sc_loadout_v1_2,apply)+sizeof(service.apply);++bytes) {
        auto short_table=std::make_unique<unsigned char[]>(bytes);
        std::memcpy(short_table.get(),&service,bytes); std::memcpy(short_table.get(),&bytes,4); publication=short_table.get();
        Require(Call("loadout.slots","primary=Fixture")==SCO_UNAVAILABLE,"physically short apply pointer refused");
    }
    publication=&service; service.apply=nullptr;
    Require(Call("loadout.slots","primary=Fixture")==SCO_UNAVAILABLE,"null apply refused"); service.apply=Apply;
    apply_result=SCO_FAILED;
    Require(Call("loadout.slots","primary=Fixture,sidearm=Other")==SCO_FAILED && std::strstr(reply,"primary=unavailable") && std::strstr(reply,"sidearm=applied"),"partial status forwarded");
    apply_result=SCO_NOT_FOUND; Require(Call("loadout.slots","primary=Fixture")==SCO_NOT_FOUND,"structural result forwarded"); apply_result=SCO_OK;
    apply_result=SCO_WRONG_THREAD; Require(Call("loadout.slots","primary=Fixture")==SCO_WRONG_THREAD,"provider thread refusal forwarded"); apply_result=SCO_OK;
    for (corrupt=1;corrupt<=6;++corrupt) Require(Call("loadout.slots","primary=Fixture")==SCO_FAILED,"inconsistent sync report refused");
    corrupt=0;
    Require(player.count(20) && player.count(21) && player.count(22) && player.count(23) && player.count(24),"bounded manual controls published");
    Send(20,SCO_WIDGET_TEXT_INPUT,"sidearm_barrel=FixtureBarrel"); Send(21,SCO_WIDGET_BUTTON);
    Require(captured.size()==1 && captured[0].slot==14,"widget action routes to public slot");
    const auto calls=applies; Send(20,SCO_WIDGET_TEXT_INPUT,std::string(256,'x').c_str()); Send(21,SCO_WIDGET_BUTTON);
    Require(applies==calls+1 && captured[0].slot==14,"oversized forged widget text preserves bounded field");
}
void ManualRefusals(const char* command,uint32_t button,bool command_only=false) {
    for (const char* invalid:{"", "primary=", "primary=None", "primary=Fixture,primary=Other",
                              "unknown=Fixture", "primary=Bad-Class", "primary=Fixture,"}) {
        Reset();
        Require(Call("loadout.slots","primary=FixtureWeapon")==SCO_OK,"prior successful apply");
        const auto before_apply=applies,before_submit=submits;
        if (command_only) {
            Require(Call(command,invalid)==SCO_BAD_ARG,"direct command parse refusal result");
            Require(std::strstr(reply,"Manual slots refused") && std::strstr(reply,"None/clear") &&
                    !std::strstr(reply,"applied="),"direct command replaces empty reply with refusal");
            Require(applies==before_apply && submits==before_submit,"direct command refusal makes no provider request");
            continue;
        }
        Send(20,SCO_WIDGET_TEXT_INPUT,invalid);
        Require(copied_status.find("applied=1/1")!=std::string::npos,"copied panel holds previous receipt");
        Send(button,SCO_WIDGET_BUTTON);
        Require(copied_status.find("Manual slots refused")!=std::string::npos &&
                copied_status.find("None/clear")!=std::string::npos &&
                copied_status.find("applied=")==std::string::npos,"widget replaces prior success with refusal");
        Require(applies==before_apply && submits==before_submit,"widget parse refusal makes no provider request");
        Require(Call(command,invalid)==SCO_BAD_ARG,"manual command parse refusal result");
        Require(std::strstr(reply,"Manual slots refused") && std::strstr(reply,"None/clear") &&
                !std::strstr(reply,"applied="),"manual command returns descriptive refusal");
        Require(applies==before_apply && submits==before_submit,"command parse refusal makes no provider request");
    }
    Reset();
    sco_arg wrong{}; wrong.type=SCO_ARG_INT;
    Require(commands.at(command)(&wrong,1,nullptr,reply,sizeof(reply))==SCO_BAD_ARG &&
            std::strstr(reply,"Manual slots refused"),"wrong typed argument has refusal reply");
    reply[0]=0;
    Require(commands.at(command)(nullptr,0,nullptr,reply,sizeof(reply))==SCO_BAD_ARG &&
            std::strstr(reply,"Manual slots refused"),"missing argument has refusal reply");
    Require(applies==0 && submits==0,"invalid command arguments make no provider request");
}
void CopiedAsync() {
    Reset();
    Require(Call("loadout.items","")==SCO_OK && std::strstr(reply,"primary:WeaponA [requires 1.1]") &&
            std::strstr(reply,"sidearm:SidearmA [requires 1.1]") && std::strstr(reply,"ammo:AmmoA [unsupported]") &&
            std::strstr(reply,"helmet:HelmetA;") && !std::strstr(reply,"primary:WeaponA [unsupported]"),"list command labels armor, weapon version and unsupported selections");
    Require(Call("loadout.submit_gear","TorsoA")==SCO_OK && Call("loadout.cancel")==SCO_OK,"fixture valid");
    Require(Call("loadout.submit_gear","HelmetA")==SCO_BAD_ARG,"ambiguous item refused");
    list_items="[helmet]\nHelmetA\nDefault\nNone\n[torso]\nTorsoA\n[ammo]\nAmmoA\n[primary]\nWeaponA\n[sidearm]\nSidearmA\n";
    Require(Call("loadout.reload_lists")==SCO_OK,"reload unambiguous list");
    reenter_submit=true;
    Require(Call("loadout.submit_gear","HelmetA,TorsoA")==SCO_OK && captured.size()==2 && captured[0].slot==SC_LOADOUT_SLOT_HEAD && captured[1].slot==SC_LOADOUT_SLOT_TORSO,"copied gear order");
    Require(nested==SCO_UNAVAILABLE,"recursive submission refused"); reenter_submit=false;
    Require(Call("loadout.submit_gear","HelmetA")==SCO_UNAVAILABLE,"copied job retained");
    Require(Call("loadout.cancel")==SCO_OK,"release copied job");
    Require(Call("loadout.submit_gear","WeaponA,SidearmA")==SCO_OK && captured[0].slot==SC_LOADOUT_SLOT_PRIMARY && captured[1].slot==SC_LOADOUT_SLOT_SIDEARM,"copied public weapon slots");
    Require(Call("loadout.cancel")==SCO_OK,"release weapon job");
    Require(Call("loadout.equip","HelmetA")==SCO_OK && queried==SC_LOADOUT_VERSION_1_0 && captured[0].slot==SC_LOADOUT_SLOT_HEAD,"armor uses 1.0");
    Require(Call("loadout.equip","HelmetA,WeaponA,SidearmA")==SCO_OK && queried==SC_LOADOUT_VERSION_1_1 && captured.size()==3 && captured[0].slot==SC_LOADOUT_SLOT_HEAD && captured[1].slot==SC_LOADOUT_SLOT_PRIMARY && captured[2].slot==SC_LOADOUT_SLOT_SIDEARM,"mixed copied gear uses 1.1 in selection order");
    const auto before_clear=applies;
    for (const char* selection:{"None", "dEfAuLt", "WeaponA,nOnE", "HelmetA,WeaponA,DEFAULT"})
        Require(Call("loadout.equip",selection)==SCO_BAD_ARG,"armor-only and mixed clear/default refused case-insensitively");
    Require(applies==before_clear,"clear/default selections make no mutation calls");
    const auto prior_applies=applies;
    supported=SC_LOADOUT_VERSION_1_0;
    Require(Call("loadout.equip","HelmetA,WeaponA")==SCO_UNAVAILABLE && queried==SC_LOADOUT_VERSION_1_1,"mixed selection refuses old provider");
    Require(Call("loadout.equip","HelmetA")==SCO_OK && queried==SC_LOADOUT_VERSION_1_0,"armor remains available on 1.0");
    supported=SC_LOADOUT_VERSION_1_2;
    const auto after_armor=applies;
    for (int capability=0;capability<2;++capability) {
        apply_cap=capability!=0; weapons_cap=capability==0;
        Require(Call("loadout.equip","HelmetA,WeaponA")==SCO_UNAVAILABLE,"mixed selection requires both capabilities");
    }
    apply_cap=weapons_cap=true; present=false;
    Require(Call("loadout.equip","HelmetA,WeaponA")==SCO_UNAVAILABLE,"mixed selection provider absent"); present=true;
    for (uint32_t bytes=4;bytes<offsetof(sc_loadout_v1,apply)+sizeof(service.apply);++bytes) {
        auto short_table=std::make_unique<unsigned char[]>(bytes);
        std::memcpy(short_table.get(),&service,bytes); std::memcpy(short_table.get(),&bytes,4); publication=short_table.get();
        Require(Call("loadout.equip","HelmetA,WeaponA")==SCO_UNAVAILABLE,"mixed selection refuses short apply extent");
    }
    publication=&service; service.apply=nullptr;
    Require(Call("loadout.equip","HelmetA,WeaponA")==SCO_UNAVAILABLE,"mixed selection refuses null apply"); service.apply=Apply;
    Require(Call("loadout.equip","WeaponA,WeaponA")==SCO_BAD_ARG && Call("loadout.equip","HelmetA,AmmoA")==SCO_UNAVAILABLE,"mixed duplicate and unsupported refuse");
    Require(applies==after_armor && after_armor==prior_applies+1,"mixed refusals make no mutation calls");
    apply_result=SCO_FAILED;
    Require(Call("loadout.equip","HelmetA,WeaponA")==SCO_FAILED && std::strstr(reply,"helmet=unavailable") && std::strstr(reply,"primary=applied"),"mixed partial results preserve input labels");
    apply_result=SCO_OK;
    Require(Call("loadout.submit_gear","WeaponA")==SCO_OK,"retain copied weapon job");
    const auto busy_applies=applies;
    Require(Call("loadout.equip","HelmetA,WeaponA")==SCO_UNAVAILABLE && applies==busy_applies,"retained job blocks mixed apply");
    Require(Call("loadout.cancel")==SCO_OK,"release job after busy check");
    const auto before=submits;
    Require(Call("loadout.submit_gear","HelmetA,HelmetA")==SCO_BAD_ARG,"duplicate gear refused");
    Require(Call("loadout.submit_gear","AmmoA")==SCO_UNAVAILABLE,"unsupported gear refused");
    Require(Call("loadout.submit_gear","Default")==SCO_BAD_ARG && Call("loadout.submit_gear","None")==SCO_BAD_ARG,"clear/default refused");
    Require(Call("loadout.submit_gear","Missing")==SCO_NOT_FOUND,"missing gear refused");
    Require(Call("loadout.submit_gear",std::string(1024,'A').c_str())==SCO_BAD_ARG,"oversized gear refused");
    Require(Call("loadout.submit_outfit","Good")==SCO_UNAVAILABLE,"spoilers required");
    sco_arg confirm{}; confirm.type=SCO_ARG_BOOL; confirm.v.i=1;
    Require(commands.at("loadout.spoilers")(&confirm,1,nullptr,reply,sizeof(reply))==SCO_OK,"confirm spoilers");
    Require(Call("loadout.submit_outfit","Bad")==SCO_UNAVAILABLE && Call("loadout.submit_outfit","Duplicate")==SCO_BAD_ARG,"unsupported and duplicate outfit refused");
    list_outfits+="[Good]\nArmor_Arms Other\n[Large]\n";
    for(int i=0;i<17;++i) list_outfits+="Armor_Helmet HelmetA\n";
    Require(Call("loadout.reload_lists")==SCO_OK,"reload ambiguous and large outfits");
    Require(Call("loadout.submit_outfit","Good")==SCO_BAD_ARG,"ambiguous outfit refused");
    Require(Call("loadout.submit_outfit","Large")==SCO_TOO_MANY,"oversized outfit refused before submit");
    list_outfits="[Good]\nArmor_Torso TorsoA\nArmor_Helmet HelmetA\n";
    Require(Call("loadout.reload_lists")==SCO_OK,"reload valid outfit");
    Require(submits==before,"invalid copied selections do not call provider");
    supported=SC_LOADOUT_VERSION_1_1; Require(Call("loadout.submit_outfit","Good")==SCO_UNAVAILABLE,"copied route old provider refuses"); supported=SC_LOADOUT_VERSION_1_2;
    service.status=nullptr; Require(Call("loadout.submit_outfit","Good")==SCO_UNAVAILABLE,"copied route null member refuses"); service.status=Status;
    const auto full_size=service.size; service.size=offsetof(sc_loadout_v1_2,cancel);
    Require(Call("loadout.submit_outfit","Good")==SCO_UNAVAILABLE,"copied route short table refuses"); service.size=full_size;
    Require(Call("loadout.submit_outfit","Good")==SCO_OK && captured.size()==2 && captured[0].slot==SC_LOADOUT_SLOT_TORSO && captured[1].slot==SC_LOADOUT_SLOT_HEAD,"outfit preserves source order");
    SetProgress(SC_LOADOUT_JOB_PENDING,1); Require(Call("loadout.progress")==SCO_OK && std::strstr(reply,"Armor_Torso=applied"),"copied result order");
    Require(Call("loadout.cancel")==SCO_OK,"release outfit job");
}
void ReentrantCancel() {
    Reset(); Require(Call("loadout.submit","primary=Fixture")==SCO_OK,"job for reentrant cancellation");
    reenter_cancel=true; Require(Call("loadout.cancel")==SCO_OK,"outer cancellation"); reenter_cancel=false;
    Require(nested_cancel==SCO_UNAVAILABLE && nested==SCO_UNAVAILABLE && submits==1 && cancels==1,"recursive cancellation and replacement refused");
    Require(Call("loadout.submit","primary=Replacement")==SCO_OK,"new job after cancellation returns");
    SetProgress(SC_LOADOUT_JOB_PENDING,0);
    Require(Call("loadout.progress")==SCO_OK && Call("loadout.cancel")==SCO_OK,"replacement remains retained and releasable");
    Reset(); Require(Call("loadout.submit","primary=Fixture")==SCO_OK,"job for reentrant lookup");
    unload_on_lookup=true; const auto prior=cancels;
    Require(Call("loadout.cancel")==SCO_UNAVAILABLE && cancels==prior,"owner invalidated during lookup is not passed to provider");
    Require(Call("loadout.progress")==SCO_UNAVAILABLE,"owner unloaded during lookup");
}
void Async() {
    Reset(); async_cap=false;
    Require(Call("loadout.submit","primary=Fixture")==SCO_UNAVAILABLE && submits==0,"async capability required independently of sync");
    async_cap=true; apply_cap=weapons_cap=false;
    Require(Call("loadout.submit","primary=Fixture")==SCO_OK,"async admitted independently of sync capabilities");
    Require(Call("loadout.cancel")==SCO_OK,"release independent async job");
    submits=0; apply_cap=weapons_cap=true; supported=SC_LOADOUT_VERSION_1_1;
    Require(Call("loadout.submit","primary=Fixture")==SCO_UNAVAILABLE && queried==SC_LOADOUT_VERSION_1_2,"async requires1.2 query"); supported=SC_LOADOUT_VERSION_1_2;
    for (uint32_t bytes=4;bytes<sizeof(service);++bytes) {
        auto short_table=std::make_unique<unsigned char[]>(bytes);
        std::memcpy(short_table.get(),&service,bytes); std::memcpy(short_table.get(),&bytes,4); publication=short_table.get();
        Require(Call("loadout.submit","primary=Fixture")==SCO_UNAVAILABLE,"incomplete async lifecycle refused before submit");
    }
    publication=&service;
    for (int member=0;member<3;++member) {
        service.submit=member==0 ? nullptr : Submit; service.status=member==1 ? nullptr : Status; service.cancel=member==2 ? nullptr : Cancel;
        Require(Call("loadout.submit","primary=Fixture")==SCO_UNAVAILABLE,"null async lifecycle member");
    }
    service.submit=Submit; service.status=Status; service.cancel=Cancel;
    Require(submits==0,"invalid lifecycle makes no job");
    submit_result=SCO_TOO_MANY; Require(Call("loadout.submit","primary=Fixture")==SCO_TOO_MANY,"submit refusal forwarded");
    submit_result=SCO_OK; submitted_id=0; Require(Call("loadout.submit","primary=Fixture")==SCO_FAILED,"zero job rejected"); submitted_id=42;
    std::string input="primary_optic=FixtureOptic,primary=FixtureWeapon";
    Require(Call("loadout.submit",input.c_str())==SCO_OK,"async accepted"); input.assign(100,'x');
    Require(!std::strcmp(captured[0].item_class,"FixtureWeapon") && !std::strcmp(captured[1].item_class,"FixtureOptic"),"provider copied request");
    const auto before=submits;
    Require(Call("loadout.submit","primary=Other")==SCO_UNAVAILABLE && submits==before,"one retained job, no replacement");
    Require(Call("loadout.slots","primary=Other")==SCO_UNAVAILABLE && applies==0,"no sync race with retained job");
    SetProgress(SC_LOADOUT_JOB_PENDING,1);
    Require(Call("loadout.progress")==SCO_OK && std::strstr(reply,"pending") && std::strstr(reply,"completed=1/2") && std::strstr(reply,"primary=applied") && !std::strstr(reply,"primary_optic=applied"),"completed prefix without premature success");
    for (uint32_t bytes=4;bytes<sizeof(service);++bytes) {
        auto short_table=std::make_unique<unsigned char[]>(bytes);
        std::memcpy(short_table.get(),&service,bytes); std::memcpy(short_table.get(),&bytes,4); publication=short_table.get();
        if (bytes<offsetof(sc_loadout_v1_2,status)+sizeof(service.status))
            Require(Call("loadout.progress")==SCO_UNAVAILABLE,"physically short status pointer refused");
        Require(Call("loadout.cancel")==SCO_UNAVAILABLE,"physically short cancel pointer refused, job retained");
    }
    publication=&service; present=false;
    Require(Call("loadout.progress")==SCO_UNAVAILABLE && Call("loadout.cancel")==SCO_UNAVAILABLE,"withdrawn provider refuses without stale pointer");
    present=true; supported=SC_LOADOUT_VERSION_1_1;
    Require(Call("loadout.progress")==SCO_UNAVAILABLE && Call("loadout.cancel")==SCO_UNAVAILABLE,"version withdrawal does not use prefix tail");
    supported=SC_LOADOUT_VERSION_1_2; service.status=nullptr;
    Require(Call("loadout.progress")==SCO_UNAVAILABLE,"null status after submit"); service.status=Status; service.cancel=nullptr;
    Require(Call("loadout.cancel")==SCO_UNAVAILABLE,"null cancel after submit"); service.cancel=Cancel;
    SetProgress(SC_LOADOUT_JOB_COMPLETE,2); progress.report.slots[1].status=SC_LOADOUT_ITEM_MISSING; progress.report.applied=1;
    Require(Call("loadout.progress")==SCO_OK && std::strstr(reply,"complete") && std::strstr(reply,"applied=1/2") && std::strstr(reply,"item-missing"),"complete is not all applied");
    for (int invalid=0;invalid<7;++invalid) {
        SetProgress(SC_LOADOUT_JOB_PENDING,1);
        switch(invalid) {
            case 0: --progress.size; break; case 1: --progress.report.size; break;
            case 2: progress.completed=3; break; case 3: progress.report.applied=2; break;
            case 4: ++progress.report.slots[0].slot; break;
            case 5: progress.state=SC_LOADOUT_JOB_COMPLETE; break;
            case 6: progress.state=static_cast<sc_loadout_job_state>(7); break;
        }
        Require(Call("loadout.progress")==SCO_FAILED,"invalid progress refused");
    }
    SetProgress(SC_LOADOUT_JOB_FAILED,1);
    Require(Call("loadout.progress")==SCO_OK && std::strstr(reply,"failed") && std::strstr(reply,"no rollback"),"failed job not retried/rolled back");
    status_result=SCO_UNAVAILABLE; Require(Call("loadout.progress")==SCO_UNAVAILABLE,"status failure forwarded"); status_result=SCO_OK;
    cancel_result=SCO_UNAVAILABLE; Require(Call("loadout.cancel")==SCO_UNAVAILABLE,"cancel refusal forwarded and retained");
    cancel_result=SCO_OK; async_cap=apply_cap=weapons_cap=false;
    Require(Call("loadout.cancel")==SCO_OK,"cancel allowed after capability revocation");
    Require(Call("loadout.progress")==SCO_NOT_FOUND,"local job released after cancel");
    async_cap=apply_cap=weapons_cap=true;
    Send(20,SCO_WIDGET_TEXT_INPUT,"sidearm_tool_module=FixtureModule"); Send(22,SCO_WIDGET_BUTTON);
    Require(captured.size()==1 && captured[0].slot==19,"widget submit after cancellation");
    SetProgress(SC_LOADOUT_JOB_PENDING,0); Send(23,SCO_WIDGET_BUTTON);
    Require(statuses>0,"widget progress");
    const auto cancelled=cancels; async_cap=apply_cap=weapons_cap=false;
    sco::features::loadout::Unload(); sco::features::loadout::Unload();
    Require(cancels==cancelled+1,"unload cancels once despite capability revocation");
    Require(Call("loadout.progress")==SCO_UNAVAILABLE,"stale callback after unload refuses");
    Reset(); Require(Call("loadout.progress")==SCO_NOT_FOUND,"reload retains no stale job");
    Require(Call("loadout.submit","primary=Fixture")==SCO_OK,"retained owner job for not-found cancel");
    cancel_result=SCO_NOT_FOUND;
    Require(Call("loadout.cancel")==SCO_NOT_FOUND && Call("loadout.progress")==SCO_NOT_FOUND,"provider-gone job released locally without success claim");
    cancel_result=SCO_OK;
    Require(Call("loadout.submit","primary=Fixture")==SCO_OK,"job before provider withdrawal");
    const auto prior=cancels; present=false; sco::features::loadout::Unload();
    Require(cancels==prior,"unload after withdrawal does not call stale table");
    Reset(); Require(Call("loadout.submit","primary=Fixture")==SCO_OK,"job before cancellation refusal");
    cancel_result=SCO_FAILED; const auto refused=cancels; sco::features::loadout::Unload();
    Require(cancels==refused+1,"unload attempts refused cancellation once, no rollback/retry");
}
}
int main(int argc,char** argv) {
    if (argc==2 && !std::strcmp(argv[1],"--refusal-apply")) ManualRefusals("loadout.slots",21);
    else if (argc==2 && !std::strcmp(argv[1],"--refusal-submit")) ManualRefusals("loadout.submit",22);
    else if (argc==2 && !std::strcmp(argv[1],"--command-refusal-apply")) ManualRefusals("loadout.slots",21,true);
    else if (argc==2 && !std::strcmp(argv[1],"--command-refusal-submit")) ManualRefusals("loadout.submit",22,true);
    else {
        Require(argc==1,"unknown test option");
        Sync(); Async(); CopiedAsync(); ReentrantCancel(); ManualRefusals("loadout.slots",21); ManualRefusals("loadout.submit",22);
    }
    sco::features::loadout::Unload();
    std::printf("loadout public route: %u checks PASS (synthetic providers; not run in game)\n",checks);
}
