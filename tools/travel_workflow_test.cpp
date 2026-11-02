// SPDX-License-Identifier: GPL-3.0-only
// Synthetic public-provider fixtures only; no game data or private frames.
#include "../plugins/spatial/travel_model.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace sco::features::travel;
static uint64_t epoch = 7;
static bool expire = false;
static int pages = 0, poses = 0;
static int Record(uint64_t id, sc_game_zone* z) {
    if (id < 1 || id > 3) return 0;
    *z = {sizeof(*z), SC_GAME_ZONE_SYSTEM_UNKNOWN, epoch, id, id == 3 ? 0u : id+1, 3, 0, {}};
    std::snprintf(z->name,sizeof(z->name),"zone%llu",static_cast<unsigned long long>(id));
    return 1;
}
static int Find(const char* name, sc_game_zone* z) {
    for (uint64_t id=1;id<=3;++id) { sc_game_zone x{sizeof(x)}; Record(id,&x); if (!std::strcmp(name,x.name)) { *z=x; return 1; } }
    return 0;
}
static int Convert(uint64_t from,uint64_t to,const double* p,const double* q,double* op,double* oq) {
    ++poses;
    assert(from == 1 && to >= 1 && to <= 3);
    // Two nested 90 degree synthetic frames, preserving the full quaternion.
    const double a = (to-1)*1.5707963267948966;
    op[0]=std::cos(a)*p[0]-std::sin(a)*p[1]; op[1]=std::sin(a)*p[0]+std::cos(a)*p[1]; op[2]=p[2];
    oq[0]=q[0]; oq[1]=q[1]; oq[2]=std::sin(a/2)*q[3]+std::cos(a/2)*q[2]; oq[3]=std::cos(a/2)*q[3]-std::sin(a/2)*q[2];
    if (expire) ++epoch;
    return 1;
}
static bool cap=false; static int requests=0; static uint32_t state=SC_RELOCATION_PENDING, reason=0;
static sc_relocation_request last{};
static int HasCap(const char* name) { return cap && !std::strcmp(name,SC_RELOCATION_NAME); }
static sco_result Request(sco_plugin*,const sc_relocation_request* q,uint64_t* t) {
    ++requests; assert(q && q->size==sizeof(*q) && q->timeout_ms>=1 && q->timeout_ms<=60000); last=*q; *t=42; return SCO_OK;
}
static sco_result Cancel(sco_plugin*,uint64_t) { return SCO_OK; }
static sco_result Status(sco_plugin*,uint64_t t,sc_relocation_status* s) {
    if (t!=42) return SCO_NOT_FOUND;
    assert(s->size==sizeof(*s)); s->state=state; s->reason=reason; s->applied=state==SC_RELOCATION_COMPLETE; s->ticket=t; return SCO_OK;
}
static int Player(double* p,double* q,uint64_t* id) { p[0]=1;p[1]=0;p[2]=2;q[0]=0;q[1]=0;q[2]=0;q[3]=1;*id=1;return 1; }
static int List(uint64_t e,uint32_t offset,sc_game_zone* z,uint32_t stride,uint32_t cap,uint32_t* n,uint32_t* more,uint64_t* out) {
    ++pages; assert(cap <= 64 && stride == sizeof(*z));
    if (e && e != epoch) return 0;
    *n=cap; *more=256-offset-cap; *out=epoch;
    for (uint32_t i=0;i<cap;++i) { z[i]={sizeof(*z),SC_GAME_ZONE_SYSTEM_UNKNOWN,epoch,offset+i+1,0,offset+i+1,0,{}}; std::snprintf(z[i].name,96,"observed%u",offset+i); }
    return 1;
}
int main() {
    sc_game_spatial_v1 sp{sizeof(sp),Convert,Record,Find,List};
    sc_spatial_v1 old{}; old.size=sizeof(old); old.player_pose=Player;
    Spot s;
    assert(Capture(&old,&sp,s) == SCO_OK);
    assert(s.z.size()==3 && poses==3);
    assert(std::fabs(s.z[1].pos[1]-1)<1e-9 && std::fabs(s.z[2].rot[2]-1)<1e-9);
    const auto text=SpotText(s); Spot again;
    assert(ParseSpot(text,again) && again.z.size()==3 && std::fabs(again.z[2].rot[2]-1)<1e-9);
    // ParseSpot also reads quick-save files and SQL values directly.
    for (const auto* input:{"spot 1 2 3 zone1\npose 0 0 0 1\n", "spot 1 2 3 zone1\r\npose 0 0 0 1\r\n", "spot 1 2 3 zone1\r\npose 0 0 0 1\n"}) {
        Spot parsed; assert(ParseSpot(input,parsed) && parsed.z.size()==1 && parsed.z[0].name=="zone1");
    }
    Spot controls;
    assert(ParseSpot("spot 1 2 3 zone\rname\n",controls) && controls.z[0].name=="zone\rname");
    assert(ParseSpot("spot 1 2 3 zone1\r\r\n",controls) && controls.z[0].name=="zone1\r");
    assert(ParseSpot("spot 1 2 3 zone1\r",controls) && controls.z[0].name=="zone1\r");
    assert(ParseSpot("spot 1 2 3 zone1",controls) && controls.z[0].name=="zone1");
    assert(ParseSpot("spot 1 2 3 "+std::string(95,'z')+"\r\n",controls) && controls.z[0].name.size()==95);
    const auto bounded=SpotText(controls);
    assert(!ParseSpot("spot 1 2 3 "+std::string(96,'z')+"\r\n",controls) && SpotText(controls)==bounded);
    assert(!ParseSpot(std::string(65537,'x'),controls) && SpotText(controls)==bounded);
    std::string manySpots;for(int i=0;i<13;++i)manySpots+="spot 1 2 3 zone1\r\n";
    assert(ParseSpot(manySpots,controls) && controls.z.size()==12);
    const auto unchanged=SpotText(controls);
    assert(!ParseSpot("spot 1 2 3 zone1\r\npose 0 0 0 0\r\n",controls) && SpotText(controls)==unchanged);
    Spot legacy; assert(ParseSpot("spot 1 2 3 vanished\nspot 4 5 6 zone2\nspot 7 8 9 zone3\n",legacy));
    Restoration r; assert(Resolve(&sp,legacy,r)==SCO_OK && r.level==1 && r.partial && r.pose.zone==2);
    assert(!r.system_known); // names/root never certify system identity or authorize relocation
    legacy.z.erase(legacy.z.begin()+1); assert(Resolve(&sp,legacy,r)==SCO_UNAVAILABLE); // root-only fallback
    const auto before=SpotText(s); expire=true;
    assert(Capture(&old,&sp,s)==SCO_UNAVAILABLE && SpotText(s)==before); expire=false;
    std::vector<sc_game_zone> records; assert(Scan(&sp,records)==SCO_OK && records.size()==256 && pages==4);
    sp.size=offsetof(sc_game_spatial_v1,list_zones)+sizeof(sp.list_zones)-1;
    assert(Scan(&sp,records)==SCO_UNAVAILABLE && records.size()==256);
    sp.size=sizeof(sp); sp.find_zone=nullptr; assert(Resolve(&sp,again,r)==SCO_UNAVAILABLE);
    // game.relocation: fresh capability, full member extents, valid pose, then one request and status polling.
    sco_api api{}; api.size=sizeof(api); api.has=HasCap;
    sc_relocation_v1 rel{sizeof(rel),Request,Cancel,Status};
    uint64_t ticket=0;
    assert(Relocate(&api,&rel,nullptr,s.z[0],ticket)==SCO_UNAVAILABLE && !requests && !ticket); // capability off
    cap=true; rel.size=offsetof(sc_relocation_v1,status)+sizeof(rel.status)-1;
    assert(Relocate(&api,&rel,nullptr,s.z[0],ticket)==SCO_UNAVAILABLE && !requests); rel.size=sizeof(rel); // partial table
    assert(Relocate(&api,nullptr,nullptr,s.z[0],ticket)==SCO_UNAVAILABLE && !requests); // service absent
    Pose bad=s.z[0]; bad.zone=0; assert(Relocate(&api,&rel,nullptr,bad,ticket)==SCO_BAD_ARG && !requests);
    bad=s.z[0]; bad.rot[0]=bad.rot[1]=bad.rot[2]=bad.rot[3]=0; assert(Relocate(&api,&rel,nullptr,bad,ticket)==SCO_BAD_ARG && !requests);
    assert(Relocate(&api,&rel,nullptr,s.z[1],ticket)==SCO_OK && ticket==42 && requests==1);
    assert(last.zone_id==s.z[1].zone && last.pos[0]==s.z[1].pos[0] && last.pos[1]==s.z[1].pos[1] && last.rot_xyzw[2]==s.z[1].rot[2] && last.rot_xyzw[3]==s.z[1].rot[3]);
    sc_relocation_status st{}; assert(Poll(&rel,nullptr,ticket,st)==SCO_OK && st.state==SC_RELOCATION_PENDING);
    state=SC_RELOCATION_COMPLETE; assert(Poll(&rel,nullptr,ticket,st)==SCO_OK && st.state==SC_RELOCATION_COMPLETE && st.applied);
    state=SC_RELOCATION_FAILED; reason=SC_RELOCATION_REASON_SYSTEM;
    assert(Poll(&rel,nullptr,ticket,st)==SCO_OK && st.state==SC_RELOCATION_FAILED && st.reason==SC_RELOCATION_REASON_SYSTEM && ReasonText(st.reason)[0]);
    state=9; assert(Poll(&rel,nullptr,ticket,st)==SCO_FAILED && st.state==SC_RELOCATION_FAILED); // unknown state: output unchanged
    assert(Poll(&rel,nullptr,7,st)==SCO_NOT_FOUND && Poll(&rel,nullptr,0,st)==SCO_UNAVAILABLE);
    cap=false; assert(Relocate(&api,&rel,nullptr,s.z[1],ticket)==SCO_UNAVAILABLE && requests==1); // re-checked per request
    assert(!ParseSpot("spot nan 0 0 bad\n",again));
    std::puts("travel model: nested rotated poses, legacy format, partial/root refusal, expiry, 256/64 bounded scan, partial/null tails, game.relocation request/status gating: PASS");
}
