// SPDX-License-Identifier: GPL-3.0-only
// Optional SDK-only ASOP file editor. No filesystem or engine access from the plugin.
#if __has_include(<sc_asop_selection.h>)
struct AsopPicker {
    sc_asop_selection_state state{};
    std::vector<std::string> catalogue,draft;
    uint64_t lease=0,key=0,pending=0;
    uint32_t page=0,base=0;
    bool ready=false,dirty=false,stale=false,releasing=false;
    char message[200]="ASOP selection unavailable";
} asop_picker;
static uint32_t asop_epoch=0;
const sc_asop_selection_v1* AsopTable() { return Open<sc_asop_selection_v1>(SC_ASOP_SELECTION_NAME); }
bool AsopHas(const sc_asop_selection_v1* t) {
    return HAS(t,sc_asop_selection_v1,state) && HAS(t,sc_asop_selection_v1,refresh) &&
        HAS(t,sc_asop_selection_v1,rows) && HAS(t,sc_asop_selection_v1,acquire) &&
        HAS(t,sc_asop_selection_v1,release) && HAS(t,sc_asop_selection_v1,replace) && HAS(t,sc_asop_selection_v1,status);
}
bool AsopEqual(const std::string& a,const std::string& b) {
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i){auto fold=[](char c){return c>='A'&&c<='Z'?static_cast<char>(c+'a'-'A'):c;};if(fold(a[i])!=fold(b[i]))return false;}
    return true;
}
bool AsopRows(const sc_asop_selection_v1* t,uint32_t kind,uint64_t revision,std::vector<std::string>& out) {
    sc_asop_selection_read query{};query.size=sizeof(query);query.kind=kind;query.session=asop_picker.state.session;query.revision=revision;
    out.clear();
    for(;;) {
        sc_asop_selection_row rows[SC_ASOP_SELECTION_MAX_PAGE]{};
        for(auto& row:rows)row.size=sizeof(row);
        uint32_t count=0,more=0;
        if(t->rows(g.self,&query,rows,sizeof(rows[0]),SC_ASOP_SELECTION_MAX_PAGE,&count,&more)!=SCO_OK ||
           count>SC_ASOP_SELECTION_MAX_PAGE || out.size()+count>SC_ASOP_SELECTION_MAX_CLASSES || (more && !count))return false;
        for(uint32_t i=0;i<count;++i){if(!std::memchr(rows[i].class_name,0,sizeof(rows[i].class_name)))return false;out.emplace_back(rows[i].class_name);}
        if(!more)return true;
        query.offset+=count;
    }
}
bool AsopReload(bool refresh) {
    const auto* t=AsopTable();
    if(!AsopHas(t)){std::snprintf(asop_picker.message,sizeof(asop_picker.message),"ASOP selection service unavailable");asop_picker.ready=false;return false;}
    if(refresh && t->refresh(g.self)!=SCO_OK){std::snprintf(asop_picker.message,sizeof(asop_picker.message),"File refresh failed; selection unknown");asop_picker.ready=false;return false;}
    sc_asop_selection_state state{};state.size=sizeof(state);
    if(t->state(g.self,&state)!=SCO_OK || !(state.flags&SC_ASOP_SELECTION_FILE_KNOWN)) {
        std::snprintf(asop_picker.message,sizeof(asop_picker.message),"Selection file unavailable or invalid");asop_picker.ready=false;return false;
    }
    const auto previous=asop_picker.state;asop_picker.state=state;
    if(previous.session && previous.session!=state.session){asop_picker.lease=0;asop_picker.key=0;asop_picker.pending=0;asop_picker.releasing=false;}
    std::vector<std::string> file,catalogue;
    if(!AsopRows(t,SC_ASOP_SELECTION_ROWS_FILE,state.file_revision,file) ||
       !AsopRows(t,SC_ASOP_SELECTION_ROWS_CATALOGUE,state.catalogue_generation,catalogue)) {
        asop_picker.state=previous;asop_picker.ready=false;std::snprintf(asop_picker.message,sizeof(asop_picker.message),"Selection changed while loading; refresh again");return false;
    }
    asop_picker.draft=std::move(file);asop_picker.catalogue=std::move(catalogue);
    asop_picker.ready=true;asop_picker.dirty=false;asop_picker.stale=false;asop_picker.page=0;
    std::snprintf(asop_picker.message,sizeof(asop_picker.message),"%s; saved file only, native application unverified",(state.flags&SC_ASOP_SELECTION_FILE_PRESENT)?"Selection loaded":"Selection file absent");return true;
}
void RenderAsop();
// A release can be temporarily unavailable while the coordinator is busy.
// Keep the lease until the provider confirms it is released or no longer exists.
bool AsopRelease(const sc_asop_selection_v1* t) {
    auto& p=asop_picker;
    if(!p.lease){p.releasing=false;return true;}
    if(!AsopHas(t)){p.releasing=true;return false;}
    const auto result=t->release(g.self,p.lease);
    if(result==SCO_OK || result==SCO_NOT_FOUND){p.lease=0;p.key=0;p.releasing=false;return true;}
    p.releasing=true;return false;
}
void AsopEvent(const char* panel,const sco_widget_event* event,void*) {
    if(!panel || std::strcmp(panel,"spawn.asop") || !event || event->size<sizeof(*event))return;
    auto& p=asop_picker;const uint32_t id=event->node_id;
    if(!p.base || id<=p.base || id>p.base+40)return;
    const auto* t=AsopTable();if(!AsopHas(t))return;
    const uint32_t action=id-p.base;
    if(event->kind==SCO_WIDGET_BUTTON && action==1 && !p.pending && !p.releasing) {AsopReload(true);RenderAsop();return;}
    if(!p.ready || p.pending || p.stale || p.releasing)return;
    sc_asop_selection_state current{};current.size=sizeof(current);
    if(t->state(g.self,&current)!=SCO_OK || !(current.flags&SC_ASOP_SELECTION_FILE_KNOWN) ||
       current.session!=p.state.session || current.file_revision!=p.state.file_revision || current.catalogue_generation!=p.state.catalogue_generation) {
        p.stale=true;std::snprintf(p.message,sizeof(p.message),"Selection changed externally; draft retained, refresh to reconcile");RenderAsop();return;
    }
    if(event->kind==SCO_WIDGET_BUTTON) {
        if(action==2){p.draft=p.catalogue;p.dirty=true;}
        else if(action==3){p.draft.clear();p.dirty=true;}
        else if(action==4 && p.page>0)--p.page;
        else if(action==5 && (static_cast<size_t>(p.page)+1)*16<p.catalogue.size())++p.page;
        else if(action==6 && p.dirty) {
            if(!p.lease && t->acquire(g.self,p.state.session,&p.lease)!=SCO_OK){std::snprintf(p.message,sizeof(p.message),"Another editor owns the lease");RenderAsop();return;}
            if(p.key==std::numeric_limits<uint64_t>::max()){(void)AsopRelease(t);std::snprintf(p.message,sizeof(p.message),"Edit key exhausted");RenderAsop();return;}
            std::vector<sc_asop_selection_class> classes(p.draft.size());
            for(size_t i=0;i<classes.size();++i){classes[i].size=sizeof(classes[i]);std::memcpy(classes[i].class_name,p.draft[i].c_str(),p.draft[i].size()+1);}
            sc_asop_selection_request request{};request.size=sizeof(request);request.session=p.state.session;request.file_revision=p.state.file_revision;
            std::memcpy(request.file_digest,p.state.file_digest,sizeof(request.file_digest));request.client_key=++p.key;
            request.classes=classes.empty()?nullptr:classes.data();request.count=static_cast<uint32_t>(classes.size());request.class_stride=sizeof(sc_asop_selection_class);
            uint64_t id_out=0;const auto result=t->replace(g.self,p.lease,&request,&id_out);
            if(result==SCO_OK){p.pending=id_out;std::snprintf(p.message,sizeof(p.message),"Edit accepted; waiting for file commit");}
            else { (void)AsopRelease(t); std::snprintf(p.message,sizeof(p.message),"Save refused (%u); draft retained",static_cast<unsigned>(result)); }
        }
    } else if(event->kind==SCO_WIDGET_TOGGLE && action>=16 && action<32 && (event->value==0 || event->value==1)) {
        const size_t index=static_cast<size_t>(p.page)*16+(action-16);
        if(index>=p.catalogue.size())return;
        const auto& name=p.catalogue[index];auto found=std::find_if(p.draft.begin(),p.draft.end(),[&](const std::string& x){return AsopEqual(x,name);});
        if(event->value && found==p.draft.end()){if(p.draft.size()>=SC_ASOP_SELECTION_MAX_CLASSES)return;p.draft.push_back(name);p.dirty=true;}
        else if(!event->value && found!=p.draft.end()){p.draft.erase(found);p.dirty=true;}
    } else return;
    RenderAsop();
}
void RenderAsop() {
    using sco::picker::Node;
    const auto* w=Open<sco_widgets_v1>(SCO_WIDGETS_NAME);if(!HAS(w,sco_widgets_v1,set_panel))return;
    auto& p=asop_picker;
    if(asop_epoch>std::numeric_limits<uint32_t>::max()-64){p.base=0;return;}
    p.base=(asop_epoch+=64);
    std::vector<sco_widget_node> nodes;
    nodes.push_back(Node(SCO_WIDGET_LABEL,p.base+7,p.message));
    nodes.push_back(Node(SCO_WIDGET_BUTTON,p.base+1,p.dirty?"Reload file (discard draft)":"Refresh selection",(p.pending || p.releasing)?SCO_WIDGET_DISABLED:0));
    const uint32_t disabled=(!p.ready || p.pending || p.stale || p.releasing)?SCO_WIDGET_DISABLED:0;
    nodes.push_back(Node(SCO_WIDGET_BUTTON,p.base+2,"Select all classes",disabled));
    nodes.push_back(Node(SCO_WIDGET_BUTTON,p.base+3,"Select none",disabled));
    nodes.push_back(Node(SCO_WIDGET_BUTTON,p.base+4,"Previous classes",disabled));
    nodes.push_back(Node(SCO_WIDGET_BUTTON,p.base+5,"Next classes",disabled));
    nodes.push_back(Node(SCO_WIDGET_BUTTON,p.base+6,"Save selection",disabled || !p.dirty?SCO_WIDGET_DISABLED:0));
    char status[160];std::snprintf(status,sizeof(status),"Page %u; selected %zu; native publication and observation unavailable",p.page+1,p.draft.size());
    nodes.push_back(Node(SCO_WIDGET_LABEL,p.base+8,status));
    const size_t start=static_cast<size_t>(p.page)*16;
    for(size_t i=0;i<16 && start+i<p.catalogue.size();++i) {
        const auto& name=p.catalogue[start+i];auto node=Node(SCO_WIDGET_TOGGLE,p.base+16+static_cast<uint32_t>(i),name.c_str(),disabled);
        node.value=std::any_of(p.draft.begin(),p.draft.end(),[&](const std::string& x){return AsopEqual(x,name);})?1:0;nodes.push_back(node);
    }
    (void)w->set_panel(g.self,"spawn.asop","ASOP selection",nodes.data(),static_cast<uint32_t>(nodes.size()),sizeof(nodes[0]),AsopEvent,nullptr);
}
void PollAsop() {
    auto& p=asop_picker;
    const auto* t=AsopTable();
    if(p.releasing) {
        if(AsopRelease(t)) RenderAsop();
        return;
    }
    if(!p.pending)return;
    if(!AsopHas(t)) {
        if(std::strcmp(p.message,"Save outcome unknown; waiting for selection service")) {
            p.stale=true;
            std::snprintf(p.message,sizeof(p.message),"Save outcome unknown; waiting for selection service");
            RenderAsop();
        }
        return;
    }
    sc_asop_selection_receipt receipt{};receipt.size=sizeof(receipt);
    const auto result=t->status(g.self,p.lease,p.pending,&receipt);
    if(result==SCO_NOT_FOUND) {
        // A restarted provider invalidates the old lease and receipt. Never report
        // success or retry the write: the user must reload and reconcile the file.
        p.pending=0;p.stale=true;(void)AsopRelease(t);
        std::snprintf(p.message,sizeof(p.message),"Save outcome unknown; draft retained, refresh to reconcile");RenderAsop();return;
    }
    if(result!=SCO_OK)return; // busy/unavailable is not evidence of a terminal result
    if(receipt.phase==SC_ASOP_SELECTION_FILE_COMMITTED){
        p.pending=0;(void)AsopRelease(t);
        if(AsopReload(false))std::snprintf(p.message,sizeof(p.message),"Selection saved to file; native application unverified");
        else std::snprintf(p.message,sizeof(p.message),"File saved; current selection could not be refreshed");RenderAsop();
    } else if(receipt.phase==SC_ASOP_SELECTION_EDIT_CONFLICT || receipt.phase==SC_ASOP_SELECTION_EDIT_UNKNOWN || receipt.phase==SC_ASOP_SELECTION_EDIT_REVOKED){
        p.pending=0;p.stale=true;(void)AsopRelease(t);
        std::snprintf(p.message,sizeof(p.message),"Save conflicted or is unknown; draft retained, refresh to reconcile");RenderAsop();
    }
}
void StartAsop(){asop_picker={};AsopReload(false);RenderAsop();}
void StopAsop(){const auto* t=AsopTable();if(asop_picker.lease && AsopHas(t))(void)t->release(g.self,asop_picker.lease);const auto* w=Open<sco_widgets_v1>(SCO_WIDGETS_NAME);if(HAS(w,sco_widgets_v1,remove_panel))(void)w->remove_panel(g.self,"spawn.asop");asop_picker={};}
#else
void PollAsop() {}
void StartAsop() {const auto* w=Open<sco_widgets_v1>(SCO_WIDGETS_NAME);if(HAS(w,sco_widgets_v1,set_panel)){auto n=sco::picker::Node(SCO_WIDGET_LABEL,1,"ASOP selection unavailable on this SDK");(void)w->set_panel(g.self,"spawn.asop","ASOP selection",&n,1,sizeof(n),nullptr,nullptr);}}
void StopAsop() {const auto* w=Open<sco_widgets_v1>(SCO_WIDGETS_NAME);if(HAS(w,sco_widgets_v1,remove_panel))(void)w->remove_panel(g.self,"spawn.asop");}
#endif
