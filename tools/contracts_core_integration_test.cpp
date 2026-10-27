// SPDX-License-Identifier: GPL-3.0-only
// Actual contracts module and Core's missions/economy services with a synthetic backend.
#include "workflow_module.h"
#include "sco/host.h"
#include "sco/runtime.h"
#include "game/services/offline_missions.h"
#include <sc_economy.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
namespace M=sco::game::offline;
struct Fake {
 M::Admission admission{1,true,true,false}; uint32_t state=SC_MISSION_ACTIVE, objective=0;
 int64_t balance=50; unsigned accepts=0,advances=0,payments=0,abandons=0; bool uncertain=false;
 static M::Admission Admit(void* p){return static_cast<Fake*>(p)->admission;}
 static sco_result Offer(void*,uint32_t i,sc_mission_offer* out){if(i)return SCO_NOT_FOUND;*out={sizeof(*out),0,10,20,1,1,999,1};return SCO_OK;}
 static sco_result Status(void* p,uint64_t id,sc_mission_status* out){auto& f=*static_cast<Fake*>(p);if(id!=29+f.accepts)return SCO_NOT_FOUND;*out={sizeof(*out),f.state,id,10,0,f.objective,1,1,f.admission.session};return SCO_OK;}
 static sco_result Accept(void* p,const sc_mission_offer*,uint64_t* id){*id=29+(++static_cast<Fake*>(p)->accepts);return SCO_OK;}
 static sco_result Objective(void* p,uint64_t,uint32_t,uint32_t){++static_cast<Fake*>(p)->objective;return SCO_OK;}
 static sco_result Advance(void* p,uint64_t,uint32_t){auto& f=*static_cast<Fake*>(p);++f.advances;f.state=SC_MISSION_COMPLETED;return SCO_OK;}
 static sco_result Abandon(void* p,uint64_t){++static_cast<Fake*>(p)->abandons;return SCO_OK;}
 static sco_result Balance(void* p,int64_t* out){*out=static_cast<Fake*>(p)->balance;return SCO_OK;}
 static sco_result Apply(void* p,int64_t expected,int64_t delta,int64_t* out){auto& f=*static_cast<Fake*>(p);++f.payments;if(f.balance!=expected)return SCO_FAILED;f.balance+=delta;*out=f.balance;return f.uncertain?SCO_FAILED:SCO_OK;}
};
static sco_arg Int(int64_t value){sco_arg a{};a.type=SCO_ARG_INT;a.v.i=value;return a;}
int main(int argc,char** argv){
 test_module::Open(argc,argv);sco::SetGameThread();const auto* api=sco::host::BuildApi({"contracts integration fixture"});
 auto* owner=sco::host::NewPlugin("contracts");auto* foreign=sco::host::NewPlugin("contracts_foreign");CHECK(api && owner && foreign && test_module::Load(api,owner)==SCO_OK);
 CHECK(api->invoke(owner,"contracts.status",nullptr,0,nullptr,nullptr)==SCO_UNAVAILABLE);
 Fake f; M::Backend b{&f,Fake::Admit,Fake::Offer,Fake::Status,Fake::Accept,Fake::Objective,Fake::Advance,Fake::Abandon,nullptr,Fake::Balance,Fake::Apply};CHECK(M::Start(b)==sco::Result::Ok);
 sco_arg accept[]{Int(0),Int(7)};CHECK(api->invoke(owner,"contracts.accept",accept,2,nullptr,nullptr)==SCO_OK && f.accepts==1);
 CHECK(api->invoke(owner,"contracts.accept",accept,2,nullptr,nullptr)==SCO_OK && f.accepts==1);
 sco_arg obj[]{Int(30),Int(0),Int(0)}, step[]{Int(30),Int(0)};
 const void* raw=nullptr;CHECK(api->query_service(SC_MISSIONS_SERVICE_NAME,SC_MISSIONS_SERVICE_VERSION,&raw)==SCO_OK);
 const auto* missions=static_cast<const sc_missions_v1*>(raw);CHECK(missions->objective(foreign,30,0,0)==SCO_NOT_FOUND);
 CHECK(api->invoke(owner,"contracts.objective",obj,3,nullptr,nullptr)==SCO_OK);
 CHECK(api->invoke(owner,"contracts.advance",step,2,nullptr,nullptr)==SCO_OK && f.advances==1 && f.payments==0 && f.balance==50);
 sco_arg payment[]{Int(100),Int(50),Int(20)};CHECK(api->invoke(owner,"contracts.apply",payment,3,nullptr,nullptr)==SCO_OK && f.payments==1 && f.balance==70);
 CHECK(api->invoke(owner,"contracts.apply",payment,3,nullptr,nullptr)==SCO_OK && f.payments==1);
 sco_arg receipt=Int(100);raw=nullptr;CHECK(api->query_service(SC_ECONOMY_SERVICE_NAME,SC_ECONOMY_SERVICE_VERSION,&raw)==SCO_OK);
 auto* economy=static_cast<const sc_economy_v1*>(raw);sc_economy_receipt value{};value.size=sizeof(value);CHECK(economy->receipt(foreign,100,&value)==SCO_NOT_FOUND);
 f.admission.offline=false;CHECK(api->invoke(owner,"contracts.balance",nullptr,0,nullptr,nullptr)==SCO_UNAVAILABLE);f.admission.offline=true;
 f.uncertain=true;sco_arg uncertain[]{Int(101),Int(70),Int(5)};CHECK(api->invoke(owner,"contracts.apply",uncertain,3,nullptr,nullptr)==SCO_FAILED && f.payments==2 && f.balance==75);
 CHECK(api->invoke(owner,"contracts.apply",uncertain,3,nullptr,nullptr)==SCO_FAILED && f.payments==2);
 sco_arg unknown=Int(101);CHECK(api->invoke(owner,"contracts.receipt",&unknown,1,nullptr,nullptr)==SCO_FAILED);
 CHECK(M::Stop()==sco::Result::Ok);CHECK(api->invoke(owner,"contracts.receipt",&receipt,1,nullptr,nullptr)==SCO_UNAVAILABLE);
 // A new provider session and active mission are cleaned on owner release, even
 // without a cooperative module unload. This is not durable settlement.
 f.admission.session=2;f.state=SC_MISSION_ACTIVE;f.objective=0;f.accepts=0;f.uncertain=false;
 CHECK(M::Start(b)==sco::Result::Ok);accept[1]=Int(8);
 CHECK(api->invoke(owner,"contracts.accept",accept,2,nullptr,nullptr)==SCO_OK);
 CHECK(sco::Release(owner)==sco::Result::Ok && f.abandons==1);
 test_module::Unload();CHECK(M::Stop()==sco::Result::Ok);CHECK(sco::Release(foreign)==sco::Result::Ok);test_module::Close();
 std::puts("actual contracts module + Core missions/economy + synthetic backend: PASS; no automatic or durable reward, not run in game");
}
