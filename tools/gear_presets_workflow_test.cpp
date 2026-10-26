// SPDX-License-Identifier: GPL-3.0-only
#include <sco/host.h>
#include <sco/runtime.h>
#include <sco/storage.h>
#include <sco/widgets.h>
#include "game/services/inventory.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <chrono>
#include <string>
#include <dlfcn.h>
namespace W=sco::widgets;
namespace S=sco::game::services;
static unsigned checks;
static void Check(bool ok) { ++checks; if(!ok) { std::fprintf(stderr,"workflow failure %u\n",checks); std::exit(1); } }
struct World { bool admitted=true; uint32_t category=22, storage=100; uint64_t generation=1; } world;
static bool Admitted(void* p) { return static_cast<World*>(p)->admitted; }
static sco_result Read(void* p,sc_inventory_layout_snapshot* out,sc_inventory_layout_entry* rows,uint32_t capacity) {
    auto& w=*static_cast<World*>(p); if(capacity!=128) return SCO_BAD_ARG;
    out->count=out->total_seen=2; out->flags=1; out->source_epoch=1; out->observation_generation=w.generation;
    std::strcpy(out->identity_domain,"dev.gear.v1"); std::strcpy(out->layout_key,"human.v1");
    rows[0].role=1; rows[0].state=2; rows[0].flags=3; rows[0].raw_item_type=w.category; rows[0].entity_id=999;
    std::strcpy(rows[0].port_key,"helmet");
    rows[1].role=2; rows[1].state=2; rows[1].flags=1; rows[1].raw_item_type=w.storage;
    std::strcpy(rows[1].port_key,"pack"); return SCO_OK;
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
static void Done(sco_result result,const char* text,void* context) {
    auto* reply=static_cast<std::string*>(context);
    if(result==SCO_OK && text) *reply=text;
}
int main(int argc,char** argv) {
    Check(argc==3);
    void* module=dlopen(argv[2],RTLD_NOW|RTLD_LOCAL); Check(module!=nullptr);
    auto load=reinterpret_cast<sco_result (*)(const sco_api*,sco_plugin*)>(dlsym(module,"sco_plugin_load"));
    auto unload=reinterpret_cast<void (*)()>(dlsym(module,"sco_plugin_unload")); Check(load && unload);
    sco::SetGameThread(); const auto* api=sco::host::BuildApi({"preset workflow fixture"});
    sco::storage::Options options; options.dataRoot=std::filesystem::path(argv[1]) /
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    Check(sco::storage::Start(options)==sco::Result::Ok); Check(W::Start()==sco::Result::Ok);
    Check(S::StartGameInventoryWithLayout({}, {&world,true,Admitted,Read})==sco::Result::Ok);
    auto* owner=sco::host::NewPlugin("gear_presets"); Check(owner && load(api,owner)==SCO_OK);
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
    unload(); Check(W::Panels().empty()); Check(sco::Release(owner)==sco::Result::Ok);
    owner=sco::host::NewPlugin("gear_presets"); Check(owner && load(api,owner)==SCO_OK && Choices()==1);
    Event(5,SCO_WIDGET_COMBO,0); Event(6,SCO_WIDGET_BUTTON); Check(Has("different category"));
    S::StopGameInventory();
    static sc_inventory_v1 old{sizeof(old),0,nullptr};
    Check(sco::host::ProvideGameService(SC_INVENTORY_SERVICE_NAME,SC_INVENTORY_SERVICE_VERSION_1_1,&old)==sco::Result::Ok);
    Event(2,SCO_WIDGET_BUTTON); Check(Has("unavailable or incomplete") && Choices()==1);
    unload(); Check(sco::Release(owner)==sco::Result::Ok);
    Check(sco::host::WithdrawGameService(SC_INVENTORY_SERVICE_NAME)==sco::Result::Ok);
    W::Stop(); sco::storage::Stop();
    Check(dlclose(module)==0);
    std::printf("real host preset workflow: %u checks, 0 failed\n",checks);
}
