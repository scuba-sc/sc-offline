// SPDX-License-Identifier: GPL-3.0-only
#include <sco/host.h>
#include <sco/runtime.h>
#include <sco/storage.h>
#include <sco_storage.h>
#include <sco/widgets.h>
#include "game/services/inventory.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <chrono>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace W=sco::widgets;
namespace S=sco::game::services;
static unsigned checks;
static const sco_api* host_api=nullptr;
static int fail_storage_query_after=0;
static sco_result Query(const char* name,uint32_t version,const void** out) {
    if(!std::strcmp(name,SCO_STORAGE_NAME) && fail_storage_query_after && --fail_storage_query_after==0) {
        *out=nullptr;return SCO_UNAVAILABLE;
    }
    return host_api->query_service(name,version,out);
}
static void Check(bool ok) { ++checks; if(!ok) { std::fprintf(stderr,"workflow failure %u\n",checks); std::exit(1); } }
template<size_t N,size_t M> static void Copy(char (&destination)[N],const char (&source)[M]) {
    static_assert(M<=N); std::memcpy(destination,source,M);
}
struct World { bool admitted=true; uint32_t category=22, storage=100; uint64_t generation=1; } world;
static bool Admitted(void* p) { return static_cast<World*>(p)->admitted; }
static sco_result Read(void* p,sc_inventory_layout_snapshot* out,sc_inventory_layout_entry* rows,uint32_t capacity) {
    auto& w=*static_cast<World*>(p); if(capacity!=128) return SCO_BAD_ARG;
    out->count=out->total_seen=2; out->flags=1; out->source_epoch=1; out->observation_generation=w.generation;
    Copy(out->identity_domain,"dev.gear.v1"); Copy(out->layout_key,"human.v1");
    rows[0].role=1; rows[0].state=2; rows[0].flags=3; rows[0].raw_item_type=w.category; rows[0].entity_id=999;
    Copy(rows[0].port_key,"helmet");
    rows[1].role=2; rows[1].state=2; rows[1].flags=1; rows[1].raw_item_type=w.storage;
    Copy(rows[1].port_key,"pack"); return SCO_OK;
}
struct Renderer:W::Renderer {
    W::Event event;
    void Render(const W::Panel&,std::vector<W::Event>& events) override { events.push_back(event); }
};
static void Event(uint32_t id,uint32_t kind,int index=0,const char* text="") {
    Renderer r; r.event.node_id=id; r.event.kind=kind; r.event.index=index; r.event.text=text;
    Check(W::RenderPanel("gear_presets.main",r)==sco::Result::Ok);
}
static bool Has(const char* part) {
    const auto panels=W::Panels(); if(panels.size()!=1) return false;
    for(const auto& node:panels[0].nodes) if(node.label.find(part)!=std::string::npos) return true;
    return false;
}
static size_t Choices() {
    for(const auto& panel:W::Panels()) for(const auto& node:panel.nodes) if(node.id==5) return node.items.size();
    return 0;
}
static sco_result last_result=SCO_FAILED;
static void Done(sco_result result,const char* text,void* context) {
    last_result=result;
    auto* reply=static_cast<std::string*>(context);
    if(result==SCO_OK && text) *reply=text;
}
int main(int argc,char** argv) {
    Check(argc==3);
    using LoadFn=sco_result (*)(const sco_api*,sco_plugin*);
    using UnloadFn=void (*)();
#ifdef _WIN32
    HMODULE module=LoadLibraryA(argv[2]); Check(module!=nullptr);
    const auto rawLoad=GetProcAddress(module,"sco_plugin_load"), rawUnload=GetProcAddress(module,"sco_plugin_unload");
    LoadFn load=nullptr; UnloadFn unload=nullptr;
    static_assert(sizeof(load)==sizeof(rawLoad) && sizeof(unload)==sizeof(rawUnload));
    std::memcpy(&load,&rawLoad,sizeof(load)); std::memcpy(&unload,&rawUnload,sizeof(unload));
#else
    void* module=dlopen(argv[2],RTLD_NOW|RTLD_LOCAL); Check(module!=nullptr);
    auto load=reinterpret_cast<LoadFn>(dlsym(module,"sco_plugin_load"));
    auto unload=reinterpret_cast<UnloadFn>(dlsym(module,"sco_plugin_unload"));
#endif
    Check(load && unload);
    sco::SetGameThread(); const auto* api=sco::host::BuildApi({"preset workflow fixture"});
    host_api=api; sco_api wrapped=*api; wrapped.query_service=Query;
    sco::storage::Options options; options.dataRoot=std::filesystem::path(argv[1]) /
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    Check(sco::storage::Start(options)==sco::Result::Ok); Check(W::Start()==sco::Result::Ok);
    Check(S::StartGameInventoryWithLayout({}, {&world,true,Admitted,Read})==sco::Result::Ok);
    auto* owner=sco::host::NewPlugin("gear_presets"); Check(owner && load(&wrapped,owner)==SCO_OK);
    Check(Has("Capture a complete layout") && Choices()==0);
    Event(1,SCO_WIDGET_TEXT_INPUT,0,"Helmet kit"); Event(2,SCO_WIDGET_BUTTON); Check(Has("Captured complete layout"));
    Event(3,SCO_WIDGET_BUTTON); Check(Has("Preset saved") && Choices()==1);
    std::string reply; Check(api->invoke(owner,"gear_presets.list",nullptr,0,Done,&reply)==SCO_OK && reply.find("1")!=std::string::npos);
    Event(5,SCO_WIDGET_COMBO,0); Event(6,SCO_WIDGET_BUTTON); Check(Has("same category; exact class unknown"));
    world.category=23; world.storage=101; ++world.generation;
    Event(6,SCO_WIDGET_BUTTON); Check(Has("different category") && !Has("pack:"));
    world.admitted=false; Event(2,SCO_WIDGET_BUTTON); Check(Has("unavailable or incomplete"));
    Event(3,SCO_WIDGET_BUTTON); Check(Choices()==1); // disabled save never persists stale capture
    world.admitted=true;
    Event(2,SCO_WIDGET_BUTTON); Check(Has("Captured complete layout"));
    sco_arg arg{}; arg.type=SCO_ARG_STRING; arg.v.s="Second kit";
    fail_storage_query_after=2; reply.clear();
    Check(api->invoke(owner,"gear_presets.save",&arg,1,Done,&reply)==SCO_OK);
    Check(reply.find("saved, but the picker could not refresh")!=std::string::npos && Choices()==1);
    reply.clear(); Check(api->invoke(owner,"gear_presets.list",nullptr,0,Done,&reply)==SCO_OK && reply.find(" 2")!=std::string::npos && Choices()==2);
    Event(3,SCO_WIDGET_BUTTON); Check(Choices()==2); // a committed capture cannot be saved twice
    last_result=SCO_OK; Check(api->invoke(owner,"gear_presets.save",&arg,1,Done,&reply)==SCO_UNAVAILABLE && last_result==SCO_UNAVAILABLE && Choices()==2);
    unload(); Check(W::Panels().empty()); Check(sco::Release(owner)==sco::Result::Ok);
    owner=sco::host::NewPlugin("gear_presets"); Check(owner && load(&wrapped,owner)==SCO_OK && Choices()==2);
    Event(5,SCO_WIDGET_COMBO,0); Event(6,SCO_WIDGET_BUTTON); Check(Has("different category"));
    S::StopGameInventory();
    static sc_inventory_v1 old{sizeof(old),0,nullptr};
    Check(sco::host::ProvideGameService(SC_INVENTORY_SERVICE_NAME,SC_INVENTORY_SERVICE_VERSION_1_1,&old)==sco::Result::Ok);
    Event(2,SCO_WIDGET_BUTTON); Check(Has("unavailable or incomplete") && Choices()==2);
    unload(); Check(sco::Release(owner)==sco::Result::Ok);
    Check(sco::host::WithdrawGameService(SC_INVENTORY_SERVICE_NAME)==sco::Result::Ok);
    W::Stop(); sco::storage::Stop();
#ifdef _WIN32
    Check(FreeLibrary(module)!=0);
#else
    Check(dlclose(module)==0);
#endif
    std::printf("real host preset workflow: %u checks, 0 failed\n",checks);
}
