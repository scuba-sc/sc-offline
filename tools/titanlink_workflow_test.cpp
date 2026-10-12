// SPDX-License-Identifier: GPL-3.0-only
// Actual dynamically loaded module; synthetic public providers, no game/host code.
#include "workflow_module.h"
#include <sco_ipc.h>
#include <sc_spatial.h>
#include <sc_game_spatial.h>
#include <sc_titanlink.h>
#include <sco_widgets.h>
#include <sco_ui.h>
#include <sco_settings.h>
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include <cmath>
#include <limits>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
namespace {
int tag; auto* owner=reinterpret_cast<sco_plugin*>(&tag);
std::map<std::string,sco_command> commands;
sco_event_fn tick=nullptr; void* tick_ctx=nullptr;
sco_ipc_v1 ipc{};sc_spatial_v1 sp{};sc_game_spatial_v1 full{};
alignas(64) uint8_t memory[TL_CHANNEL_BYTES]{};sc_ipc_chan channel{},other{};sc_ipc_ring_view to_peer{},from_peer{};
uint64_t wire_now=10000;
sco_result Wire(int r){return r==SC_IPC_OK?SCO_OK:r==SC_IPC_EMPTY||r==SC_IPC_NOT_READY||r==SC_IPC_GONE?SCO_NOT_FOUND:r==SC_IPC_FULL||r==SC_IPC_TOO_SMALL?SCO_TOO_MANY:SCO_FAILED;}
tl_sc_state output{};tl_tf_state peer{};tl_frame frame{};
uint32_t age=0;uint64_t zone=91;bool opened=false;int creates=0,closes=0,conversions=0;
std::vector<uint32_t> messages;std::string reply;
const char* absent=nullptr;const char* override_name=nullptr;const void* override_table=nullptr;
sco_result age_result=SCO_OK,pop_result=SCO_NOT_FOUND;int pop_calls=0;bool short_frame=false;
sco_widgets_v1 widgets{};sco_ui_v1 ui{};sco_settings_v1 settings{};
sco_widget_event_fn widget_event=nullptr;int panels=0,removes=0,binds=0;std::map<uint32_t,std::string> labels;
sco_result Panel(sco_plugin* self,const char* id,const char*,const sco_widget_node* nodes,uint32_t count,uint32_t stride,sco_widget_event_fn fn,void*) {
 CHECK(self==owner && !std::strcmp(id,"titanlink.titanfall") && stride==sizeof(sco_widget_node));++panels;widget_event=fn;labels.clear();for(uint32_t i=0;i<count;++i)labels[nodes[i].id]=nodes[i].label?nodes[i].label:"";return SCO_OK;
}
sco_result RemovePanel(sco_plugin*,const char*){++removes;widget_event=nullptr;return SCO_OK;}
sco_result Bind(sco_plugin*,const char* key,const char* command,const sco_arg*,uint32_t){CHECK(!std::strcmp(key,"f9") && !std::strcmp(command,"titanlink.pilot"));++binds;return SCO_OK;}
sco_result Unbind(sco_plugin*,const char*){return SCO_OK;}
sco_result SettingString(sco_plugin*,const char* name,char* out,uint32_t* cap){const char* s=!std::strcmp(name,"map")?"synthetic_map":!std::strcmp(name,"mode")?"synthetic_mode":"";CHECK(*cap>std::strlen(s));std::strcpy(out,s);return SCO_OK;}
sco_result SettingFloat(sco_plugin*,const char*,double* out){*out=75;return SCO_OK;}
sco_result SettingBool(sco_plugin*,const char*,int32_t* out){*out=0;return SCO_OK;}

sco_result Create(sco_plugin* self,const char* name,uint64_t bytes,uint32_t layout,uint32_t version,uint64_t* out){
 CHECK(self==owner && !std::strcmp(name,TL_CHANNEL_NAME));CHECK(bytes==TL_CHANNEL_BYTES && layout==TL_LAYOUT_ID && version==TL_LAYOUT_VERSION);std::memset(memory,0,sizeof(memory));CHECK(sc_ipc_init(memory,sizeof(memory),1,layout,version,wire_now,&channel)==SC_IPC_OK);
 CHECK(sc_ipc_attach(memory,sizeof(memory),0x314B4C54u,2,&other)==SC_IPC_MISMATCH);
 CHECK(sc_ipc_attach(memory,sizeof(memory),0x314B4C54u,1,&other)==SC_IPC_OK);
 opened=true;++creates;*out=1;return SCO_OK;}
sco_result Close(sco_plugin* self,uint64_t ch){CHECK(self==owner && ch==1 && opened);sc_ipc_close(&channel);CHECK(sc_ipc_check(&other)==SC_IPC_GONE);opened=false;++closes;return SCO_OK;}
sco_result Age(sco_plugin*,uint64_t,uint32_t* out){if(age_result!=SCO_OK)return age_result;if(sc_ipc_check(&channel)!=SC_IPC_OK)return SCO_NOT_FOUND;return Wire(sc_ipc_peer_age_ms(&channel,wire_now,out));}
sco_result Write(sco_plugin*,uint64_t,uint64_t off,const void* data,uint32_t size){CHECK(opened && off==TL_OFF_SC_STATE && size==sizeof(output));std::memcpy(&output,data,size);return Wire(sc_ipc_block_write(&channel,off,data,size));}
sco_result Read(sco_plugin*,uint64_t,uint64_t off,void* data,uint32_t size){CHECK(opened);return Wire(sc_ipc_block_read(&channel,off,data,size));}
sco_result Init(sco_plugin*,uint64_t,uint64_t off,uint64_t cap,uint32_t dir){CHECK((off==TL_OFF_TO_PEER && cap==TL_RING_TO_PEER && dir==SCO_IPC_TO_PEER)||(off==TL_OFF_FROM_PEER && cap==TL_RING_FROM_PEER && dir==SCO_IPC_FROM_PEER));return Wire(sc_ipc_ring_init(&channel,off,cap,dir,off==TL_OFF_TO_PEER?&to_peer:&from_peer));}
sco_result Push(sco_plugin*,uint64_t,uint64_t,uint32_t type,const void* data,uint32_t size){if(type==TL_MSG_START_MATCH){CHECK(size==sizeof(tl_msg_start_match));const auto* m=static_cast<const tl_msg_start_match*>(data);CHECK(!std::strcmp(m->map,"synthetic_map") && !std::strcmp(m->mode,"synthetic_mode"));}messages.push_back(type);const auto r=Wire(sc_ipc_ring_push(&to_peer,type,data,size));uint32_t got=0,cap=256;uint8_t payload[256];CHECK(sc_ipc_ring_pop(&to_peer,&got,payload,&cap)==SC_IPC_OK && got==type);return r;}
sco_result Pop(sco_plugin*,uint64_t,uint64_t,uint32_t* type,void* data,uint32_t* size){++pop_calls;return Wire(sc_ipc_ring_pop(&from_peer,type,data,size));}
int Pose(double p[3],double q[4],uint64_t* z){p[0]=p[1]=p[2]=0;q[0]=q[1]=q[2]=0;q[3]=1;*z=zone;return 1;}
int Legacy(uint64_t,uint64_t,const double in[3],double out[3]){std::memcpy(out,in,3*sizeof(double));return 1;}
int Convert(uint64_t from,uint64_t to,const double*,const double*,double out[3],double rot[4]){CHECK(from==92 && to==91);++conversions;out[0]=2;out[1]=3;out[2]=4;rot[0]=rot[1]=0;rot[2]=std::sqrt(.5);rot[3]=std::sqrt(.5);return 1;}
sco_result Query(const char* name,uint32_t,const void** out){*out=!std::strcmp(name,SCO_WIDGETS_NAME)?static_cast<void*>(&widgets):!std::strcmp(name,SCO_UI_NAME)?static_cast<void*>(&ui):!std::strcmp(name,SCO_SETTINGS_NAME)?static_cast<void*>(&settings):!std::strcmp(name,SCO_IPC_NAME)?static_cast<void*>(&ipc):!std::strcmp(name,SC_SPATIAL_SERVICE_NAME)?static_cast<void*>(&sp):!std::strcmp(name,SC_GAME_SPATIAL_NAME)?static_cast<void*>(&full):nullptr;if(absent && !std::strcmp(absent,name)){return SCO_NOT_FOUND;}if(override_name && !std::strcmp(override_name,name))*out=override_table;return *out?SCO_OK:SCO_NOT_FOUND;}
void Log(sco_plugin* self,sco_log_level,const char*){CHECK(self==owner);}
sco_result Register(sco_plugin* self,const sco_command* c){CHECK(self==owner && !c->capability);commands[c->name]=*c;return SCO_OK;}
sco_result Subscribe(sco_plugin*,const char* topic,sco_event_fn fn,void* ctx){CHECK(!std::strcmp(topic,"tick"));tick=fn;tick_ctx=ctx;return SCO_OK;}
int Has(const char*){return 1;}
sco_result Run(const char* name){auto c=commands.at(name);char b[768]{};auto r=c.fn(nullptr,0,c.ctx,b,sizeof(b));reply=b;return r;}
void Tick(){CHECK(tick);if(opened && sc_ipc_check(&other)==SC_IPC_OK){++wire_now;sc_ipc_peer_beat(&other,2,wire_now-age);CHECK(sc_ipc_block_write(&other,TL_OFF_TF_STATE,&peer,sizeof(peer))==SC_IPC_OK);CHECK(sc_ipc_block_write(&other,TL_OFF_FRAME,&frame,short_frame?sizeof(frame)-1:sizeof(frame))==SC_IPC_OK);
 if(pop_result==SCO_FAILED)from_peer.ring->head=from_peer.ring->tail+from_peer.capacity+8;}
 tick("tick",nullptr,tick_ctx);}
}
int main(int argc,char** argv){
 test_module::Open(argc,argv);CHECK(!std::strcmp(test_module::Query()->name,"titanlink"));
 ipc.size=sizeof(ipc);ipc.create=Create;ipc.close=Close;ipc.peer_age_ms=Age;ipc.block_write=Write;ipc.block_read=Read;ipc.ring_init=Init;ipc.ring_push=Push;ipc.ring_pop=Pop;
 sp.size=sizeof(sp);sp.player_pose=Pose;sp.zone_to_zone=Legacy;full.size=sizeof(full);full.pose_to_zone=Convert;
 sco_api api{};api.size=sizeof(api);api.major=1;api.minor=1;api.log=Log;api.has=Has;api.register_command=Register;api.subscribe=Subscribe;api.query_service=Query;
 widgets.size=sizeof(widgets);widgets.set_panel=Panel;widgets.remove_panel=RemovePanel;ui.size=sizeof(ui);ui.bind_hotkey=Bind;ui.unbind_hotkey=Unbind;
 settings.size=sizeof(settings);settings.get_string=SettingString;settings.get_float=SettingFloat;settings.get_bool=SettingBool;
 CHECK(test_module::Load(&api,owner)==SCO_OK);CHECK(creates==0 && commands.size()==5);CHECK(panels==1 && binds==1 && widget_event);
 CHECK(Run("titanlink.pilot")==SCO_OK);peer.flags=TL_TF_IN_MATCH;Tick();CHECK(output.flags&TL_SC_PILOT);
 CHECK(Run("titanlink.titan")==SCO_OK && messages.back()==TL_MSG_CALL_TITAN);
 peer.flags=TL_TF_IN_MATCH|TL_TF_PARKED;Tick();CHECK(Run("titanlink.embark")==SCO_OK && messages.back()==TL_MSG_EMBARK);
 peer.flags=TL_TF_IN_MATCH|TL_TF_IN_TITAN;Tick();CHECK(Run("titanlink.embark")==SCO_OK && messages.back()==TL_MSG_DISEMBARK);peer.flags=TL_TF_IN_MATCH;
 zone=92;Tick();CHECK(conversions>0);CHECK(std::fabs(output.feet[0]-2*TL_UNITS_PER_METRE)<.01);CHECK(std::fabs(std::fabs(output.ang[1])-180)<.01);
 absent=SC_GAME_SPATIAL_NAME;Tick();CHECK(!(output.flags&TL_SC_ANCHORED));absent=nullptr;Tick();
 CHECK(Run("titanlink.status")==SCO_OK);CHECK(reply.find("G2 relocation refused")!=std::string::npos && reply.find("G7 cvar writes refused")!=std::string::npos);
 frame.width=9000;std::strcpy(frame.status,"untrusted frame");Tick();CHECK(Run("titanlink.status")==SCO_OK);CHECK(reply.find("frame rejected")!=std::string::npos);
 frame={};std::strcpy(frame.status,"valid");Tick();short_frame=true;Tick();CHECK(Run("titanlink.status")==SCO_OK && reply.find("frame unavailable")!=std::string::npos);short_frame=false;
 peer.origin[0]=std::numeric_limits<float>::quiet_NaN();Tick();CHECK(!(output.flags&TL_SC_PILOT));CHECK(Run("titanlink.titan")==SCO_UNAVAILABLE);peer.origin[0]=0;
 CHECK(Run("titanlink.pilot")==SCO_OK);Tick();CHECK(output.flags&TL_SC_PILOT);
 age=3999;Tick();CHECK(output.flags&TL_SC_PILOT);age=4000;Tick();CHECK(!(output.flags&TL_SC_PILOT));age=0;
 CHECK(Run("titanlink.pilot")==SCO_OK);Tick();pop_result=SCO_FAILED;Tick();const int pops=pop_calls;Tick();CHECK(pop_calls==pops);pop_result=SCO_NOT_FOUND;
 sc_ipc_close(&other);Tick();CHECK(!opened);const int closed=closes;Tick();CHECK(closes==closed);age_result=SCO_OK;
 test_module::Unload();test_module::Unload();CHECK(closes==closed);commands.clear();
 absent=SCO_IPC_NAME;CHECK(test_module::Load(&api,owner)==SCO_OK);CHECK(Run("titanlink.pilot")==SCO_UNAVAILABLE && !opened);test_module::Unload();commands.clear();absent=nullptr;
 // Physical partial callback tails: actual allocations end inside pointers.
 struct Tail {const char* name;const void* table;size_t extent;};
 for(auto tail : {Tail{SCO_IPC_NAME,&ipc,offsetof(sco_ipc_v1,create)+sizeof(ipc.create)},
 Tail{SCO_IPC_NAME,&ipc,offsetof(sco_ipc_v1,peer_age_ms)+sizeof(ipc.peer_age_ms)},
 Tail{SCO_IPC_NAME,&ipc,offsetof(sco_ipc_v1,block_read)+sizeof(ipc.block_read)},
 Tail{SCO_IPC_NAME,&ipc,offsetof(sco_ipc_v1,block_write)+sizeof(ipc.block_write)},
 Tail{SCO_IPC_NAME,&ipc,offsetof(sco_ipc_v1,ring_init)+sizeof(ipc.ring_init)},
 Tail{SCO_IPC_NAME,&ipc,offsetof(sco_ipc_v1,ring_push)+sizeof(ipc.ring_push)},
 Tail{SCO_IPC_NAME,&ipc,offsetof(sco_ipc_v1,ring_pop)+sizeof(ipc.ring_pop)},
 Tail{SCO_IPC_NAME,&ipc,offsetof(sco_ipc_v1,close)+sizeof(ipc.close)},Tail{SC_SPATIAL_SERVICE_NAME,&sp,offsetof(sc_spatial_v1,player_pose)+sizeof(sp.player_pose)},Tail{SC_GAME_SPATIAL_NAME,&full,offsetof(sc_game_spatial_v1,pose_to_zone)+sizeof(full.pose_to_zone)}}){
  const auto bytes=static_cast<uint32_t>(tail.extent-1);void* memory=std::malloc(bytes);CHECK(memory);std::memcpy(memory,tail.table,bytes);std::memcpy(memory,&bytes,sizeof(bytes));override_name=tail.name;override_table=memory;
  CHECK(test_module::Load(&api,owner)==SCO_OK);CHECK(Run("titanlink.pilot")==SCO_UNAVAILABLE && !opened);test_module::Unload();commands.clear();override_name=nullptr;std::free(memory);
 }
 auto saved=ipc.block_read;ipc.block_read=nullptr;CHECK(test_module::Load(&api,owner)==SCO_OK);CHECK(Run("titanlink.pilot")==SCO_UNAVAILABLE && !opened);test_module::Unload();ipc.block_read=saved;commands.clear();
 const uint32_t api_bytes=offsetof(sco_api,query_service)+sizeof(api.query_service)-1;
 void* api_tail=std::malloc(api_bytes);std::memcpy(api_tail,&api,api_bytes);std::memcpy(api_tail,&api_bytes,sizeof(api_bytes));CHECK(test_module::Load(static_cast<const sco_api*>(api_tail),owner)==SCO_UNAVAILABLE);std::free(api_tail);
 zone=91;CHECK(test_module::Load(&api,owner)==SCO_OK);CHECK(Run("titanlink.pilot")==SCO_OK);Tick();CHECK(widget_event);
 sco_widget_event event{};event.size=sizeof(event);event.node_id=4;event.kind=SCO_WIDGET_BUTTON;widget_event("titanlink.titanfall",&event,nullptr);CHECK(!opened);
 test_module::Unload();CHECK(!opened && !widget_event && removes>0);test_module::Unload();test_module::Close();
 std::puts("titanlink-module-workflow: PASS (public wire, G3 full pose, commands/settings/UI, malformed/heartbeat/closed peer, physical partial tables, reload; synthetic, not run in game)");
}
