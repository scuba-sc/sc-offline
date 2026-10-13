// SPDX-License-Identifier: GPL-3.0-only
// Actual module, synthetic public providers; NOT Windows/game/effects evidence.
#include <sco_api.h>
#include <sc_quantum.h>
#include <sco_widgets.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace {
std::map<std::string,sco_command> commands;
sco_event_fn tick=nullptr; void* tick_ctx=nullptr;
sco_widget_event_fn event=nullptr; void* event_ctx=nullptr;
bool capability=false, enabled=false, held=false, available=true;
int enables=0,holds=0,releases=0,panels=0; uint32_t duration=0;
const void* service=nullptr;
int Has(const char* name) { return capability && !std::strcmp(name,"game.quantum.control"); }
sco_result Register(sco_plugin*,const sco_command* c) { assert(c && c->size==sizeof(*c)); commands[c->name]=*c; return SCO_OK; }
sco_result Subscribe(sco_plugin*,const char* name,sco_event_fn fn,void* ctx) { assert(!std::strcmp(name,"tick") && !tick); tick=fn; tick_ctx=ctx; return SCO_OK; }
sco_result Unsubscribe(sco_plugin*,const char*,sco_event_fn fn) { assert(tick==fn); tick=nullptr; tick_ctx=nullptr; return SCO_OK; }
sco_result Enable(sco_plugin*,uint32_t on) { ++enables; assert(on<=1); if (on && !available) return SCO_UNAVAILABLE; enabled=on!=0; if (!on) held=false; return SCO_OK; }
sco_result Hold(sco_plugin*,uint32_t ms) { ++holds; assert(ms>=1 && ms<=1000); if (!enabled || !available) return SCO_UNAVAILABLE; held=true; duration=ms; return SCO_OK; }
sco_result Release(sco_plugin*) { ++releases; held=false; return enabled ? SCO_OK:SCO_NOT_FOUND; }
sco_result State(sco_plugin*,sc_quantum_state* s) { assert(s->size==sizeof(*s)); if (!available) return SCO_UNAVAILABLE; *s={sizeof(*s),enabled?1u:0u,held?1u:0u,SC_QUANTUM_CHARGING,0,250000,1}; return SCO_OK; }
sc_quantum_v1 q{sizeof(q),0,Enable,Hold,Release,State};
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
    if (!std::strcmp(name,SCO_WIDGETS_NAME)) *out=&widgets;
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
    api.has=Has; api.query_service=Query; api.register_command=Register; api.subscribe=Subscribe; api.unsubscribe=Unsubscribe;
    int identity=1; auto* self=reinterpret_cast<sco_plugin*>(&identity);
    assert(load(&api,self)==SCO_OK && panels && tick && enables==0 && holds==0);
    for (const auto* name:{"quantum.travel","quantum.bookmark","quantum.save_bookmark","quantum.scan","quantum.status","quantum.enable","quantum.hold","quantum.release","quantum.boost","quantum.control_status"}) assert(commands.count(name));
    sco_arg on{}; on.type=SCO_ARG_BOOL; on.v.i=1;
    assert(Call("quantum.enable",&on,1)==SCO_UNAVAILABLE && enables==0);
    assert(Call("quantum.travel")==SCO_UNAVAILABLE); // G2 unchanged even with future capabilities
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
    capability=false; uint32_t now=500; tick("tick",&now,tick_ctx); assert(!enabled && !held);
    capability=true; assert(Call("quantum.enable",&on,1)==SCO_OK);
    assert(Call("quantum.hold",&ms,1)==SCO_OK);
    unload(); unload(); assert(!enabled && !held && !tick && !event);
    // A provider release hook is NOT emulated here: ordinary unload requests cleanup.
    commands.clear(); const auto writes=enables; assert(load(&api,self)==SCO_OK && enables==writes);
    available=false; assert(Call("quantum.control_status")==SCO_UNAVAILABLE); unload();
#ifdef _WIN32
    FreeLibrary(module);
#else
    dlclose(module);
#endif
    std::puts("quantum actual-module PASS: legacy commands/Travel, no startup writes, absent/partial/null refusal, bounded controls, capability loss, widgets and unload/reload; synthetic G8 only");
}
