// SPDX-License-Identifier: GPL-3.0-only
// Actual external module with copied synthetic public providers; no renderer/game.
#include "workflow_module.h"
#include "panel_dump.h"
#include <sco_content.h>
#include <sco_widgets.h>
#include <sco_ui.h>
#include <sc_world_control.h>
#include <sc_world.h>
#include <sc_spatial.h>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace {
unsigned checks=0,itemCalls=0,readCalls=0,panelCalls=0,removeCalls=0;
sco_plugin* owner=nullptr;
void Check(bool ok,const char* text) { ++checks; if(!ok){std::fprintf(stderr,"build picker: %s\n",text);std::exit(1);} }
std::map<std::string,sco_command> commands;
struct File { std::string name,bytes; };
std::vector<File> files={{"lists/buildables.txt","[objects]\nFixtureBox\n[sq42]\nFixtureS42\n"}};
struct Node { uint32_t kind=0,flags=0; int32_t index=-1; double value=0,maximum=0; std::string label,text; std::vector<std::string> items; };
struct Panel { std::map<uint32_t,Node> nodes; sco_widget_event_fn fn=nullptr; void* ctx=nullptr; } panel;
sco_widgets_v1 widgets{}; sco_content_v1 content{};
sc_world_control_v1 control{}; sc_world_v1 world{}; sc_spatial_v1 spatial{};
bool g5=false, failRelease=false; int reenter=0; sco_result nested=SCO_OK; const sco_api* testApi=nullptr; unsigned acquires=0, releases=0; std::string hotkeyCommand; sco_ui_v1 ui{};
sco_result Bind(sco_plugin*,const char* key,const char* command,const sco_arg*,uint32_t){Check(!std::strcmp(key,"f6"),"F6 hotkey");hotkeyCommand=command;return SCO_OK;}
sco_result Acquire(sco_plugin*,uint64_t* lease){++acquires; if(reenter==1){reenter=0;char text[64]{};nested=commands.at("build.toggle").fn(nullptr,0,nullptr,text,sizeof(text));} else if(reenter==2){reenter=0;test_module::Unload();nested=test_module::Load(testApi,owner);} *lease=42;return SCO_OK;}
sco_result Release(sco_plugin*,uint64_t){++releases;return failRelease?SCO_FAILED:SCO_OK;}
sco_result Step(sco_plugin*,uint64_t,const sc_world_camera_step*){return SCO_OK;}
sco_result RayFiltered(sco_plugin*,uint64_t,const double*,const double*,double,const uint64_t*,uint32_t,sc_world_hit*){return SCO_NOT_FOUND;}
sco_result Ray(uint64_t,const double*,const double*,double,sc_world_hit*){return SCO_NOT_FOUND;}
sco_result Camera(double*,double*,uint64_t*){return SCO_OK;}
int Pose(double*,double*,uint64_t*){return 0;}
int Convert(uint64_t,const double*,double*){return 0;}
const void* contentTable=nullptr;const void* widgetsTable=nullptr;
bool contentPresent=true,widgetsPresent=true,endless=false;
int badMetadata=0,badRead=0;
sco_result contentResult=SCO_OK,panelResult=SCO_OK;
std::vector<std::string> invoked,names;
std::vector<double> distances;
sco_event_fn tick=nullptr;void* tickCtx=nullptr;
void Log(sco_plugin*,sco_log_level,const char*) {}
int Has(const char*) {return g5?1:0;}
sco_result Register(sco_plugin* self,const sco_command* c){Check(self==owner,"command owner");commands[c->name]=*c;return SCO_OK;}
sco_result Provide(sco_plugin*,const char*,uint32_t,const void*){return SCO_OK;}
sco_result Subscribe(sco_plugin*,const char*,sco_event_fn fn,void* ctx){tick=fn;tickCtx=ctx;return SCO_OK;}
sco_result Item(sco_plugin* self,sco_content_kind kind,uint32_t i,sco_content_item* out){
    ++itemCalls;Check(self==owner && kind==SCO_CONTENT_LIST,"content owner/kind");
    if(i>=files.size() && !endless)return SCO_NOT_FOUND;
    *out={};out->size=sizeof(*out);out->kind=kind;out->id=i+1;
    std::snprintf(out->pack,sizeof(out->pack),"synthetic");
    std::snprintf(out->name,sizeof(out->name),"%s",endless?"lists/other.txt":files[i].name.c_str());
    switch(badMetadata) {case 1:--out->size;break;case 2:out->kind=SCO_CONTENT_SCRIPT;break;case 3:out->id=0;break;case 4:std::memset(out->name,'x',sizeof(out->name));break;case 5:std::memset(out->pack,'x',sizeof(out->pack));break;default:break;}
    return SCO_OK;
}
sco_result Read(sco_plugin* self,uint64_t id,void* out,uint32_t* n){
    ++readCalls;Check(self==owner && id && id<=files.size(),"read owner/id");const auto& bytes=files[static_cast<size_t>(id-1)].bytes;
    if(!out && badRead==1){*n=SCO_CONTENT_MAX_FILE_BYTES+1;return SCO_TOO_MANY;}
    if(!out && badRead==2){*n=static_cast<uint32_t>(bytes.size());return SCO_OK;}
    if(out && badRead==3)return SCO_UNAVAILABLE;
    if(*n<bytes.size()){*n=static_cast<uint32_t>(bytes.size());return SCO_TOO_MANY;}
    if (out && !bytes.empty()) std::memcpy(out, bytes.data(), bytes.size());
    *n = static_cast<uint32_t>(bytes.size()) - (badRead == 4 && !bytes.empty() ? 1u : 0u);
    return SCO_OK;
}
sco_result SetPanel(sco_plugin* self,const char* id,const char* title,const sco_widget_node* nodes,uint32_t n,uint32_t stride,sco_widget_event_fn fn,void* ctx){
    ++panelCalls;Check(self==owner && !std::strcmp(id,"build.picker") && !std::strcmp(title,"Build"),"owned panel/title");Check(stride==sizeof(*nodes),"public v1 nodes");
    if(panelResult!=SCO_OK)return panelResult;
    {const auto why=sc_offline::panel_dump::Violation(nodes,n,stride);if(!why.empty())std::printf("build.picker: %s\n",why.c_str());
     Check(why.empty(),"panel follows the sco.widgets tree rules");sc_offline::panel_dump::Record(id,title,nodes,n,stride);}
    Panel next;next.fn=fn;next.ctx=ctx;
    for(uint32_t i=0;i<n;++i){Check(nodes[i].size==sizeof(*nodes),"full node");Check(nodes[i].label && std::strlen(nodes[i].label)<=255,"bounded label");
        auto& copy=next.nodes[nodes[i].id];copy.kind=nodes[i].kind;copy.flags=nodes[i].flags;copy.index=nodes[i].index;copy.value=nodes[i].value;copy.maximum=nodes[i].maximum;copy.label=nodes[i].label;copy.text=nodes[i].text?nodes[i].text:"";
        Check((nodes[i].item_count!=0)==(nodes[i].items!=nullptr),"empty list pointer invariant");
        for(uint32_t j=0;j<nodes[i].item_count;++j){Check(nodes[i].items[j] && std::strlen(nodes[i].items[j])<=255,"bounded copied item");copy.items.emplace_back(nodes[i].items[j]);}
    }
    panel=std::move(next);return SCO_OK;
}
sco_result Remove(sco_plugin* self,const char* id){Check(self==owner && !std::strcmp(id,"build.picker"),"owned panel removal");++removeCalls;return SCO_OK;}
sco_result Query(const char* name,uint32_t version,const void** out){Check(version==0x10000u,"public v1 query");*out=nullptr;if(reenter==3 && !std::strcmp(name,SC_WORLD_CONTROL_NAME)){reenter=0;test_module::Unload();return SCO_NOT_FOUND;} if(!std::strcmp(name,SCO_UI_NAME))*out=&ui; if(g5 && !std::strcmp(name,SC_WORLD_CONTROL_NAME))*out=&control; if(g5 && !std::strcmp(name,SC_WORLD_NAME))*out=&world; if(g5 && !std::strcmp(name,SC_SPATIAL_SERVICE_NAME))*out=&spatial; if(!std::strcmp(name,SCO_WIDGETS_NAME)&&widgetsPresent)*out=widgetsTable;if(!std::strcmp(name,SCO_CONTENT_NAME)&&contentPresent){*out=contentTable;return contentResult;}return *out?SCO_OK:SCO_NOT_FOUND;}
sco_result Invoke(sco_plugin* self,const char* name,const sco_arg* args,uint32_t n,sco_invoke_done done,void* ctx){
    Check(self==owner && commands.count(name),"public owner command route");invoked.emplace_back(name);
    if(n==2){Check(args[0].type==SCO_ARG_STRING && args[1].type==SCO_ARG_FLOAT,"typed placement arguments");names.emplace_back(args[0].v.s);distances.push_back(args[1].v.f);}
    char text[1024]{};const auto& command=commands.at(name);const auto r=command.fn(args,n,command.ctx,text,sizeof(text));if(done)done(r,text,ctx);return r;
}
void Send(const Panel& target,uint32_t id,uint32_t kind,int32_t index=-1,double value=0,const char* text=nullptr){sco_widget_event e{};e.size=sizeof(e);e.node_id=id;e.kind=kind;e.index=index;e.value=value;e.text=text;Check(target.fn!=nullptr,"published callback");target.fn("build.picker",&e,target.ctx);}
void Send(uint32_t id,uint32_t kind,int32_t index=-1,double value=0,const char* text=nullptr){const auto old=panel;Send(old,id,kind,index,value,text);}
bool Disabled(uint32_t id){return (panel.nodes.at(id).flags&SCO_WIDGET_DISABLED)!=0;}
void Search(const char* text){Send(2,SCO_WIDGET_TEXT_INPUT,-1,0,text);}
void Button(uint32_t id){Send(id,SCO_WIDGET_BUTTON);}
void Tick(){Check(tick!=nullptr,"tick subscription");tick("game.tick",nullptr,tickCtx);}
}
int main(int argc,char** argv){
    test_module::Open(argc,argv);int token=0;owner=reinterpret_cast<sco_plugin*>(&token);
    sco_api api{};api.size=sizeof(api);api.major=SCO_API_MAJOR;api.minor=SCO_API_MINOR;api.log=Log;api.has=Has;api.register_command=Register;api.provide_service=Provide;api.subscribe=Subscribe;api.query_service=Query;api.invoke=Invoke;
    testApi=&api;ui.size=sizeof(ui);ui.bind_hotkey=Bind;widgets.size=sizeof(widgets);widgets.set_panel=SetPanel;widgets.remove_panel=Remove;content.size=sizeof(content);content.item=Item;content.read=Read;contentTable=&content;widgetsTable=&widgets;
    Check(test_module::Load(&api,owner)==SCO_OK,"load");
    Check(!panel.nodes.empty(),"existing Build owner publishes grouped picker");
    Check(panel.nodes[1].items==std::vector<std::string>{"Objects","Squadron 42"},"copied ordered categories with readable names");Check(panel.nodes[3].items==std::vector<std::string>{"FixtureBox"},"copied first-category row");
    Check(panel.nodes.at(30).label=="Choose an item"&&panel.nodes.at(33).label=="Adjust placement"&&panel.nodes.at(34).label=="Place"&&panel.nodes.at(38).label=="Things you placed"&&panel.nodes.at(40).label=="Status","sections in the order a player uses them");
    Check(!panel.nodes.count(9)&&!panel.nodes.count(10),"no aim or start-building controls while the free camera is unavailable");
    Check(Disabled(11)&&Disabled(12)&&Disabled(13),"place, undo and remove-all disabled until there is something to act on");
    auto reload=[&]{test_module::Unload();commands.clear();panel={};itemCalls=readCalls=0;Check(test_module::Load(&api,owner)==SCO_OK,"consumer reload");};
    const auto startup=itemCalls+readCalls;files[0].bytes="[objects]\nChanged\n";Tick();Check(panel.nodes[3].items[0]=="FixtureBox" && itemCalls+readCalls==startup,"loaded catalogue owns copies; tick does not reopen");
    Send(3,SCO_WIDGET_LIST,0);Button(9);Check(invoked.back()=="build.select"&&names.back()=="FixtureBox"&&distances.back()==50,"select exact raw identifier through actual command");
    Send(8,SCO_WIDGET_SLIDER,-1,80);Check(panel.nodes[8].value==80,"distance routed to actual command/state");Button(11);Check(invoked.back()=="build.place"&&distances.back()==80&&panel.nodes[19].label=="Can't place anything yet because the game world isn't ready.","place uses the distance and the missing world refuses in plain words");
    {sco_arg longReach{};longReach.type=SCO_ARG_FLOAT;longReach.v.f=300;Check(Invoke(owner,"build.reach",&longReach,1,nullptr,nullptr)==SCO_OK,"direct long reach");Tick();Check(panel.nodes[8].value==100&&panel.nodes[8].maximum==100,"slider clamped to what placing can use");Button(11);Check(distances.back()==100,"near-player clamps100");}
    char toggleReply[192]{};const auto toggleResult=commands.at("build.toggle").fn(nullptr,0,nullptr,toggleReply,sizeof(toggleReply));
    Check(toggleResult==SCO_UNAVAILABLE&&std::strstr(toggleReply,"G5 has no production adapter; no private fallback"),"selected build toggle reports unavailable G5 without private fallback");
    Tick();Check(panel.nodes[19].label=="Aiming with the free camera isn't available yet. Use Place instead.","panel explains the refusal in plain words");
    Button(10);Check(invoked[invoked.size()-2]=="build.select"&&invoked.back()=="build.toggle"&&panel.nodes[19].label.find("free camera")!=std::string::npos&&panel.nodes[19].label.find("G5")==std::string::npos,"preview preserves existing G5 refusal");Button(12);Check(invoked.back()=="build.undo","undo route");Button(13);Check(invoked.back()=="build.clear","clear route");
    auto stale=panel;Button(14);Check(panel.nodes[1].index==1&&Disabled(11)&&!panel.nodes.count(17),"SQ42 shortcut changes group and clears selection");const auto calls=invoked.size();Send(stale,11,SCO_WIDGET_BUTTON);Check(invoked.size()==calls,"old publication cookie cannot place");
    Send(3,SCO_WIDGET_LIST,0);Check(panel.nodes.count(17)&&panel.nodes.count(15)&&panel.nodes.count(16),"Squadron 42 settings appear for a Squadron 42 item");Button(17);Check(names.back()=="FixtureS42"&&distances.back()==8,"SQ42 default in-front distance");Send(16,SCO_WIDGET_SLIDER,-1,100);Button(17);Check(distances.back()==100,"SQ42 bounded explicit ahead");Send(15,SCO_WIDGET_TOGGLE,-1,0);Check(Disabled(16),"in-front toggle disables ahead");Button(17);Check(distances.back()==0,"SQ42 not-in-front route is current player");
    files[0].bytes="[one]\n";for(unsigned i=0;i<70;++i)files[0].bytes+="Fixture_"+std::to_string(i)+"\n";files[0].bytes+="Fixture_0\n[two]\nOther_Fixture\nPipe|Preserved\n";reload();
    Check(panel.nodes[3].items.size()==64&&Disabled(4)&&!Disabled(5),"first64-row page");Send(3,SCO_WIDGET_LIST,0);Button(5);Check(panel.nodes[3].items.size()==7&&panel.nodes[3].items[0]=="Fixture 64"&&Disabled(11)&&Disabled(5),"last page preserves order and clears selection");
    Send(3,SCO_WIDGET_LIST,6);Button(9);Check(names.back()=="Fixture_0","duplicate rows retain independent global mapping");Button(4);Check(panel.nodes[3].items[0]=="Fixture 0"&&panel.nodes[6].label=="Page 1 of 2 (71 items)","previous page route");
    Search("Other Fixture");Check(panel.nodes[3].items==std::vector<std::string>{"Other Fixture (Two)"}&&Disabled(11),"global case/underscore word search and category tag");Send(3,SCO_WIDGET_LIST,0);Button(9);Check(names.back()=="Other_Fixture","tag is display only");
    Search("Pipe");Send(3,SCO_WIDGET_LIST,0);Button(9);Check(names.back()=="Pipe|Preserved","Build identifiers never use Loadout pipe splitting");
    Search("absent");Check(panel.nodes[3].items.empty()&&Disabled(3)&&Disabled(11),"empty search state");Search(" ");Check(panel.nodes[3].items.size()==64,"blank search stays current category");
    Search(std::string(64,'x').c_str());Check(panel.nodes[19].label=="Search text is too long (63 characters at most).","search byte bound");Search("bad\ntext");Check(panel.nodes[19].label.find("plain letters")!=std::string::npos,"search controls refused");
    files[0].bytes="[one]\n"+std::string(511,'x')+"\n";reload();Check(panel.nodes[3].items[0].size()==255,"display-only truncation");Send(3,SCO_WIDGET_LIST,0);Button(9);Check(names.back().size()==511,"full511-byte raw identifier retained");
    stale=panel;panelResult=SCO_FAILED;Search("none");auto failedCalls=invoked.size();Send(stale,11,SCO_WIDGET_BUTTON);Check(invoked.size()==failedCalls,"failed replacement disables old actions");panelResult=SCO_OK;Tick();Check(panel.nodes[3].items.empty(),"failed publication retries atomically");
    Search("");Send(3,SCO_WIDGET_LIST,0);stale=panel;widgetsPresent=false;Search("none");Send(stale,11,SCO_WIDGET_BUTTON);Check(invoked.size()==failedCalls,"missing widget provider pauses actions");widgetsPresent=true;Tick();Check(panel.nodes[3].items.empty(),"widget requery recovers without cached lease");
    for(const auto& bad:std::vector<std::string>{"row before group\n","[]\n","[unclosed\n","[one] suffix\n","["+std::string(24,'g')+"]\n","[one]\n"+std::string(512,'x')+"\n","[one]\nGood\n[ONE]\nBad\n","[one]\nGood\nBad\tName\n","[one]\nGood\rHidden\n",std::string("[one]\nGood\n")+'\0',"[one]\nGood\n\xc3\xa9\n"}){
        files[0].bytes=bad;reload();Check(panel.nodes[3].items.empty()&&panel.nodes[18].label.find("damaged")!=std::string::npos,"malformed group/row refuses atomically");
    }
    files[0].bytes="["+std::string(23,'g')+"]\r\nFixture\r\n";reload();Check(panel.nodes[1].items[0].size()==23&&panel.nodes[3].items[0]=="Fixture","exact group label bound and CRLF");
    files[0].bytes=std::string(SCO_CONTENT_MAX_FILE_BYTES+1,'x');reload();Check(panel.nodes[3].items.empty()&&panel.nodes[18].label.find("too large")!=std::string::npos,"actual copied byte count exceeds1MiB");
    files[0].bytes.clear();reload();Check(panel.nodes[1].items.empty()&&panel.nodes[18].label=="The item list is empty.","explicit empty file valid");
    files[0].bytes="[one]\n";for(unsigned i=0;i<4096;++i)files[0].bytes+="Fixture\n";reload();Check(!panel.nodes.count(18)&&panel.nodes[3].items.size()==64,"exact4096 rows accepted");files[0].bytes+="Extra\n";reload();Check(panel.nodes[3].items.empty(),"4097 rows atomic refusal");
    files[0].bytes.clear();for(unsigned i=0;i<24;++i)files[0].bytes+="[group"+std::to_string(i)+"]\nFixture\n";reload();Check(panel.nodes[1].items.size()==24,"exact24 groups accepted");files[0].bytes+="[extra]\nFixture\n";reload();Check(panel.nodes[1].items.empty(),"25 groups atomic refusal");
    files[0].bytes="[one]\nGood\n";
    for(const char* wrong:{"buildables.txt","/lists/buildables.txt","Lists/buildables.txt","lists/buildables.TXT","lists/nested/buildables.txt"}){files[0].name=wrong;reload();Check(panel.nodes[3].items.empty()&&!readCalls,"exact canonical list namespace only");}files[0].name="lists/buildables.txt";
    files.push_back(files[0]);reload();Check(panel.nodes[3].items.empty()&&!readCalls&&panel.nodes[18].label.find("More than one")!=std::string::npos,"duplicate canonical sources refuse without guessing");files.resize(1);
    for(badMetadata=1;badMetadata<=5;++badMetadata){reload();Check(panel.nodes[3].items.empty()&&!readCalls,"malformed copied metadata rejected");}badMetadata=0;
    for(badRead=1;badRead<=4;++badRead){reload();Check(panel.nodes[3].items.empty(),"oversized/inconsistent/refused read has no partial catalogue");}badRead=0;
    endless=true;reload();Check(itemCalls==SCO_CONTENT_MAX_ITEMS+1&&panel.nodes[3].items.empty(),"enumeration bounded4096");endless=false;
    for(uint32_t size=4;size<offsetof(sco_content_v1,read)+sizeof(content.read);++size){auto table=std::make_unique<unsigned char[]>(size);std::memcpy(table.get(),&content,size);std::memcpy(table.get(),&size,4);contentTable=table.get();reload();Check(!itemCalls&&!readCalls&&panel.nodes[3].items.empty(),"physically short content table refuses");}contentTable=&content;
    content.item=nullptr;reload();Check(!itemCalls&&!readCalls,"null item");content.item=Item;content.read=nullptr;reload();Check(!itemCalls&&!readCalls,"null read");content.read=Read;
    contentTable=nullptr;reload();Check(!itemCalls&&!readCalls,"OK null content table");contentTable=&content;contentResult=SCO_FAILED;reload();Check(!itemCalls&&!readCalls,"failed query table ignored");contentResult=SCO_OK;
    contentPresent=false;reload();Check(panel.nodes[18].label.find("could not be loaded")!=std::string::npos,"missing public content explicit");contentPresent=true;
    files.clear();reload();Check(panel.nodes[18].label.find("No item list")!=std::string::npos,"missing pack no implicit raw fallback");
    files={{"lists/buildables.txt","[one]\nFixture\n"}};
    for(uint32_t size=4;size<offsetof(sco_widgets_v1,remove_panel)+sizeof(widgets.remove_panel);++size){auto table=std::make_unique<unsigned char[]>(size);std::memcpy(table.get(),&widgets,size);std::memcpy(table.get(),&size,4);widgetsTable=table.get();reload();Check(panel.nodes.empty(),"physically short widgets table refuses publication");}widgetsTable=&widgets;
    widgets.set_panel=nullptr;reload();Check(panel.nodes.empty(),"null panel callback");widgets.set_panel=SetPanel;widgets.remove_panel=nullptr;reload();Check(panel.nodes.empty(),"null remove callback");widgets.remove_panel=Remove;reload();
    Send(3,SCO_WIDGET_LIST,0);stale=panel;const auto beforeUnload=invoked.size();const auto beforeRemove=removeCalls;test_module::Unload();Check(removeCalls==beforeRemove+1,"unload removes owned panel");Send(stale,11,SCO_WIDGET_BUTTON);Check(invoked.size()==beforeUnload,"old callback after unload inert");Check(test_module::Load(&api,owner)==SCO_OK,"reload after unload");Send(stale,11,SCO_WIDGET_BUTTON);Check(invoked.size()==beforeUnload,"old callback cannot target new owner session");test_module::Unload();
    for(uint32_t size=4;size<offsetof(sco_api,subscribe)+sizeof(api.subscribe);++size){auto table=std::make_unique<unsigned char[]>(size);std::memcpy(table.get(),&api,size);std::memcpy(table.get(),&size,4);Check(test_module::Load(reinterpret_cast<const sco_api*>(table.get()),owner)==SCO_UNAVAILABLE,"physically short API refused before access");}
    control.size=sizeof(control);control.camera_acquire=Acquire;control.camera_release=Release;control.camera_step=Step;control.raycast_filtered=RayFiltered;
    world.size=sizeof(world);world.raycast=Ray;world.camera=Camera;
    spatial.size=sizeof(spatial);spatial.player_pose=Pose;spatial.world_to_local=Convert;spatial.local_to_world=Convert;g5=true;
    auto toggle=[&](){return Invoke(owner,"build.toggle",nullptr,0,nullptr,nullptr);};
    files={{"lists/buildables.txt","[one]\ninvalid.socpak\nNPC_First\nFixture\n"}};reload();
    Check(hotkeyCommand=="build.toggle" && Invoke(owner,hotkeyCommand.c_str(),nullptr,0,nullptr,nullptr)==SCO_OK && acquires==1,"F6 route selects first admissible default and acquires once");Tick();Check(panel.nodes[22].label=="Aiming with: NPC First","default preserves first NPC without native assumptions");Check(panel.nodes.count(9)&&panel.nodes[10].label=="Stop aiming","aim controls appear when the free camera is available");
    Send(3,SCO_WIDGET_LIST,2);Check(panel.nodes[7].label=="Selected: Fixture"&&panel.nodes[22].label=="Aiming with: NPC First","highlight does not commit selection");
    Search("absent");Check(panel.nodes[7].label=="Nothing selected yet."&&panel.nodes[22].label=="Aiming with: NPC First","filter preserves effective selection");
    failRelease=true;Check(toggle()==SCO_FAILED && releases==1,"failed release retains lease");Tick();Check(panel.nodes[10].label=="Stop aiming","retained lease keeps stop action");failRelease=false;Button(10);Check(releases==2,"picker retries retained lease stop");
    files.clear();reload();auto before=acquires;Check(toggle()==SCO_UNAVAILABLE && acquires==before,"missing catalogue refuses before acquisition");
    sco_arg explicitArgs[2]{};explicitArgs[0].type=SCO_ARG_STRING;explicitArgs[0].v.s="Manual";explicitArgs[1].type=SCO_ARG_FLOAT;explicitArgs[1].v.f=123;
    Check(Invoke(owner,"build.select",explicitArgs,2,nullptr,nullptr)==SCO_OK && toggle()==SCO_OK && acquires==before+1,"explicit selection works without catalogue");Tick();Check(panel.nodes[22].label.find("Manual")!=std::string::npos && panel.nodes[8].value==123,"explicit selection and reach preserved");Check(toggle()==SCO_OK,"explicit stop");
    for(const auto& bytes:std::vector<std::string>{"", "[one]\ninvalid.socpak\n", "[one]\nGood\n[ONE]\nBad\n"}){files={{"lists/buildables.txt",bytes}};reload();before=acquires;Check(toggle()==SCO_UNAVAILABLE && acquires==before,"empty invalid or all-invalid catalogue refuses before acquisition");}
    files={{"lists/buildables.txt","[one]\nGood\n"},{"lists/buildables.txt","[two]\nOther\n"}};reload();before=acquires;Check(toggle()==SCO_UNAVAILABLE && acquires==before,"ambiguous sources refuse before acquisition");
    files.resize(1);badRead=3;reload();before=acquires;Check(toggle()==SCO_UNAVAILABLE && acquires==before,"content read error refuses before acquisition");badRead=0;
    // Camera admission retains its original prerequisites: player_pose is used by
    // later pose/placement operations, not by toggle admission.
    files={{"lists/buildables.txt","[one]\nFixture\n"}};reload();spatial.player_pose=nullptr;
    explicitArgs[0].v.s="ManualNoPose";explicitArgs[1].v.f=123;
    before=acquires;Check(Invoke(owner,"build.select",explicitArgs,2,nullptr,nullptr)==SCO_OK && toggle()==SCO_OK && acquires==before+1,"explicit selection and toggle do not require player_pose");
    Check(toggle()==SCO_OK,"stop without player_pose");spatial.player_pose=Pose;
    // Direct command changes effective bytes without changing reach; failed publication retries.
    files={{"lists/buildables.txt","[one]\nFixture\n"}};reload();
    auto oldPanel=panel;explicitArgs[0].v.s="Direct";explicitArgs[1].v.f=50;
    Check(Invoke(owner,"build.select",explicitArgs,2,nullptr,nullptr)==SCO_OK,"direct selection");
    panelResult=SCO_FAILED;Tick();Check(panel.nodes[22].label.find("Direct")==std::string::npos,"failed publication preserves old snapshot");
    before=static_cast<unsigned>(invoked.size());Send(oldPanel,9,SCO_WIDGET_BUTTON);Check(invoked.size()==before,"failed publication revokes callback");
    panelResult=SCO_OK;Tick();Check(panel.nodes[22].label=="Aiming with: Direct","retry publishes direct selection");
    reenter=1;before=acquires;Check(toggle()==SCO_OK && nested==SCO_UNAVAILABLE && acquires==before+1,"nested toggle refused during acquisition");Check(toggle()==SCO_OK,"nested acquisition remains stoppable");
    reenter=3;before=acquires;Check(toggle()==SCO_UNAVAILABLE && acquires==before,"query unload stops startup before acquire");
    Check(test_module::Load(&api,owner)==SCO_OK,"reload after query unload");
    reenter=2;before=acquires;Check(toggle()==SCO_UNAVAILABLE && acquires==before+1 && nested!=SCO_OK,"acquire unload rejects reentrant reload and does not reuse stale owner or table");
    Check(test_module::Load(&api,owner)==SCO_OK,"reload after acquire unload");
    test_module::Unload();test_module::Close();std::printf("build public picker: %u checks PASS; actual module, synthetic providers only\n",checks);
}
