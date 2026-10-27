// SPDX-License-Identifier: GPL-3.0-only
// Actual product quantum module + core service + synthetic backend; never a native adapter.
#include "workflow_module.h"
#include "sco/host.h"
#include "sco/runtime.h"
#include "game/services/quantum.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
namespace Q=sco::game::services;
struct Fake { Q::QuantumObservation observation{true,true,true,SC_QUANTUM_CHARGING,0,0}; bool enabled=false,held=false; int writes=0; };
bool Observe(void* ctx,Q::QuantumObservation* out){*out=static_cast<Fake*>(ctx)->observation;return true;}
bool Apply(void* ctx,bool enabled,bool held){auto& f=*static_cast<Fake*>(ctx); f.enabled=enabled;f.held=held;++f.writes;return true;}
int main(int argc,char** argv){
 test_module::Open(argc,argv); sco::SetGameThread(); const auto* api=sco::host::BuildApi({"quantum integration fixture"});
 CHECK(sco::AddReleaseHook(Q::ReleaseQuantumOwner)==sco::Result::Ok);
 CHECK(Q::StartQuantum()==sco::Result::Ok); auto* owner=sco::host::NewPlugin("quantum"); CHECK(owner && test_module::Load(api,owner)==SCO_OK);
 sco_arg on{};on.type=SCO_ARG_BOOL;on.v.i=1;
 CHECK(api->invoke(owner,"quantum.enable",&on,1,nullptr,nullptr)==SCO_UNAVAILABLE);
 test_module::Unload(); CHECK(sco::Release(owner)==sco::Result::Ok); Q::StopQuantum();
 Fake f; CHECK(Q::ConfigureProductionQuantum({&f,Observe,Apply,nullptr,0})==sco::Result::Ok);
 CHECK(Q::StartProductionQuantum(true)==sco::Result::Ok && api->has("game.quantum.control"));
 owner=sco::host::NewPlugin("quantum");CHECK(owner && test_module::Load(api,owner)==SCO_OK && f.writes==0);
 CHECK(api->invoke(owner,"quantum.enable",&on,1,nullptr,nullptr)==SCO_OK && f.enabled);
 sco_arg ms{};ms.type=SCO_ARG_INT;ms.v.i=1000;CHECK(api->invoke(owner,"quantum.hold",&ms,1,nullptr,nullptr)==SCO_OK && f.held);
 CHECK(api->invoke(owner,"quantum.release",nullptr,0,nullptr,nullptr)==SCO_OK && !f.held);
 CHECK(api->invoke(owner,"quantum.hold",&ms,1,nullptr,nullptr)==SCO_OK && f.held);
 f.observation.offline=false;Q::TickQuantum(100);CHECK(!f.enabled && !f.held);
 CHECK(api->invoke(owner,"quantum.enable",&on,1,nullptr,nullptr)!=SCO_OK);
 f.observation.offline=true;CHECK(api->invoke(owner,"quantum.enable",&on,1,nullptr,nullptr)==SCO_OK && f.enabled);
 CHECK(api->invoke(owner,"quantum.hold",&ms,1,nullptr,nullptr)==SCO_OK && f.held);
 test_module::Unload();CHECK(sco::Release(owner)==sco::Result::Ok && !f.enabled && !f.held);
 // Host owner release must also restore an abandoned lease without a cooperative unload.
 owner=sco::host::NewPlugin("quantum");CHECK(owner && test_module::Load(api,owner)==SCO_OK);
 CHECK(api->invoke(owner,"quantum.enable",&on,1,nullptr,nullptr)==SCO_OK);
 CHECK(api->invoke(owner,"quantum.hold",&ms,1,nullptr,nullptr)==SCO_OK && f.held);
 CHECK(sco::Release(owner)==sco::Result::Ok && !f.enabled && !f.held);
 test_module::Unload();
 Q::StopQuantum(); CHECK(sco::RemoveReleaseHook(Q::ReleaseQuantumOwner)==sco::Result::Ok);test_module::Close();
 std::puts("actual quantum module + core provider + synthetic backend: PASS; not run in game");
}
