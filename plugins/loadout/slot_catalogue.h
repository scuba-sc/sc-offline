// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sc_loadout.h"
#include "sco_content.h"
#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace sco::features::loadout::catalogue {
struct NamedSlot { const char* name; uint32_t slot; };
// Public logical targets only; compatibility and native ports belong to the provider.
inline constexpr NamedSlot kSlots[]{
    {"primary",SC_LOADOUT_SLOT_PRIMARY},{"sidearm",SC_LOADOUT_SLOT_SIDEARM},
    {"primary_optic",SC_LOADOUT_SLOT_PRIMARY_OPTIC},{"primary_barrel",SC_LOADOUT_SLOT_PRIMARY_BARREL},
    {"primary_underbarrel",SC_LOADOUT_SLOT_PRIMARY_UNDERBARREL},{"sidearm_optic",SC_LOADOUT_SLOT_SIDEARM_OPTIC},
    {"sidearm_barrel",SC_LOADOUT_SLOT_SIDEARM_BARREL},{"sidearm_underbarrel",SC_LOADOUT_SLOT_SIDEARM_UNDERBARREL},
    {"primary_tool_module",SC_LOADOUT_SLOT_PRIMARY_TOOL_MODULE},{"primary_tool_canister",SC_LOADOUT_SLOT_PRIMARY_TOOL_CANISTER},
    {"primary_tool_battery",SC_LOADOUT_SLOT_PRIMARY_TOOL_BATTERY},{"sidearm_tool_module",SC_LOADOUT_SLOT_SIDEARM_TOOL_MODULE},
    {"sidearm_tool_canister",SC_LOADOUT_SLOT_SIDEARM_TOOL_CANISTER},{"sidearm_tool_battery",SC_LOADOUT_SLOT_SIDEARM_TOOL_BATTERY},
};
struct Data {
    std::array<std::vector<std::string>,std::size(kSlots)> classes;
    uint32_t count=0;
};
inline std::string_view Trim(std::string_view value) {
    const auto first=value.find_first_not_of(" \t\r");
    return first==value.npos ? std::string_view{} : value.substr(first,value.find_last_not_of(" \t\r")-first+1);
}
inline bool Same(std::string_view a,std::string_view b) {
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i) {
        const auto lower=[](unsigned char c){return c>='A'&&c<='Z'?static_cast<unsigned char>(c+32):c;};
        if(lower(static_cast<unsigned char>(a[i]))!=lower(static_cast<unsigned char>(b[i])))return false;
    }
    return true;
}
// Display names from Alpha-Rome0's public GPL PR127, source 7ea9731b693bb9042ccbb721099ca88acb80d6f4.
inline std::string_view Alias(std::string_view value) {
    struct Name { const char* cls; const char* label; };
    static constexpr Name names[]{
        {"grin_multitool_01_mining","OreBit Mining Attachment"},
        {"grin_multitool_01_cutter","OxyTorch Cutter Attachment"},
        {"grin_multitool_01_tractorbeam","TruHold Tractor Beam Attachment"},
        {"grin_multitool_01_healing","LifeGuard Medical Attachment"},
        {"grin_multitool_01_salvage_repair","Cambio-Lite SRT Attachment"},
        {"grin_multitool_resource_salvage_repair_01","Cambio-Lite SRT Canister (empty)"},
        {"grin_multitool_resource_salvage_repair_01_filled","Cambio-Lite SRT Canister (filled)"},
    };
    for(const auto& name:names)if(Same(value,name.cls))return name.label;
    return {};
}
inline std::string Display(std::string_view value) {
    const auto alias=Alias(value);
    return alias.empty()?std::string(value):std::string(alias)+" ["+std::string(value)+"]";
}
inline bool Contains(std::string_view text,std::string_view query) {
    if(query.size()>text.size())return false;
    for(size_t i=0;i<=text.size()-query.size();++i)if(Same(text.substr(i,query.size()),query))return true;
    return false;
}
inline bool Matches(std::string_view value,std::string_view query) {
    return Contains(value,query)||Contains(Alias(value),query);
}
inline std::vector<int32_t> Rows(const std::vector<std::string>& classes,int32_t selected,std::string_view filter) {
    std::vector<int32_t> rows{0};
    for(size_t i=0;i<classes.size();++i)
        if(selected==static_cast<int32_t>(i+1)||Matches(classes[i],filter))rows.push_back(static_cast<int32_t>(i+1));
    return rows;
}
inline bool Class(std::string_view value) {
    if(value.empty()||value.size()>=SC_LOADOUT_ITEM_NAME_BYTES||Same(value,"none"))return false;
    for(unsigned char c:value)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'))return false;
    return true;
}
inline sco_result Parse(std::string_view bytes,Data& out) {
    if(bytes.size()>SCO_CONTENT_MAX_FILE_BYTES)return SCO_TOO_MANY;
    if(bytes.find('\0')!=bytes.npos)return SCO_BAD_ARG;
    Data next;std::array<bool,std::size(kSlots)> seen{};size_t section=std::size(kSlots);
    while(!bytes.empty()) {
        const auto end=bytes.find('\n');const auto raw=bytes.substr(0,end);
        bytes=end==bytes.npos?std::string_view{}:bytes.substr(end+1);
        if(raw.size()>255)return SCO_TOO_MANY;
        const auto line=Trim(raw.substr(0,raw.find('#')));if(line.empty())continue;
        if(line.front()=='[') {
            if(line.size()<3||line.back()!=']')return SCO_BAD_ARG;
            const auto name=line.substr(1,line.size()-2);section=std::size(kSlots);
            for(size_t i=0;i<std::size(kSlots);++i)if(name==kSlots[i].name)section=i;
            if(section==std::size(kSlots)||seen[section])return SCO_BAD_ARG;
            seen[section]=true;continue;
        }
        if(section==std::size(kSlots)||!Class(line))return SCO_BAD_ARG;
        auto& values=next.classes[section];
        if(values.size()==255||next.count==3000)return SCO_TOO_MANY;
        for(const auto& old:values)if(Same(old,line))return SCO_BAD_ARG;
        values.emplace_back(line);++next.count;
    }
    out=std::move(next);return SCO_OK;
}
inline sco_result Read(const sco_api* api,sco_plugin* self,Data& out) {
#define CATALOGUE_HAS(t,type,m) ((t)&&(t)->size>=offsetof(type,m)+sizeof((t)->m)&&(t)->m)
    const void* table=nullptr;
    if(!CATALOGUE_HAS(api,sco_api,query_service)||api->query_service(SCO_CONTENT_NAME,SCO_CONTENT_VERSION_1_0,&table)!=SCO_OK)return SCO_UNAVAILABLE;
    const auto* content=static_cast<const sco_content_v1*>(table);
    if(!CATALOGUE_HAS(content,sco_content_v1,item)||!CATALOGUE_HAS(content,sco_content_v1,read))return SCO_UNAVAILABLE;
    uint64_t id=0;uint32_t matches=0;
    for(uint32_t ordinal=0;;++ordinal) {
        sco_content_item item{};item.size=sizeof(item);
        const auto result=content->item(self,SCO_CONTENT_LIST,ordinal,&item);
        if(result==SCO_NOT_FOUND)break;
        if(result!=SCO_OK)return result;
        if(ordinal==SCO_CONTENT_MAX_ITEMS)return SCO_TOO_MANY;
        if(item.size<sizeof(item)||item.kind!=SCO_CONTENT_LIST||!item.id||
           !std::memchr(item.name,0,sizeof(item.name))||!std::memchr(item.pack,0,sizeof(item.pack)))return SCO_FAILED;
        if(!std::strcmp(item.name,"lists/loadout_slots.txt")){id=item.id;++matches;}
    }
    if(!matches)return SCO_NOT_FOUND;
    if(matches!=1)return SCO_BAD_ARG;
    uint32_t count=0;const auto asked=content->read(self,id,nullptr,&count);
    if(count>SCO_CONTENT_MAX_FILE_BYTES)return SCO_TOO_MANY;
    if((count&&asked!=SCO_TOO_MANY)||(!count&&asked!=SCO_OK))return asked==SCO_OK?SCO_FAILED:asked;
    std::string bytes(count,'\0');
    if(count){uint32_t copied=count;const auto result=content->read(self,id,bytes.data(),&copied);if(result!=SCO_OK)return result;if(copied!=count)return SCO_FAILED;}
    return Parse(bytes,out);
#undef CATALOGUE_HAS
}
}
