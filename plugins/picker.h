// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <sco_content.h>
#include <sco_widgets.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace sco::picker {
template<class T> const T* Open(const sco_api* api,const char* name) {
    const void* p=nullptr;
    return api && api->size >= offsetof(sco_api,query_service)+sizeof(api->query_service) && api->query_service &&
        api->query_service(name,0x10000u,&p)==SCO_OK ? static_cast<const T*>(p) : nullptr;
}
#define PICKER_HAS(t,type,m) ((t) && (t)->size >= offsetof(type,m)+sizeof((t)->m) && (t)->m)

inline unsigned char FoldAscii(unsigned char c) noexcept {
    return c>='A' && c<='Z' ? static_cast<unsigned char>(c+('a'-'A')) : c;
}
inline bool EqualAsciiInsensitive(std::string_view a,std::string_view b) noexcept {
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i)
        if(FoldAscii(static_cast<unsigned char>(a[i]))!=FoldAscii(static_cast<unsigned char>(b[i])))return false;
    return true;
}
inline bool ContainsAsciiInsensitive(std::string_view text,std::string_view query) noexcept {
    if(query.size()>text.size())return false;
    for(size_t i=0;i<=text.size()-query.size();++i)
        if(EqualAsciiInsensitive(text.substr(i,query.size()),query))return true;
    return false;
}
inline bool MatchesEveryWordAsciiInsensitive(std::string_view text,std::string_view query) noexcept {
    size_t begin=0;
    while(begin<query.size()) {
        const auto end=query.find(' ',begin);
        const auto word=query.substr(begin,end==query.npos?query.size()-begin:end-begin);
        if(!word.empty() && !ContainsAsciiInsensitive(text,word))return false;
        if(end==query.npos)break;
        begin=end+1;
    }
    return true;
}
inline std::string_view Trim(std::string_view value) noexcept {
    const auto first=value.find_first_not_of(" \t\r");
    return first==value.npos ? std::string_view{} : value.substr(first,value.find_last_not_of(" \t\r")-first+1);
}
inline bool ClassId(std::string_view value,size_t maxLength) noexcept {
    if(value.empty() || value.size()>maxLength)return false;
    for(unsigned char c:value)
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c=='-'))return false;
    return true;
}
// Alias values become public widget labels. Validate UTF-8 and reject controls
// before displaying human-authored text copied from an optional data pack.
inline bool PrintableUtf8(std::string_view text) noexcept {
    size_t i=0;
    while(i<text.size()) {
        const auto first=static_cast<unsigned char>(text[i]);
        if(first<0x80) {
            if(first<0x20 || first==0x7f)return false;
            ++i;
            continue;
        }
        size_t count=0;
        if(first>=0xc2 && first<=0xdf)count=2;
        else if(first>=0xe0 && first<=0xef)count=3;
        else if(first>=0xf0 && first<=0xf4)count=4;
        else return false;
        if(count>text.size()-i)return false;
        const auto second=static_cast<unsigned char>(text[i+1]);
        if((second&0xc0)!=0x80 || (first==0xe0 && second<0xa0) ||
           (first==0xed && second>=0xa0) || (first==0xf0 && second<0x90) ||
           (first==0xf4 && second>=0x90))return false;
        for(size_t j=2;j<count;++j)
            if((static_cast<unsigned char>(text[i+j])&0xc0)!=0x80)return false;
        uint32_t codepoint=first&static_cast<unsigned char>((1u<<(7-count))-1u);
        for(size_t j=1;j<count;++j)
            codepoint=(codepoint<<6)|(static_cast<unsigned char>(text[i+j])&0x3f);
        // Keep terminal/widget control and bidirectional formatting characters
        // out of labels while allowing ordinary authored UTF-8 text.
        if((codepoint>=0x80 && codepoint<=0x9f) || codepoint==0x00ad || codepoint==0x061c ||
           codepoint==0x200b || codepoint==0x200e || codepoint==0x200f || codepoint==0x2028 || codepoint==0x2029 ||
           (codepoint>=0x202a && codepoint<=0x202e) || (codepoint>=0x2060 && codepoint<=0x206f) ||
           codepoint==0xfeff || (codepoint>=0xfff9 && codepoint<=0xfffb))return false;
        i+=count;
    }
    return !text.empty();
}

struct Choice {
    std::string value,label,pack;
    uint64_t sourceItem=0;
    uint32_t sourceLine=0;
};
inline bool SetAliasLabel(Choice& choice,std::string_view alias) {
    std::string label(alias);
    label+=" [";label+=choice.value;
    label+="] [";label+=choice.pack;label+=']';
    if(label.size()>SCO_WIDGETS_MAX_TEXT)return false;
    choice.label=std::move(label);
    return true;
}
struct AliasResult {
    std::map<std::string,std::string> values;
    std::set<std::string> conflicts;
    size_t files=0;
    bool rejected=false;
};
struct Catalog {
    std::vector<Choice> choices;
    std::vector<size_t> visible;
    std::vector<const char*> labels;
    std::string filter;
    uint32_t page=0;
    char status[256]{};
    static constexpr size_t pageSize=64, maxChoices=8192;
    static constexpr size_t maxAliasBytes=59, maxAliasLineBytes=255, maxAliasRows=8192;
    static constexpr size_t maxAliasFiles=64, maxAliasTotalBytes=1024u*1024u;

    static std::string AliasName(const char* name) {
        if(!name)return {};
        std::string result(name);
        const auto ext=result.rfind(".txt");
        if(ext==std::string::npos || ext+4!=result.size())return {};
        result.insert(ext,".aliases");
        return result;
    }
    static bool ParseAliasFile(std::string_view bytes,size_t maxLength,std::map<std::string,std::string>& out) {
        if(bytes.size()>maxAliasTotalBytes || bytes.find('\0')!=bytes.npos)return false;
        std::map<std::string,std::string> parsed;
        size_t start=0,lineNumber=0,rows=0;
        while(start<bytes.size()) {
            ++lineNumber;
            const auto end=bytes.find('\n',start);
            auto raw=bytes.substr(start,end==bytes.npos?bytes.size()-start:end-start);
            start=end==bytes.npos?bytes.size():end+1;
            if(raw.size()>maxAliasLineBytes)return false;
            if(lineNumber==1 && raw.size()>=3 && static_cast<unsigned char>(raw[0])==0xef &&
               static_cast<unsigned char>(raw[1])==0xbb && static_cast<unsigned char>(raw[2])==0xbf)
                raw.remove_prefix(3);
            const auto line=Trim(raw);
            if(line.empty() || line.front()=='#')continue;
            const auto tab=line.find('\t');
            if(tab==line.npos || line.find('\t',tab+1)!=line.npos)return false;
            const auto id=Trim(line.substr(0,tab));
            const auto label=Trim(line.substr(tab+1));
            if(!ClassId(id,maxLength) || label.empty() || label.size()>maxAliasBytes || !PrintableUtf8(label))return false;
            if(!parsed.emplace(std::string(id),std::string(label)).second)return false;
            if(++rows>maxAliasRows)return false;
        }
        out=std::move(parsed);
        return true;
    }
    static AliasResult ReadAliases(const sco_content_v1* content,sco_plugin* self,const char* name,size_t maxLength) {
        AliasResult result;
        const auto aliasName=AliasName(name);
        if(aliasName.empty())return result;
        std::map<std::string,std::string> values;
        std::set<std::string> conflicts;
        size_t totalBytes=0;
        bool ended=false;
        for(uint32_t ordinal=0;ordinal<SCO_CONTENT_MAX_ITEMS;++ordinal) {
            sco_content_item item{};item.size=sizeof(item);
            const auto listed=content->item(self,SCO_CONTENT_LIST,ordinal,&item);
            if(listed==SCO_NOT_FOUND){ended=true;break;}
            if(listed!=SCO_OK || item.size<sizeof(item) || item.kind!=SCO_CONTENT_LIST || !item.id ||
               !std::memchr(item.name,0,sizeof(item.name)) || !std::memchr(item.pack,0,sizeof(item.pack))) {
                result.rejected=true;break;
            }
            if(std::strcmp(item.name,aliasName.c_str()))continue;
            if(++result.files>maxAliasFiles){result.rejected=true;break;}
            uint32_t bytes=0;
            auto read=content->read(self,item.id,nullptr,&bytes);
            if((read!=SCO_OK && read!=SCO_TOO_MANY) || bytes>maxAliasTotalBytes ||
               bytes>maxAliasTotalBytes-totalBytes) {result.rejected=true;break;}
            std::string data(bytes,'\0');
            uint32_t copied=bytes;
            read=content->read(self,item.id,bytes?data.data():nullptr,&copied);
            if(read!=SCO_OK || copied!=bytes) {result.rejected=true;break;}
            totalBytes+=bytes;
            std::map<std::string,std::string> file;
            if(!ParseAliasFile(data,maxLength,file) || file.size()>maxAliasRows) {result.rejected=true;break;}
            for(const auto& entry:file) {
                if(conflicts.count(entry.first))continue;
                const auto inserted=values.emplace(entry.first,entry.second);
                if(!inserted.second && inserted.first->second!=entry.second) {
                    values.erase(inserted.first);
                    conflicts.insert(entry.first);
                }
                if(values.size()+conflicts.size()>maxAliasRows) {result.rejected=true;break;}
            }
            if(result.rejected)break;
        }
        if(!ended)result.rejected=true; // Incomplete enumeration cannot rule out another conflicting alias pack.
        if(result.rejected) {
            result.values.clear();result.conflicts.clear();
            return result;
        }
        result.values=std::move(values);
        result.conflicts=std::move(conflicts);
        return result;
    }

    void Load(const sco_api* api,sco_plugin* self,const char* name,size_t maxLength) {
        choices.clear();visible.clear();labels.clear();page=0;
        const auto* c=Open<sco_content_v1>(api,SCO_CONTENT_NAME);
        if(!PICKER_HAS(c,sco_content_v1,item) || !PICKER_HAS(c,sco_content_v1,read)) {
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
            size_t start=0;uint32_t sourceLine=0;
            while(start<data.size() && choices.size()<maxChoices) {
                ++sourceLine;
                auto end=data.find('\n',start);if(end==std::string::npos)end=data.size();
                std::string line=data.substr(start,end-start);start=end+1;
                auto comment=line.find('#');if(comment!=std::string::npos)line.resize(comment);
                auto first=line.find_first_not_of(" \t\r");if(first==std::string::npos)continue;
                auto last=line.find_last_not_of(" \t\r");line=line.substr(first,last-first+1);
                if(line.size()>maxLength || !ClassId(line,maxLength)) {++rejected;continue;}
                choices.push_back({line,line+" ["+item.pack+"]",item.pack,item.id,sourceLine}); // Keep every source row and exact class ID.
            }
        }
        AliasResult aliases;
        if(result==SCO_OK)aliases=ReadAliases(c,self,name,maxLength);
        size_t displayedAliases=0;
        if(!aliases.rejected) {
            for(auto& choice:choices) {
                const auto alias=aliases.values.find(choice.value);
                if(alias==aliases.values.end())continue;
                if(SetAliasLabel(choice,alias->second))++displayedAliases;
            }
        }
        std::snprintf(status,sizeof(status),"%zu catalog rows; skipped=%zu; result=%u%s; base files unchanged",
                      choices.size(),rejected,static_cast<unsigned>(result),choices.size()==maxChoices?" (catalog limit reached)":"");
        if(aliases.rejected) {
            const auto used=std::strlen(status);
            std::snprintf(status+used,sizeof(status)-used,"; optional display aliases ignored (invalid/incomplete)");
        } else if(aliases.files) {
            const auto used=std::strlen(status);
            std::snprintf(status+used,sizeof(status)-used,"; aliases shown=%zu; ambiguous=%zu",
                          displayedAliases,aliases.conflicts.size());
        }
    }
    void View() {
        visible.clear();labels.clear();
        for(size_t i=0;i<choices.size();++i)
            if(MatchesEveryWordAsciiInsensitive(choices[i].label,filter))visible.push_back(i);
        if(static_cast<size_t>(page)*pageSize>=visible.size())page=0;
        size_t begin=static_cast<size_t>(page)*pageSize,end=std::min(begin+pageSize,visible.size());
        for(size_t i=begin;i<end;++i)labels.push_back(choices[visible[i]].label.c_str());
    }
    const Choice* SelectChoice(int32_t index) const {
        if(index<0 || static_cast<size_t>(index)>=labels.size())return nullptr;
        return &choices[visible[static_cast<size_t>(page)*pageSize+static_cast<size_t>(index)]];
    }
    const char* Select(int32_t index) const {
        const auto* choice=SelectChoice(index);
        return choice?choice->value.c_str():nullptr;
    }
    void Next(){if((static_cast<size_t>(page)+1)*pageSize<visible.size())++page;}
    void Previous(){if(page)--page;}
};
inline sco_widget_node Node(uint32_t kind,uint32_t id,const char* label,uint32_t flags=0) {
    sco_widget_node n{};n.size=sizeof(n);n.id=id;n.kind=kind;n.label=label;n.flags=flags;return n;
}
inline bool Event(const sco_widget_event* e,uint32_t kind) {return e && e->size>=sizeof(*e) && e->kind==kind;}
inline bool Text(const char* text,size_t limit) {if(!text)return false;for(size_t i=0;i<=limit;++i)if(!text[i])return true;return false;}
inline sco_arg String(const char* value) {sco_arg a{};a.type=SCO_ARG_STRING;a.v.s=value;return a;}
inline sco_arg Int(int64_t value) {sco_arg a{};a.type=SCO_ARG_INT;a.v.i=value;return a;}
inline sco_arg Float(double value) {sco_arg a{};a.type=SCO_ARG_FLOAT;a.v.f=value;return a;}
inline sco_arg Bool(bool value) {sco_arg a{};a.type=SCO_ARG_BOOL;a.v.i=value?1:0;return a;}
}
