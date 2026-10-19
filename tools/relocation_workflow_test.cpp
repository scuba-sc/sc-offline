// SPDX-License-Identifier: GPL-3.0-only
#include "workflow_module.h"
#include <sc_spatial.h>
#include <sc_game_spatial.h>
#include <sc_relocation.h>
#include <sco_storage.h>
#include <sco_settings.h>
#include <sco_widgets.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <map>
#include <string>
#include <cstring>
#include <filesystem>
namespace {
std::map<std::string,sco_command> commands;
std::map<std::string,std::string> values;
bool available=true,storageReady=true;unsigned requests=0,cancels=0;
sco_event_fn tick=nullptr;void* tickContext=nullptr;
sco_widget_event_fn event=nullptr;void* eventContext=nullptr;
uint32_t requestState=SC_RELOCATION_PENDING;
bool malformed=false;std::string panelStatus;
sco_result reportedResult=SCO_OK;
std::string root;
int Has(const char* name){return available&&!std::strcmp(name,SC_RELOCATION_NAME);}
sco_result Register(sco_plugin*,const sco_command* c){commands[c->name]=*c;return SCO_OK;}
void Log(sco_plugin*,sco_log_level level,const char*){if(level==SCO_LOG_WARN)reportedResult=SCO_FAILED;}
sco_result Subscribe(sco_plugin*,const char*,sco_event_fn fn,void* context){tick=fn;tickContext=context;return SCO_OK;}
sco_result Unsubscribe(sco_plugin*,const char*,sco_event_fn){tick=nullptr;return SCO_OK;}
int Pose(double p[3],double q[4],uint64_t* zone){p[0]=1;p[1]=2;p[2]=3;q[0]=q[1]=q[2]=0;q[3]=1;*zone=7;return 1;}
int Zone(uint64_t id,sc_game_zone* out){if(id!=7)return 0;*out={};out->size=sizeof(*out);out->zone_id=7;out->root_id=7;out->system_id=1;out->epoch=1;std::snprintf(out->name,sizeof(out->name),"Synthetic_Zone");return 1;}
int Find(const char* name,sc_game_zone* out){return !std::strcmp(name,"Synthetic_Zone")?Zone(7,out):0;}
int Convert(uint64_t,uint64_t,const double p[3],const double q[4],double outP[3],double outQ[4]){std::memcpy(outP,p,3*sizeof(double));std::memcpy(outQ,q,4*sizeof(double));return 1;}
sco_result Put(sco_plugin*,const char* key,const void* value,uint32_t size){if(!storageReady)return SCO_FAILED;values[key]=std::string(static_cast<const char*>(value),size);return SCO_OK;}
sco_result Get(sco_plugin*,const char* key,void* out,uint32_t* size){auto it=values.find(key);if(it==values.end()){*size=0;return SCO_NOT_FOUND;}if(*size<it->second.size()){*size=static_cast<uint32_t>(it->second.size());return SCO_TOO_MANY;}std::memcpy(out,it->second.data(),it->second.size());*size=static_cast<uint32_t>(it->second.size());return SCO_OK;}
sco_result Tx(sco_plugin*){return SCO_OK;}
sco_result Exec(sco_plugin*,const char*,const sco_sql_value*,uint32_t,int64_t*){return SCO_OK;}
sco_result Sql(sco_plugin*,const char*,const sco_sql_value*,uint32_t,uint64_t*){return SCO_UNAVAILABLE;}
sco_result Step(sco_plugin*,uint64_t){return SCO_NOT_FOUND;}
sco_result Column(sco_plugin*,uint64_t,uint32_t,sco_sql_value*,void*,uint32_t*){return SCO_UNAVAILABLE;}
sco_result Close(sco_plugin*,uint64_t){return SCO_OK;}
sco_result Setting(sco_plugin*,const char* name,char* out,uint32_t* size){if(std::strcmp(name,"data_root"))return SCO_NOT_FOUND;if(*size<root.size()+1){*size=static_cast<uint32_t>(root.size()+1);return SCO_TOO_MANY;}std::memcpy(out,root.c_str(),root.size()+1);*size=static_cast<uint32_t>(root.size()+1);return SCO_OK;}
sco_result Bool(sco_plugin*,const char*,int32_t* out){*out=1;return SCO_OK;}
sco_result Request(sco_plugin*,const sc_relocation_request* in,uint64_t* ticket){assert(in->zone_id==7&&in->pos[0]==1&&in->pos[1]==2&&in->pos[2]==3);++requests;requestState=SC_RELOCATION_PENDING;*ticket=42;return SCO_OK;}
sco_result Cancel(sco_plugin*,uint64_t){++cancels;return SCO_OK;}
sco_result Status(sco_plugin*,uint64_t ticket,sc_relocation_status* out){out->state=requestState;out->ticket=malformed?0:ticket;out->applied=requestState==SC_RELOCATION_COMPLETE;return SCO_OK;}
sco_result Panel(sco_plugin*,const char*,const char*,const sco_widget_node* nodes,uint32_t count,uint32_t,sco_widget_event_fn fn,void* context){event=fn;eventContext=context;for(uint32_t i=0;i<count;++i)if(nodes[i].id==1)panelStatus=nodes[i].label;return SCO_OK;}
sco_result Remove(sco_plugin*,const char*){event=nullptr;return SCO_OK;}
sc_spatial_v1 spatial{};sc_game_spatial_v1 zones{};sco_storage_v1 storage{};sco_settings_v1 settings{};sco_widgets_v1 widgets{sizeof(widgets),0,Panel,Remove};sc_relocation_v1 relocation{sizeof(relocation),Request,Cancel,Status};
sco_result Query(const char* name,uint32_t,const void** out){*out=nullptr;
if(!std::strcmp(name,SC_SPATIAL_SERVICE_NAME))*out=&spatial;else if(!std::strcmp(name,SC_GAME_SPATIAL_NAME))*out=&zones;
else if(!std::strcmp(name,SCO_STORAGE_NAME))*out=&storage;else if(!std::strcmp(name,SCO_SETTINGS_NAME))*out=&settings;
else if(!std::strcmp(name,SCO_WIDGETS_NAME))*out=&widgets;else if(!std::strcmp(name,SC_RELOCATION_NAME)&&available)*out=&relocation;
return *out?SCO_OK:SCO_NOT_FOUND;}
sco_result Call(const char* name){char reply[512]{};const auto& c=commands.at(name);return c.fn(nullptr,0,c.ctx,reply,sizeof(reply));}
}
int main(int argc,char** argv){test_module::Open(argc,argv);
root=(std::filesystem::temp_directory_path()/"relocation-dll-workflow").string();std::filesystem::create_directories(root);
spatial.size=sizeof(spatial);spatial.player_pose=Pose;zones.size=sizeof(zones);zones.zone_record=Zone;zones.find_zone=Find;zones.pose_to_zone=Convert;
storage.size=sizeof(storage);storage.put=Put;storage.get=Get;storage.begin=Tx;storage.commit=Tx;storage.rollback=Tx;storage.exec=Exec;storage.query=Sql;storage.step=Step;storage.column=Column;storage.close=Close;
settings.size=sizeof(settings);settings.get_string=Setting;settings.get_bool=Bool;
sco_api api{};api.size=sizeof(api);api.major=SCO_API_MAJOR;api.minor=SCO_API_MINOR;api.has=Has;api.query_service=Query;api.register_command=Register;api.subscribe=Subscribe;api.unsubscribe=Unsubscribe;api.log=Log;
auto* self=reinterpret_cast<sco_plugin*>(uintptr_t{1});assert(test_module::Load(&api,self)==SCO_OK&&event);
sco_widget_event save{};save.size=sizeof(save);save.node_id=16;save.kind=SCO_WIDGET_BUTTON;event("teleport.travel",&save,eventContext);
assert(values.count("spot")&&requests==0);const auto saved=values.at("spot");
storageReady=false;assert(Call("teleport.save")==SCO_FAILED&&values.at("spot")==saved);storageReady=true;
test_module::Unload();commands.clear();assert(test_module::Load(&api,self)==SCO_OK);
assert(Call("teleport.go")==SCO_OK&&requests==1&&tick);uint32_t now=1;tick("tick",&now,tickContext);assert(tick);
requestState=SC_RELOCATION_COMPLETE;tick("tick",&now,tickContext);assert(!tick);
available=false;assert(Call("teleport.go")==SCO_UNAVAILABLE&&requests==1);available=true;
assert(Call("teleport.go")==SCO_OK&&requests==2);malformed=true;requestState=SC_RELOCATION_COMPLETE;tick("tick",&now,tickContext);
assert(!tick&&cancels==1&&reportedResult==SCO_FAILED);malformed=false;
assert(Call("teleport.go")==SCO_OK&&requests==3);test_module::Unload();assert(cancels==2&&!tick&&!event);test_module::Close();
std::filesystem::remove_all(root);std::puts("relocation workflow PASS actual teleport DLL save/reload/move/readback/refusal/cancel");return 0;}
