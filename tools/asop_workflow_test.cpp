// SPDX-License-Identifier: GPL-3.0-only
#include "workflow_module.h"
#include <sco_content.h>
#include <sco_storage.h>
#include <sco_widgets.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <map>
#include <string>
namespace {
std::string bytes;bool saved=false,storage=true,fail=false;
sco_widget_event_fn event=nullptr;void* eventContext=nullptr;
sco_raw_fn snapshot=nullptr;void* rawContext=nullptr;
sco_command saveCommand{};std::map<uint32_t,double> values;
sco_result Item(sco_plugin*,sco_content_kind kind,uint32_t index,sco_content_item* out){
    if(kind!=SCO_CONTENT_LIST||index)return SCO_NOT_FOUND;
    out->id=1;std::snprintf(out->name,sizeof(out->name),"lists/ships.txt");std::snprintf(out->pack,sizeof(out->pack),"synthetic");return SCO_OK;
}
sco_result Read(sco_plugin*,uint64_t,void* out,uint32_t* n){const std::string catalog="Test_Ship_A\nTest_Ship_B\n";if(!out||*n<catalog.size()){*n=static_cast<uint32_t>(catalog.size());return SCO_TOO_MANY;}std::memcpy(out,catalog.data(),catalog.size());*n=static_cast<uint32_t>(catalog.size());return SCO_OK;}
sco_result Put(sco_plugin*,const char*,const void* in,uint32_t n){if(fail)return SCO_FAILED;bytes.assign(static_cast<const char*>(in),n);saved=true;return SCO_OK;}
sco_result Get(sco_plugin*,const char*,void* out,uint32_t* n){if(!saved){*n=0;return SCO_NOT_FOUND;}if(*n<bytes.size()){*n=static_cast<uint32_t>(bytes.size());return SCO_TOO_MANY;}if(!bytes.empty())std::memcpy(out,bytes.data(),bytes.size());*n=static_cast<uint32_t>(bytes.size());return SCO_OK;}
sco_result Panel(sco_plugin*,const char*,const char*,const sco_widget_node* nodes,uint32_t n,uint32_t stride,sco_widget_event_fn fn,void* ctx){event=fn;eventContext=ctx;values.clear();for(uint32_t i=0;i<n;++i){const auto* p=reinterpret_cast<const sco_widget_node*>(reinterpret_cast<const char*>(nodes)+i*stride);values[p->id]=p->value;}return SCO_OK;}
sco_content_v1 content{sizeof(content),0,Item,Read};sco_storage_v1 store{};sco_widgets_v1 widgets{sizeof(widgets),0,Panel,nullptr};
sco_result Query(const char* name,uint32_t,const void** out){*out=nullptr;if(!std::strcmp(name,SCO_CONTENT_NAME))*out=&content;else if(!std::strcmp(name,SCO_STORAGE_NAME)&&storage)*out=&store;else if(!std::strcmp(name,SCO_WIDGETS_NAME))*out=&widgets;return *out?SCO_OK:SCO_NOT_FOUND;}
sco_result Register(sco_plugin*,const sco_command* c){saveCommand=*c;return SCO_OK;}
sco_result Raw(sco_plugin*,const char*,const char*,sco_raw_fn fn,void* ctx){snapshot=fn;rawContext=ctx;return SCO_OK;}
void Toggle(uint32_t id,double v){sco_widget_event e{sizeof(e),id,SCO_WIDGET_TOGGLE,-1,v,nullptr};event("asop_selector.main",&e,eventContext);}
sco_result Snapshot(std::string& out){uint32_t n=0;auto r=snapshot(nullptr,0,nullptr,&n,rawContext);if(r!=SCO_OK&&r!=SCO_TOO_MANY)return r;out.resize(n);return snapshot(nullptr,0,n?out.data():nullptr,&n,rawContext);}
}
int main(int argc,char** argv){
    test_module::Open(argc,argv);store.size=sizeof(store);store.put=Put;store.get=Get;
    sco_api api{};api.size=sizeof(api);api.major=SCO_API_MAJOR;api.minor=SCO_API_MINOR;api.query_service=Query;api.register_command=Register;api.register_raw=Raw;
    auto* owner=reinterpret_cast<sco_plugin*>(uintptr_t{1});char reply[256]{};std::string out;
    assert(test_module::Load(&api,owner)==SCO_OK);assert(values[100]==1&&values[101]==1);assert(Snapshot(out)==SCO_NOT_FOUND);
    Toggle(101,0);assert(values[101]==0);assert(saveCommand.fn(nullptr,0,saveCommand.ctx,reply,sizeof(reply))==SCO_OK);
    assert(Snapshot(out)==SCO_OK&&out=="Test_Ship_A\n");test_module::Unload();
    assert(test_module::Load(&api,owner)==SCO_OK&&values[100]==1&&values[101]==0);
    fail=true;Toggle(100,0);assert(saveCommand.fn(nullptr,0,saveCommand.ctx,reply,sizeof(reply))==SCO_FAILED);assert(Snapshot(out)==SCO_OK&&out=="Test_Ship_A\n");
    fail=false;assert(saveCommand.fn(nullptr,0,saveCommand.ctx,reply,sizeof(reply))==SCO_OK);assert(Snapshot(out)==SCO_OK&&out.empty());test_module::Unload();
    bytes="../bad\n";assert(test_module::Load(&api,owner)==SCO_OK);assert(Snapshot(out)==SCO_UNAVAILABLE);test_module::Unload();
    storage=false;assert(test_module::Load(&api,owner)==SCO_OK);assert(saveCommand.fn(nullptr,0,saveCommand.ctx,reply,sizeof(reply))==SCO_UNAVAILABLE);test_module::Unload();test_module::Close();
    std::puts("ASOP_SELECTOR_PASS persistence, subset, empty selection, failed save, invalid data, absent storage");
}
