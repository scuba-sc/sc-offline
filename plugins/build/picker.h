// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sco_api.h"
#include "sco_content.h"
#include "sco_widgets.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace sco::features::build::picker {
#define PICKER_HAS(t, type, m) ((t) && (t)->size >= offsetof(type, m) + sizeof((t)->m) && (t)->m)
constexpr uint32_t kPage=64, kRows=4096, kGroups=24;
struct Row { std::string name; uint32_t group; };
struct Observation { double reach=50; bool active=false; uint32_t retained=0, removing=0; };
using Observe=Observation(*)();
struct State {
    const sco_api* api=nullptr; sco_plugin* self=nullptr; Observe observe=nullptr;
    std::vector<std::string> groups; std::vector<Row> rows; std::vector<uint32_t> published;
    std::string filter; uint32_t group=0,page=0; int32_t selected=-1; double ahead=8;
    bool inFront=true,dirty=true,events=false; uintptr_t cookie=0;
    Observation shown{}; char source[192]{},message[256]{};
} state;
// Opaque callback identities survive state resets; no callback dereferences ctx.
inline uintptr_t nextCookie=0;
inline void Message(const char* text) {
    const char* source = text ? text : "";
    const size_t length = std::min(std::strlen(source), sizeof(state.message) - 1);
    std::memcpy(state.message, source, length);
    state.message[length] = '\0';
    state.dirty = true;
}
inline bool Equal(const std::string& a,const char* b) {
    size_t i=0;for(;i<a.size() && b[i];++i) {
        auto lower=[](unsigned char c){return c>='A'&&c<='Z'?static_cast<unsigned char>(c+32):c;};
        if(lower(static_cast<unsigned char>(a[i]))!=lower(static_cast<unsigned char>(b[i])))return false;
    }
    return i==a.size() && !b[i];
}
inline bool Parse(const std::string& bytes,std::vector<std::string>& groups,std::vector<Row>& rows) {
    if(bytes.find('\0')!=bytes.npos)return false;
    size_t start=0;
    while(start<bytes.size()) {
        const auto end=bytes.find('\n',start);const size_t stop=end==bytes.npos?bytes.size():end;
        if(stop-start>1023)return false;
        std::string line=bytes.substr(start,stop-start);start=end==bytes.npos?bytes.size():end+1;
        const auto cut=line.find('#');if(cut!=line.npos)line.resize(cut);
        if(!line.empty() && line.back()=='\r')line.pop_back();
        const auto first=line.find_first_not_of(" \t");if(first==line.npos)continue;line.erase(0,first);
        for(unsigned char c:line)if(c<32 || c>126)return false;
        if(line[0]=='[') {
            const auto close=line.find(']');
            if(close==line.npos || close==1 || close-1>23 || line.find_first_not_of(" \t",close+1)!=line.npos || groups.size()==kGroups)return false;
            const auto group=line.substr(1,close-1);
            for(const auto& old:groups)if(Equal(old,group.c_str()))return false;
            groups.push_back(group);
        } else {
            if(groups.empty() || line.size()>511 || rows.size()==kRows)return false;
            rows.push_back({line,static_cast<uint32_t>(groups.size()-1)});
        }
    }
    return true;
}
inline void ReadContent() {
    auto fail=[](const char* text){std::snprintf(state.source,sizeof(state.source),"%s",text);};
    const void* out=nullptr;
    if(!PICKER_HAS(state.api,sco_api,query_service) || state.api->query_service(SCO_CONTENT_NAME,SCO_CONTENT_VERSION_1_0,&out)!=SCO_OK) {fail("Unavailable: public sco.content 1.0 required");return;}
    const auto* content=static_cast<const sco_content_v1*>(out);
    if(!PICKER_HAS(content,sco_content_v1,item) || !PICKER_HAS(content,sco_content_v1,read)){fail("Unavailable: complete public content callbacks required");return;}
    uint64_t id=0;uint32_t matches=0;
    for(uint32_t i=0;;++i) {
        sco_content_item item{};item.size=sizeof(item);const auto r=content->item(state.self,SCO_CONTENT_LIST,i,&item);
        if(r==SCO_NOT_FOUND)break;
        if(r!=SCO_OK){fail("Unavailable: content enumeration refused");return;}
        if(i==SCO_CONTENT_MAX_ITEMS){fail("Invalid content: enumeration exceeds 4096 items");return;}
        if(item.size<sizeof(item) || item.kind!=SCO_CONTENT_LIST || !item.id || !std::memchr(item.name,0,sizeof(item.name)) || !std::memchr(item.pack,0,sizeof(item.pack))){fail("Invalid copied content metadata");return;}
        if(!std::strcmp(item.name,"lists/buildables.txt")){id=item.id;++matches;}
    }
    if(!matches){fail("No enabled lists/buildables.txt data pack; no base-file import");return;}
    if(matches!=1){fail("Ambiguous lists/buildables.txt sources; keep exactly one enabled");return;}
    uint32_t size=0;auto r=content->read(state.self,id,nullptr,&size);
    if(size>SCO_CONTENT_MAX_FILE_BYTES){fail("Invalid content: file exceeds 1MiB");return;}
    if((size && r!=SCO_TOO_MANY) || (!size && r!=SCO_OK)){fail("Unavailable: content size query refused/inconsistent");return;}
    std::string bytes(size,'\0');
    if(size){uint32_t copied=size;r=content->read(state.self,id,bytes.data(),&copied);if(r!=SCO_OK || copied!=size){fail("Unavailable: exact copied content read refused");return;}}
    std::vector<std::string> groups;std::vector<Row> rows;
    if(!Parse(bytes,groups,rows)){fail("Invalid grouped list: no partial catalogue loaded (24 groups, 4096 rows, 511-byte items)");return;}
    state.groups=std::move(groups);state.rows=std::move(rows);
    std::snprintf(state.source,sizeof(state.source),"Loaded %u groups / %u rows from lists/buildables.txt",static_cast<unsigned>(state.groups.size()),static_cast<unsigned>(state.rows.size()));
}
inline std::string SearchKey(std::string text) {
    for(char& c:text){if(c=='_')c=' ';else if(c>='A'&&c<='Z')c=static_cast<char>(c+32);}return text;
}
inline bool Matches(const Row& row) {
    if(state.filter.find_first_not_of(" _")==std::string::npos)return row.group==state.group;
    const auto key=SearchKey(row.name);const auto terms=SearchKey(state.filter);size_t begin=0;
    while(begin<terms.size()) {const auto end=terms.find(' ',begin);const auto word=terms.substr(begin,end==terms.npos?terms.size()-begin:end-begin);if(!word.empty() && key.find(word)==key.npos)return false;if(end==terms.npos)break;begin=end+1;}return true;
}
inline const sco_widgets_v1* Widgets() {
    const void* out=nullptr;
    if(!PICKER_HAS(state.api,sco_api,query_service) || state.api->query_service(SCO_WIDGETS_NAME,SCO_WIDGETS_VERSION_1_0,&out)!=SCO_OK)return nullptr;
    const auto* widgets=static_cast<const sco_widgets_v1*>(out);
    return PICKER_HAS(widgets,sco_widgets_v1,set_panel) && PICKER_HAS(widgets,sco_widgets_v1,remove_panel)?widgets:nullptr;
}
inline void Event(const char*,const sco_widget_event*,void*);
inline void Publish() {
    if(!state.api || !state.dirty)return;
    state.events=false;
    const auto* widgets=Widgets();if(!widgets)return;
    try {
        std::vector<uint32_t> filtered;for(uint32_t i=0;i<state.rows.size();++i)if(Matches(state.rows[i]))filtered.push_back(i);
        const uint32_t pages=std::max(1u,(static_cast<uint32_t>(filtered.size())+kPage-1)/kPage);
        state.page=std::min(state.page,pages-1);
        const uint32_t begin=state.page*kPage,end=std::min(begin+kPage,static_cast<uint32_t>(filtered.size()));
        std::vector<uint32_t> map(filtered.begin()+begin,filtered.begin()+end);
        std::vector<std::string> labels;for(auto i:map){const auto& row=state.rows[i];std::string label=state.filter.find_first_not_of(" _")==std::string::npos?row.name:"["+state.groups[row.group]+"] "+row.name;label.resize(std::min<size_t>(label.size(),SCO_WIDGETS_MAX_TEXT));labels.push_back(std::move(label));}
        std::vector<const char*> rows,groups;for(const auto& s:labels)rows.push_back(s.c_str());for(const auto& s:state.groups)groups.push_back(s.c_str());
        const auto obs=state.observe();
        char page[96],native[192];std::snprintf(page,sizeof(page),"Page %u / %u (%u matching rows)",state.page+1,pages,static_cast<unsigned>(filtered.size()));
        std::snprintf(native,sizeof(native),"Retained %u; removal-requested %u. Streaming/disappearance unconfirmed.",obs.retained,obs.removing);
        const bool selected=state.selected>=0 && static_cast<size_t>(state.selected)<state.rows.size();
        const bool sq42=selected && Equal(state.groups[state.rows[static_cast<size_t>(state.selected)].group],"sq42");
        std::string selection=selected?state.rows[static_cast<size_t>(state.selected)].name:"No picker row selected";selection.resize(std::min<size_t>(selection.size(),255));
        std::vector<sco_widget_node> nodes;
        auto node=[&](uint32_t id,uint32_t kind,const char* label,bool disabled=false)->sco_widget_node&{sco_widget_node n{};n.size=sizeof(n);n.id=id;n.kind=kind;n.index=-1;n.label=label;n.flags=disabled?SCO_WIDGET_DISABLED:0;nodes.push_back(n);return nodes.back();};
        auto& cat=node(1,SCO_WIDGET_COMBO,"Category",groups.empty());cat.items=groups.empty()?nullptr:groups.data();cat.item_count=static_cast<uint32_t>(groups.size());cat.index=groups.empty()?-1:static_cast<int32_t>(state.group);
        node(2,SCO_WIDGET_TEXT_INPUT,"Search all groups").text=state.filter.c_str();
        auto& list=node(3,SCO_WIDGET_LIST,"Buildables",rows.empty());list.items=rows.empty()?nullptr:rows.data();list.item_count=static_cast<uint32_t>(rows.size());
        for(size_t i=0;i<map.size();++i)if(static_cast<int32_t>(map[i])==state.selected)list.index=static_cast<int32_t>(i);
        node(4,SCO_WIDGET_BUTTON,"Previous page",!state.page);node(5,SCO_WIDGET_BUTTON,"Next page",state.page+1==pages);
        node(6,SCO_WIDGET_LABEL,page);node(7,SCO_WIDGET_LABEL,selection.c_str());
        auto& reach=node(8,SCO_WIDGET_SLIDER,"Build reach (metres)");reach.value=obs.reach;reach.minimum=5;reach.maximum=300;
        node(9,SCO_WIDGET_BUTTON,"Select preview",!selected);node(10,SCO_WIDGET_BUTTON,obs.active?"Stop building":"Start building",!obs.active&&!selected);
        node(11,SCO_WIDGET_BUTTON,"Place near player",!selected);node(12,SCO_WIDGET_BUTTON,"Undo last retained");node(13,SCO_WIDGET_BUTTON,"Clear retained");
        bool hasSq42=false;for(const auto& group:state.groups)if(Equal(group,"sq42"))hasSq42=true;
        node(14,SCO_WIDGET_BUTTON,"Browse SQ42",!hasSq42);
        node(15,SCO_WIDGET_TOGGLE,"SQ42 in front of player",!sq42).value=state.inFront?1:0;
        auto& ahead=node(16,SCO_WIDGET_SLIDER,"SQ42 metres ahead",!sq42||!state.inFront);ahead.value=state.ahead;ahead.minimum=0;ahead.maximum=100;
        node(17,SCO_WIDGET_BUTTON,"Place SQ42 selection",!sq42);
        node(18,SCO_WIDGET_LABEL,state.source);node(19,SCO_WIDGET_LABEL,state.message);node(20,SCO_WIDGET_LABEL,native);
        if(nextCookie==std::numeric_limits<uintptr_t>::max())return;
        const auto cookie=++nextCookie;
        const auto result=widgets->set_panel(state.self,"build.picker","Build",nodes.data(),static_cast<uint32_t>(nodes.size()),sizeof(sco_widget_node),Event,reinterpret_cast<void*>(cookie));
        if(result==SCO_OK){state.cookie=cookie;state.published=std::move(map);state.shown=obs;state.dirty=false;state.events=true;}
    } catch(...) {Message("Picker publication failed; actions paused until a successful retry");}
}
struct Completion { bool done=false; sco_result result=SCO_FAILED; };
inline void Done(sco_result result,const char* text,void* ctx) {auto& c=*static_cast<Completion*>(ctx);c.done=true;c.result=result;Message(text && *text?text:(result==SCO_OK?"Command accepted":"Public command refused"));}
inline sco_result Invoke(const char* name,const sco_arg* args=nullptr,uint32_t count=0) {
    if(!PICKER_HAS(state.api,sco_api,invoke)){Message("Unavailable: public command invocation required");return SCO_UNAVAILABLE;}
    Completion c;const auto r=state.api->invoke(state.self,name,args,count,Done,&c);
    if (!c.done) Message("Public command invocation refused without a reply");
    return c.done ? c.result : r;
}
inline void Event(const char* panel,const sco_widget_event* e,void* ctx) {
    if(!state.api || !state.self || !state.events || reinterpret_cast<uintptr_t>(ctx)!=state.cookie || !panel || std::strcmp(panel,"build.picker") || !e || e->size<sizeof(*e))return;
    try {
        auto view=[](){state.page=0;state.selected=-1;state.events=false;state.dirty=true;};
        auto selected=[](){return state.selected>=0 && static_cast<size_t>(state.selected)<state.rows.size();};
        auto args=[&](double value){sco_arg a{};a.type=SCO_ARG_STRING;a.v.s=state.rows[static_cast<size_t>(state.selected)].name.c_str();sco_arg b{};b.type=SCO_ARG_FLOAT;b.v.f=value;return std::vector<sco_arg>{a,b};};
        if(e->node_id==1 && e->kind==SCO_WIDGET_COMBO && e->index>=0 && static_cast<size_t>(e->index)<state.groups.size()){state.group=static_cast<uint32_t>(e->index);view();}
        else if(e->node_id==2 && e->kind==SCO_WIDGET_TEXT_INPUT && e->text) {
            size_t n=0;while(n<=63 && e->text[n])++n;
            if(n>63){Message("Search is limited to 63 bytes");}
            else {bool valid=true;for(size_t i=0;i<n;++i)if(static_cast<unsigned char>(e->text[i])<32 || static_cast<unsigned char>(e->text[i])>126)valid=false;if(valid){state.filter.assign(e->text,n);view();}else Message("Search requires printable ASCII");}
        } else if(e->node_id==3 && e->kind==SCO_WIDGET_LIST && e->index>=0 && static_cast<size_t>(e->index)<state.published.size()){state.selected=static_cast<int32_t>(state.published[static_cast<size_t>(e->index)]);state.dirty=true;}
        else if((e->node_id==4 || e->node_id==5) && e->kind==SCO_WIDGET_BUTTON) {
            uint32_t matches=0;for(const auto& row:state.rows)if(Matches(row))++matches;
            const auto pages=std::max(1u,(matches+kPage-1)/kPage);
            if(e->node_id==4 && state.page){--state.page;state.selected=-1;state.dirty=true;}
            if(e->node_id==5 && state.page+1<pages){++state.page;state.selected=-1;state.dirty=true;}
        } else if(e->node_id==8 && e->kind==SCO_WIDGET_SLIDER && std::isfinite(e->value) && e->value>=5 && e->value<=300){sco_arg a{};a.type=SCO_ARG_FLOAT;a.v.f=e->value;(void)Invoke("build.reach",&a,1);}
        else if(e->node_id==14 && e->kind==SCO_WIDGET_BUTTON){for(uint32_t i=0;i<state.groups.size();++i)if(Equal(state.groups[i],"sq42")){state.group=i;state.filter.clear();view();break;}}
        else if((e->node_id==15 || e->node_id==16) && selected() && Equal(state.groups[state.rows[static_cast<size_t>(state.selected)].group],"sq42")) {
            if(e->node_id==15 && e->kind==SCO_WIDGET_TOGGLE && (e->value==0 || e->value==1)){state.inFront=e->value!=0;state.dirty=true;}
            if(e->node_id==16 && e->kind==SCO_WIDGET_SLIDER && state.inFront && std::isfinite(e->value) && e->value>=0 && e->value<=100){state.ahead=e->value;state.dirty=true;}
        } else if(e->kind==SCO_WIDGET_BUTTON) {
            if(e->node_id==12)(void)Invoke("build.undo");
            else if(e->node_id==13)(void)Invoke("build.clear");
            else if(e->node_id==10 && state.observe().active)(void)Invoke("build.toggle");
            else if(selected()) {
                if(e->node_id==9 || e->node_id==10){const auto a=args(state.observe().reach);if(Invoke("build.select",a.data(),2)==SCO_OK && e->node_id==10)(void)Invoke("build.toggle");}
                else if(e->node_id==11){const auto a=args(std::min(state.observe().reach,100.0));(void)Invoke("build.place",a.data(),2);}
                else if(e->node_id==17 && Equal(state.groups[state.rows[static_cast<size_t>(state.selected)].group],"sq42")){const auto a=args(state.inFront?state.ahead:0);(void)Invoke("build.place",a.data(),2);}
            }
        }
        Publish();
    }catch(...){state.events=false;Message("Picker event refused; publication retry required");}
}
inline void Tick() {
    if (!state.api) return;
    const auto now = state.observe();
    if(now.reach!=state.shown.reach || now.active!=state.shown.active || now.retained!=state.shown.retained || now.removing!=state.shown.removing)state.dirty=true;
    Publish();
}
inline void Load(const sco_api* api,sco_plugin* self,Observe observe) {
    state={};state.api=api;state.self=self;state.observe=observe;
    try{ReadContent();}catch(...){std::snprintf(state.source,sizeof(state.source),"Unavailable: catalogue allocation/parse refused");state.groups.clear();state.rows.clear();}
    Message("Choose a row; native placement/control requires the existing public providers");Publish();
    if(PICKER_HAS(api,sco_api,log))api->log(self,SCO_LOG_INFO,state.source);
}
inline void Unload() {
    state.events=false;
    if(state.api){const auto* widgets=Widgets();if(widgets)(void)widgets->remove_panel(state.self,"build.picker");}
    state={};
}
#undef PICKER_HAS
} // namespace sco::features::build::picker
