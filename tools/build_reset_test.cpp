// SPDX-License-Identifier: GPL-3.0-only
// Exercise the actual unload/reset against synthetic public providers, without a game.
#include "../plugins/build/build.cpp"
#include <cstdlib>
#include <string>
using namespace sco::features::build;
namespace {
std::string calls;
sco_plugin* owner;
sc_entities_v1 entities{}; sc_world_control_v1 control{}; sc_owned_v1 owned{}; sco_widgets_v1 widgets{};
void Check(bool ok,const char* what) { if(!ok){std::fprintf(stderr,"build reset: %s\n",what);std::exit(1);} }
void Log(sco_plugin*,sco_log_level,const char*) {}
sco_result DespawnMock(sco_plugin* self,uint64_t id) {Check(self==owner && (id==21 || id==22),"preview cleanup owner/id");Check(g.api && g.count==2,"state retained during preview cleanup");calls+=id==21?'g':'p';return SCO_OK;}
sco_result ReleaseMock(sco_plugin* self,uint64_t lease) {Check(self==owner && lease==23 && g.count==2,"camera cleanup owner/lease");calls+='c';return SCO_OK;}
sco_result RemoveMock(sco_plugin* self,uint64_t id,uint64_t* ticket) {Check(self==owner && id==24 && g.api && g.count==2,"retained cleanup owner/id");calls+='r';*ticket=99;return SCO_OK;}
sco_result SetPanelMock(sco_plugin*,const char*,const char*,const sco_widget_node*,uint32_t,uint32_t,sco_widget_event_fn,void*) {return SCO_OK;}
sco_result PanelMock(sco_plugin* self,const char* name) {Check(self==owner && !std::strcmp(name,"build.picker"),"picker owner/id");if(calls=="gpcr")Check(g.api && g.count==2,"picker cleanup before reset");calls+='w';return SCO_OK;}
sco_result QueryMock(const char* name,uint32_t,const void** out) {
    *out=nullptr;
    if(!std::strcmp(name,SC_ENTITIES_NAME))*out=&entities;
    if(!std::strcmp(name,SC_WORLD_CONTROL_NAME))*out=&control;
    if(!std::strcmp(name,SC_OWNED_NAME))*out=&owned;
    if(!std::strcmp(name,SCO_WIDGETS_NAME))*out=&widgets;
    return *out?SCO_OK:SCO_NOT_FOUND;
}
int Has(const char*) {return 0;}
sco_result Register(sco_plugin*,const sco_command*) {return SCO_OK;}
sco_result Provide(sco_plugin*,const char*,uint32_t,const void*) {return SCO_OK;}
sco_result Subscribe(sco_plugin*,const char*,sco_event_fn,void*) {return SCO_OK;}
void Defaults() {
    Check(!g.api && !g.self && !g.count && !g.lease && !g.preview && !g.ghost && !g.zone,"scalar handles");
    Check(!g.active && !g.clocked && !g.suppressed && !g.stillSince,"flags and time");
    Check(g.reach==50 && g.yaw==0,"reach and yaw defaults");
    for(const auto& p:g.placed)Check(!p.id && !p.receipt && p.kind==Kind::Entity && !p.removing,"placement defaults");
    for(char c:g.selected)Check(c==0,"selected bytes");
    for(double v:g.lastPos)Check(v==0,"last position");
    for(double v:g.lastRot)Check(v==0,"last rotation");
    for(double v:g.lastCamera)Check(v==0,"last camera");
    for(double v:g.lastCameraRot)Check(v==0,"last camera rotation");
}
}
int main() {
    int token=0;owner=reinterpret_cast<sco_plugin*>(&token);
    sco_api api{};api.size=sizeof(api);api.major=SCO_API_MAJOR;api.minor=SCO_API_MINOR;api.log=Log;api.has=Has;api.query_service=QueryMock;api.register_command=Register;api.provide_service=Provide;api.subscribe=Subscribe;
    entities.size=sizeof(entities);entities.despawn=DespawnMock;control.size=sizeof(control);control.camera_release=ReleaseMock;owned.size=sizeof(owned);owned.remove_build=RemoveMock;widgets.size=sizeof(widgets);widgets.set_panel=SetPanelMock;widgets.remove_panel=PanelMock;
    g.api=&api;g.self=owner;for(auto& p:g.placed){p.id=24;p.receipt=88;p.kind=Kind::Npc;p.removing=true;}g.placed[0]={24,0,Kind::Entity,false};g.count=2;
    g.ghost=21;g.preview=22;g.lease=23;g.zone=25;g.active=true;g.clocked=true;g.suppressed=true;g.stillSince=26;
    for(auto& c:g.selected)c='x';
    g.reach=123;g.yaw=3;
    for(auto& v:g.lastPos)v=1;
    for(auto& v:g.lastRot)v=2;
    for(auto& v:g.lastCamera)v=3;
    for(auto& v:g.lastCameraRot)v=4;
    picker::state.api=&api;picker::state.self=owner;
    Unload();Check(calls=="gpcrw","cleanup order Stop, Clear, picker then reset");Defaults();
    Unload();Check(calls=="gpcrw","repeated unload is inert");
    Check(Load(&api,owner)==SCO_OK,"load after reset");Check(g.reach==50 && !g.count && !g.selected[0],"fresh reload defaults");
    Unload();Defaults();std::puts("build reset: PASS; actual unload and synthetic providers; not run in game");
}
