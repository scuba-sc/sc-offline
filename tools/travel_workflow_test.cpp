// SPDX-License-Identifier: GPL-3.0-only
// Synthetic public-provider fixtures only; no game data or private frames.
#define TRAVEL_TELEPORT
#include "../plugins/spatial/travel_plugin.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
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
static sco_result RelRequestOld(sco_plugin*,const sc_relocation_request* q,uint64_t* t) {
    ++requests; assert(q && q->size==sizeof(*q) && q->timeout_ms>=1 && q->timeout_ms<=60000); last=*q; *t=42; return SCO_OK;
}
static sco_result RelCancelOld(sco_plugin*,uint64_t) { return SCO_OK; }
static sco_result RelStatusOld(sco_plugin*,uint64_t t,sc_relocation_status* s) {
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
    sc_relocation_v1 rel{sizeof(rel),RelRequestOld,RelCancelOld,RelStatusOld};
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
    // Already there: the player (1,0,2 in zone1) is within 2 m of the saved spot, so no move is requested.
    sp.size=sizeof(sp); sp.find_zone=Find; sp.zone_record=Record; sp.pose_to_zone=Convert; Restoration nearby;
    nearby.pose.zone=1; nearby.pose.pos[0]=1.5; nearby.pose.pos[1]=0; nearby.pose.pos[2]=2; assert(AlreadyThere(&old,&sp,nearby));
    nearby.pose.pos[0]=2.9; assert(AlreadyThere(&old,&sp,nearby)); nearby.pose.pos[0]=3.1; assert(!AlreadyThere(&old,&sp,nearby));
    nearby.pose.zone=2; nearby.pose.pos[0]=0; nearby.pose.pos[1]=2; nearby.pose.pos[2]=2; assert(AlreadyThere(&old,&sp,nearby)); // other zone frame
    nearby.pose.pos[1]=5; assert(!AlreadyThere(&old,&sp,nearby));
    nearby.pose.zone=0; assert(!AlreadyThere(&old,&sp,nearby)); nearby.pose.zone=1;
    assert(!AlreadyThere(nullptr,&sp,nearby) && !AlreadyThere(&old,nullptr,nearby)); // missing service: move proceeds as before
    // Core can report a timeout after the move really happened. Applied plus a pose within 2 m of the target is arrival.
    {
        Pose t; t.zone=1; t.pos[0]=2.5; t.pos[1]=0; t.pos[2]=2; // player is at 1,0,2 in zone1: 1.5 m away
        assert(ArrivedDespiteReport(&old,&sp,t,SC_RELOCATION_FAILED,1));
        assert(!ArrivedDespiteReport(&old,&sp,t,SC_RELOCATION_FAILED,0)); // never applied: still a failure
        assert(!ArrivedDespiteReport(&old,&sp,t,SC_RELOCATION_COMPLETE,1) && !ArrivedDespiteReport(&old,&sp,t,SC_RELOCATION_CANCELLED,1));
        assert(!ArrivedDespiteReport(&old,&sp,t,SC_RELOCATION_PENDING,1));
        t.pos[0]=3.5; assert(!ArrivedDespiteReport(&old,&sp,t,SC_RELOCATION_FAILED,1)); // 2.5 m away: real failure
        t.pos[0]=2.9; assert(ArrivedDespiteReport(&old,&sp,t,SC_RELOCATION_FAILED,1)); // 1.9 m: arrived
        t.zone=2; t.pos[0]=0; t.pos[1]=2; t.pos[2]=2; assert(ArrivedDespiteReport(&old,&sp,t,SC_RELOCATION_FAILED,1)); // other zone frame
        t.pos[1]=9; assert(!ArrivedDespiteReport(&old,&sp,t,SC_RELOCATION_FAILED,1));
        assert(!ArrivedDespiteReport(nullptr,&sp,t,SC_RELOCATION_FAILED,1) && !ArrivedDespiteReport(&old,nullptr,t,SC_RELOCATION_FAILED,1)); // unreadable pose: failure stands
        assert(std::string(at_saved_spot_plain)=="You are at your saved spot.");
    }
    // Zone ids are saved with the spot. game.spatial finds a zone by name only inside the player's own ancestry,
    // so a spot saved elsewhere resolves through zone_record(id): accepted only while the record keeps the saved name.
    {
        sc_game_spatial_v1 farSp{sizeof(farSp),Convert,Record,[](const char*,sc_game_zone*)->int{return 0;},List}; // find_zone answers nothing: player is elsewhere
        Spot saved; assert(Capture(&old,&sp,saved)==SCO_OK);
        const auto kept=SpotText(saved); assert(kept.find("zoneid 1\n")!=std::string::npos && kept.find("zoneid 3\n")!=std::string::npos);
        Spot reread; assert(ParseSpot(kept,reread) && reread.z.size()==3 && reread.z[0].zone==1 && reread.z[1].zone==2 && reread.z[2].zone==3 && SpotText(reread)==kept);
        Restoration far_r; assert(Resolve(&farSp,reread,far_r)==SCO_OK && far_r.level==0 && !far_r.partial && far_r.pose.zone==1); // exact, by id
        Spot old_file; assert(ParseSpot("spot 1 2 3 zone1\npose 0 0 0 1\n",old_file) && old_file.z[0].zone==0); // old files carry no id
        assert(Resolve(&farSp,old_file,far_r)==SCO_UNAVAILABLE); // and cannot be found by name from outside the zone: honest refusal
        assert(Resolve(&sp,old_file,far_r)==SCO_OK && far_r.pose.zone==1); // still found by name inside it
        Spot renamed=reread; renamed.z[0].name="somewhere_else"; // the id now belongs to another place: never trusted
        assert(Resolve(&farSp,renamed,far_r)==SCO_OK && far_r.level==1 && far_r.partial && far_r.pose.zone==2);
        Spot reused=reread; reused.z[1].zone=1; reused.z[0].zone=0; reused.z[0].name="gone"; // zone1 exists under another name, so level 1 must not match;
        assert(Resolve(&farSp,reused,far_r)==SCO_UNAVAILABLE); // level 2 is the root, and a root-only match refuses as before
        Spot junk; assert(ParseSpot("spot 1 2 3 zone1\nzoneid nope\nzoneid 0\n",junk) && junk.z[0].zone==0); // a bad hint is ignored, the spot is kept
        farSp.zone_record=nullptr; assert(Resolve(&farSp,reread,far_r)==SCO_UNAVAILABLE); // a service without zone_record is refused outright
    }
    // The teleport plugin itself, against fake spatial and relocation services: the player's own pose decides arrival.
    // Synthetic only; not run in game.
    {
        namespace tp=sco::features::travel::implementation;
        struct FTicket { uint32_t state,reason,applied; };
        static std::vector<FTicket> tickets; static int fRequests=0,fCancels=0;
        static double px=1,py=0,pz=2; static bool relocationOn=true;
        static std::vector<std::pair<int,std::string>> logs; static std::string hostStatus;
        static std::map<std::string,sco_command> commands; static std::vector<sco_event_fn> subs;
        static int dummy=0; static sc_game_spatial_v1 fsp{sizeof(fsp),Convert,Record,Find,List};
        static sc_spatial_v1 fold{}; fold.size=sizeof(fold);
        fold.player_pose=[](double* p,double* q,uint64_t* id)->int{ p[0]=px;p[1]=py;p[2]=pz;q[0]=0;q[1]=0;q[2]=0;q[3]=1;*id=1;return 1; };
        static sc_relocation_v1 frel{sizeof(frel),
            [](sco_plugin*,const sc_relocation_request*,uint64_t* t)->sco_result{ // Core allows one active request globally
                for (const auto& k:tickets) if (k.state==SC_RELOCATION_PENDING) return SCO_TOO_MANY;
                tickets.push_back({SC_RELOCATION_PENDING,0,1}); ++fRequests; *t=1000+tickets.size(); return SCO_OK; },
            [](sco_plugin*,uint64_t t)->sco_result{ ++fCancels; auto& k=tickets.at(t-1001);
                if (k.state!=SC_RELOCATION_PENDING) return SCO_NOT_FOUND; k.state=SC_RELOCATION_CANCELLED; return SCO_OK; },
            [](sco_plugin*,uint64_t t,sc_relocation_status* o)->sco_result{ if (t<1001||t>1000+tickets.size()) return SCO_NOT_FOUND;
                const auto& k=tickets[t-1001]; o->state=k.state; o->reason=k.reason; o->applied=k.applied; o->ticket=t; return SCO_OK; }};
        static sco_api fapi{}; fapi.size=sizeof(fapi); fapi.major=SCO_API_MAJOR; fapi.minor=SCO_API_MINOR;
        fapi.has=[](const char* n)->int{ return relocationOn && !std::strcmp(n,SC_RELOCATION_NAME); };
        fapi.query_service=[](const char* n,uint32_t,const void** out)->sco_result{
            if (!std::strcmp(n,SC_RELOCATION_NAME) && relocationOn) { *out=&frel; return SCO_OK; }
            if (!std::strcmp(n,SC_SPATIAL_SERVICE_NAME)) { *out=&fold; return SCO_OK; }
            if (!std::strcmp(n,SC_GAME_SPATIAL_NAME)) { *out=&fsp; return SCO_OK; }
            return SCO_NOT_FOUND; };
        fapi.subscribe=[](sco_plugin*,const char*,sco_event_fn fn,void*)->sco_result{ for (auto f:subs) assert(f!=fn); subs.push_back(fn); return SCO_OK; };
        fapi.unsubscribe=[](sco_plugin*,const char*,sco_event_fn fn)->sco_result{
            for (auto i=subs.begin();i!=subs.end();++i) if (*i==fn) { subs.erase(i); return SCO_OK; } return SCO_NOT_FOUND; };
        fapi.status=[](sco_plugin*,const char* m){ hostStatus=m; };
        fapi.log=[](sco_plugin*,sco_log_level l,const char* m){ logs.emplace_back(static_cast<int>(l),m); };
        fapi.register_command=[](sco_plugin*,const sco_command* c)->sco_result{ commands[c->name]=*c; return SCO_OK; };
        auto* self=reinterpret_cast<sco_plugin*>(&dummy);
        const auto dir=std::filesystem::temp_directory_path()/"travel_workflow_test_spawn"; std::filesystem::remove_all(dir); std::filesystem::create_directories(dir);
        const auto spawn=(dir/"spawn.txt").string();
#ifdef _WIN32
        _putenv_s("SC_OFFLINE_SPAWN_FILE",spawn.c_str());
#else
        setenv("SC_OFFLINE_SPAWN_FILE",spawn.c_str(),1);
#endif
        auto call=[&](const char* name)->sco_result{ char reply[512]{}; const auto& c=commands.at(name); return c.fn(nullptr,0,c.ctx,reply,sizeof(reply)); };
        auto tick=[&]{ const auto copy=subs; for (auto f:copy) f("tick",nullptr,nullptr); };
        auto at=[&]{ px=1;py=0;pz=2; }; auto away=[&]{ px=100;py=0;pz=2; };
        auto logged=[&](const char* needle,int level){ for (const auto& l:logs) if (l.first==level && l.second.find(needle)!=std::string::npos) return true; return false; };
        const std::string arrivedSaved="You are at your saved spot.";
        assert(Load(&fapi,self)==SCO_OK && commands.size()==4 && subs.empty());
        at(); assert(call("teleport.save")==SCO_OK);

        // 1. The player arrives before Core confirms: success once, the page is free again, the slot is released.
        away(); assert(call("teleport.go")==SCO_OK && fRequests==1 && subs.size()==1 && tp::Moving());
        tick(); assert(tp::Moving() && tp::g.last_status.find("Moving to")==0); // still away: nothing reported
        assert(call("teleport.go")==SCO_TOO_MANY && fRequests==1); // a move is genuinely pending
        at(); tick(); // the player is at the spot; Core's own check has not passed
        assert(tp::g.last_status==arrivedSaved && hostStatus==arrivedSaved && !tp::Moving() && tp::g.moving.empty());
        assert(fCancels==1 && tickets[0].state==SC_RELOCATION_CANCELLED && tickets[0].applied==1); // cancel frees the slot; it does not undo the move
        assert(!logged("confirmation not received",SCO_LOG_INFO)); // not yet: Core has said nothing
        // 4. A second F8 after arrival, while Core's first request is still being watched, is accepted.
        away(); assert(call("teleport.go")==SCO_OK && fRequests==2 && tp::Moving() && subs.size()==1);
        tick(); assert(tp::Moving() && tp::g.last_status.find("Moving to")==0 && !logged("confirmation not received",SCO_LOG_INFO));
        tickets[1].state=SC_RELOCATION_COMPLETE; tick(); // Core confirms the second move
        assert(tp::g.last_status=="Arrived at your saved spot." && subs.empty() && !tp::Moving());

        // 1b. Arrival, then the released request reads as ended: logged at info, the player's message stands.
        away(); logs.clear(); assert(call("teleport.go")==SCO_OK && fRequests==3);
        at(); tick(); assert(tp::g.last_status==arrivedSaved && subs.size()==1);
        tick();
        assert(logged("confirmation not received; you had arrived",SCO_LOG_INFO) && !logged("confirmation not received",SCO_LOG_WARN));
        assert(tp::g.last_status==arrivedSaved && subs.empty() && !tp::g.ticket);

        // 2. Core confirms first: unchanged behaviour, no cancel, no late message.
        const int cancelsBefore=fCancels; away(); logs.clear(); assert(call("teleport.go")==SCO_OK);
        tickets.back().state=SC_RELOCATION_COMPLETE; at(); tick();
        assert(tp::g.last_status=="Arrived at your saved spot." && subs.empty() && fCancels==cancelsBefore && !logged("confirmation not received",SCO_LOG_INFO));

        // 3. The player never comes within 2 m and Core times out: the failure message stays.
        away(); assert(call("teleport.go")==SCO_OK); tick(); tick();
        tickets.back().state=SC_RELOCATION_FAILED; tickets.back().reason=SC_RELOCATION_REASON_TIMEOUT; tick();
        assert(tp::g.last_status.find("failed")!=std::string::npos && tp::g.last_status.find("didn't confirm")!=std::string::npos);
        assert(subs.empty() && !tp::Moving() && fCancels==cancelsBefore);
        // ...and close but outside 2 m is still not arrival.
        px=3.5; assert(call("teleport.go")==SCO_OK); tick(); assert(tp::Moving() && tp::g.last_status.find("Moving to")==0);
        tickets.back().state=SC_RELOCATION_FAILED; tickets.back().reason=SC_RELOCATION_REASON_TIMEOUT; tick();
        assert(tp::g.last_status.find("failed")!=std::string::npos); at();

        // 5. The service offers no cancel: arrival is still reported; Core's late timeout is ignored and logged.
        const auto realCancel=frel.cancel; frel.cancel=nullptr; away(); logs.clear(); assert(call("teleport.go")==SCO_OK);
        at(); tick(); assert(tp::g.last_status==arrivedSaved && !tp::Moving() && subs.size()==1);
        tickets.back().state=SC_RELOCATION_FAILED; tickets.back().reason=SC_RELOCATION_REASON_TIMEOUT; tick();
        assert(tp::g.last_status==arrivedSaved && subs.empty() && logged("confirmation not received; you had arrived",SCO_LOG_INFO));
        frel.cancel=realCancel;

        Unload(); std::filesystem::remove_all(dir);
    }
    const std::string timeout=PlainReason(SC_RELOCATION_REASON_TIMEOUT);
    assert(timeout.find("too long")==std::string::npos && timeout.find("time")!=std::string::npos);
    assert(std::string(already_there_plain)=="You are already at that spot.");
    assert(!ParseSpot("spot nan 0 0 bad\n",again));
    std::puts("travel plugin: pose-confirmed arrival before/after Core, never-arrives, second move after arrival; travel model: nested rotated poses, legacy format, partial/root refusal, expiry, 256/64 bounded scan, partial/null tails, game.relocation request/status gating: PASS");
}
