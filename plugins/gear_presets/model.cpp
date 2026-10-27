// SPDX-License-Identifier: GPL-3.0-only
#if __has_include(<scosdk/game/inventory_layout.hpp>)
#include "model.h"
#include <algorithm>
#include <cstring>

namespace sco::features::gear_presets {
namespace {
bool Utf8(std::string_view s) {
    if (s.empty() || s.size() > 80) return false;
    for (size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { if (c < 0x20 || c == 0x7f) return false; ++i; continue; }
        unsigned n = 0; uint32_t cp = 0, min = 0;
        if (c >= 0xc2 && c <= 0xdf) { n = 2; cp = c & 31u; min = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { n = 3; cp = c & 15u; min = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { n = 4; cp = c & 7u; min = 0x10000; }
        else return false;
        if (n > s.size() - i) return false;
        for (unsigned j = 1; j < n; ++j) { const auto x = static_cast<unsigned char>(s[i+j]); if ((x & 0xc0) != 0x80) return false; cp = (cp << 6) | (x & 63u); }
        if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
        i += n;
    }
    return true;
}
bool Key(std::string_view s, bool domain) {
    if (s.empty() || s.size() > 63) return false;
    for (unsigned char c : s) if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') &&
        c != '.' && c != '_' && c != '-' && !(!domain && c >= 'A' && c <= 'Z')) return false;
    return true;
}
bool Valid(const Preset& p) {
    if (!Utf8(p.name) || !Key(p.domain, true) || !Key(p.layout, false) || p.ports.size() > 128) return false;
    std::string previous;
    for (const auto& r : p.ports) {
        if (!Key(r.key, false) || (!previous.empty() && previous >= r.key) || r.role > 2 || r.state > 2 ||
            (r.flags & ~1u) || (r.state != 2 && (r.flags || r.category)) || (!(r.flags & 1u) && r.category)) return false;
        previous = r.key;
    }
    return true;
}
void Put32(std::string& s, uint32_t x) { for (unsigned i=0; i<4; ++i) s.push_back(static_cast<char>(x >> (i*8))); }
void PutString(std::string& s, std::string_view x) { s.push_back(static_cast<char>(x.size())); s.append(x); }
bool Take32(std::string_view s, size_t& pos, uint32_t& x) {
    if (pos > s.size() || s.size()-pos < 4) return false;
    x=0; for(unsigned i=0;i<4;++i) x |= uint32_t(static_cast<unsigned char>(s[pos++])) << (8*i); return true;
}
bool TakeString(std::string_view s,size_t& pos,std::string& x) {
    if (pos >= s.size()) return false;
    const auto n=static_cast<unsigned char>(s[pos++]);
    if (n > s.size()-pos) return false;
    x.assign(s.substr(pos,n)); pos+=n; return true;
}
}
bool Capture(std::string_view name, const sc_inventory_layout_snapshot& info,
             std::span<const sc_inventory_layout_entry> rows, Preset& out) {
    if (rows.empty() || rows.size() > 128 || info.size != sizeof(info) || info.flags != SC_INVENTORY_LAYOUT_COMPLETE ||
        !sc_inventory_layout_valid(&info, rows.data(), sizeof(rows[0]), static_cast<uint32_t>(rows.size()))) return false;
    Preset next; next.name=name; next.domain=info.identity_domain; next.layout=info.layout_key;
    for (uint32_t i=0;i<info.count;++i) {
        const auto& r=rows[i]; next.ports.push_back({r.port_key,r.role,r.state,r.flags & SC_INVENTORY_ENTRY_TYPE_KNOWN,r.raw_item_type});
    }
    std::sort(next.ports.begin(),next.ports.end(),[](const Port& a,const Port& b){return a.key < b.key;});
    if (!Valid(next)) return false;
    out=std::move(next); return true;
}
bool Encode(const Preset& p,std::string& out) {
    if (!Valid(p)) return false;
    std::string s="SGP1"; PutString(s,p.name); PutString(s,p.domain); PutString(s,p.layout); Put32(s,static_cast<uint32_t>(p.ports.size()));
    for (const auto& r:p.ports) { PutString(s,r.key); Put32(s,r.role); Put32(s,r.state); Put32(s,r.flags); Put32(s,r.category); }
    if (s.size()>16384) return false;
    out=std::move(s); return true;
}
bool Decode(std::string_view s,Preset& out) {
    if (s.size()>16384 || s.size()<4 || s.substr(0,4)!="SGP1") return false;
    size_t pos=4; uint32_t count=0; Preset p;
    if (!TakeString(s,pos,p.name) || !TakeString(s,pos,p.domain) || !TakeString(s,pos,p.layout) || !Take32(s,pos,count) || count>128) return false;
    for (uint32_t i=0;i<count;++i) {
        Port r;
        if (!TakeString(s,pos,r.key) || !Take32(s,pos,r.role) || !Take32(s,pos,r.state) ||
            !Take32(s,pos,r.flags) || !Take32(s,pos,r.category)) return false;
        p.ports.push_back(std::move(r));
    }
    if (pos!=s.size() || !Valid(p)) return false;
    out=std::move(p); return true;
}
bool Compare(const Preset& saved,const Preset& current,std::vector<Difference>& out) {
    if (!Valid(saved) || !Valid(current) || saved.domain!=current.domain || saved.layout!=current.layout || saved.ports.size()!=current.ports.size()) return false;
    for (size_t i=0;i<saved.ports.size();++i) if (saved.ports[i].key!=current.ports[i].key || saved.ports[i].role!=current.ports[i].role) return false;
    std::vector<Difference> result;
    for (size_t i=0;i<saved.ports.size();++i) {
        const auto& a=saved.ports[i]; const auto& b=current.ports[i];
        if (a.role==SC_INVENTORY_ROLE_UNKNOWN) { result.push_back({a.key,Advice::Unclassified,0,0}); continue; }
        if (a.role!=SC_INVENTORY_ROLE_EQUIPMENT) continue;
        Advice advice=Advice::Unknown;
        if (a.state==SC_INVENTORY_PORT_EMPTY && b.state==SC_INVENTORY_PORT_EMPTY) continue;
        if (a.state==SC_INVENTORY_PORT_EMPTY && b.state==SC_INVENTORY_PORT_OCCUPIED) advice=Advice::RemoveItem;
        else if (a.state==SC_INVENTORY_PORT_OCCUPIED && b.state==SC_INVENTORY_PORT_EMPTY && (a.flags&1u)) advice=Advice::EquipCategory;
        else if (a.state==SC_INVENTORY_PORT_OCCUPIED && b.state==SC_INVENTORY_PORT_OCCUPIED && (a.flags&1u) && (b.flags&1u))
            advice=a.category==b.category ? Advice::ExactClassUnknown : Advice::ReplaceCategory;
        result.push_back({a.key,advice,a.category,b.category});
    }
    out=std::move(result); return true;
}
}

#endif // SDK inventory layout is available
