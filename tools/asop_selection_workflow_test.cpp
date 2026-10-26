// SPDX-License-Identifier: GPL-3.0-only
// Loads the actual spawn module against synthetic public selection and widget tables.
#include "workflow_module.h"
#include <sc_asop_selection.h>
#include <sco_widgets.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
int owner_tag;auto* owner=reinterpret_cast<sco_plugin*>(&owner_tag);
struct Node {uint32_t id,kind,flags;double value;std::string label;};
std::vector<Node> nodes;
sco_widget_event_fn callback=nullptr;void* callback_context=nullptr;
sco_event_fn tick=nullptr;void* tick_context=nullptr;
std::vector<std::string> file={"A","Unresolved"},catalogue={"A","B"},accepted;
uint64_t revision=1,key=0;uint32_t phase=SC_ASOP_SELECTION_EDIT_ACCEPTED;bool available=true;
int writes=0,releases=0;
sco_result State(sco_plugin*,sc_asop_selection_state* s){s->size=sizeof(*s);s->flags=SC_ASOP_SELECTION_FILE_KNOWN|SC_ASOP_SELECTION_FILE_PRESENT;s->session=1;s->file_revision=revision;s->catalogue_generation=1;s->semantic_revision=revision;s->file_count=static_cast<uint32_t>(file.size());std::memset(s->file_digest,static_cast<int>(revision),32);return SCO_OK;}
sco_result Refresh(sco_plugin*) {return SCO_OK;}
sco_result Rows(sco_plugin*,const sc_asop_selection_read* q,sc_asop_selection_row* out,uint32_t stride,uint32_t capacity,uint32_t* count,uint32_t* more){
 if(q->session!=1 || q->revision!=(q->kind==SC_ASOP_SELECTION_ROWS_FILE?revision:1))return SCO_NOT_FOUND;
 auto& data=q->kind==SC_ASOP_SELECTION_ROWS_FILE?file:catalogue;if(q->offset>data.size())return SCO_BAD_ARG;
 *count=std::min<uint32_t>(capacity,static_cast<uint32_t>(data.size()-q->offset));*more=static_cast<uint32_t>(data.size()-q->offset-*count);
 for(uint32_t i=0;i<*count;++i){auto* row=reinterpret_cast<sc_asop_selection_row*>(reinterpret_cast<unsigned char*>(out)+i*stride);CHECK(row->size>=sizeof(*row));std::strcpy(row->class_name,data[q->offset+i].c_str());}return SCO_OK;
}
sco_result Acquire(sco_plugin*,uint64_t session,uint64_t* lease){CHECK(session==1);*lease=9;return SCO_OK;}
sco_result Release(sco_plugin*,uint64_t lease){CHECK(lease==9);++releases;return SCO_OK;}
sco_result Replace(sco_plugin*,uint64_t lease,const sc_asop_selection_request* r,uint64_t* id){CHECK(lease==9 && r->session==1 && r->file_revision==revision && r->client_key>key);key=r->client_key;accepted.clear();
 for(uint32_t i=0;i<r->count;++i){const auto* c=reinterpret_cast<const sc_asop_selection_class*>(reinterpret_cast<const unsigned char*>(r->classes)+i*r->class_stride);accepted.emplace_back(c->class_name);}++writes;phase=SC_ASOP_SELECTION_EDIT_ACCEPTED;*id=writes;return SCO_OK;}
sco_result Status(sco_plugin*,uint64_t lease,uint64_t,sc_asop_selection_receipt* r){CHECK(lease==9);r->phase=phase;return SCO_OK;}
sc_asop_selection_v1 selection{sizeof(selection),0,State,Refresh,Rows,Acquire,Release,Replace,Status};
sco_result Panel(sco_plugin*,const char* id,const char*,const sco_widget_node* values,uint32_t count,uint32_t stride,sco_widget_event_fn fn,void* ctx){
 if(std::strcmp(id,"spawn.asop"))return SCO_OK;nodes.clear();callback=fn;callback_context=ctx;
 for(uint32_t i=0;i<count;++i){const auto* n=reinterpret_cast<const sco_widget_node*>(reinterpret_cast<const unsigned char*>(values)+i*stride);nodes.push_back({n->id,n->kind,n->flags,n->value,n->label?n->label:""});}return SCO_OK;}
sco_result RemovePanel(sco_plugin*,const char*) {return SCO_OK;}
sco_widgets_v1 widgets{sizeof(widgets),0,Panel,RemovePanel,nullptr,nullptr,nullptr};
sco_result Query(const char* name,uint32_t,const void** out){*out=nullptr;if(!std::strcmp(name,SCO_WIDGETS_NAME))*out=&widgets;else if(available && !std::strcmp(name,SC_ASOP_SELECTION_NAME))*out=&selection;return *out?SCO_OK:SCO_NOT_FOUND;}
void Log(sco_plugin*,sco_log_level,const char*){}
int Has(const char*){return 0;}
sco_result Register(sco_plugin*,const sco_command*){return SCO_OK;}
sco_result Subscribe(sco_plugin*,const char*,sco_event_fn fn,void* ctx){tick=fn;tick_context=ctx;return SCO_OK;}
sco_result Unsubscribe(sco_plugin*,const char*,sco_event_fn){return SCO_OK;}
Node Get(const char* label){for(const auto& n:nodes)if(n.label==label)return n;std::fprintf(stderr,"missing node %s\n",label);std::exit(1);}
void Fire(uint32_t id,uint32_t kind,double value=0){sco_widget_event event{};event.size=sizeof(event);event.node_id=id;event.kind=kind;event.value=value;CHECK(callback);callback("spawn.asop",&event,callback_context);}
void Press(const char* label){const auto n=Get(label);CHECK(!(n.flags&SCO_WIDGET_DISABLED));Fire(n.id,SCO_WIDGET_BUTTON);}
void Tick(){const uint32_t n=1;CHECK(tick);tick("tick",&n,tick_context);}
}
int main(int argc,char** argv){
 test_module::Open(argc,argv);sco_api api{};api.size=sizeof(api);api.major=SCO_API_MAJOR;api.minor=SCO_API_MINOR;api.log=Log;api.has=Has;api.register_command=Register;api.query_service=Query;api.subscribe=Subscribe;api.unsubscribe=Unsubscribe;
 CHECK(test_module::Load(&api,owner)==SCO_OK);CHECK(Get("A").value==1 && Get("B").value==0);
 auto old=Get("B");Fire(old.id,SCO_WIDGET_TOGGLE,1);CHECK(Get("B").value==1);
 Fire(old.id,SCO_WIDGET_TOGGLE,0);CHECK(Get("B").value==1); // stale panel event ignored
 Press("Save selection");CHECK(writes==1 && accepted==std::vector<std::string>({"A","Unresolved","B"}));
 phase=SC_ASOP_SELECTION_FILE_COMMITTED;file=accepted;++revision;Tick();CHECK(Get("B").value==1);
 Press("Select none");Press("Save selection");CHECK(writes==2 && accepted.empty());phase=SC_ASOP_SELECTION_EDIT_CONFLICT;Tick();CHECK(Get("Save selection").flags&SCO_WIDGET_DISABLED);
 CHECK(Get("Reload file (discard draft)").kind==SCO_WIDGET_BUTTON);Press("Reload file (discard draft)");
 Press("Select all classes");Press("Save selection");CHECK(writes==3 && accepted==std::vector<std::string>({"A","B"}));
 phase=SC_ASOP_SELECTION_FILE_COMMITTED;file=accepted;++revision;Tick();auto stale=Get("A");++revision;Fire(stale.id,SCO_WIDGET_TOGGLE,0);CHECK(Get("Save selection").flags&SCO_WIDGET_DISABLED);
 test_module::Unload();CHECK(releases==1);
 available=false;CHECK(test_module::Load(&api,owner)==SCO_OK);
 CHECK(Get("ASOP selection service unavailable").kind==SCO_WIDGET_LABEL);
 CHECK(Get("Save selection").flags&SCO_WIDGET_DISABLED);
 test_module::Unload();test_module::Close();
 std::puts("ASOP actual spawn module workflow: PASS (synthetic services)");return 0;
}
