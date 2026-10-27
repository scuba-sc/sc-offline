// SPDX-License-Identifier: GPL-3.0-only
// Actual independent module, synthetic public providers; no host/game implementation.
#include "workflow_module.h"
#include "sc_spawn.h"
#include "sc_entities.h"
#include "sc_spatial.h"
#include "sc_actors.h"
#include "sc_vehicles.h"
#include <map>
#include <vector>
#include <set>
#include <string>
#include <cstring>
#include <cmath>
#include <limits>
#include <bit>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
namespace {
int tag;
sco_plugin* const owner = reinterpret_cast<sco_plugin*>(&tag);
std::map<std::string,sco_command> commands;
std::map<std::string,std::vector<sco_arg_def>> definitions;
std::set<uint64_t> issued;
uint64_t next_id=101;
int spawned=0,removed=0,seated=0,powered=0;
bool visible=false, linked=false;
const char* absent=nullptr;const char* missing_cap=nullptr;bool null_success=false;
const char* override_name=nullptr;const void* override_table=nullptr;
const char* reopen_name=nullptr;int reopen_at=-1,reopen_calls=0;
uint32_t seat_more=0,seat_count=2,seat_flags=SC_SEAT_USABLE|SC_SEAT_USABLE_KNOWN|SC_SEAT_PILOT;
bool player_present=true;int subscribe_result=SCO_OK,fail_registration=-1,registrations=0;
uint64_t occupant=0;
sco_result seat_result=SCO_OK, power_result=SCO_OK, remove_result=SCO_OK;
sc_spawn_service_v1 classes{};
sc_entities_v1 entities{};
sc_spatial_v1 spatial{};
sc_actors_v1 actors{};
sc_vehicles_v1 vehicles{};
sco_event_fn tick=nullptr; void* tick_ctx=nullptr;
std::string reply;
sco_result Query(const char* name,uint32_t,const void** out) {
 *out=!std::strcmp(name,SC_SPAWN_SERVICE_NAME)? static_cast<void*>(&classes):
 !std::strcmp(name,SC_ENTITIES_NAME)? static_cast<void*>(&entities):
 !std::strcmp(name,SC_SPATIAL_SERVICE_NAME)? static_cast<void*>(&spatial):
 !std::strcmp(name,SC_ACTORS_NAME)? static_cast<void*>(&actors):
 !std::strcmp(name,SC_VEHICLES_SERVICE_NAME)? static_cast<void*>(&vehicles): nullptr;
 if(absent && !std::strcmp(name,absent)) { *out=nullptr;return null_success?SCO_OK:SCO_NOT_FOUND; }
 if(override_name && !std::strcmp(name,override_name)) *out=override_table;
 if(reopen_name && !std::strcmp(name,reopen_name) && ++reopen_calls==reopen_at) return SCO_FAILED; // Output may be non-null on refusal.
 return *out? SCO_OK:SCO_NOT_FOUND;
}
int Has(const char* name) {return !missing_cap || std::strcmp(name,missing_cap);}
sco_result Player(uint64_t* actor,uint64_t* entity){if(!player_present)return SCO_NOT_FOUND;if(actor)*actor=7;if(entity)*entity=7;return SCO_OK;}
sco_result PlayerShip(uint64_t* id){*id=linked?101:0;return linked?SCO_OK:SCO_NOT_FOUND;}
sco_result Seats(uint64_t id,sc_vehicle_seat* out,uint32_t max,uint32_t* count,uint32_t* more){
 CHECK(id==101 && issued.count(id));*count=0;*more=0;if(!visible)return SCO_NOT_FOUND;CHECK(max>=2);
 out[0]={};out[0].index=0;out[0].flags=seat_flags;std::strcpy(out[0].name,"synthetic pilot");
 out[1]={};out[1].index=1;out[1].flags=SC_SEAT_USABLE|SC_SEAT_USABLE_KNOWN;std::strcpy(out[1].name,"synthetic copilot");*count=seat_count;*more=seat_more;return SCO_OK;
}
sco_result Occupant(uint64_t id,uint32_t index,uint64_t* actor){CHECK(id==101 && index<2);*actor=linked?7:occupant;return SCO_OK;}
sco_result Seat(sco_plugin* self,uint64_t actor,uint64_t id,uint32_t index){CHECK(self==owner && actor==7 && id==101 && index<2 && issued.count(id));CHECK(!occupant);++seated;return seat_result;}
sco_result Power(sco_plugin* self,uint64_t id){CHECK(self==owner && id==101 && linked);++powered;return power_result;}
void Advance(uint32_t now){CHECK(tick);tick("tick",&now,tick_ctx);}
sco_result Run(const char*,const sco_arg*,uint32_t);
sco_result Options(const char* seat="pilot",bool power=true){sco_arg a[4]{};a[0].type=SCO_ARG_STRING;a[0].v.s="synthetic_ship";a[1].type=SCO_ARG_FLOAT;a[1].v.f=30;a[2].type=SCO_ARG_STRING;a[2].v.s=seat;a[3].type=SCO_ARG_BOOL;a[3].v.i=power;return Run("spawn.ship_options",a,4);}

void Log(sco_plugin* self,sco_log_level,const char*) { CHECK(self==owner); }
int cleanup_warnings=0,unsubscriptions=0;
void CleanupLog(sco_plugin* self,sco_log_level level,const char* text) {
 CHECK(self==owner);if(level==SCO_LOG_WARN && !std::strcmp(text,"Cleanup refused; host owner-release is required"))++cleanup_warnings;
}
sco_result Unsubscribe(sco_plugin* self,const char* topic,sco_event_fn fn) {
 CHECK(self==owner && !std::strcmp(topic,"tick") && fn==tick);++unsubscriptions;tick=nullptr;return SCO_OK;
}
sco_result Register(sco_plugin* self,const sco_command* c) {
 CHECK(self==owner);if(registrations++==fail_registration)return SCO_FAILED; commands[c->name]=*c;
 if(c->nargs) definitions[c->name]={c->args,c->args+c->nargs}; return SCO_OK;
}
sco_result Subscribe(sco_plugin* self,const char* topic,sco_event_fn fn,void* ctx) {
 CHECK(self==owner && !std::strcmp(topic,"tick")); tick=fn;tick_ctx=ctx;return static_cast<sco_result>(subscribe_result);
}
int ClassExists(const char* cls){return !std::strcmp(cls,"synthetic_ship");}
int Pose(double p[3],double q[4],uint64_t* z){p[0]=1;p[1]=2;p[2]=3;q[0]=q[1]=q[2]=0;q[3]=1;*z=91;return 1;}
sco_result Spawn(sco_plugin* self,const char* cls,uint64_t z,const double p[3],const double q[4],uint64_t* id){
 CHECK(self==owner && !std::strcmp(cls,"synthetic_ship") && z==91);
 CHECK(p[0]==1 && p[1]==2 && p[2]==33 && q[3]==1);
 *id=next_id++;issued.insert(*id);++spawned;return SCO_OK;
}
sco_result Remove(sco_plugin* self,uint64_t id){CHECK(self==owner && issued.count(id));++removed;if(remove_result==SCO_OK)issued.erase(id);return remove_result;}
int Alive(uint64_t id){CHECK(id>=101 && id<next_id);return visible;}
sco_api Api(){sco_api a{};a.size=sizeof(a);a.major=1;a.minor=1;a.log=Log;a.has=Has;a.register_command=Register;a.query_service=Query;a.subscribe=Subscribe;return a;}
sco_result Run(const char* name,const sco_arg* args=nullptr,uint32_t n=0){auto c=commands.at(name);char b[512]{};auto r=c.fn(args,n,c.ctx,b,sizeof(b));reply=b;return r;}
sco_result Ship(const char* cls="synthetic_ship"){sco_arg a[2]{};a[0].type=SCO_ARG_STRING;a[0].v.s=cls;a[1].type=SCO_ARG_FLOAT;a[1].v.f=30;return Run("spawn.ship",a,2);}
sco_result Id(const char* command,uint64_t id){sco_arg a{};a.type=SCO_ARG_INT;a.v.i=std::bit_cast<int64_t>(id);return Run(command,&a,1);}
void Contains(const char* t){CHECK(reply.find(t)!=std::string::npos);}
void Reset(){test_module::Unload();commands.clear();definitions.clear();issued.clear();next_id=101;spawned=removed=seated=powered=0;visible=false;tick=nullptr;
 classes={};classes.size=sizeof(classes);classes.class_exists=ClassExists;
 entities={};entities.size=sizeof(entities);entities.spawn=Spawn;entities.despawn=Remove;entities.alive=Alive;
 spatial={};spatial.size=sizeof(spatial);spatial.player_pose=Pose;
 actors={};actors.size=sizeof(actors);actors.local_player=Player;
 vehicles={};vehicles.size=sizeof(vehicles);vehicles.seats=Seats;vehicles.seat_occupant=Occupant;vehicles.seat=Seat;vehicles.power_on=Power;vehicles.player_ship=PlayerShip;
 linked=false;occupant=0;seat_result=power_result=remove_result=SCO_OK;
 null_success=false;absent=missing_cap=override_name=reopen_name=nullptr;override_table=nullptr;reopen_calls=0;reopen_at=-1;
 seat_more=0;seat_count=2;seat_flags=SC_SEAT_USABLE|SC_SEAT_USABLE_KNOWN|SC_SEAT_PILOT;
 player_present=true;subscribe_result=SCO_OK;fail_registration=-1;registrations=0;
}
void ProviderNegatives(){Reset();auto api=Api();CHECK(test_module::Load(&api,owner)==SCO_OK);
 for(const auto* name:{SC_SPAWN_SERVICE_NAME,SC_ENTITIES_NAME,SC_SPATIAL_SERVICE_NAME}) {
  absent=name;CHECK(Ship()==SCO_UNAVAILABLE && !spawned);null_success=true;CHECK(Ship()==SCO_UNAVAILABLE && !spawned);null_success=false;absent=nullptr;
 }
 absent=SC_ENTITIES_NAME;CHECK(Run("spawn.status")==SCO_UNAVAILABLE);Contains("ownership unchanged");absent=nullptr;
 for(const char* cap:{"spawn.ship","game.entities.spawn"}) {missing_cap=cap;CHECK(Ship()==SCO_UNAVAILABLE && !spawned);}missing_cap=nullptr;
 classes.class_exists=nullptr;CHECK(Ship()==SCO_UNAVAILABLE);classes.class_exists=ClassExists;
 entities.spawn=nullptr;CHECK(Ship()==SCO_UNAVAILABLE);entities.spawn=Spawn;
 entities.despawn=nullptr;CHECK(Ship()==SCO_UNAVAILABLE);entities.despawn=Remove;
 spatial.player_pose=nullptr;CHECK(Ship()==SCO_UNAVAILABLE);spatial.player_pose=Pose;
 struct Tail {const char* name;const void* table;size_t extent;};
 for(const auto t:{Tail{SC_SPAWN_SERVICE_NAME,&classes,offsetof(sc_spawn_service_v1,class_exists)+sizeof(classes.class_exists)},
   Tail{SC_ENTITIES_NAME,&entities,offsetof(sc_entities_v1,spawn)+sizeof(entities.spawn)},
   Tail{SC_ENTITIES_NAME,&entities,offsetof(sc_entities_v1,despawn)+sizeof(entities.despawn)},
   Tail{SC_SPATIAL_SERVICE_NAME,&spatial,offsetof(sc_spatial_v1,player_pose)+sizeof(spatial.player_pose)}}) {
  // Allocation actually ends inside the callback pointer; ASan checks overreads.
  const auto bytes=static_cast<uint32_t>(t.extent-1);void* memory=std::malloc(bytes);CHECK(memory);
  std::memcpy(memory,t.table,bytes);std::memcpy(memory,&bytes,sizeof(bytes));override_name=t.name;override_table=memory;
  CHECK(Ship()==SCO_UNAVAILABLE && !spawned);override_name=nullptr;std::free(memory);
 }
 for(const char* name:{SC_ACTORS_NAME,SC_VEHICLES_SERVICE_NAME}){absent=name;CHECK(Options()==SCO_UNAVAILABLE && !spawned);}absent=nullptr;
 for(const auto t:{Tail{SC_ACTORS_NAME,&actors,offsetof(sc_actors_v1,local_player)+sizeof(actors.local_player)},
   Tail{SC_VEHICLES_SERVICE_NAME,&vehicles,offsetof(sc_vehicles_v1,seats)+sizeof(vehicles.seats)},
   Tail{SC_VEHICLES_SERVICE_NAME,&vehicles,offsetof(sc_vehicles_v1,seat_occupant)+sizeof(vehicles.seat_occupant)},
   Tail{SC_VEHICLES_SERVICE_NAME,&vehicles,offsetof(sc_vehicles_v1,seat)+sizeof(vehicles.seat)},
   Tail{SC_VEHICLES_SERVICE_NAME,&vehicles,offsetof(sc_vehicles_v1,power_on)+sizeof(vehicles.power_on)},
   Tail{SC_VEHICLES_SERVICE_NAME,&vehicles,offsetof(sc_vehicles_v1,player_ship)+sizeof(vehicles.player_ship)}}) {
  const auto bytes=static_cast<uint32_t>(t.extent-1);void* memory=std::malloc(bytes);CHECK(memory);
  std::memcpy(memory,t.table,bytes);std::memcpy(memory,&bytes,sizeof(bytes));override_name=t.name;override_table=memory;
  CHECK(Options()==SCO_UNAVAILABLE && !spawned);override_name=nullptr;std::free(memory);
 }
 actors.local_player=nullptr;CHECK(Options()==SCO_UNAVAILABLE);actors.local_player=Player;
 vehicles.seats=nullptr;CHECK(Options()==SCO_UNAVAILABLE);vehicles.seats=Seats;
 vehicles.seat_occupant=nullptr;CHECK(Options()==SCO_UNAVAILABLE);vehicles.seat_occupant=Occupant;
 vehicles.seat=nullptr;CHECK(Options()==SCO_UNAVAILABLE);vehicles.seat=Seat;
 vehicles.power_on=nullptr;CHECK(Options()==SCO_UNAVAILABLE);vehicles.power_on=Power;
 vehicles.player_ship=nullptr;CHECK(Options()==SCO_UNAVAILABLE);vehicles.player_ship=PlayerShip;
 CHECK(Options()==SCO_OK);visible=true;
 reopen_name=SC_VEHICLES_SERVICE_NAME;reopen_at=2;Advance(1);CHECK(!seated);reopen_name=nullptr;Advance(2);CHECK(seated==1);
 entities.alive=nullptr;Advance(3);CHECK(seated==1);entities.alive=Alive;
 const uint32_t short_size=offsetof(sc_entities_v1,alive)+sizeof(entities.alive)-1;
 void* short_ent=std::malloc(short_size);CHECK(short_ent);std::memcpy(short_ent,&entities,short_size);std::memcpy(short_ent,&short_size,sizeof(short_size));
 override_name=SC_ENTITIES_NAME;override_table=short_ent;Advance(3);CHECK(seated==1);override_name=nullptr;std::free(short_ent);
 absent=SC_ENTITIES_NAME;CHECK(Id("spawn.cancel",101)==SCO_UNAVAILABLE && !removed);Advance(3);CHECK(seated==1);
 absent=nullptr;Advance(4);CHECK(removed==1);test_module::Unload();CHECK(removed==1);
}
void SeatRefusals(){
 for(int mode=0;mode<7;++mode){Reset();auto api=Api();CHECK(test_module::Load(&api,owner)==SCO_OK);CHECK(Options()==SCO_OK);visible=true;
  if(mode==0)occupant=9001;if(mode==1)seat_more=1;if(mode==2)seat_count=65;
  if(mode==3)seat_flags=SC_SEAT_PILOT;if(mode==4)player_present=false;if(mode==5)missing_cap="game.vehicles.seat";if(mode==6)missing_cap="game.entities.transform";
  Advance(10);CHECK(!seated && !powered);Advance(60010);CHECK(removed==1 && issued.empty());
  test_module::Unload(); // The borrowed host API must outlive unload.
 }
}
void HostAndCommandNegatives(){Reset();auto api=Api();
 CHECK(test_module::Load(nullptr,owner)==SCO_BAD_ARG);CHECK(test_module::Load(&api,nullptr)==SCO_BAD_ARG);
 for(uint32_t n=0;n<offsetof(sco_api,query_service)+sizeof(api.query_service);++n){api.size=n;CHECK(test_module::Load(&api,owner)==SCO_UNAVAILABLE);}
 api=Api();api.query_service=nullptr;CHECK(test_module::Load(&api,owner)==SCO_UNAVAILABLE);
 api=Api();api.subscribe=nullptr;CHECK(test_module::Load(&api,owner)==SCO_UNAVAILABLE);
 api=Api();api.has=nullptr;CHECK(test_module::Load(&api,owner)==SCO_UNAVAILABLE);
 api=Api();api.log=nullptr;CHECK(test_module::Load(&api,owner)==SCO_UNAVAILABLE);
 api=Api();api.register_command=nullptr;CHECK(test_module::Load(&api,owner)==SCO_UNAVAILABLE);
 api=Api();api.major=2;CHECK(test_module::Load(&api,owner)==SCO_UNAVAILABLE);api=Api();api.minor=0;CHECK(test_module::Load(&api,owner)==SCO_UNAVAILABLE);
 for(int i=0;i<8;++i){Reset();api=Api();fail_registration=i;CHECK(test_module::Load(&api,owner)==SCO_FAILED);test_module::Unload();}
 Reset();api=Api();subscribe_result=SCO_FAILED;CHECK(test_module::Load(&api,owner)==SCO_FAILED);test_module::Unload();
 Reset();api=Api();CHECK(test_module::Load(&api,owner)==SCO_OK);CHECK(test_module::Load(&api,owner)==SCO_BAD_ARG);
 CHECK(Run("spawn.ship")==SCO_BAD_ARG);CHECK(Id("spawn.cancel",0)==SCO_BAD_ARG);
 sco_arg a[2]{};a[0].type=SCO_ARG_STRING;a[0].v.s="synthetic_ship";a[1].type=SCO_ARG_FLOAT;
 for(double bad:{-1.0,10001.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){a[1].v.f=bad;CHECK(Run("spawn.ship",a,2)==SCO_BAD_ARG);}
 a[1].v.f=30;a[0].v.s="";CHECK(Run("spawn.ship",a,2)==SCO_BAD_ARG);a[0].v.s=nullptr;CHECK(Run("spawn.ship",a,2)==SCO_BAD_ARG);
 a[0].v.s="synthetic_ship";auto command=commands.at("spawn.ship");CHECK(command.fn(a,2,command.ctx,nullptr,1)==SCO_BAD_ARG);CHECK(!spawned);
 next_id=0x8000000000000065ULL;CHECK(Ship()==SCO_OK);CHECK(Id("spawn.remove",0x8000000000000065ULL)==SCO_OK);CHECK(removed==1);
 test_module::Unload();test_module::Unload();
}
void BoundedOwnership(){Reset();auto api=Api();CHECK(test_module::Load(&api,owner)==SCO_OK);
 for(int i=0;i<64;++i)CHECK(Ship()==SCO_OK);CHECK(Ship()==SCO_TOO_MANY && spawned==64);
 CHECK(Run("spawn.clear")==SCO_OK && removed==64 && issued.empty());CHECK(Ship()==SCO_TOO_MANY); // Removal tickets retained; no alive-based recycling.
 Advance(1);CHECK(Run("spawn.status")==SCO_OK);Contains("removing=64");test_module::Unload();CHECK(removed==64);
}
void TimeoutCancel(){Reset();auto api=Api();CHECK(test_module::Load(&api,owner)==SCO_OK);CHECK(Options()==SCO_OK);
 Advance(0xfffffff0u);Advance(0x0000ea50u);CHECK(removed==1 && issued.empty()); // uint32 wrap, 60000 ms.
 CHECK(Run("spawn.status")==SCO_OK);Contains("removing=1");visible=true;Advance(0x0000ea60u);CHECK(!seated);
 Reset();api=Api();CHECK(test_module::Load(&api,owner)==SCO_OK);CHECK(Options()==SCO_OK);
 remove_result=SCO_WRONG_THREAD;CHECK(Id("spawn.cancel",101)==SCO_WRONG_THREAD);CHECK(issued.count(101));
 visible=true;Advance(5);CHECK(seated==0 && powered==0);remove_result=SCO_OK;Advance(1005);CHECK(issued.empty());
 const int attempts=removed;test_module::Unload();test_module::Unload();CHECK(removed==attempts);
}
sco_result Board(const char* seat,bool power,uint64_t id=101){sco_arg a[3]{};a[0].type=SCO_ARG_INT;a[0].v.i=std::bit_cast<int64_t>(id);a[1].type=SCO_ARG_STRING;a[1].v.s=seat;a[2].type=SCO_ARG_BOOL;a[2].v.i=power;return Run("spawn.board",a,3);}
void BoardLaterAndStartup(){Reset();auto api=Api();CHECK(test_module::Load(&api,owner)==SCO_OK);
 CHECK(commands.count("spawn.board")==1 && commands.count("spawn.startup")==1);
 CHECK(Run("spawn.startup")==SCO_UNAVAILABLE);Contains("G2/G3");CHECK(spawned==0);
 CHECK(Options("none",true)==SCO_BAD_ARG && spawned==0);CHECK(Options("none",false)==SCO_OK);
 CHECK(Board("pilot",false,9001)==SCO_NOT_FOUND && seated==0);CHECK(Board("synthetic copilot",false)==SCO_OK);
 visible=true;Advance(1);CHECK(seated==1 && powered==0);linked=true;Advance(2);CHECK(Run("spawn.status")==SCO_OK);Contains("ready=1");
 test_module::Unload();CHECK(removed==1);
}
void Boarding(){Reset();auto api=Api();CHECK(test_module::Load(&api,owner)==SCO_OK);
 CHECK(commands.count("spawn.ship_options")==1);CHECK(Options()==SCO_OK);Advance(100);CHECK(seated==0 && powered==0);
 visible=true;Advance(200);CHECK(seated==1 && powered==0);CHECK(Run("spawn.status")==SCO_OK);Contains("boarding=1");
 Advance(250);CHECK(seated==1); // Accepted asynchronous link must not be reset every frame.
 linked=true;power_result=SCO_FAILED;Advance(300);CHECK(powered==1);power_result=SCO_OK;Advance(400);CHECK(powered==1);Advance(1300);CHECK(powered==2);
 CHECK(Run("spawn.status")==SCO_OK);Contains("ready=1");Advance(1400);CHECK(powered==2 && seated==1);
 test_module::Unload();CHECK(removed==1);
}
void Lifecycle(){Reset();auto api=Api();CHECK(test_module::Load(&api,owner)==SCO_OK);CHECK(!std::strcmp(test_module::Query()->name,"spawn"));
 CHECK(definitions.at("spawn.ship").size()==2);CHECK(!std::strcmp(definitions.at("spawn.ship")[0].name,"class"));CHECK(definitions.at("spawn.ship")[1].type==SCO_ARG_FLOAT);
 CHECK(Ship("invalid")==SCO_NOT_FOUND && spawned==0);CHECK(Ship()==SCO_OK && spawned==1);Contains("101");
 CHECK(Run("spawn.status")==SCO_OK);Contains("pending=1");visible=true;uint32_t now=1;CHECK(tick);tick("tick",&now,tick_ctx);
 CHECK(Run("spawn.status")==SCO_OK);Contains("ready=1");
 CHECK(Id("spawn.remove",9001)==SCO_NOT_FOUND && removed==0);CHECK(Id("spawn.remove",101)==SCO_OK && removed==1);
 visible=false;CHECK(Run("spawn.status")==SCO_OK);Contains("removing=1");CHECK(Id("spawn.remove",101)==SCO_NOT_FOUND);
 CHECK(Ship()==SCO_OK);test_module::Unload();test_module::Unload();CHECK(removed==2);CHECK(Run("spawn.status")==SCO_UNAVAILABLE);
 // Host deregistration/release is a stand-in, not real host guard evidence.
 commands.clear();tick=nullptr;test_module::Close();
}
void RefusedCleanupReload(){Reset();auto api=Api();api.log=CleanupLog;api.unsubscribe=Unsubscribe;cleanup_warnings=unsubscriptions=0;
 CHECK(test_module::Load(&api,owner)==SCO_OK);CHECK(Ship()==SCO_OK);remove_result=SCO_WRONG_THREAD;
 test_module::Unload();CHECK(unsubscriptions==1 && cleanup_warnings==1 && removed==1 && issued.count(101));
 test_module::Unload();CHECK(unsubscriptions==1 && cleanup_warnings==1 && removed==1);
 remove_result=SCO_OK;CHECK(test_module::Load(&api,owner)==SCO_OK);CHECK(Run("spawn.status")==SCO_OK);Contains("pending=0");
 CHECK(Id("spawn.remove",101)==SCO_NOT_FOUND && removed==1);test_module::Unload();CHECK(unsubscriptions==2 && removed==1);
}
}
int main(int argc,char** argv){test_module::Open(argc,argv);ProviderNegatives();HostAndCommandNegatives();BoundedOwnership();SeatRefusals();BoardLaterAndStartup();TimeoutCancel();Boarding();RefusedCleanupReload();Lifecycle();test_module::Open(argc,argv);Reset();auto api=Api();CHECK(test_module::Load(&api,owner)==SCO_OK);CHECK(Ship()==SCO_OK);test_module::Unload();CHECK(removed==1);test_module::Close();std::puts("spawn dynamic workflow: owned lifecycle, delayed board/power, timeout/cancel, providers/tails/reopen, bounds, unload/reload PASS (synthetic, not game)");}
