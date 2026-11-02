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
#include <new>
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
static const sco_storage_v1* real_storage=nullptr;
static sco_storage_v1 wrapped_storage{};
static int fail_delete=0, fail_commit=0;
static sco_result Delete(sco_plugin* self,const char* key) {
    if(fail_delete==1) return SCO_FAILED;
    const auto r=real_storage->del(self,key);
    return fail_delete==2 && r==SCO_OK ? SCO_FAILED : r;
}
static sco_result Commit(sco_plugin* self) {
    if(fail_commit==1) return SCO_FAILED;
    const auto r=real_storage->commit(self);
    return fail_commit==2 && r==SCO_OK ? SCO_FAILED : r;
}
static const sco_widgets_v1* real_widgets=nullptr;
static sco_widgets_v1 wrapped_widgets{};
static bool fail_panel=false, throw_panel=false;
static sco_widget_event_fn last_callback=nullptr;
static void* last_context=nullptr;
static sco_result SetPanel(sco_plugin* self,const char* id,const char* title,const sco_widget_node* nodes,
                           uint32_t count,uint32_t stride,sco_widget_event_fn fn,void* ctx) {
    if(throw_panel) { throw_panel=false; throw std::bad_alloc{}; }
    if(fail_panel) { fail_panel=false; return SCO_FAILED; }
    const auto r=real_widgets->set_panel(self,id,title,nodes,count,stride,fn,ctx);
    if(r==SCO_OK) { last_callback=fn; last_context=ctx; }
    return r;
}
static sco_result Query(const char* name,uint32_t version,const void** out) {
    if(!std::strcmp(name,SCO_STORAGE_NAME) && fail_storage_query_after && --fail_storage_query_after==0) {
        *out=nullptr;return SCO_UNAVAILABLE;
    }
    if(!std::strcmp(name,SCO_STORAGE_NAME)) {
        const auto r=host_api->query_service(name,version,out);
        if(r==SCO_OK) { real_storage=static_cast<const sco_storage_v1*>(*out); wrapped_storage=*real_storage;
            wrapped_storage.del=Delete; wrapped_storage.commit=Commit; *out=&wrapped_storage; }
        return r;
    }
    if(!std::strcmp(name,SCO_WIDGETS_NAME)) {
        const auto r=host_api->query_service(name,version,out);
        if(r==SCO_OK) { real_widgets=static_cast<const sco_widgets_v1*>(*out); wrapped_widgets=*real_widgets;
            wrapped_widgets.set_panel=SetPanel; *out=&wrapped_widgets; }
        return r;
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
    // A post-save presentation exception must not replace the completed result.
    auto save_callback=last_callback; auto* save_context=last_context;
    throw_panel=true; Event(3,SCO_WIDGET_BUTTON); Check(Choices()==0);
    sco_widget_event no_op{}; no_op.size=sizeof(no_op); no_op.node_id=999; no_op.kind=SCO_WIDGET_BUTTON;
    save_callback("gear_presets.main",&no_op,save_context);
    Check(Has("Preset saved") && Choices()==1);
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
    // A callback from an older published panel must not delete a new selection.
    Event(5,SCO_WIDGET_COMBO,0); auto stale_callback=last_callback; auto* stale_context=last_context;
    fail_panel=true; Event(5,SCO_WIDGET_COMBO,1);
    sco_widget_event stale{}; stale.size=sizeof(stale); stale.node_id=9; stale.kind=SCO_WIDGET_BUTTON;
    stale_callback("gear_presets.main",&stale,stale_context); Check(Choices()==2);
    reply.clear(); Check(api->invoke(owner,"gear_presets.list",nullptr,0,Done,&reply)==SCO_OK && reply.find(" 1")!=std::string::npos && reply.find(" 2")!=std::string::npos);
    Event(5,SCO_WIDGET_COMBO,1);
    auto live_callback=last_callback; auto* live_context=last_context;
    throw_panel=true; Event(9,SCO_WIDGET_BUTTON); Check(Choices()==2);
    live_callback("gear_presets.main",&stale,live_context); // old published panel was not replaced
    reply.clear(); Check(api->invoke(owner,"gear_presets.list",nullptr,0,Done,&reply)==SCO_OK);
    Check(Choices()==1 && reply.find(" 1")!=std::string::npos && reply.find(" 2")==std::string::npos);
    sco_arg id{}; id.type=SCO_ARG_INT; id.v.i=0;
    Check(api->invoke(owner,"gear_presets.delete",&id,1,Done,&reply)==SCO_BAD_ARG);
    id.v.i=2; Check(api->invoke(owner,"gear_presets.delete",&id,1,Done,&reply)==SCO_NOT_FOUND);
    id.v.i=1; fail_storage_query_after=2; reply.clear();
    Check(api->invoke(owner,"gear_presets.delete",&id,1,Done,&reply)==SCO_OK);
    Check(reply.find("deleted, but the picker could not refresh")!=std::string::npos && Choices()==0);
    Event(2,SCO_WIDGET_BUTTON); Event(3,SCO_WIDGET_BUTTON); Check(Choices()==1);
    reply.clear(); Check(api->invoke(owner,"gear_presets.list",nullptr,0,Done,&reply)==SCO_OK && reply.find(" 3")!=std::string::npos);
    auto old_callback=last_callback; auto* old_context=last_context;
    unload(); Check(W::Panels().empty()); Check(sco::Release(owner)==sco::Result::Ok);
    owner=sco::host::NewPlugin("gear_presets"); Check(owner && load(&wrapped,owner)==SCO_OK && Choices()==1);
    Event(5,SCO_WIDGET_COMBO,0); Event(6,SCO_WIDGET_BUTTON); Check(Has("same category"));
    old_callback("gear_presets.main",&stale,old_context); Check(Choices()==1);
    id.v.i=3; fail_delete=1; Check(api->invoke(owner,"gear_presets.delete",&id,1,Done,&reply)==SCO_FAILED);
    Check(Has("outcome is unknown") && Choices()==1);
    fail_delete=2; Check(api->invoke(owner,"gear_presets.delete",&id,1,Done,&reply)==SCO_FAILED);
    Check(Has("outcome is unknown") && Choices()==0); fail_delete=0;
    Event(2,SCO_WIDGET_BUTTON); fail_commit=2;
    Check(api->invoke(owner,"gear_presets.save",&arg,1,Done,&reply)==SCO_FAILED);
    Check(Has("outcome is unknown")); fail_commit=0;
    reply.clear(); Check(api->invoke(owner,"gear_presets.list",nullptr,0,Done,&reply)==SCO_OK && reply.find(" 4")!=std::string::npos && Choices()==1);
    S::StopGameInventory();
    static sc_inventory_v1 old{sizeof(old),0,nullptr};
    Check(sco::host::ProvideGameService(SC_INVENTORY_SERVICE_NAME,SC_INVENTORY_SERVICE_VERSION_1_1,&old)==sco::Result::Ok);
    Event(2,SCO_WIDGET_BUTTON); Check(Has("unavailable or incomplete") && Choices()==1);
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
