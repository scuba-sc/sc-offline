// SPDX-License-Identifier: GPL-3.0-only
// Actual external module, copied synthetic content/widgets and typed loadout records.
#include "workflow_module.h"
#include <sc_loadout.h>
#include <sco_content.h>
#include <sco_widgets.h>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace {
unsigned checks=0,applies=0,submits=0,statuses=0,cancels=0,reads=0,items=0;
sco_plugin* owner=nullptr;
void Check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"loadout catalogue: %s\n",why);std::exit(1);}}
struct File{std::string name,bytes;};
std::vector<File> files={{"lists/loadout_slots.txt","[primary_optic]\nFixtureOptic\n[primary]\nFixtureWeapon\n[sidearm_tool_battery]\nFixtureBattery\n"}};
struct Node{uint32_t kind=0,flags=0;int32_t index=0;std::string label,text;std::vector<std::string> options;};
struct Panel{std::map<uint32_t,Node> nodes;sco_widget_event_fn fn=nullptr;void* ctx=nullptr;} panel;
std::map<std::string,sco_command> commands;
sco_widgets_v1 widgets{};sco_content_v1 content{};sc_loadout_v1_2 loadout{};
const void* contentTable=&content;const void* loadoutTable=&loadout;
bool applyCap=true,weaponsCap=true,asyncCap=true,contentPresent=true,endless=false;
uint32_t version=SC_LOADOUT_VERSION_1_2,queried=0;
sco_result panelResult=SCO_OK,applyResult=SCO_OK,cancelResult=SCO_OK;
int badMetadata=0,badRead=0,corruptReport=0;
std::vector<sc_loadout_item> captured;
sc_loadout_job_report progress{};
sco_event_fn tick=nullptr;void* tickCtx=nullptr;
void Log(sco_plugin*,sco_log_level,const char*){}
int Has(const char* name){return !std::strcmp(name,"game.loadout.apply")?applyCap:!std::strcmp(name,"game.loadout.weapons")?weaponsCap:!std::strcmp(name,"game.loadout.async")&&asyncCap;}
sco_result Register(sco_plugin* self,const sco_command* c){Check(self==owner,"command owner");commands[c->name]=*c;return SCO_OK;}
sco_result Subscribe(sco_plugin*,const char*,sco_event_fn fn,void* ctx){tick=fn;tickCtx=ctx;return SCO_OK;}
sco_result Unsubscribe(sco_plugin*,const char*,sco_event_fn){return SCO_OK;}
sco_result Item(sco_plugin* self,sco_content_kind kind,uint32_t ordinal,sco_content_item* out){
    ++items;Check(self==owner&&kind==SCO_CONTENT_LIST,"content owner/kind");if(ordinal>=files.size()&&!endless)return SCO_NOT_FOUND;
    *out={};out->size=sizeof(*out);out->kind=kind;out->id=ordinal+1;
    std::snprintf(out->pack,sizeof(out->pack),"synthetic");std::snprintf(out->name,sizeof(out->name),"%s",endless?"lists/other.txt":files[ordinal].name.c_str());
    switch(badMetadata){case 1:--out->size;break;case 2:out->kind=SCO_CONTENT_SCRIPT;break;case 3:out->id=0;break;case 4:std::memset(out->name,'x',sizeof(out->name));break;case 5:std::memset(out->pack,'x',sizeof(out->pack));break;default:break;}return SCO_OK;
}
sco_result Read(sco_plugin* self,uint64_t id,void* out,uint32_t* size){
    ++reads;Check(self==owner&&id&&id<=files.size(),"read owner/id");const auto& b=files[static_cast<size_t>(id-1)].bytes;
    if(!out&&badRead==1){*size=SCO_CONTENT_MAX_FILE_BYTES+1;return SCO_TOO_MANY;}
    if(!out&&badRead==2){*size=static_cast<uint32_t>(b.size());return SCO_OK;}
    if(out&&badRead==3)return SCO_UNAVAILABLE;
    if(*size<b.size()){*size=static_cast<uint32_t>(b.size());return SCO_TOO_MANY;}
    if(out&&!b.empty())std::memcpy(out,b.data(),b.size());
    *size=static_cast<uint32_t>(b.size())-(badRead==4&&!b.empty()?1u:0u);
    return SCO_OK;
}
sco_result SetPanel(sco_plugin* self,const char* id,const char*,const sco_widget_node* nodes,uint32_t count,uint32_t stride,sco_widget_event_fn fn,void* ctx){
    Check(self==owner&&stride==sizeof(*nodes),"widget owner/stride");if(std::strcmp(id,"loadout.player"))return SCO_OK;if(panelResult!=SCO_OK)return panelResult;
    Panel next;next.fn=fn;next.ctx=ctx;
    for(uint32_t i=0;i<count;++i){Check(nodes[i].size==sizeof(*nodes),"node extent");Check((nodes[i].item_count==0)==(nodes[i].items==nullptr),"empty array invariant");Check(nodes[i].label&&std::strlen(nodes[i].label)<=255,"label byte bound");
        auto& n=next.nodes[nodes[i].id];n.kind=nodes[i].kind;n.flags=nodes[i].flags;n.index=nodes[i].index;n.label=nodes[i].label;n.text=nodes[i].text?nodes[i].text:"";
        if(nodes[i].kind==SCO_WIDGET_COMBO)Check(nodes[i].item_count<=256&&nodes[i].index>=-1&&(nodes[i].index<0||static_cast<uint32_t>(nodes[i].index)<nodes[i].item_count),"combo contract bounds");
        for(uint32_t j=0;j<nodes[i].item_count;++j){Check(nodes[i].items[j]&&std::strlen(nodes[i].items[j])<=255,"option bound");n.options.emplace_back(nodes[i].items[j]);}
    }
    panel=std::move(next);return SCO_OK;
}
sco_result Remove(sco_plugin* self,const char*){Check(self==owner,"panel removal owner");return SCO_OK;}
void Capture(sco_plugin* self,const sc_loadout_request* r){Check(self==owner&&r&&r->size==sizeof(*r)&&r->item_stride==sizeof(sc_loadout_item)&&r->count&&r->count<=16,"public typed batch");captured.assign(r->items,r->items+r->count);}
sc_loadout_report Report(){sc_loadout_report r{};r.size=sizeof(r);r.count=static_cast<uint32_t>(captured.size());r.applied=r.count;for(uint32_t i=0;i<r.count;++i)r.slots[i]={captured[i].slot,SC_LOADOUT_APPLIED};return r;}
sco_result Apply(sco_plugin* self,const sc_loadout_request* r,sc_loadout_report* out){++applies;Capture(self,r);*out=Report();if(applyResult==SCO_FAILED){out->slots[0].status=SC_LOADOUT_ITEM_MISSING;--out->applied;}if(corruptReport)++out->count;return applyResult;}
sco_result Submit(sco_plugin* self,const sc_loadout_request* r,uint64_t* job){++submits;Capture(self,r);*job=42;return SCO_OK;}
sco_result Status(sco_plugin* self,uint64_t job,sc_loadout_job_report* out){++statuses;Check(self==owner&&job==42,"status owner/job");*out=progress;return SCO_OK;}
sco_result Cancel(sco_plugin* self,uint64_t job){++cancels;Check(self==owner&&job==42,"cancel owner/job");return cancelResult;}
sco_result Query(const char* name,uint32_t requested,const void** out){
    *out=nullptr;
    if(!std::strcmp(name,SCO_CONTENT_NAME)&&contentPresent)*out=contentTable;
    if(!std::strcmp(name,SCO_WIDGETS_NAME))*out=&widgets;
    if(!std::strcmp(name,SC_LOADOUT_SERVICE_NAME)){queried=requested;if(requested<=version)*out=loadoutTable;}
    return *out?SCO_OK:SCO_NOT_FOUND;
}
sco_result Call(const char* name){Check(commands.count(name)!=0,"registered command");char text[2048]{};const auto& c=commands.at(name);return c.fn(nullptr,0,c.ctx,text,sizeof(text));}
void Send(const Panel& target,uint32_t id,uint32_t kind,int32_t index=0,const char* text=nullptr){Check(target.fn!=nullptr,"published callback");sco_widget_event e{};e.size=sizeof(e);e.node_id=id;e.kind=kind;e.index=index;e.text=text;target.fn("loadout.player",&e,target.ctx);}
void Send(uint32_t id,uint32_t kind,int32_t index=0,const char* text=nullptr){const auto old=panel;Send(old,id,kind,index,text);}
void Filter(const char* text){Send(1,SCO_WIDGET_TEXT_INPUT,0,text);}
void Select(uint32_t slot,int32_t row){Send(200+slot,SCO_WIDGET_COMBO,row);}
void Button(uint32_t id){Send(id,SCO_WIDGET_BUTTON);}
bool Disabled(uint32_t id){return (panel.nodes.at(id).flags&SCO_WIDGET_DISABLED)!=0;}
void Tick(){static uint32_t now=100000;now+=200;Check(tick!=nullptr,"tick callback");tick("tick",&now,tickCtx);}
}
int main(int argc,char** argv){
    Check(argc==2||argc==3,"module and optional source catalogue");test_module::Open(2,argv);int token=0;owner=reinterpret_cast<sco_plugin*>(&token);
    widgets.size=sizeof(widgets);widgets.set_panel=SetPanel;widgets.remove_panel=Remove;content.size=sizeof(content);content.item=Item;content.read=Read;
    loadout.size=sizeof(loadout);loadout.apply=Apply;loadout.submit=Submit;loadout.status=Status;loadout.cancel=Cancel;
    sco_api api{};api.size=sizeof(api);api.major=SCO_API_MAJOR;api.minor=SCO_API_MINOR;api.log=Log;api.has=Has;api.register_command=Register;api.subscribe=Subscribe;api.unsubscribe=Unsubscribe;api.query_service=Query;
    Check(test_module::Load(&api,owner)==SCO_OK,"module load");
    Check(panel.nodes.count(200)&&panel.nodes.count(213),"independent public-slot selectors published by actual module");
    Check(panel.nodes[200].options==std::vector<std::string>{"Unchanged (omit slot)","FixtureWeapon"},"explicit parent catalogue options");
    Check(panel.nodes[202].options==std::vector<std::string>{"Unchanged (omit slot)","FixtureOptic"},"explicit optic catalogue options");
    Check(Disabled(220)&&Disabled(221),"all unchanged initially, no implicit mutation");Button(220);Check(!applies,"forged empty apply makes no mutation");
    Select(2,1);Button(220);Check(applies==1&&queried==SC_LOADOUT_VERSION_1_1&&captured.size()==1&&captured[0].slot==SC_LOADOUT_SLOT_PRIMARY_OPTIC,"attachment alone omits unchanged parent");
    Select(0,1);Button(220);Check(captured.size()==2&&captured[0].slot==8&&captured[1].slot==10,"parent-first ordering independent of click order");
    Select(13,1);Button(220);Check(captured.size()==3&&captured[2].slot==21,"independent sidearm battery");
    Select(0,0);Button(220);Check(captured.size()==2&&captured[0].slot==10,"Unchanged removes only request assignment, no clear/default");
    auto stale=panel;Button(222);Check(Disabled(220),"clear selection only");const auto before=applies;Send(stale,220,SCO_WIDGET_BUTTON);Check(applies==before,"old publication cannot replay selection");
    Select(0,1);Select(2,1);applyResult=SCO_FAILED;Button(220);Check(panel.nodes[14].label.find("applied=1/2")!=std::string::npos&&panel.nodes[14].label.find("item-missing")!=std::string::npos,"partial result surfaced without rollback");Check(applies==before+1,"no automatic retry");applyResult=SCO_OK;corruptReport=1;Button(220);Check(panel.nodes[14].label.find("Invalid loadout report")!=std::string::npos,"malformed provider report refused");corruptReport=0;
    version=SC_LOADOUT_VERSION_1_0;auto oldApplies=applies;Button(220);Check(applies==oldApplies,"old provider no downgrade");version=SC_LOADOUT_VERSION_1_2;
    applyCap=false;Button(220);Check(applies==oldApplies,"sync apply capability guard");applyCap=true;weaponsCap=false;Button(220);Check(applies==oldApplies,"sync weapons capability guard");
    // Async admission is independently gated by game.loadout.async.
    applyCap=false;asyncCap=true;Button(221);Check(submits==1&&queried==SC_LOADOUT_VERSION_1_2&&Disabled(220)&&Disabled(221),"curated job uses existing independent async route");
    Button(220);Button(221);Check(applies==oldApplies&&submits==1,"retained job blocks new mutation");
    progress={};progress.size=sizeof(progress);progress.state=SC_LOADOUT_JOB_PENDING;progress.completed=1;progress.report=Report();progress.report.applied=1;progress.report.slots[1].status=SC_LOADOUT_UNAVAILABLE;
    Button(23);Check(statuses==1&&panel.nodes[14].label.find("pending; completed=1/2")!=std::string::npos,"pending completed prefix only");
    asyncCap=false;Button(24);Check(cancels==1&&!Disabled(220),"cancel remains available after capability revocation");Button(221);Check(submits==1,"async revocation prevents new submit");
    applyCap=weaponsCap=asyncCap=true;
    version=SC_LOADOUT_VERSION_1_1;Button(221);Check(submits==1,"old async version has no synchronous fallback");version=SC_LOADOUT_VERSION_1_2;
    for(uint32_t size=4;size<offsetof(sc_loadout_v1_2,cancel)+sizeof(loadout.cancel);++size){auto shortTable=std::make_unique<unsigned char[]>(size);std::memcpy(shortTable.get(),&loadout,size);std::memcpy(shortTable.get(),&size,4);loadoutTable=shortTable.get();Button(221);Check(submits==1,"curated async full member extent");}loadoutTable=&loadout;
    loadout.cancel=nullptr;Button(221);Check(submits==1,"curated async requires cleanup callback");loadout.cancel=Cancel;
    auto reload=[&]{Check(Call("loadout.reload_slots")==SCO_OK,"reload copied slot catalogue");Tick();};
    files.push_back({"lists/items.txt","[helmet]\nFixtureAlpha\nFixtureBeta\n"});Check(Call("loadout.reload_lists")==SCO_OK,"copied legacy gear for shared-filter proof");Tick();
    files[0].bytes="[primary]\nFixtureAlpha\nFixtureBeta\nFixtureGamma\n";reload();const auto filterReads=reads;
    Filter("bEtA");Check(panel.nodes[200].options==std::vector<std::string>{"Unchanged (omit slot)","FixtureBeta"},"shared filter is case-insensitive and uses original class");
    Check(panel.nodes[2].options==std::vector<std::string>{"helmet: FixtureBeta"},"same shared filter narrows copied legacy gear browser");
    Select(0,1);Filter("Gamma");Check(panel.nodes[200].options==std::vector<std::string>{"Unchanged (omit slot)","FixtureBeta","FixtureGamma"}&&panel.nodes[200].index==1,"nonmatching selected class remains visible with original identity");
    Select(0,2);Button(220);Check(captured.size()==1&&!std::strcmp(captured[0].item_class,"FixtureGamma"),"filtered visible row maps to original third class");
    stale=panel;Filter("Alpha");Send(stale,200,SCO_WIDGET_COMBO,1);Select(0,-1);Select(0,3);Select(0,0x7fffffff);Send(200,SCO_WIDGET_BUTTON);
    Button(220);Check(!std::strcmp(captured[0].item_class,"FixtureGamma")&&panel.nodes[200].index==2,"changed-filter stale and malformed selections cannot rebind class");
    Filter("no matches");Check(panel.nodes[200].options==std::vector<std::string>{"Unchanged (omit slot)","FixtureGamma"}&&!Disabled(220),"empty matches retain selected class and apply");
    const std::string longest(63,'x');Filter(longest.c_str());Check(panel.nodes[1].text==longest,"exact63byte printable filter accepted");
    const std::string oversized(64,'x');for(const char* bad:{oversized.c_str(),"bad\nfilter","\x7f","\xff",static_cast<const char*>(nullptr)}){Filter(bad);Check(panel.nodes[1].text==longest&&panel.nodes[200].index==1,"malformed/oversized filter preserves filter and picks");}
    char unterminated[64];std::memset(unterminated,'x',sizeof(unterminated));Filter(unterminated);Check(panel.nodes[1].text==longest,"unterminated filter is read only through the fixed63byte bound");
    Send(1,SCO_WIDGET_BUTTON,0,"Alpha");Check(panel.nodes[1].text==longest,"wrong filter event kind ignored");
    auto shortEvent=sco_widget_event{};shortEvent.size=sizeof(shortEvent)-1;shortEvent.node_id=1;shortEvent.kind=SCO_WIDGET_TEXT_INPUT;shortEvent.text="Alpha";panel.fn("loadout.player",&shortEvent,panel.ctx);Check(panel.nodes[1].text==longest,"short filter event ignored");
    Filter("");Select(0,2);stale=panel;panelResult=SCO_FAILED;const auto filteredApplies=applies;Filter("Gamma");Send(stale,200,SCO_WIDGET_COMBO,1);Send(stale,220,SCO_WIDGET_BUTTON);Check(applies==filteredApplies,"failed filtered publication pauses stale callbacks");panelResult=SCO_OK;Tick();Button(220);Check(!std::strcmp(captured[0].item_class,"FixtureBeta"),"filter publication retry preserves exact selected class");
    Button(221);Check(captured.size()==1&&!std::strcmp(captured[0].item_class,"FixtureBeta"),"filtered selection uses unchanged async route and raw class");Filter("Alpha");const auto heldApplies=applies;Button(220);Check(applies==heldApplies&&Disabled(221),"filter edits preserve retained job admission");Button(24);
    Button(222);Filter("no matches");Check(panel.nodes[200].options.size()==1&&Disabled(200)&&Disabled(220),"no matches with no selection omits slot only");Check(reads==filterReads,"search, selection and jobs use copied catalogue without content reads");
    struct Alias{const char* cls;const char* label;uint32_t slot;};
    const Alias aliases[]{
        {"grin_multitool_01_mining","OreBit Mining Attachment",8},{"grin_multitool_01_cutter","OxyTorch Cutter Attachment",8},
        {"grin_multitool_01_tractorbeam","TruHold Tractor Beam Attachment",8},{"grin_multitool_01_healing","LifeGuard Medical Attachment",8},
        {"grin_multitool_01_salvage_repair","Cambio-Lite SRT Attachment",8},
        {"grin_multitool_resource_salvage_repair_01","Cambio-Lite SRT Canister (empty)",9},
        {"grin_multitool_resource_salvage_repair_01_filled","Cambio-Lite SRT Canister (filled)",9},
    };
    files[0].bytes="[primary_tool_module]\n";for(const auto& alias:aliases)if(alias.slot==8)files[0].bytes+=std::string(alias.cls)+'\n';
    files[0].bytes+="[primary_tool_canister]\n";for(const auto& alias:aliases)if(alias.slot==9)files[0].bytes+=std::string(alias.cls)+'\n';
    Filter("");reload();
    for(const auto& alias:aliases){Button(222);Filter(alias.label);const auto id=200+alias.slot;Check(panel.nodes[id].options==std::vector<std::string>{"Unchanged (omit slot)",std::string(alias.label)+" ["+alias.cls+"]"},"public friendly label remains paired with raw identity");Select(alias.slot,1);Button(220);Check(captured.size()==1&&!std::strcmp(captured[0].item_class,alias.cls),"friendly-name selection sends exact raw public class");}
    Button(222);Filter("TRUHOLD");Check(panel.nodes[208].options.size()==2,"friendly alias matching ignores ASCII case");Filter("resource_salvage");Check(panel.nodes[209].options.size()==3,"raw canister identity remains searchable");
    Filter("");Button(222);files[1].bytes.clear();Check(Call("loadout.reload_lists")==SCO_OK,"clear synthetic legacy list snapshot");Tick();files.resize(1);
    std::string all;const char* names[]{"primary","sidearm","primary_optic","primary_barrel","primary_underbarrel","sidearm_optic","sidearm_barrel","sidearm_underbarrel","primary_tool_module","primary_tool_canister","primary_tool_battery","sidearm_tool_module","sidearm_tool_canister","sidearm_tool_battery"};
    for(const auto* name:names)all+=std::string("[")+name+"]\nSharedClass\n";
    files[0].bytes=all;reload();
    for(uint32_t i=0;i<14;++i)Select(i,1);
    Button(220);Check(captured.size()==14,"all fourteen independent selections");
    for(uint32_t i=0;i<14;++i)Check(captured[i].slot==8+i,"canonical parent-before-attachment order");
    const auto loadedReads=reads;files[0].bytes="[primary]\nChanged\n";Button(220);Check(captured.size()==14&&reads==loadedReads,"widget selections own startup copies, no read on apply");
    stale=panel;Check(Call("loadout.reload_slots")==SCO_OK,"explicit reload");oldApplies=applies;Send(stale,220,SCO_WIDGET_BUTTON);Check(applies==oldApplies,"reload invalidates old mapping before publication");Tick();Check(Disabled(220)&&panel.nodes[200].options[1]=="Changed","reload clears picks and installs new copied snapshot");
    Select(0,1);stale=panel;panelResult=SCO_FAILED;Button(222);Send(stale,220,SCO_WIDGET_BUTTON);Check(applies==oldApplies,"failed publication pauses old actions");panelResult=SCO_OK;Tick();Check(Disabled(220),"publication retry retains clear selection");
    for(const std::string& bad:std::vector<std::string>{"Unsectioned\n","[optic]\nFixture\n","[PRIMARY]\nFixture\n","[primary] suffix\nFixture\n","[primary]\nNone\n","[primary]\nBad-Class\n","[primary]\nA\n[primary]\nB\n","[primary]\nA\na\n",std::string("[primary]\nA\n")+'\0',"[primary]\n"+std::string(96,'A')+"\n"}){
        files[0].bytes=bad;Check(Call("loadout.reload_slots")!=SCO_OK,"malformed/ambiguous catalogue refuses atomically");Tick();Check(panel.nodes[200].options.size()==1&&Disabled(220),"failed reload clears old choices/picks");
    }
    files[0].bytes="[primary]\n";for(unsigned i=0;i<255;++i)files[0].bytes+="Fixture"+std::to_string(i)+"\n";reload();Check(panel.nodes[200].options.size()==256,"exact255 plus unchanged obeys public combo limit");files[0].bytes+="Extra\n";Check(Call("loadout.reload_slots")==SCO_TOO_MANY,"256classes in one slot refused, not truncated");Tick();
    files[0].bytes.clear();unsigned total=0;
    for(unsigned section=0;section<12;++section){files[0].bytes+=std::string("[")+names[section]+"]\n";for(unsigned row=0;row<255&&total<3000;++row,++total)files[0].bytes+="Fixture"+std::to_string(row)+"\n";}
    reload();Check(panel.nodes[225].label.find("3000 copied choices")!=std::string::npos,"exact3000 total catalogue choices");files[0].bytes+="Extra\n";Check(Call("loadout.reload_slots")==SCO_TOO_MANY,"3001total refuses atomically");Tick();
    files[0].bytes.clear();while(files[0].bytes.size()+256<=SCO_CONTENT_MAX_FILE_BYTES)files[0].bytes+='#'+std::string(254,'x')+'\n';if(files[0].bytes.size()<SCO_CONTENT_MAX_FILE_BYTES)files[0].bytes+='#'+std::string(SCO_CONTENT_MAX_FILE_BYTES-files[0].bytes.size()-1,'x');reload();Check(panel.nodes[225].label.find("0 copied choices")!=std::string::npos,"exact1MiB copied input accepted");files[0].bytes+='x';Check(Call("loadout.reload_slots")==SCO_TOO_MANY,"1MiB plus one refused before partial parse");Tick();
    files[0].bytes="";reload();Check(panel.nodes[200].options.size()==1&&Disabled(200),"empty catalogue valid and no mutation choices");
    files[0].bytes="[primary]\nFixture\n";files.push_back(files[0]);const auto readBefore=reads;Check(Call("loadout.reload_slots")==SCO_BAD_ARG&&reads==readBefore,"multiple exact Lists refuse without guessing");files.resize(1);Tick();
    for(const char* wrong:{"loadout_slots.txt","Lists/loadout_slots.txt","lists/loadout_slots.TXT","lists/nested/loadout_slots.txt"}){files[0].name=wrong;Check(Call("loadout.reload_slots")==SCO_NOT_FOUND,"canonical namespace exact");}files[0].name="lists/loadout_slots.txt";
    for(badMetadata=1;badMetadata<=5;++badMetadata)Check(Call("loadout.reload_slots")!=SCO_OK,"malformed copied metadata refuses");
    badMetadata=0;
    for(badRead=1;badRead<=4;++badRead)Check(Call("loadout.reload_slots")!=SCO_OK,"oversized/refused/inconsistent copied read refuses");
    badRead=0;
    endless=true;const auto beforeItems=items;Check(Call("loadout.reload_slots")==SCO_TOO_MANY&&items-beforeItems==SCO_CONTENT_MAX_ITEMS+1,"bounded content enumeration");endless=false;
    for(uint32_t size=4;size<offsetof(sco_content_v1,read)+sizeof(content.read);++size){auto bytes=std::make_unique<unsigned char[]>(size);std::memcpy(bytes.get(),&content,size);std::memcpy(bytes.get(),&size,4);contentTable=bytes.get();Check(Call("loadout.reload_slots")==SCO_UNAVAILABLE,"physically short content member refuses");}contentTable=&content;
    content.read=nullptr;Check(Call("loadout.reload_slots")==SCO_UNAVAILABLE,"null read callback");content.read=Read;contentPresent=false;Check(Call("loadout.reload_slots")==SCO_UNAVAILABLE,"missing content provider");contentPresent=true;
    reload();Select(0,1);stale=panel;oldApplies=applies;test_module::Unload();Send(stale,220,SCO_WIDGET_BUTTON);Check(applies==oldApplies,"callback after unload inert");commands.clear();Check(test_module::Load(&api,owner)==SCO_OK,"module reload");Send(stale,220,SCO_WIDGET_BUTTON);Check(applies==oldApplies&&Disabled(220),"old cookie cannot target new loaded session");
    if(argc==3){std::ifstream file(argv[2],std::ios::binary);Check(bool(file),"public seed catalogue file");files[0].bytes=std::string(std::istreambuf_iterator<char>(file),{});reload();unsigned count=0;for(uint32_t i=0;i<14;++i){count+=static_cast<unsigned>(panel.nodes[200+i].options.size()-1);Select(i,1);}Check(count==344,"all344 attributed seed choices parsed by actual module");Button(220);Check(captured.size()==14,"seed choices reach all14 public targets, no compatibility claim");}
    Select(0,1);Button(221);Check(Disabled(220),"new curated job retained");progress={};progress.size=sizeof(progress);progress.state=SC_LOADOUT_JOB_COMPLETE;progress.report=Report();progress.completed=progress.report.count;progress.report.slots[0].status=SC_LOADOUT_ITEM_MISSING;--progress.report.applied;
    Button(23);Check(panel.nodes[14].label.find("complete;")!=std::string::npos&&panel.nodes[14].label.find("item-missing")!=std::string::npos,"complete job with partial slot refusal is not whole-batch success");
    cancelResult=SCO_UNAVAILABLE;Button(24);Check(Disabled(220),"refused cancel retains job");cancelResult=SCO_OK;const auto cancelBefore=cancels;asyncCap=false;
    test_module::Unload();Check(cancels==cancelBefore+1,"unload cancels retained job after capability revocation");test_module::Close();std::printf("loadout curated catalogue: %u checks PASS; actual module, synthetic providers only\n",checks);
}
