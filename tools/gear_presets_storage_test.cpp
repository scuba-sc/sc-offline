// SPDX-License-Identifier: GPL-3.0-only
#include "store.h"
#include <sco/host.h>
#include <sco/runtime.h>
#include <sco/storage.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <chrono>
using namespace sco::features::gear_presets;
static unsigned checks;
static void CheckAt(bool ok, int line) { ++checks; if (!ok) { std::fprintf(stderr,"storage failure %u at line %d\n",checks,line); std::exit(1); } }
#define Check(ok) CheckAt((ok),__LINE__)
static const sco_api* real_api;
static const sco_storage_v1* real_storage;
static sco_storage_v1 fault_table;
static bool fail_counter;
static int fail_commit;
static int fail_delete;
static sco_result Put(sco_plugin* self,const char* key,const void* bytes,uint32_t size) {
    if (fail_counter && std::strcmp(key,"preset.v1.next_id")==0) return SCO_TOO_MANY;
    return real_storage->put(self,key,bytes,size);
}
static sco_result Delete(sco_plugin* self,const char* key) {
    if (fail_delete==1) return SCO_FAILED;
    const auto r=real_storage->del(self,key);
    return fail_delete==2 && r==SCO_OK ? SCO_FAILED : r;
}
static sco_result Commit(sco_plugin* self) {
    if(fail_commit==1) return SCO_FAILED;
    const auto r=real_storage->commit(self);
    return fail_commit==2 && r==SCO_OK ? SCO_FAILED : r;
}
static sco_result Query(const char* name,uint32_t version,const void** out) {
    if (std::strcmp(name,SCO_STORAGE_NAME)==0) { *out=&fault_table; return SCO_OK; }
    return real_api->query_service(name,version,out);
}
int main(int argc,char** argv) {
    Check(argc==2); sco::SetGameThread(); real_api=sco::host::BuildApi({"preset storage fixture"});
    sco::storage::Options opts; opts.dataRoot=std::filesystem::path(argv[1]) /
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    Check(sco::storage::Start(opts)==sco::Result::Ok);
    real_storage=sco::storage::Table(); fault_table=*real_storage; fault_table.put=Put; fault_table.commit=Commit; fault_table.del=Delete;
    sco_api api=*real_api; api.query_service=Query;
    auto* owner=sco::host::NewPlugin("gear_preset_test"); auto* other=sco::host::NewPlugin("gear_preset_other"); Check(owner && other);
    PresetStore store; Check(store.Open(&api,owner)==SCO_OK);
    Preset preset{"First","dev.gear.v1","human.v1",{{"helmet",1,2,1,22},{"pack",2,1,0,0}}};
    uint64_t id=0; Check(store.Save(preset,id)==SCO_OK && id==1);
    std::vector<SavedPreset> list; Check(store.List(list)==SCO_OK && list.size()==1 && list[0].id==1 && list[0].name=="First");
    Preset loaded; Check(store.Load(id,loaded)==SCO_OK && loaded.ports.size()==2 && loaded.ports[1].role==2);
    fail_counter=true; id=999; Check(store.Save(preset,id)==SCO_TOO_MANY && id==999); fail_counter=false;
    Check(store.List(list)==SCO_OK && list.size()==1);
    fail_commit=true; Check(store.Save(preset,id)==SCO_FAILED && id==999); fail_commit=false;
    Check(store.List(list)==SCO_OK && list.size()==1);
    preset.name="Second"; Check(store.Save(preset,id)==SCO_OK && id==2);
    PresetStore isolated; Check(isolated.Open(real_api,other)==SCO_OK && isolated.List(list)==SCO_OK && list.empty());
    Check(real_storage->put(owner,"preset.v1.item.3","bad",3)==SCO_OK);
    list={{99,"untouched"}}; Check(store.List(list)==SCO_FAILED && list.size()==1 && list[0].id==99);
    Check(real_storage->del(owner,"preset.v1.item.3")==SCO_OK);
    Check(sco::Release(owner)==sco::Result::Ok); Check(store.Load(1,loaded)==SCO_BAD_ARG);
    owner=sco::host::NewPlugin("gear_preset_test"); Check(owner && store.Open(&api,owner)==SCO_OK);
    Check(store.List(list)==SCO_OK && list.size()==2 && store.Load(1,loaded)==SCO_OK && loaded.name=="First");
    Check(real_storage->put(owner,"preset.v1.next_id","1",1)==SCO_OK);
    id=999; Check(store.Save(preset,id)==SCO_FAILED && id==999);
    Check(real_storage->put(owner,"preset.v1.next_id","3",1)==SCO_OK);
    for (uint64_t expected=3; expected<=32; ++expected) Check(store.Save(preset,id)==SCO_OK && id==expected);
    id=999; Check(store.Save(preset,id)==SCO_TOO_MANY && id==999);
    Check(store.List(list)==SCO_OK && list.size()==32);
    Check(real_storage->put(owner,"unrelated.key","keep",4)==SCO_OK);
    auto del= fault_table.del; fault_table.del=nullptr;
    Check(store.Delete(1)==SCO_UNAVAILABLE && store.Load(1,loaded)==SCO_OK); fault_table.del=del;
    Check(store.Delete(0)==SCO_BAD_ARG && store.Delete(99)==SCO_NOT_FOUND);
    Check(isolated.Delete(1)==SCO_NOT_FOUND && store.Load(1,loaded)==SCO_OK);
    uint64_t other_id=0; Check(isolated.Save(preset,other_id)==SCO_OK && other_id==1);
    fail_delete=1; Check(store.Delete(1)==SCO_FAILED && store.Load(1,loaded)==SCO_OK); fail_delete=0;
    Check(store.Delete(1)==SCO_OK && store.Load(1,loaded)==SCO_NOT_FOUND && isolated.Load(1,loaded)==SCO_OK);
    Check(store.Delete(1)==SCO_NOT_FOUND);
    char counter[20]{}; uint32_t counter_size=sizeof(counter);
    Check(real_storage->get(owner,"preset.v1.next_id",counter,&counter_size)==SCO_OK && counter_size==2 && !std::memcmp(counter,"33",2));
    Check(store.Save(preset,id)==SCO_OK && id==33 && store.List(list)==SCO_OK && list.size()==32);
    char untouched[8]{}; uint32_t size=sizeof(untouched);
    Check(real_storage->get(owner,"unrelated.key",untouched,&size)==SCO_OK && size==4 && !std::memcmp(untouched,"keep",4));
    fail_delete=2; Check(store.Delete(2)==SCO_FAILED); fail_delete=0;
    Check(store.Load(2,loaded)==SCO_NOT_FOUND && store.Save(preset,id)==SCO_OK && id==34);
    Check(store.List(list)==SCO_OK && list.size()==32);
    Check(store.Delete(3)==SCO_OK); id=999; fail_commit=2;
    Check(store.Save(preset,id)==SCO_FAILED && id==999); fail_commit=0;
    Check(store.Load(35,loaded)==SCO_OK && store.List(list)==SCO_OK && list.size()==32);
    Check(sco::Release(owner)==sco::Result::Ok && sco::Release(other)==sco::Result::Ok); sco::storage::Stop();
    std::printf("real SQLite preset store: %u checks, 0 failed\n",checks);
}
