// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sco_api.h"
#include "sc_game_spatial.h"
#include "sc_spatial.h"
#include "sc_relocation.h"
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_set>
namespace sco::features::travel {
#define TRAVEL_FN(t, type, m) ((t) && (t)->size >= offsetof(type,m)+sizeof((t)->m) && (t)->m)
struct Pose { uint64_t zone=0; double pos[3]{}; double rot[4]{0,0,0,1}; };
struct ZoneSpot : Pose { std::string name; };
struct Spot { std::vector<ZoneSpot> z; };
struct Restoration { Pose pose; size_t level=0; bool partial=false,system_known=false; uint64_t system=0; };
inline bool Valid(const Pose& p) {
    double norm=0;
    for (double v:p.pos) if (!std::isfinite(v) || std::fabs(v)>=1e12) return false;
    for (double v:p.rot) { if (!std::isfinite(v)) return false; norm+=v*v; }
    return std::isfinite(norm) && norm>=1e-12;
}
inline bool RecordValid(const sc_game_zone& z) {
    return z.size>=sizeof(z) && z.zone_id && z.epoch && z.root_id &&
        !(z.flags&SC_GAME_ZONE_NAME_TRUNCATED) && z.name[0] && std::memchr(z.name,0,sizeof(z.name));
}
inline std::string SpotText(const Spot& s) {
    std::string out; char line[512];
    for (const auto& z:s.z) {
        std::snprintf(line,sizeof(line),"spot %.6f %.6f %.6f %s\n",z.pos[0],z.pos[1],z.pos[2],z.name.c_str()); out+=line;
        // Additive extension: old readers ignore pose lines; original spot syntax is unchanged.
        std::snprintf(line,sizeof(line),"pose %.17g %.17g %.17g %.17g\n",z.rot[0],z.rot[1],z.rot[2],z.rot[3]); out+=line;
    }
    return out;
}
inline bool ParseSpot(const std::string& text,Spot& out) {
    if (text.size()>65536) return false;
    Spot next; std::istringstream in(text); std::string line;
    while (std::getline(in,line)) {
        if (line.starts_with("spot ") && next.z.size()<12) {
            ZoneSpot z; std::istringstream fields(line.substr(5));
            if (!(fields>>z.pos[0]>>z.pos[1]>>z.pos[2])) continue;
            std::getline(fields>>std::ws,z.name);
            if (!z.name.empty() && z.name.size()<96 && Valid(z)) next.z.push_back(z);
        } else if (line.starts_with("pose ") && !next.z.empty()) {
            auto z=next.z.back(); std::istringstream fields(line.substr(5));
            if (!(fields>>z.rot[0]>>z.rot[1]>>z.rot[2]>>z.rot[3]) || !Valid(z)) return false;
            const double n=std::sqrt(z.rot[0]*z.rot[0]+z.rot[1]*z.rot[1]+z.rot[2]*z.rot[2]+z.rot[3]*z.rot[3]);
            for (double& v:z.rot) v/=n;
            next.z.back()=z;
        }
    }
    if (next.z.empty()) return false;
    out=std::move(next); return true;
}
inline sco_result Capture(const sc_spatial_v1* old,const sc_game_spatial_v1* sp,Spot& out) {
    if (!TRAVEL_FN(old,sc_spatial_v1,player_pose) || !TRAVEL_FN(sp,sc_game_spatial_v1,zone_record) || !TRAVEL_FN(sp,sc_game_spatial_v1,pose_to_zone)) return SCO_UNAVAILABLE;
    Pose player;
    if (!old->player_pose(player.pos,player.rot,&player.zone) || !player.zone || !Valid(player)) return SCO_UNAVAILABLE;
    std::vector<sc_game_zone> chain; std::unordered_set<uint64_t> seen; uint64_t id=player.zone,epoch=0;
    while (id) {
        if (chain.size()==12 || !seen.insert(id).second) return SCO_UNAVAILABLE;
        sc_game_zone z{}; z.size=sizeof(z);
        if (!sp->zone_record(id,&z) || !RecordValid(z) || z.zone_id!=id || (epoch && epoch!=z.epoch)) return SCO_UNAVAILABLE;
        epoch=z.epoch; chain.push_back(z); id=z.parent_id;
    }
    Spot next;
    for (const auto& record:chain) {
        ZoneSpot z; z.zone=record.zone_id; z.name=record.name;
        if (!sp->pose_to_zone(player.zone,z.zone,player.pos,player.rot,z.pos,z.rot) || !Valid(z)) return SCO_UNAVAILABLE;
        next.z.push_back(z);
    }
    sc_game_zone final{}; final.size=sizeof(final);
    if (!sp->zone_record(player.zone,&final) || final.epoch!=epoch) return SCO_UNAVAILABLE;
    out=std::move(next); return SCO_OK;
}
inline sco_result Resolve(const sc_game_spatial_v1* sp,const Spot& spot,Restoration& out) {
    if (!TRAVEL_FN(sp,sc_game_spatial_v1,find_zone) || !TRAVEL_FN(sp,sc_game_spatial_v1,zone_record)) return SCO_UNAVAILABLE;
    for (size_t i=0;i<spot.z.size() && i<12;++i) {
        sc_game_zone z{}; z.size=sizeof(z);
        if (!sp->find_zone(spot.z[i].name.c_str(),&z)) continue;
        if (!RecordValid(z) || (i && !z.parent_id)) return SCO_UNAVAILABLE;
        sc_game_zone current{}; current.size=sizeof(current);
        if (!sp->zone_record(z.zone_id,&current) || current.epoch!=z.epoch || current.parent_id!=z.parent_id || std::strcmp(current.name,z.name)) return SCO_UNAVAILABLE;
        Restoration r; r.pose=spot.z[i]; r.pose.zone=z.zone_id; r.level=i; r.partial=i!=0;
        r.system_known=z.system_id && !(z.flags&SC_GAME_ZONE_SYSTEM_UNKNOWN); r.system=z.system_id;
        if (!Valid(r.pose)) return SCO_BAD_ARG;
        out=r; return SCO_OK;
    }
    return SCO_UNAVAILABLE;
}
inline sco_result Scan(const sc_game_spatial_v1* sp,std::vector<sc_game_zone>& out) {
    if (!TRAVEL_FN(sp,sc_game_spatial_v1,list_zones)) return SCO_UNAVAILABLE;
    std::vector<sc_game_zone> next; uint64_t epoch=0; uint32_t offset=0; std::unordered_set<uint64_t> ids;
    for (uint32_t page=0;page<4;++page) {
        sc_game_zone slots[64]{}; for (auto& z:slots) z.size=sizeof(z);
        uint32_t n=0,more=0; uint64_t got=0;
        if (!sp->list_zones(epoch,offset,slots,sizeof(slots[0]),64,&n,&more,&got) || !got || (epoch && epoch!=got) || n>64 || n+offset>256 || more>256-offset-n || (more && !n)) return SCO_UNAVAILABLE;
        epoch=got;
        for (uint32_t i=0;i<n;++i) {
            if (!RecordValid(slots[i]) || slots[i].epoch!=epoch || !ids.insert(slots[i].zone_id).second) return SCO_UNAVAILABLE;
            next.push_back(slots[i]);
        }
        offset+=n;
        if (!more) { out=std::move(next); return SCO_OK; }
    }
    return SCO_UNAVAILABLE;
}
// game.relocation 1.0 (sc_relocation.h): one request per move, tracked by ticket across ticks.
inline constexpr uint32_t relocation_timeout_ms=30000;
inline constexpr const char* unavailable="Player relocation unavailable: game.relocation is off (needs a host on sco-core main e616ed6 or later, offline admission, a live player and a known star system); not moved";
inline constexpr const char* switched_off="Moving the player is off in this plugin's settings (relocation = false); not moved";
// Fresh capability and full member extents before every request; ticket unchanged on refusal.
inline sco_result Relocate(const sco_api* api,const sc_relocation_v1* service,sco_plugin* self,const Pose& target,uint64_t& ticket) {
    if (!TRAVEL_FN(api,sco_api,has) || !api->has(SC_RELOCATION_NAME)) return SCO_UNAVAILABLE;
    if (!TRAVEL_FN(service,sc_relocation_v1,request) || !TRAVEL_FN(service,sc_relocation_v1,status)) return SCO_UNAVAILABLE;
    if (!target.zone || !Valid(target)) return SCO_BAD_ARG;
    sc_relocation_request q{}; q.size=sizeof(q); q.timeout_ms=relocation_timeout_ms; q.zone_id=target.zone;
    for (int i=0;i<3;++i) q.pos[i]=target.pos[i];
    for (int i=0;i<4;++i) q.rot_xyzw[i]=target.rot[i];
    uint64_t next=0; const auto r=service->request(self,&q,&next);
    if (r!=SCO_OK) return r;
    if (!next) return SCO_FAILED;
    ticket=next; return SCO_OK;
}
inline sco_result Poll(const sc_relocation_v1* service,sco_plugin* self,uint64_t ticket,sc_relocation_status& out) {
    if (!ticket || !TRAVEL_FN(service,sc_relocation_v1,status)) return SCO_UNAVAILABLE;
    sc_relocation_status s{}; s.size=sizeof(s);
    const auto r=service->status(self,ticket,&s);
    if (r!=SCO_OK) return r;
    if (s.size<sizeof(s) || s.ticket!=ticket || s.applied>1 || s.state<SC_RELOCATION_PENDING || s.state>SC_RELOCATION_FAILED) return SCO_FAILED;
    out=s; return SCO_OK;
}
inline const char* ReasonText(uint32_t reason) {
    switch (reason) {
    case SC_RELOCATION_REASON_TIMEOUT: return "timed out";
    case SC_RELOCATION_REASON_SESSION: return "session, player or star system changed";
    case SC_RELOCATION_REASON_SAFETY: return "offline admission or relocation switch no longer allows it";
    case SC_RELOCATION_REASON_SYSTEM: return "destination is not in your current star system";
    case SC_RELOCATION_REASON_TRANSPORT: return "the game did not start the move";
    case SC_RELOCATION_REASON_FAULT: return "relocation provider fault";
    case SC_RELOCATION_REASON_NONE: return "no reason given";
    default: return "unknown reason";
    }
}
}
