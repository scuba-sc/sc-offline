// SPDX-License-Identifier: GPL-3.0-only
#include "workflow_module.h"
#include <sc_entities.h>
#include <sc_spawn.h>
#include <sc_spatial.h>
#include <sc_world.h>
#include <sc_owned.h>
#include "../plugins/build/build_state.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <map>
#include <string>
#include <cstring>
#include <cstdio>
namespace {
std::map<std::string,sco_command> commands;
bool ready=true;unsigned spawned=0,removed=0,retained=0;
const sc_offline_build_state_v1* observation=nullptr;
sco_event_fn tick=nullptr;
int Has(const char*){return ready?1:0;}
void Log(sco_plugin*,sco_log_level,const char*){}
sco_result Register(sco_plugin*,const sco_command* command){commands[command->name]=*command;return SCO_OK;}
sco_result Provide(sco_plugin*,const char* name,uint32_t,const void* table){assert(!std::strcmp(name,SC_OFFLINE_BUILD_STATE_NAME));observation=static_cast<const sc_offline_build_state_v1*>(table);return SCO_OK;}
sco_result Subscribe(sco_plugin*,const char*,sco_event_fn fn,void*){tick=fn;return SCO_OK;}
int Pose(double p[3],double q[4],uint64_t* zone){p[0]=p[1]=p[2]=0;q[0]=q[1]=q[2]=0;q[3]=1;*zone=7;return 1;}
sco_result Ray(uint64_t,const double[3],const double[3],double,sc_world_hit*){return SCO_NOT_FOUND;}
int Exists(const char* name){return !std::strcmp(name,"Synthetic_Box");}
sco_result Spawn(sco_plugin*,const char*,uint64_t zone,const double[3],const double[4],uint64_t* id){assert(zone==7);*id=100+ ++spawned;return SCO_OK;}
sco_result Despawn(sco_plugin*,uint64_t){return SCO_OK;}
sco_result Retain(sco_plugin*,uint64_t){++retained;return SCO_OK;}
sco_result Remove(sco_plugin*,uint64_t,uint64_t* ticket){*ticket=++removed;return SCO_OK;}
sc_spatial_v1 spatial{};sc_world_v1 world{};sc_spawn_service_v1 classes{};sc_entities_v1 entities{};sc_owned_v1 owned{};
sco_result Query(const char* name,uint32_t,const void** out){*out=nullptr;if(!ready)return SCO_NOT_FOUND;
if(!std::strcmp(name,SC_SPATIAL_SERVICE_NAME))*out=&spatial;
else if(!std::strcmp(name,SC_WORLD_NAME))*out=&world;
else if(!std::strcmp(name,SC_SPAWN_SERVICE_NAME))*out=&classes;
else if(!std::strcmp(name,SC_ENTITIES_NAME))*out=&entities;
else if(!std::strcmp(name,SC_OWNED_NAME))*out=&owned;
return *out?SCO_OK:SCO_NOT_FOUND;}
sco_result Call(const char* name,const sco_arg* args=nullptr,uint32_t count=0){char reply[256]{};const auto& command=commands.at(name);return command.fn(args,count,command.ctx,reply,sizeof(reply));}
}
int main(int argc,char** argv){test_module::Open(argc,argv);
sco_api api{};api.size=sizeof(api);api.major=SCO_API_MAJOR;api.minor=SCO_API_MINOR;api.has=Has;api.log=Log;api.query_service=Query;api.register_command=Register;api.provide_service=Provide;api.subscribe=Subscribe;
spatial.size=sizeof(spatial);spatial.player_pose=Pose;world.size=sizeof(world);world.raycast=Ray;classes.size=sizeof(classes);classes.class_exists=Exists;
entities.size=sizeof(entities);entities.spawn=Spawn;entities.despawn=Despawn;owned.size=sizeof(owned);owned.retain_build=Retain;owned.remove_build=Remove;
auto* self=reinterpret_cast<sco_plugin*>(uintptr_t{1});assert(test_module::Load(&api,self)==SCO_OK&&observation&&tick);
sco_arg args[2]{};args[0].type=SCO_ARG_STRING;args[0].v.s="Synthetic_Box";args[1].type=SCO_ARG_FLOAT;args[1].v.f=3;
assert(Call("build.place",args,2)==SCO_OK&&spawned==1&&retained==1&&observation->placed_count()==1);
assert(Call("build.undo")==SCO_OK&&removed==1&&observation->placed_count()==0);
ready=false;assert(Call("build.place",args,2)==SCO_UNAVAILABLE&&spawned==1);ready=true;
assert(Call("build.place",args,2)==SCO_OK&&spawned==2);test_module::Unload();assert(removed==2);test_module::Close();
std::puts("build workflow PASS placement/retain/undo/missing capability/unload owned removal; synthetic only");return 0;}
