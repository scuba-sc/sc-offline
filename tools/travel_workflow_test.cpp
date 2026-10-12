// SPDX-License-Identifier: GPL-3.0-only
// Synthetic public-provider fixtures only; no game data or private frames.
#include "../plugins/spatial/travel_model.h"
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
    assert(Relocate()==SCO_UNAVAILABLE); // unconditional even if a provider advertises mutation
    assert(!ParseSpot("spot nan 0 0 bad\n",again));
    std::puts("travel model: nested rotated poses, legacy format, partial/root refusal, expiry, 256/64 bounded scan, partial/null tails, G2 REFUSE: PASS");
}
