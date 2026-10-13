// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <sco_content.h>
#include <sco_widgets.h>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
namespace sco::picker {
template<class T> const T* Open(const sco_api* api,const char* name) {
    const void* p=nullptr;
    return api && api->size >= offsetof(sco_api,query_service)+sizeof(api->query_service) && api->query_service &&
        api->query_service(name,0x10000u,&p)==SCO_OK ? static_cast<const T*>(p) : nullptr;
}
#define PICKER_HAS(t,type,m) ((t) && (t)->size >= offsetof(type,m)+sizeof((t)->m) && (t)->m)
struct Choice { std::string value,label; };
struct Catalog {
    std::vector<Choice> choices;
    std::vector<size_t> visible;
    std::vector<const char*> labels;
    std::string filter;
    uint32_t page=0;
    char status[256]{};
    static constexpr size_t pageSize=64, maxChoices=8192;
    void Load(const sco_api* api,sco_plugin* self,const char* name,size_t maxLength) {
        choices.clear(); page=0;
        const auto* c=Open<sco_content_v1>(api,SCO_CONTENT_NAME);
        if (!PICKER_HAS(c,sco_content_v1,item) || !PICKER_HAS(c,sco_content_v1,read)) {
            std::snprintf(status,sizeof(status),"Catalog unavailable: sco.content absent; enter a class manually"); return;
        }
        size_t rejected=0; sco_result result=SCO_OK;
        for(uint32_t i=0;i<SCO_CONTENT_MAX_ITEMS && choices.size()<maxChoices;++i) {
            sco_content_item item{};item.size=sizeof(item);
            result=c->item(self,SCO_CONTENT_LIST,i,&item);
            if(result==SCO_NOT_FOUND){result=SCO_OK;break;}
            if(result!=SCO_OK)break;
            if(!std::memchr(item.name,0,sizeof(item.name)) || !std::memchr(item.pack,0,sizeof(item.pack))) {result=SCO_FAILED;break;}
            if(std::strcmp(item.name,name))continue;
            uint32_t bytes=0;result=c->read(self,item.id,nullptr,&bytes);
            if(result!=SCO_OK && result!=SCO_TOO_MANY)break;
            if(bytes>SCO_CONTENT_MAX_FILE_BYTES){result=SCO_TOO_MANY;break;}
            std::string data(bytes,'\0');uint32_t copied=bytes;
            result=c->read(self,item.id,bytes?data.data():nullptr,&copied);
            if(result!=SCO_OK)break;
            if(copied!=bytes || data.find('\0')!=std::string::npos){result=SCO_BAD_ARG;break;}
            size_t start=0;
            while(start<data.size() && choices.size()<maxChoices) {
                auto end=data.find('\n',start);if(end==std::string::npos)end=data.size();
                std::string line=data.substr(start,end-start);start=end+1;
                auto comment=line.find('#');if(comment!=std::string::npos)line.resize(comment);
                auto first=line.find_first_not_of(" \t\r");if(first==std::string::npos)continue;
                auto last=line.find_last_not_of(" \t\r");line=line.substr(first,last-first+1);
                if(line.size()>maxLength || !std::all_of(line.begin(),line.end(),[](unsigned char ch){return std::isalnum(ch)||ch=='_'||ch=='.'||ch=='-';})) {++rejected;continue;}
                choices.push_back({line,line+" ["+item.pack+"]"}); // Never deduplicate pack or row identity.
            }
        }
        std::snprintf(status,sizeof(status),"%zu catalog rows; skipped=%zu; result=%u%s; base files unchanged",choices.size(),rejected,static_cast<unsigned>(result),choices.size()==maxChoices?" (catalog limit reached)":"");
    }
    void View() {
        visible.clear();labels.clear();std::string words=filter;
        std::transform(words.begin(),words.end(),words.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        for(size_t i=0;i<choices.size();++i) {
            std::string value=choices[i].label;
            std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            bool match=true;size_t p=0;
            while(p<words.size()) {auto e=words.find(' ',p);if(e==std::string::npos)e=words.size();auto word=words.substr(p,e-p);if(!word.empty() && value.find(word)==std::string::npos)match=false;p=e+1;}
            if(match)visible.push_back(i);
        }
        if(static_cast<size_t>(page)*pageSize>=visible.size())page=0;
        size_t begin=static_cast<size_t>(page)*pageSize,end=std::min(begin+pageSize,visible.size());
        for(size_t i=begin;i<end;++i)labels.push_back(choices[visible[i]].label.c_str());
    }
    const char* Select(int32_t index) const {
        if(index<0 || static_cast<size_t>(index)>=labels.size())return nullptr;
        return choices[visible[static_cast<size_t>(page)*pageSize+static_cast<size_t>(index)]].value.c_str();
    }
    void Next(){if((static_cast<size_t>(page)+1)*pageSize<visible.size())++page;}
    void Previous(){if(page)--page;}
};
inline sco_widget_node Node(uint32_t kind,uint32_t id,const char* label,uint32_t flags=0) {
    sco_widget_node n{};n.size=sizeof(n);n.kind=kind;n.id=id;n.label=label;n.flags=flags;return n;
}
inline bool Event(const sco_widget_event* e,uint32_t kind) {return e && e->size>=sizeof(*e) && e->kind==kind;}
inline bool Text(const char* text,size_t limit) {if(!text)return false;for(size_t i=0;i<=limit;++i)if(!text[i])return true;return false;}
inline sco_arg String(const char* value) {sco_arg a{};a.type=SCO_ARG_STRING;a.v.s=value;return a;}
inline sco_arg Int(int64_t value) {sco_arg a{};a.type=SCO_ARG_INT;a.v.i=value;return a;}
inline sco_arg Float(double value) {sco_arg a{};a.type=SCO_ARG_FLOAT;a.v.f=value;return a;}
inline sco_arg Bool(bool value) {sco_arg a{};a.type=SCO_ARG_BOOL;a.v.i=value?1:0;return a;}
}
