// SPDX-License-Identifier: GPL-3.0-only
// Real public spawn module + Core coordinator/widgets; synthetic in-memory disk.
#include "workflow_module.h"
#include <sco/host.h>
#include <sco/widgets.h>
#include <sco/game/asop_selection.h>
#include <sc_asop_selection.h>
#include <algorithm>
#include <cstdio>
#include <string>
namespace S=sco::game::asop_selection;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Disk {
    S::Document file;int writes=0;
    static S::FileResult Read(void* p,S::Document& out){out=static_cast<Disk*>(p)->file;return S::FileResult::Ok;}
    static S::FileResult Replace(void* p,const S::Document& expected,std::string_view bytes,S::Document& out,const S::CommitPermit& permit){
        auto& d=*static_cast<Disk*>(p);if(d.file.bytes!=expected.bytes || d.file.identity!=expected.identity || d.file.present!=expected.present)return S::FileResult::Conflict;
        if(!permit.begin(permit.context))return S::FileResult::Cancelled;
        d.file.present=true;d.file.bytes=bytes;++d.file.identity[0];++d.writes;out=d.file;return S::FileResult::Ok;
    }
};
struct Click:sco::widgets::Renderer {
    std::string label;uint32_t kind;double value;
    void Render(const sco::widgets::Panel& panel,std::vector<sco::widgets::Event>& events) override {
        auto it=std::find_if(panel.nodes.begin(),panel.nodes.end(),[&](const auto& n){return n.label==label;});CHECK(it!=panel.nodes.end());
        sco::widgets::Event e{};e.node_id=it->id;e.kind=kind;e.value=value;events.push_back(e);
    }
};
void Fire(const char* label,uint32_t kind,double value=0){Click click;click.label=label;click.kind=kind;click.value=value;CHECK(sco::widgets::RenderPanel("spawn.asop",click)==sco::Result::Ok);}
int main(int argc,char** argv){
    sco::SetGameThread();const auto* api=sco::host::BuildApi({"ASOP test"});CHECK(api);CHECK(sco::widgets::Start()==sco::Result::Ok);
    Disk disk;disk.file.present=true;disk.file.bytes="A\nUnknown\n";
    S::Options options;options.file={&disk,Disk::Read,Disk::Replace};options.catalogue={"A","B"};CHECK(S::Start(options)==sco::Result::Ok);
    test_module::Open(argc,argv);auto* plugin=sco::host::NewPlugin("spawn");CHECK(plugin && test_module::Load(api,plugin)==SCO_OK);
    Fire("B",SCO_WIDGET_TOGGLE,1);Fire("Save selection",SCO_WIDGET_BUTTON);CHECK(disk.writes==0);
    CHECK(S::Pump()==sco::Result::Ok && disk.writes==1);CHECK(sco::GameThreadTick(1)==sco::Result::Ok);
    CHECK(disk.file.bytes.find("A\n")!=std::string::npos && disk.file.bytes.find("B\n")!=std::string::npos && disk.file.bytes.find("Unknown\n")!=std::string::npos);
    Fire("A",SCO_WIDGET_TOGGLE,0);Fire("Save selection",SCO_WIDGET_BUTTON);
    disk.file.bytes="External\n";++disk.file.identity[0];
    CHECK(S::Pump()==sco::Result::Ok && disk.writes==1 && disk.file.bytes=="External\n");
    CHECK(sco::GameThreadTick(2)==sco::Result::Ok);
    Fire("Reload file (discard draft)",SCO_WIDGET_BUTTON);
    Fire("Select none",SCO_WIDGET_BUTTON);Fire("Save selection",SCO_WIDGET_BUTTON);
    test_module::Unload();CHECK(sco::Release(plugin)==sco::Result::Ok);CHECK(S::Pump()==sco::Result::Ok && disk.writes==1);
    disk.file.bytes="External\n";++disk.file.identity[0];auto* other=sco::host::NewPlugin("observer");const void* raw=nullptr;
    CHECK(api->query_service(SC_ASOP_SELECTION_NAME,SC_ASOP_SELECTION_VERSION,&raw)==SCO_OK);auto* table=static_cast<const sc_asop_selection_v1*>(raw);
    CHECK(table->refresh(other)==SCO_OK);sc_asop_selection_state state{};state.size=sizeof(state);CHECK(table->state(other,&state)==SCO_OK && state.file_count==1);
    CHECK(sco::Release(other)==sco::Result::Ok);CHECK(S::Stop()==sco::Result::Ok);sco::widgets::Stop();test_module::Close();
    std::puts("ASOP module + Core widgets/selection + real owner release: PASS (synthetic disk, no game)");
}
