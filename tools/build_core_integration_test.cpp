// SPDX-License-Identifier: GPL-3.0-only
// Actual build module + Core WorldControl/Owned policy with synthetic entity/camera backends.
#include "workflow_module.h"
#include "sco/host.h"
#include "sco/runtime.h"
#include "sco/caps.h"
#include "game/services/world_control.h"
#include "game/services/owned.h"
#include "game/services/spawn.h"
#include <sc_entities.h>
#include <sc_owned.h>
#include <sc_spatial.h>
#include <sc_spawn.h>
#include <sc_world.h>
#include <sc_world_control.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#define CHECK(x) do { if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);} }while(0)
namespace S=sco::game::services;
static sco_plugin* owner; static sco_plugin* foreign_owner; static bool live=true,fail_remove=false;static uint64_t next_id=100;static bool present[32]{};static double camera=7,saved=0;static S::WorldControl* control;
bool IsOwned(const sco_plugin* p,uint64_t id){return live && p==owner && id>=100 && id<132 && present[id-100];}
namespace sco::game::services { bool EntitiesOwnedBy(const void* p,uint64_t id){return IsOwned(static_cast<const sco_plugin*>(p),id);} bool NpcOwnedBy(const void*,uint64_t){return false;} }
static bool OnThread(){return sco::OnGameThread();} static bool Live(const sco_plugin* p){return live && (p==owner || p==foreign_owner);}
static sco_result Ray(uint64_t,const double*,const double*,double,const uint64_t*,uint32_t,sc_world_hit* out){out->pos[0]=2;out->pos[1]=2;out->pos[2]=2;out->distance=2;return SCO_OK;}
static sco_result Begin(){saved=camera;camera=20;return SCO_OK;} static sco_result Step(const sc_world_camera_step* v){camera+=v->translation[0];return SCO_OK;} static sco_result Restore(){camera=saved;return SCO_OK;}
static sco_result RayTable(sco_plugin* p,uint64_t z,const double* f,const double* d,double m,const uint64_t* ids,uint32_t n,sc_world_hit* h){return control->Ray(p,z,f,d,m,ids,n,h);}
static sco_result Acquire(sco_plugin* p,uint64_t* l){return control->Acquire(p,l);} static sco_result CameraStep(sco_plugin* p,uint64_t l,const sc_world_camera_step* s){return control->Step(p,l,s);} static sco_result Release(sco_plugin* p,uint64_t l){return control->Release(p,l);}
static sco_result Spawn(sco_plugin* p,const char*,uint64_t,const double*,const double*,uint64_t* out){if(p!=owner || !live || next_id>=132)return SCO_UNAVAILABLE;*out=next_id++;present[*out-100]=true;return SCO_OK;}
static sco_result Move(sco_plugin* p,uint64_t id,uint64_t,const double*,const double*){return IsOwned(p,id)?SCO_OK:SCO_NOT_FOUND;}
static sco_result Despawn(sco_plugin* p,uint64_t id){if(!IsOwned(p,id))return SCO_NOT_FOUND;if(fail_remove)return SCO_FAILED;present[id-100]=false;return SCO_OK;}
static int ClassExists(const char*){return 1;}
static int Pose(double p[3],double q[4],uint64_t* z){std::memset(p,0,3*sizeof(double));std::memset(q,0,4*sizeof(double));q[3]=1;*z=1;return 1;}
static int Convert(uint64_t,const double in[3],double out[3]){std::memcpy(out,in,3*sizeof(double));return 1;}
static sco_result Camera(double p[3],double q[4],uint64_t*){std::memset(p,0,3*sizeof(double));std::memset(q,0,4*sizeof(double));q[3]=1;return SCO_OK;}
static sco_result WorldRay(uint64_t,const double*,const double*,double,sc_world_hit*){return SCO_NOT_FOUND;}
static void Cleanup(const void* p){if(p==owner){CHECK(control->ReleaseOwner(p)==SCO_OK);for(bool& b:present)b=false;live=false;}}
static void Provide(const char* n,uint32_t v,const void* table){CHECK(sco::host::ProvideGameService(n,v,table)==sco::Result::Ok);}
static sco_arg Str(const char* v){sco_arg a{};a.type=SCO_ARG_STRING;a.v.s=v;return a;} static sco_arg Float(double v){sco_arg a{};a.type=SCO_ARG_FLOAT;a.v.f=v;return a;}
int main(int argc,char** argv){
 test_module::Open(argc,argv);sco::SetGameThread();const auto* api=sco::host::BuildApi({"build integration fixture"});owner=sco::host::NewPlugin("build");auto* foreign=sco::host::NewPlugin("build_foreign");foreign_owner=foreign;CHECK(api&&owner&&foreign&&test_module::Load(api,owner)==SCO_OK);
 CHECK(api->invoke(owner,"build.toggle",nullptr,0,nullptr,nullptr)==SCO_UNAVAILABLE);
 S::WorldControlProvider provider{OnThread,Live,IsOwned,Ray,Begin,Step,Restore};S::WorldControl world_control(provider,true);control=&world_control;
 CHECK(S::AddGameReleaseHook(Cleanup)==sco::Result::Ok);CHECK(S::StartOwned()==sco::Result::Ok);
 sc_world_control_v1 ctl{};ctl.size=sizeof(ctl);ctl.raycast_filtered=RayTable;ctl.camera_acquire=Acquire;ctl.camera_step=CameraStep;ctl.camera_release=Release;
 sc_entities_v1 entities{};entities.size=sizeof(entities);entities.spawn=Spawn;entities.set_transform=Move;entities.despawn=Despawn;
 sc_world_v1 world{};world.size=sizeof(world);world.raycast=WorldRay;world.camera=Camera;
 sc_spatial_v1 spatial{};spatial.size=sizeof(spatial);spatial.player_pose=Pose;spatial.world_to_local=Convert;spatial.local_to_world=Convert;
 sc_spawn_service_v1 spawn{};spawn.size=sizeof(spawn);spawn.class_exists=ClassExists;
 Provide(SC_WORLD_CONTROL_NAME,SC_WORLD_CONTROL_VERSION_1_0,&ctl);Provide(SC_ENTITIES_NAME,SC_ENTITIES_VERSION_1_0,&entities);Provide(SC_WORLD_NAME,SC_WORLD_VERSION_1_0,&world);Provide(SC_SPATIAL_SERVICE_NAME,SC_SPATIAL_SERVICE_VERSION,&spatial);Provide(SC_SPAWN_SERVICE_NAME,SC_SPAWN_SERVICE_VERSION,&spawn);
 for(const char* cap:{"game.world.free_camera","game.world.raycast_filtered","game.world.raycast","game.entities.spawn"})sco::caps::Set(cap,true,"synthetic fixture");
 sco_arg selection[]{Str("FixtureBox"),Float(50)};CHECK(api->invoke(owner,"build.select",selection,2,nullptr,nullptr)==SCO_OK);CHECK(api->invoke(owner,"build.toggle",nullptr,0,nullptr,nullptr)==SCO_OK && camera==20);
 uint32_t now=1;CHECK(sco::Dispatch("tick",&now)==sco::Result::Ok && present[0]);
 // A non-owner cannot move/remove the module's preview or own its camera lease.
 CHECK(Despawn(foreign,100)==SCO_NOT_FOUND);uint64_t foreign_lease=0;CHECK(ctl.camera_acquire(foreign,&foreign_lease)==SCO_FAILED && foreign_lease==0);
 // A failed preview removal must retain retry authority while camera restore succeeds.
 fail_remove=true;CHECK(api->invoke(owner,"build.toggle",nullptr,0,nullptr,nullptr)==SCO_FAILED && camera==7 && present[0]);
 CHECK(api->invoke(owner,"build.select",selection,2,nullptr,nullptr)==SCO_FAILED && present[0]);fail_remove=false;
 CHECK(api->invoke(owner,"build.select",selection,2,nullptr,nullptr)==SCO_OK && !present[0]);
 CHECK(api->invoke(owner,"build.toggle",nullptr,0,nullptr,nullptr)==SCO_OK);now=2;CHECK(sco::Dispatch("tick",&now)==sco::Result::Ok && present[1]);
 CHECK(api->invoke(owner,"build.drop",nullptr,0,nullptr,nullptr)==SCO_OK && present[2] && S::BuildRetainedBy(owner,102));
 const void* raw=nullptr;CHECK(api->query_service(SC_OWNED_NAME,SC_OWNED_VERSION_1_0,&raw)==SCO_OK);auto* owned=static_cast<const sc_owned_v1*>(raw);uint64_t ticket=0;CHECK(owned->remove_build(foreign,102,&ticket)==SCO_NOT_FOUND && ticket==0);
 CHECK(api->invoke(owner,"build.undo",nullptr,0,nullptr,nullptr)==SCO_OK && !present[2]);
 // Withdraw the control provider mid-session: no fallback or false cleanup claim.
 CHECK(sco::host::WithdrawGameService(SC_WORLD_CONTROL_NAME)==sco::Result::Ok);now=3;CHECK(sco::Dispatch("tick",&now)==sco::Result::Ok && camera==20);
 Provide(SC_WORLD_CONTROL_NAME,SC_WORLD_CONTROL_VERSION_1_0,&ctl);CHECK(api->invoke(owner,"build.toggle",nullptr,0,nullptr,nullptr)==SCO_OK && camera==7 && !present[1]);
 // Synthetic entity cleanup and real Core policy release before cooperative unload.
 // Entity removal here is fixture behavior, not proof of native entity cleanup.
 CHECK(api->invoke(owner,"build.toggle",nullptr,0,nullptr,nullptr)==SCO_OK);now=4;CHECK(sco::Dispatch("tick",&now)==sco::Result::Ok && present[3]);
 CHECK(api->invoke(owner,"build.drop",nullptr,0,nullptr,nullptr)==SCO_OK && present[4] && S::BuildRetainedBy(owner,104));
 CHECK(sco::Release(owner)==sco::Result::Ok && !live && camera==7 && !present[3] && !present[4] && !S::BuildRetainedBy(owner,104));
 test_module::Unload();CHECK(sco::Release(foreign)==sco::Result::Ok);
 S::StopOwned();CHECK(S::RemoveGameReleaseHook(Cleanup)==sco::Result::Ok);CHECK(world_control.Stop()==SCO_OK);test_module::Close();
 std::puts("actual build module + Core WorldControl/Owned + synthetic backend: PASS; not run in game");
}
