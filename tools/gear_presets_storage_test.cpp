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
static void Check(bool ok) { ++checks; if (!ok) { std::fprintf(stderr,"storage failure %u\n",checks); std::exit(1); } }
static const sco_api* real_api;
static const sco_storage_v1* real_storage;
static sco_storage_v1 fault_table;
static bool fail_counter, fail_commit;
static sco_result Put(sco_plugin* self,const char* key,const void* bytes,uint32_t size) {
    if (fail_counter && std::strcmp(key,"preset.v1.next_id")==0) return SCO_TOO_MANY;
    return real_storage->put(self,key,bytes,size);
}
static sco_result Commit(sco_plugin* self) { return fail_commit ? SCO_FAILED : real_storage->commit(self); }
static sco_result Query(const char* name,uint32_t version,const void** out) {
    if (std::strcmp(name,SCO_STORAGE_NAME)==0) { *out=&fault_table; return SCO_OK; }
    return real_api->query_service(name,version,out);
}
int main(int argc,char** argv) {
    Check(argc==2); sco::SetGameThread(); real_api=sco::host::BuildApi({"preset storage fixture"});
    sco::storage::Options opts; opts.dataRoot=std::filesystem::path(argv[1]) /
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    Check(sco::storage::Start(opts)==sco::Result::Ok);
    real_storage=sco::storage::Table(); fault_table=*real_storage; fault_table.put=Put; fault_table.commit=Commit;
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
    owner=sco::host::NewPlugin("gear_preset_test"); Check(owner && store.Open(real_api,owner)==SCO_OK);
    Check(store.List(list)==SCO_OK && list.size()==2 && store.Load(1,loaded)==SCO_OK && loaded.name=="First");
    Check(real_storage->put(owner,"preset.v1.next_id","1",1)==SCO_OK);
    id=999; Check(store.Save(preset,id)==SCO_FAILED && id==999);
    Check(real_storage->put(owner,"preset.v1.next_id","3",1)==SCO_OK);
    for (uint64_t expected=3; expected<=32; ++expected) Check(store.Save(preset,id)==SCO_OK && id==expected);
    id=999; Check(store.Save(preset,id)==SCO_TOO_MANY && id==999);
    Check(store.List(list)==SCO_OK && list.size()==32);
    Check(sco::Release(owner)==sco::Result::Ok && sco::Release(other)==sco::Result::Ok); sco::storage::Stop();
    std::printf("real SQLite preset store: %u checks, 0 failed\n",checks);
}
