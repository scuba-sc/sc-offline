// SPDX-License-Identifier: GPL-3.0-only
// Actual module, synthetic public providers; NOT Windows/game/effects evidence.
#include <sco_api.h>
#include <sc_quantum.h>
#include <sc_gamerules.h>
#include <sco_widgets.h>
#include <sco_settings.h>
#include <sco_content.h>
#include <sc_game_spatial.h>
#include <sc_relocation.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace {
std::map<std::string,sco_command> commands;
std::vector<std::pair<sco_event_fn,void*>> ticks; // the real host allows one subscription per fn
sco_widget_event_fn event=nullptr; void* event_ctx=nullptr;
bool capability=false, enabled=false, held=false, available=true;
int enables=0,holds=0,releases=0,panels=0; uint32_t duration=0;
const void* service=nullptr;
bool relocation_cap=false; int32_t allow=1; std::string data_root,last_status;
int requests=0; uint32_t reloc_state=SC_RELOCATION_PENDING; sc_relocation_request last_request{};
int Has(const char* name) { return !std::strcmp(name,"game.gamerules.read") || (capability && !std::strcmp(name,"game.quantum.control")) || (relocation_cap && !std::strcmp(name,SC_RELOCATION_NAME)); }
sco_result GetBool(sco_plugin*,const char* name,int32_t* out) { if (std::strcmp(name,"relocation")) return SCO_NOT_FOUND; *out=allow; return SCO_OK; }
sco_result GetString(sco_plugin*,const char* name,char* out,uint32_t* n) {
    if (std::strcmp(name,"data_root") || data_root.empty()) return SCO_NOT_FOUND;
    if (*n<data_root.size()+1) { *n=static_cast<uint32_t>(data_root.size()+1); return SCO_TOO_MANY; }
    std::memcpy(out,data_root.c_str(),data_root.size()+1); *n=static_cast<uint32_t>(data_root.size()+1); return SCO_OK;
}
sco_settings_v1 settings{sizeof(settings),0,GetBool,nullptr,nullptr,GetString,nullptr};
const char* curated="Stanton | Port | port_zone | 10\n";
sco_result ContentItem(sco_plugin*,sco_content_kind kind,uint32_t ordinal,sco_content_item* out) {
    assert(kind==SCO_CONTENT_LIST); if(ordinal)return SCO_NOT_FOUND;
    *out={};out->size=sizeof(*out);out->kind=kind;out->id=1;
    std::snprintf(out->pack,sizeof(out->pack),"synthetic_locations");std::snprintf(out->name,sizeof(out->name),"lists/locations.txt");return SCO_OK;
}
sco_result ContentRead(sco_plugin*,uint64_t id,void* out,uint32_t* size) {
    assert(id==1);const auto bytes=static_cast<uint32_t>(std::strlen(curated));
    if(*size<bytes){*size=bytes;return SCO_TOO_MANY;}
    std::memcpy(out,curated,bytes);*size=bytes;return SCO_OK;
}
sco_content_v1 content{sizeof(content),0,ContentItem,ContentRead};
int FindZone(const char* name,sc_game_zone* z) {
    if (std::strcmp(name,"port_zone")) return 0;
    *z={sizeof(*z),SC_GAME_ZONE_SYSTEM_UNKNOWN,5,77,0,77,0,{}}; std::snprintf(z->name,sizeof(z->name),"port_zone"); return 1;
}
sc_game_spatial_v1 spatial{};
sco_result RelRequest(sco_plugin*,const sc_relocation_request* r,uint64_t* t) { ++requests; assert(r && r->size==sizeof(*r)); last_request=*r; *t=9; reloc_state=SC_RELOCATION_PENDING; return SCO_OK; }
sco_result RelCancel(sco_plugin*,uint64_t) { return SCO_OK; }
sco_result RelStatus(sco_plugin*,uint64_t t,sc_relocation_status* s) { if (t!=9) return SCO_NOT_FOUND; s->state=reloc_state; s->ticket=t; s->applied=reloc_state==SC_RELOCATION_COMPLETE; return SCO_OK; }
sc_relocation_v1 relocation{sizeof(relocation),RelRequest,RelCancel,RelStatus};
void Status(sco_plugin*,const char* message) { last_status=message; }
sco_result Register(sco_plugin*,const sco_command* c) { assert(c && c->size==sizeof(*c)); commands[c->name]=*c; return SCO_OK; }
sco_result Subscribe(sco_plugin*,const char* name,sco_event_fn fn,void* ctx) {
    assert(!std::strcmp(name,"tick") && fn);
    for (const auto& t:ticks) assert(t.first!=fn);
    ticks.emplace_back(fn,ctx); return SCO_OK;
}
sco_result Unsubscribe(sco_plugin*,const char* name,sco_event_fn fn) {
    assert(!std::strcmp(name,"tick"));
    for (auto it=ticks.begin();it!=ticks.end();++it) if (it->first==fn) { ticks.erase(it); return SCO_OK; }
    assert(!"unsubscribe of an unknown tick fn"); return SCO_NOT_FOUND;
}
void Tick(uint32_t now) { const auto copy=ticks; for (const auto& t:copy) t.first("tick",&now,t.second); }
sco_result Enable(sco_plugin*,uint32_t on) { ++enables; assert(on<=1); if (on && !available) return SCO_UNAVAILABLE; enabled=on!=0; if (!on) held=false; return SCO_OK; }
sco_result Hold(sco_plugin*,uint32_t ms) { ++holds; assert(ms>=1 && ms<=1000); if (!enabled || !available) return SCO_UNAVAILABLE; held=true; duration=ms; return SCO_OK; }
sco_result Release(sco_plugin*) { ++releases; held=false; return enabled ? SCO_OK:SCO_NOT_FOUND; }
sco_result State(sco_plugin*,sc_quantum_state* s) { assert(s->size==sizeof(*s)); if (!available) return SCO_UNAVAILABLE; *s={sizeof(*s),enabled?1u:0u,held?1u:0u,SC_QUANTUM_CHARGING,0,250000,1}; return SCO_OK; }
sc_quantum_v1 q{sizeof(q),0,Enable,Hold,Release,State};
sco_result Rules(sco_plugin*,sc_gamerules_state* out) { *out={};out->size=sizeof(*out);out->valid=1;out->quantum_travel_allowed=1;out->quantum_boost_allowed=1;return SCO_OK; }
sc_gamerules_v1 rules{sizeof(rules),0,Rules,nullptr};
sco_result Panel(sco_plugin*,const char* id,const char* title,const sco_widget_node* nodes,uint32_t n,uint32_t stride,sco_widget_event_fn fn,void* ctx) {
    assert(!std::strcmp(id,"quantum.travel") && !std::strcmp(title,"Travel")); assert(stride==sizeof(*nodes));
    bool bookmarks=false,control=false;
    for (uint32_t i=0;i<n;++i) { bookmarks |= nodes[i].id==3; control |= nodes[i].id==102; }
    assert(bookmarks && control); ++panels; event=fn; event_ctx=ctx; return SCO_OK;
}
sco_result Remove(sco_plugin*,const char* id) { assert(!std::strcmp(id,"quantum.travel")); event=nullptr; event_ctx=nullptr; return SCO_OK; }
sco_widgets_v1 widgets{sizeof(widgets),0,Panel,Remove};
sco_result Query(const char* name,uint32_t version,const void** out) {
    assert(version==0x10000u); *out=nullptr;
    if (!std::strcmp(name,SC_QUANTUM_SERVICE_NAME)) *out=service;
    if (!std::strcmp(name,SC_GAMERULES_SERVICE_NAME)) *out=&rules;
    if (!std::strcmp(name,SCO_WIDGETS_NAME)) *out=&widgets;
    if (!std::strcmp(name,SCO_SETTINGS_NAME)) *out=&settings;
    if (!std::strcmp(name,SCO_CONTENT_NAME)) *out=&content;
    if (!std::strcmp(name,SC_GAME_SPATIAL_NAME)) *out=&spatial;
    if (!std::strcmp(name,SC_RELOCATION_NAME) && relocation_cap) *out=&relocation;
    return *out ? SCO_OK:SCO_NOT_FOUND;
}
sco_result Call(const char* name,const sco_arg* args=nullptr,uint32_t n=0) { char reply[512]{}; assert(commands.count(name)); const auto& c=commands.at(name); return c.fn(args,n,c.ctx,reply,sizeof(reply)); }
}
int main(int argc,char** argv) {
    assert(argc==2);
#ifdef _WIN32
    const auto module=LoadLibraryA(argv[1]); assert(module);
    const auto load=reinterpret_cast<sco_result(*)(const sco_api*,sco_plugin*)>(GetProcAddress(module,"sco_plugin_load"));
    const auto unload=reinterpret_cast<void(*)()>(GetProcAddress(module,"sco_plugin_unload"));
#else
    void* module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); if (!module) std::fprintf(stderr,"%s\n",dlerror()); assert(module);
    const auto load=reinterpret_cast<sco_result(*)(const sco_api*,sco_plugin*)>(dlsym(module,"sco_plugin_load"));
    const auto unload=reinterpret_cast<void(*)()>(dlsym(module,"sco_plugin_unload"));
#endif
    assert(load && unload);
    sco_api api{}; api.size=sizeof(api); api.major=SCO_API_MAJOR; api.minor=SCO_API_MINOR;
    api.has=Has; api.query_service=Query; api.register_command=Register; api.subscribe=Subscribe; api.unsubscribe=Unsubscribe; api.status=Status;
    spatial.size=sizeof(spatial); spatial.find_zone=FindZone;
    const auto root=std::filesystem::temp_directory_path()/"sco-quantum-workflow-test";
    std::filesystem::remove_all(root); std::filesystem::create_directories(root); data_root=root.string();
    { std::ofstream(root/"locations.txt")<<"Raw | Ignored base | wrong_zone | 999\n"; }
    int identity=1; auto* self=reinterpret_cast<sco_plugin*>(&identity);
    assert(load(&api,self)==SCO_OK && panels && ticks.size()==1 && enables==0 && holds==0);
    for (const auto* name:{"quantum.travel","quantum.bookmark","quantum.save_bookmark","quantum.scan","quantum.status","quantum.enable","quantum.hold","quantum.release","quantum.boost","quantum.control_status"}) assert(commands.count(name));
    sco_arg on{}; on.type=SCO_ARG_BOOL; on.v.i=1;
    assert(Call("quantum.enable",&on,1)==SCO_UNAVAILABLE && enables==0);
    // game.relocation: switch, then capability, both before arguments; one tracked request; terminal state reported once.
    assert(Call("quantum.travel")==SCO_UNAVAILABLE && Call("quantum.bookmark")==SCO_UNAVAILABLE && !requests); // capability absent
    relocation_cap=true;
    assert(Call("quantum.travel")==SCO_BAD_ARG && !requests); // capability on: arguments are checked, nothing requested
    allow=0; assert(Call("quantum.travel")==SCO_UNAVAILABLE && Call("quantum.bookmark")==SCO_UNAVAILABLE && !requests); allow=1; // player switch off
    sco_arg trip[2]{}; trip[0].type=SCO_ARG_STRING; trip[0].v.s="Port"; trip[1].type=SCO_ARG_FLOAT; trip[1].v.f=99;
    assert(Call("quantum.travel",trip,2)==SCO_BAD_ARG && !requests); trip[1].v.f=500;
    trip[0].v.s="Nowhere"; assert(Call("quantum.travel",trip,2)==SCO_NOT_FOUND && !requests); trip[0].v.s="Port";
    assert(Call("quantum.travel",trip,2)==SCO_OK && requests==1 && ticks.size()==2);
    assert(last_request.zone_id==77 && last_request.pos[2]==510 && last_request.rot_xyzw[3]==1 && last_request.timeout_ms>=1 && last_request.timeout_ms<=60000);
    assert(Call("quantum.travel",trip,2)==SCO_TOO_MANY && requests==1); // one active request
    uint32_t now=100; Tick(now); assert(ticks.size()==2 && last_status.empty()); // pending: still tracked
    reloc_state=SC_RELOCATION_COMPLETE; Tick(now); assert(ticks.size()==1 && !last_status.empty()); // reported once, then untracked
    last_status.clear(); assert(Call("quantum.travel",trip,2)==SCO_OK && requests==2 && ticks.size()==2);
    reloc_state=SC_RELOCATION_FAILED; Tick(now); assert(ticks.size()==1 && !last_status.empty());
    relocation_cap=false; assert(Call("quantum.travel",trip,2)==SCO_UNAVAILABLE && requests==2); // re-checked per command
    on.v.i=2; assert(Call("quantum.enable",&on,1)==SCO_BAD_ARG); on.v.i=1;
    capability=true;
    auto* short_table=static_cast<sc_quantum_v1*>(std::malloc(8)); assert(short_table); *reinterpret_cast<uint32_t*>(short_table)=8; service=short_table;
    assert(Call("quantum.enable",&on,1)==SCO_UNAVAILABLE && enables==0); std::free(short_table);
    service=&q; q.enable=nullptr; assert(Call("quantum.enable",&on,1)==SCO_UNAVAILABLE); q.enable=Enable;
    assert(Call("quantum.enable",&on,1)==SCO_OK && enabled && enables==1);
    sco_arg ms{}; ms.type=SCO_ARG_INT; ms.v.i=0; assert(Call("quantum.hold",&ms,1)==SCO_BAD_ARG);
    ms.v.i=1001; assert(Call("quantum.hold",&ms,1)==SCO_BAD_ARG && holds==0);
    ms.v.i=1000; assert(Call("quantum.hold",&ms,1)==SCO_OK && duration==1000 && held);
    assert(Call("quantum.boost")==SCO_OK && !held);
    assert(Call("quantum.boost")==SCO_OK && held && duration==1000);
    capability=false; assert(Call("quantum.release")==SCO_OK && !held); // cancellation ignores capability
    capability=true;
    sco_widget_event e{sizeof(e),103,SCO_WIDGET_BUTTON,0,0,nullptr}; event("quantum.travel",&e,event_ctx); assert(held);
    capability=false; now=500; Tick(now); assert(!enabled && !held);
    capability=true; assert(Call("quantum.enable",&on,1)==SCO_OK);
    assert(Call("quantum.hold",&ms,1)==SCO_OK);
    relocation_cap=true; assert(Call("quantum.travel",trip,2)==SCO_OK && requests==3 && ticks.size()==2); // in flight at unload
    unload(); unload(); assert(!enabled && !held && ticks.empty() && !event); relocation_cap=false;
    // A provider release hook is NOT emulated here: ordinary unload requests cleanup.
    commands.clear(); const auto writes=enables; assert(load(&api,self)==SCO_OK && enables==writes);
    available=false; assert(Call("quantum.control_status")==SCO_UNAVAILABLE); unload();
#ifdef _WIN32
    FreeLibrary(module);
#else
    dlclose(module);
#endif
    std::filesystem::remove_all(root);
    std::puts("quantum actual-module PASS: legacy commands/Travel, no startup writes, absent/partial/null refusal, bounded controls, capability loss, game.relocation switch/capability/request/tick tracking, widgets and unload/reload; synthetic only");
}
