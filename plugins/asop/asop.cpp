// SPDX-License-Identifier: GPL-3.0-only
// Product policy only: persisted copied ship classes for the next host startup.
#include <sco_api.h>
#include <sco_storage.h>
#include <sco_widgets.h>
#include "../picker.h"
#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace {
#define HAS(t,type,m) ((t)&&(t)->size>=offsetof(type,m)+sizeof((t)->m)&&(t)->m)
const sco_api* api=nullptr; sco_plugin* self=nullptr;
sco::picker::Catalog catalog;
std::set<std::string> chosen;
std::string committed,status;
bool stored=false, valid=true;
uint32_t page=0;
constexpr uint32_t kPage=32,kMax=2048,kMaxBytes=kMax*64;
const sco_plugin_info info{sizeof(info),SCO_API_MAJOR,SCO_API_MINOR,"asop_selector","1.0.0","sc-offline"};
void Publish();
bool Parse(const std::string& bytes,std::set<std::string>& out) {
    if(bytes.size()>kMaxBytes||bytes.find('\0')!=std::string::npos)return false;
    std::set<std::string> next;size_t at=0;
    while(at<bytes.size()){
        auto end=bytes.find('\n',at);if(end==std::string::npos)end=bytes.size();
        const auto name=bytes.substr(at,end-at);at=end+1;
        if(name.empty()||name.size()>63||next.size()==kMax||
           !std::all_of(name.begin(),name.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.';})||
           !next.insert(name).second)return false;
    }
    out=std::move(next);return true;
}
std::string Encode(){std::string out;for(const auto& name:chosen){out+=name;out+='\n';}return out;}
const sco_storage_v1* Storage(){return sco::picker::Open<sco_storage_v1>(api,SCO_STORAGE_NAME);}
void Restore(){
    chosen.clear();committed.clear();stored=false;valid=true;
    for(const auto& c:catalog.choices)chosen.insert(c.value);
    const auto* s=Storage();
    if(!HAS(s,sco_storage_v1,get)){valid=false;status="Storage unavailable; cannot save a selection";return;}
    uint32_t bytes=0;auto r=s->get(self,"selection",nullptr,&bytes);
    if(r==SCO_NOT_FOUND){status="Default ship catalog; save a subset for next game start";return;}
    if((r!=SCO_OK&&r!=SCO_TOO_MANY)||bytes>kMaxBytes){valid=false;status="Saved selection unavailable; no fallback";return;}
    std::string next(bytes,'\0');uint32_t count=bytes;
    r=s->get(self,"selection",bytes?next.data():nullptr,&count);
    std::set<std::string> parsed;
    if(r!=SCO_OK||count!=bytes||!Parse(next,parsed)){valid=false;status="Saved selection invalid; no fallback";return;}
    chosen=std::move(parsed);committed=std::move(next);stored=true;status="Saved selection loaded; changes apply next game start";
}
sco_result Snapshot(const void*,uint32_t inSize,void* out,uint32_t* size,void*){
    if(inSize||!size||(!out&&*size))return SCO_BAD_ARG;
    if(!valid)return SCO_UNAVAILABLE;
    if(!stored){*size=0;return SCO_NOT_FOUND;}
    const auto need=static_cast<uint32_t>(committed.size());
    if(*size<need){*size=need;return SCO_TOO_MANY;}
    if(need)std::memcpy(out,committed.data(),need);*size=need;return SCO_OK;
}
sco_result Save(){
    const auto* s=Storage();if(!HAS(s,sco_storage_v1,put))return SCO_UNAVAILABLE;
    auto next=Encode();const auto r=s->put(self,"selection",next.data(),static_cast<uint32_t>(next.size()));
    if(r==SCO_OK){committed=std::move(next);stored=true;valid=true;}
    status=r==SCO_OK?"Saved for next game start; current terminals unchanged":"Save refused; previous selection unchanged";
    return r;
}
sco_result Command(const sco_arg*,uint32_t count,void*,char* reply,uint32_t cap){
    if(count)return SCO_BAD_ARG;const auto r=Save();if(reply&&cap)std::snprintf(reply,cap,"%s",status.c_str());Publish();return r;
}
void Event(const char*,const sco_widget_event* e,void*){
    if(!e||e->size<sizeof(*e))return;
    if(e->kind==SCO_WIDGET_TOGGLE&&e->node_id>=100){
        const auto i=static_cast<size_t>(page)*kPage+(e->node_id-100);
        if(i<catalog.choices.size()){if(e->value!=0)chosen.insert(catalog.choices[i].value);else chosen.erase(catalog.choices[i].value);status="Unsaved selection; Save applies next game start";}
    }else if(e->kind==SCO_WIDGET_BUTTON){
        switch(e->node_id){
        case 1:(void)Save();break;
        case 2:chosen.clear();for(const auto& c:catalog.choices)chosen.insert(c.value);break;
        case 3:chosen.clear();break;
        case 4:if(page)--page;break;
        case 5:if((page+1)*kPage<catalog.choices.size())++page;break;
        default:break;
        }
    }
    Publish();
}
void Publish(){
    const auto* w=sco::picker::Open<sco_widgets_v1>(api,SCO_WIDGETS_NAME);if(!HAS(w,sco_widgets_v1,set_panel))return;
    std::vector<sco_widget_node> nodes;
    nodes.push_back(sco::picker::Node(SCO_WIDGET_LABEL,10,"ASOP ship selection: saved changes apply at next game start only"));
    nodes.push_back(sco::picker::Node(SCO_WIDGET_BUTTON,1,"Save selection"));
    nodes.push_back(sco::picker::Node(SCO_WIDGET_BUTTON,2,"Select all"));nodes.push_back(sco::picker::Node(SCO_WIDGET_BUTTON,3,"Select none"));
    nodes.push_back(sco::picker::Node(SCO_WIDGET_BUTTON,4,"Previous",page?0u:SCO_WIDGET_DISABLED));
    nodes.push_back(sco::picker::Node(SCO_WIDGET_BUTTON,5,"Next",(page+1)*kPage<catalog.choices.size()?0u:SCO_WIDGET_DISABLED));
    const auto begin=static_cast<size_t>(page)*kPage;
    for(size_t i=begin;i<catalog.choices.size()&&i<begin+kPage;++i){auto n=sco::picker::Node(SCO_WIDGET_TOGGLE,100+static_cast<uint32_t>(i-begin),catalog.choices[i].value.c_str());n.value=chosen.contains(catalog.choices[i].value)?1.0f:0.0f;nodes.push_back(n);}
    nodes.push_back(sco::picker::Node(SCO_WIDGET_LABEL,11,status.c_str()));
    (void)w->set_panel(self,"asop_selector.main","ASOP ships",nodes.data(),static_cast<uint32_t>(nodes.size()),sizeof(sco_widget_node),Event,nullptr);
}
}
extern "C" SCO_EXPORT const sco_plugin_info* sco_plugin_query(){return &info;}
extern "C" SCO_EXPORT sco_result sco_plugin_load(const sco_api* host,sco_plugin* owner){
    if(api||!owner||!HAS(host,sco_api,register_raw)||!HAS(host,sco_api,register_command)||!HAS(host,sco_api,query_service)||host->major!=SCO_API_MAJOR)return SCO_UNAVAILABLE;
    api=host;self=owner;page=0;catalog.Load(api,self,"lists/ships.txt",63);
    std::set<std::string> seen;std::vector<sco::picker::Choice> unique;
    for(auto& c:catalog.choices)if(seen.insert(c.value).second){if(unique.size()==kMax){api=nullptr;self=nullptr;return SCO_TOO_MANY;}unique.push_back(std::move(c));}
    catalog.choices=std::move(unique);Restore();
    auto r=api->register_raw(self,"asop_selector.snapshot",nullptr,Snapshot,nullptr);
    const sco_command command{sizeof(sco_command),0,"asop_selector.save","asop_selector.save","Save next-start ship selection",nullptr,nullptr,0,sizeof(sco_arg_def),Command,nullptr};
    if(r==SCO_OK)r=api->register_command(self,&command);
    if(r!=SCO_OK){api=nullptr;self=nullptr;return r;}Publish();return SCO_OK;
}
extern "C" SCO_EXPORT void sco_plugin_unload(){api=nullptr;self=nullptr;catalog={};chosen.clear();committed.clear();stored=false;valid=true;}
