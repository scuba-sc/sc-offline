// SPDX-License-Identifier: GPL-3.0-only
// SDK-only optional TitanLink orchestration. Windows launch/renderer is a separate boundary.
#include "titanlink.h"
#include "platform.h"
#include "math.h"
#include "frame_valid.h"
#include <sc_spatial.h>
#include <sc_game_spatial.h>
#define HAS(t,m) ((t) && (t)->size >= offsetof(std::remove_pointer_t<decltype(t)>,m)+sizeof((t)->m) && (t)->m)
#include <type_traits>
#include <sco_settings.h>
#include <sco_widgets.h>
#include <sco_ui.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#define strncpy_s(dest,src,ignored) std::snprintf(dest,sizeof(dest),"%s",src)
#define _TRUNCATE 0
namespace {

const sco_api*       g_api = nullptr;
sco_plugin*          g_self = nullptr;
const sco_ipc_v1*    g_ipc = nullptr;
const sc_spatial_v1* g_sp = nullptr;
const sc_game_spatial_v1* g_full = nullptr;
const sco_widgets_v1* g_widgets=nullptr;
const sco_ui_v1* g_ui=nullptr;
bool g_panel=false,g_hotkey=false;
char g_reply[256]{};
void RefreshUi();
uint64_t             g_ch = 0;   // the channel; 0 while closed

const sco_plugin_info kInfo = {
    sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, "titanlink", "0.1.0", "scubamount",
};

constexpr uint32_t    kLinkTimeoutMs = 4000;
constexpr double      kEyeHeight = 1.62;     // metres above your feet
constexpr double      kMaxUnits = 1.0e6;     // any coordinate of a Titanfall map is well inside this
constexpr double      kEmbarkRange = 9.0;    // metres from the Titan
constexpr double      kTitanDropAhead = 8.0; // metres in front of you
constexpr double      kPi = 3.14159265358979323846;

#define LOGI(...) BridgeLog(g_api, g_self, SCO_LOG_INFO, __VA_ARGS__)
#define LOGW(...) BridgeLog(g_api, g_self, SCO_LOG_WARN, __VA_ARGS__)

TlStatus    g_st;
tl_tf_state g_in = {};   // the last valid state from the Titanfall side; zero while unlinked

struct Config {
    char   game[260] = "C:/Program Files (x86)/Steam/steamapps/common/Titanfall2";
    char   map[64] = "mp_coliseum";
    char   mode[32] = "tdm";
    char   extra[256] = "";
    double fov = 0;   // 0 explicitly reports cl_fov unavailable and uses 60 degrees.
    bool launch = false; // only after declared setting successfully grants it
};
Config g_cfg;

// Where pilot mode began: a zone and a frame in it (your feet, facing, up). The match's floor_pt
// is this point; metres along r, f, u are the match's x, y, z.
struct Anchor {
    bool     ok = false;
    uint64_t zone = 0;
    double   o[3] = {}, r[3] = {}, f[3] = {}, u[3] = {};
};
Anchor g_anchor;

bool     g_startSent = false;   // TL_MSG_START_MATCH went out for this link
bool     g_ringBad = false;     // the peer's ring failed validation once (logged)
bool     g_stateBad = false;    // a tl_tf_state failed validation once (logged)
uint32_t    g_launchAt = 0, g_callAt = 0;
uint32_t g_buttons = 0;
bool     g_titanKeyWas = false, g_embarkKeyWas = false;
float    g_lastFeet[3] = {};
uint64_t g_lastFeetMs = 0;
uint32_t    g_fovAt = 0;
float    g_fovScale = 1.0f;


void LoadConfig() {
    g_cfg=Config();
    const void* raw=nullptr;
    const sco_settings_v1* settings=nullptr;
    if(g_api->query_service(SCO_SETTINGS_NAME,SCO_SETTINGS_VERSION_1_0,&raw)==SCO_OK)settings=static_cast<const sco_settings_v1*>(raw);
    if(HAS(settings,get_string)){
        auto get=[&](const char* key,char* dest,uint32_t cap,bool name){
            char value[260]{};uint32_t n=sizeof(value);
            if(settings->get_string(g_self,key,value,&n)!=SCO_OK || n>sizeof(value) || !std::memchr(value,0,sizeof(value)))return;
            const size_t len=std::strlen(value);if(len>=cap)return;
            if(name){if(!len)return;for(size_t i=0;i<len;++i)if(!((value[i]>='a'&&value[i]<='z')||(value[i]>='0'&&value[i]<='9')||value[i]=='_'))return;}
            std::snprintf(dest,cap,"%s",value);
        };
        get("game",g_cfg.game,sizeof(g_cfg.game),false);get("map",g_cfg.map,sizeof(g_cfg.map),true);
        get("mode",g_cfg.mode,sizeof(g_cfg.mode),true);get("extra",g_cfg.extra,sizeof(g_cfg.extra),false);
    }
    if(HAS(settings,get_float)){double f=0;if(settings->get_float(g_self,"fov",&f)==SCO_OK && std::isfinite(f) && f>=0 && f<170)g_cfg.fov=f;}
    if(HAS(settings,get_bool)){int32_t launch=0;if(settings->get_bool(g_self,"launch",&launch)==SCO_OK)g_cfg.launch=launch==1;}
    g_fovAt=0;
    LOGW("[titanlink] legacy titanlink.txt import unavailable: no public host data-root contract; copy game/map/mode/extra/fov to plugin settings");
    std::snprintf(g_st.game,sizeof(g_st.game),"%s",g_cfg.game);
    std::snprintf(g_st.map,sizeof(g_st.map),"%s",g_cfg.map);
    std::snprintf(g_st.mode,sizeof(g_st.mode),"%s",g_cfg.mode);
}
void Launch() { if(!g_cfg.launch)return;g_st.launched=TlPlatformLaunch(g_cfg.game,g_cfg.extra); g_launchAt=static_cast<uint32_t>(TlPlatformNow())|1; }
// ---- poses: teleport.spatial, converted into the match's frame ----------------------------------

template<class T> const T* QueryService(const char* name,uint32_t version){
    const void* out=nullptr;
    if(!g_api || !HAS(g_api,query_service) || g_api->query_service(name,version,&out)!=SCO_OK)return nullptr;
    return static_cast<const T*>(out);
}
void QuerySpatial(){g_sp=QueryService<sc_spatial_v1>(SC_SPATIAL_SERVICE_NAME,SC_SPATIAL_SERVICE_VERSION);g_full=QueryService<sc_game_spatial_v1>(SC_GAME_SPATIAL_NAME,SC_GAME_SPATIAL_VERSION);}
bool ValidPose(double p[3],double q[4]){
    for(int i=0;i<3;++i)if(!std::isfinite(p[i]))return false;
    double norm=0;for(int i=0;i<4;++i){if(!std::isfinite(q[i]))return false;norm+=q[i]*q[i];}
    if(!std::isfinite(norm)||norm<1e-12)return false;
    norm=std::sqrt(norm);for(int i=0;i<4;++i)q[i]/=norm;return true;
}
// Your feet, facing and up in the anchor's zone (the zone you're in, converted when it differs).
bool PoseInAnchor(double pos[3], double fwd[3], double up[3]) {
    double p[3], q[4], r[3], f[3], u[3];
    uint64_t zone = 0;
    QuerySpatial();
    if (!HAS(g_sp,player_pose) || !g_anchor.ok || !g_sp->player_pose(p, q, &zone) || !zone || !ValidPose(p,q)) return false;
    BridgeAxes(q, r, f, u);
    if (zone == g_anchor.zone) {
        memcpy(pos, p, sizeof(p));
        memcpy(fwd, f, sizeof(f));
        memcpy(up, u, sizeof(u));
        return true;
    }
    double converted[3], rotation[4];
    if (!HAS(g_full,pose_to_zone) || !g_full->pose_to_zone(zone,g_anchor.zone,p,q,converted,rotation)) return false;
    if(!ValidPose(converted,rotation))return false;
    BridgeAxes(rotation,r,f,u);
    memcpy(pos,converted,sizeof(converted));memcpy(fwd,f,sizeof(f));memcpy(up,u,sizeof(u));
    return BridgeNormalize(fwd) && BridgeNormalize(up);
}

bool SetAnchor() {
    double p[3], q[4], r[3], f[3], u[3];
    uint64_t zone = 0;
    g_anchor.ok = false;
    QuerySpatial();
    if (!HAS(g_sp,player_pose) || !g_sp->player_pose(p, q, &zone) || !zone || !ValidPose(p,q)) return false;
    BridgeAxes(q, r, f, u);
    g_anchor.zone = zone;
    memcpy(g_anchor.o, p, sizeof(p));
    memcpy(g_anchor.f, f, sizeof(f));
    memcpy(g_anchor.u, u, sizeof(u));
    BridgeCross(f, u, g_anchor.r);
    if (!BridgeNormalize(g_anchor.r) || !BridgeNormalize(g_anchor.f) || !BridgeNormalize(g_anchor.u)) return false;
    g_anchor.ok = true;
    g_lastFeetMs = 0;
    return true;
}

void ToMatch(const double local[3], float out[3]) {
    const double d[3] = { local[0] - g_anchor.o[0], local[1] - g_anchor.o[1], local[2] - g_anchor.o[2] };
    out[0] = static_cast<float>(g_in.floor_pt[0] + TL_UNITS_PER_METRE * BridgeDot(d, g_anchor.r));
    out[1] = static_cast<float>(g_in.floor_pt[1] + TL_UNITS_PER_METRE * BridgeDot(d, g_anchor.f));
    out[2] = static_cast<float>(g_in.floor_pt[2] + TL_UNITS_PER_METRE * BridgeDot(d, g_anchor.u));
}

void DirToMatch(const double v[3], double out[3]) {
    out[0] = BridgeDot(v, g_anchor.r);
    out[1] = BridgeDot(v, g_anchor.f);
    out[2] = BridgeDot(v, g_anchor.u);
}

void FromMatch(const float m[3], double local[3]) {
    const double d[3] = { (m[0] - g_in.floor_pt[0]) / TL_UNITS_PER_METRE, (m[1] - g_in.floor_pt[1]) / TL_UNITS_PER_METRE,
                          (m[2] - g_in.floor_pt[2]) / TL_UNITS_PER_METRE };
    for (int i = 0; i < 3; ++i) local[i] = g_anchor.o[i] + g_anchor.r[i] * d[0] + g_anchor.f[i] * d[1] + g_anchor.u[i] * d[2];
}

// Metres from you to the Titan, or -1.
double TitanDistance() {
    if (!(g_in.flags & (TL_TF_PARKED | TL_TF_IN_TITAN))) return -1;
    double pos[3], f[3], u[3], titan[3];
    if (!PoseInAnchor(pos, f, u)) return -1;
    FromMatch(g_in.titan_origin, titan);
    const double d[3] = { titan[0] - pos[0], titan[1] - pos[1], titan[2] - pos[2] };
    return std::sqrt(BridgeDot(d, d));
}

float FovScale(uint32_t now) {
    if (g_fovAt && now - g_fovAt < 1000) return g_fovScale;
    g_fovAt = now;
    double vfov = g_cfg.fov;
    if (vfov <= 0) vfov = 60.0;
    const double deg = kPi / 180.0;
    const double hor43 = 2.0 * std::atan(std::tan(vfov * deg * 0.5) * 4.0 / 3.0) / deg;
    double s = hor43 / 70.0;
    s = s < 0.6 ? 0.6 : s > 2.0 ? 2.0 : s;
    g_fovScale = static_cast<float>(s);
    return g_fovScale;
}

// Writes tl_sc_state. Ten times a second (the host tick): the Titanfall side interpolates with
// vel and time_ms between writes.
void Publish(uint64_t nowMs) {
    if (!g_ch) return;
    tl_sc_state s = {};
    s.time_ms = nowMs;
    const bool focused = TlPlatformFocused();
    if (g_st.pilot) s.flags |= TL_SC_PILOT;
    if (focused) s.flags |= TL_SC_FOCUSED;
    double pos[3], f[3], u[3];
    if (g_anchor.ok && PoseInAnchor(pos, f, u)) {
        s.flags |= TL_SC_ANCHORED;
        ToMatch(pos, s.feet);
        const double eye[3] = { pos[0] + u[0] * kEyeHeight, pos[1] + u[1] * kEyeHeight, pos[2] + u[2] * kEyeHeight };
        ToMatch(eye, s.eye);
        double d[3];
        DirToMatch(f, d);
        const double z = d[2] < -1.0 ? -1.0 : d[2] > 1.0 ? 1.0 : d[2];
        s.ang[0] = static_cast<float>(-std::asin(z) * 180.0 / kPi);
        s.ang[1] = static_cast<float>(std::atan2(d[1], d[0]) * 180.0 / kPi);
        const uint64_t dt = nowMs - g_lastFeetMs;
        if (g_lastFeetMs && dt > 0 && dt <= 1000)
            for (int i = 0; i < 3; ++i) s.vel[i] = static_cast<float>((s.feet[i] - g_lastFeet[i]) * 1000.0 / static_cast<double>(dt));
        memcpy(g_lastFeet, s.feet, sizeof(g_lastFeet));
        g_lastFeetMs = nowMs;
    } else {
        g_lastFeetMs = 0;
    }
    s.fov_scale = FovScale(static_cast<uint32_t>(nowMs));
    TlPlatformViewSize(s.view_w, s.view_h);
    s.buttons = g_st.pilot && focused ? g_buttons : 0;
    g_ipc->block_write(g_self, g_ch, TL_OFF_SC_STATE, &s, sizeof(s));
}

// ---- the link ----------------------------------------------------------------------------------

bool Push(uint32_t type, const void* data, uint32_t size) {
    return g_ch && g_ipc->ring_push(g_self, g_ch, TL_OFF_TO_PEER, type, data, size) == SCO_OK;
}

// Everything the Titanfall side wrote is checked here before any of it is used: flags are masked,
// every coordinate must be finite and inside a map, counts and fractions in range, text printable.
bool ValidTf(tl_tf_state& s) {
    s.flags &= TL_TF_FLAGS;
    if (!BridgeFinite3(s.origin, kMaxUnits) || !BridgeFinite3(s.titan_origin, kMaxUnits) || !BridgeFinite3(s.floor_pt, kMaxUnits) ||
        !BridgeFinite(s.eye_z, kMaxUnits) || !BridgeFinite(s.titan_yaw, 1.0e4))
        return false;
    if (!BridgeFinite(s.damage, 1.0e5) || s.damage < 0) s.damage = 0;
    if (!BridgeFinite(s.titan_health, 1.0)) s.titan_health = 0;
    if (s.titan_health < 0) s.titan_health = 0;
    if (!BridgeFinite(s.zoom_frac, 1.0) || s.zoom_frac < 0) s.zoom_frac = 0;
    if (!BridgeFinite(s.zoom_fov, 180.0) || s.zoom_fov < 0) s.zoom_fov = 0;
    if (s.clip < -1 || s.clip > 100000) s.clip = -1;
    BridgeSanitize(s.weapon, sizeof(s.weapon));
    return true;
}

// Takes what the Titanfall side queued: TL_MSG_LOG lines; other types (a newer peer) are skipped.
void DrainPeer() {
    if(g_ringBad)return;
    static uint8_t buf[TL_RING_FROM_PEER];   // a record never exceeds the ring
    for (int i = 0; i < 16; ++i) {
        uint32_t type = 0, size = sizeof(buf);
        const sco_result r = g_ipc->ring_pop(g_self, g_ch, TL_OFF_FROM_PEER, &type, buf, &size);
        if (r == SCO_NOT_FOUND) return;
        if (r != SCO_OK) {
            if (!g_ringBad) LOGW("[titanlink] the Titanfall side's message ring failed validation (%d); its messages are ignored", static_cast<int>(r));
            g_ringBad = true;
            return;
        }
        if (type != TL_MSG_LOG || size == 0 || size > TL_MSG_LOG_MAX) continue;
        memcpy(g_st.peerLog, buf, size);
        g_st.peerLog[size] = 0;
        BridgeSanitize(g_st.peerLog, sizeof(g_st.peerLog));
        LOGI("[titanlink] Titanfall: %s", g_st.peerLog);
    }
}

void PilotOff(bool tell) {
    if (!g_st.pilot && !g_st.wantPilot) return;
    const bool was = g_st.pilot;
    g_st.pilot = false;
    g_st.wantPilot = false;
    g_buttons = 0;
    TlPlatformStop();
    TlOverlayShow(false);
    if (was && tell) Push(TL_MSG_PILOT_OFF, nullptr, 0);
    Publish(TlPlatformNow());
    if (was) LOGI("[titanlink] pilot mode off");
}

void PilotOn() {
    // A parked Titan keeps its place: re-anchor only when there is none (or no anchor yet).
    if (!g_anchor.ok || !(g_in.flags & (TL_TF_PARKED | TL_TF_IN_TITAN))) SetAnchor();
    g_st.pilot = true;
    g_callAt = 0;
    g_titanKeyWas = g_embarkKeyWas = true;   // a key already held doesn't fire
    TlOverlayShow(true);
    LOGI("[titanlink] pilot mode on%s: V calls your Titan, E gets in and out, F9 ends it",
         g_anchor.ok ? "" : " (waiting for your position to anchor the match)");
}

bool OpenLink(char* reply, uint32_t size) {
    if (g_ch) return true;
    g_ipc=QueryService<sco_ipc_v1>(SCO_IPC_NAME,SCO_IPC_VERSION_1_0);QuerySpatial();
    if (!HAS(g_ipc,create) || !HAS(g_ipc,close) || !HAS(g_ipc,peer_age_ms) ||
        !HAS(g_ipc,block_write) || !HAS(g_ipc,block_read) || !HAS(g_ipc,ring_init) ||
        !HAS(g_ipc,ring_push) || !HAS(g_ipc,ring_pop) || !HAS(g_sp,player_pose) || !HAS(g_full,pose_to_zone)) {
        snprintf(reply,size,"TitanLink unavailable: complete sco.ipc, teleport.spatial and game.spatial pose_to_zone required; no private fallback");
        return false;
    }
    LoadConfig();
    uint64_t ch = 0;
    sco_result r = g_ipc->create(g_self, TL_CHANNEL_NAME, TL_CHANNEL_BYTES, TL_LAYOUT_ID, TL_LAYOUT_VERSION, &ch);
    if (r == SCO_OK) r = g_ipc->ring_init(g_self, ch, TL_OFF_TO_PEER, TL_RING_TO_PEER, SCO_IPC_TO_PEER);
    if (r == SCO_OK) r = g_ipc->ring_init(g_self, ch, TL_OFF_FROM_PEER, TL_RING_FROM_PEER, SCO_IPC_FROM_PEER);
    if (r != SCO_OK) {
        if (ch) g_ipc->close(g_self, ch);
        snprintf(reply, size, "Couldn't open the TitanLink channel (sco.ipc %d); see mod.log", static_cast<int>(r));
        LOGW("[titanlink] couldn't open Local\\SCO_titanlink.link: sco.ipc answered %d", static_cast<int>(r));
        return false;
    }
    g_ch = ch;
    g_st.linkOpen = true;
    g_startSent = g_ringBad = g_stateBad = false;
    Publish(TlPlatformNow());
    TlOverlayStart(g_ipc, g_self, ch);
    LOGI("[titanlink] link open: Local\\SCO_titanlink.link, waiting for Titanfall 2's TitanLink plugin");
    return true;
}

void CloseLink() {
    if (!g_ch) return;
    PilotOff(true);
    TlOverlayStop();
    TlPlatformStop();
    g_ipc->close(g_self, g_ch);
    g_ch = 0;
    g_in = {};
    g_anchor.ok = false;
    const TlStatus keep = g_st;
    g_st = TlStatus();
    strncpy_s(g_st.game, keep.game, _TRUNCATE);
    strncpy_s(g_st.map, keep.map, _TRUNCATE);
    strncpy_s(g_st.mode, keep.mode, _TRUNCATE);
    g_st.launched = keep.launched;
    LOGI("[titanlink] link closed");
}

void PilotKeys() {
    const bool tk = TlPlatformKey('V'), ek = TlPlatformKey('E');
    char reply[160];
    if (tk && !g_titanKeyWas && !(g_in.flags & TL_TF_IN_TITAN)) TlCallTitan(reply,sizeof(reply));
    if (ek && !g_embarkKeyWas) TlEmbark(reply,sizeof(reply));
    g_titanKeyWas=tk; g_embarkKeyWas=ek;
    g_buttons=TlPlatformButtons((g_in.flags&TL_TF_IN_TITAN)!=0,(g_in.flags&TL_TF_BUSY)!=0);
}

void CloseLink();
void OnTick(const char*, const void*, void*) {
    if (!g_ch) return;
    const uint64_t nowMs = TlPlatformNow();
    const uint32_t now = static_cast<uint32_t>(nowMs);
    uint32_t age = 0;
    const auto ageResult=g_ipc->peer_age_ms(g_self,g_ch,&age);
    if(ageResult!=SCO_OK && (g_st.linked || ageResult!=SCO_NOT_FOUND)) {
        g_st.pilot=g_st.wantPilot=false;g_buttons=0; // no final publish to a closed peer/service
        CloseLink();return;
    }
    const bool linked=ageResult==SCO_OK && age<kLinkTimeoutMs;
    if (linked != g_st.linked) LOGI(linked ? "[titanlink] Titanfall 2 linked" : "[titanlink] Titanfall 2 stopped answering");
    g_st.linked = linked;
    g_st.peerAgeMs = linked ? age : 0;
    if (linked) {
        tl_tf_state in = {};
        g_in={}; // Never act on a stale state after a malformed/torn/missing snapshot.
        if (g_ipc->block_read(g_self,g_ch,TL_OFF_TF_STATE,&in,sizeof(in))==SCO_OK && ValidTf(in)) {
            g_in=in;
        } else {
            if(!g_stateBad)LOGW("[titanlink] invalid or unavailable peer state; pilot stopped");
            g_stateBad=true;PilotOff(false);
        }
        DrainPeer();
        tl_frame fr = {};
        if (g_ipc->block_read(g_self, g_ch, TL_OFF_FRAME, &fr, sizeof(fr)) == SCO_OK) {
            if(TlValidFrame(fr)) {
                BridgeSanitize(fr.status, sizeof(fr.status));
                strncpy_s(g_st.frameStatus, fr.status, _TRUNCATE);
            } else std::snprintf(g_st.frameStatus,sizeof(g_st.frameStatus),"rejected");
        } else std::snprintf(g_st.frameStatus,sizeof(g_st.frameStatus),"unavailable");
    } else {
        g_in = {};
        g_startSent = false;
        std::snprintf(g_st.frameStatus,sizeof(g_st.frameStatus),"unavailable (peer stale)");
    }
    if (linked && !g_startSent) {
        tl_msg_start_match m = {};
        strncpy_s(m.map, g_cfg.map, _TRUNCATE);
        strncpy_s(m.mode, g_cfg.mode, _TRUNCATE);
        if (Push(TL_MSG_START_MATCH, &m, sizeof(m))) {
            g_startSent = true;
            LOGI("[titanlink] asked Titanfall 2 for a local match: %s on %s", m.mode, m.map);
        }
    }
    g_st.inMatch = linked && (g_in.flags & TL_TF_IN_MATCH);
    g_st.inTitan = (g_in.flags & TL_TF_IN_TITAN) != 0;
    g_st.titanParked = (g_in.flags & TL_TF_PARKED) != 0;
    g_st.titanHealth = g_in.titan_health;
    g_st.clip = g_in.clip;
    strncpy_s(g_st.weapon, g_in.weapon, _TRUNCATE);
    if (g_st.wantPilot && !g_st.pilot && g_st.inMatch) PilotOn();
    if (g_st.pilot && !linked) {
        LOGW("[titanlink] Titanfall 2 stopped answering; pilot mode off");
        PilotOff(false);
    }
    if (g_st.pilot) {
        if (!g_anchor.ok) SetAnchor();
        PilotKeys();
    }
    TlPlatformTick(now, linked ? g_in.pid : 0);
    g_st.titanDistance = TitanDistance();
    Publish(nowMs);
    RefreshUi();
}

// ---- commands ----------------------------------------------------------------------------------

sco_result CmdPilot(const sco_arg*, uint32_t, void*, char* reply, uint32_t size) {
    TlTogglePilot(reply, size);
    return g_st.linkOpen ? SCO_OK : SCO_UNAVAILABLE;
}
sco_result CmdTitan(const sco_arg*, uint32_t, void*, char* reply, uint32_t size) {
    if(!g_st.pilot){std::snprintf(reply,size,"TitanLink unavailable: start pilot mode first; G2 relocation refused");return SCO_UNAVAILABLE;}
    TlCallTitan(reply, size);
    return SCO_OK;
}
sco_result CmdEmbark(const sco_arg*, uint32_t, void*, char* reply, uint32_t size) {
    TlEmbark(reply, size);
    return SCO_OK;
}
sco_result CmdUnlink(const sco_arg*, uint32_t, void*, char* reply, uint32_t size) {
    TlUnlink(reply, size);
    return SCO_OK;
}
sco_result CmdStatus(const sco_arg*, uint32_t, void*, char* reply, uint32_t size) {
    snprintf(reply, size, "TitanLink: link %s, Titanfall %s%s, pilot mode %s, %s; G2 relocation refused; G7 cvar writes refused; cl_fov read unavailable; frame %s; launch/overlay %s; legacy titanlink.txt import unavailable (copy keys to plugin settings)",
             g_st.linkOpen ? "open" : "closed", g_st.linked ? "linked" : "not answering", g_st.inMatch ? " in a match" : "",
             g_st.pilot ? "on" : g_st.wantPilot ? "waiting" : "off",
             g_st.inTitan ? "in the Titan" : g_st.titanParked ? "Titan parked" : "no Titan", g_st.frameStatus, TlPlatformAvailable()?"Windows only, NOT VERIFIED":"unavailable (requires Windows)");
    return SCO_OK;
}


void WidgetEvent(const char* panel,const sco_widget_event* e,void*) {
    if(!g_api || !panel || std::strcmp(panel,"titanlink.titanfall") || !e || e->size<sizeof(*e) || e->kind!=SCO_WIDGET_BUTTON)return;
    switch(e->node_id){
      case 1:TlTogglePilot(g_reply,sizeof(g_reply));break;
      case 2:TlCallTitan(g_reply,sizeof(g_reply));break;
      case 3:TlEmbark(g_reply,sizeof(g_reply));break;
      case 4:TlUnlink(g_reply,sizeof(g_reply));break;
      default:return;
    }
    RefreshUi();
}
void RefreshUi(){
    if(!HAS(g_widgets,set_panel) || !HAS(g_widgets,remove_panel))return;
    char status[240],health[180],config[240];
    std::snprintf(status,sizeof(status),"Link: %s; peer %s (%u ms); match %s; frame: %.96s",g_st.linkOpen?"open":"closed",g_st.linked?"linked":"unavailable",g_st.peerAgeMs,g_st.inMatch?"loaded":"unavailable",g_st.frameStatus);
    std::snprintf(health,sizeof(health),"Titan %s; distance %.1f m; health %.0f%%; weapon %.48s; clip %d",g_st.inTitan?"piloting":g_st.titanParked?"parked":"absent",g_st.titanDistance,g_st.titanHealth*100,g_st.weapon,g_st.clip);
    std::snprintf(config,sizeof(config),"Game: %.120s; %.31s on %.63s",g_st.game,g_st.mode,g_st.map);
    const char* text[]={g_st.pilot?"Stop pilot mode (F9)":g_st.wantPilot?"Cancel (F9)":"Start pilot mode (F9)","Call Titan (V)",g_st.inTitan?"Disembark (E)":"Embark (E)","Close link",status,health,config,g_st.peerLog,g_reply,
      "G2 relocation refused; G7 cvar writes refused. cl_fov read unavailable: set fov or use 60 degrees. Legacy titanlink.txt import unavailable: copy keys into declared plugin settings.",
      TlPlatformAvailable()?"Owned Windows D3D11 overlay/launch: NOT VERIFIED":"Overlay/launch/input unavailable: requires Windows; commands and IPC retained."};
    sco_widget_node nodes[11]{};
    for(uint32_t i=0;i<11;++i){nodes[i].size=sizeof(nodes[i]);nodes[i].id=i<4?i+1:i+10;nodes[i].kind=i<4?SCO_WIDGET_BUTTON:SCO_WIDGET_LABEL;nodes[i].label=text[i];}
    const auto r=g_widgets->set_panel(g_self,"titanlink.titanfall","Titanfall",nodes,11,sizeof(nodes[0]),WidgetEvent,nullptr);
    if(r==SCO_OK)g_panel=true;
    else if(!g_panel)LOGW("[titanlink] UI unavailable (%d); use retained commands",static_cast<int>(r));
}

const sco_plugin_info* TlQuery() { return &kInfo; }
sco_result TlLoad(const sco_api* api, sco_plugin* self) {
    if (!self || !api || api->size < offsetof(sco_api,query_service)+sizeof(api->query_service) || api->major != SCO_API_MAJOR || !api->query_service || !api->log || !api->register_command || !api->subscribe) return SCO_UNAVAILABLE;
    g_api=api;g_self=self;TlPlatformConfigure(api,self);
    const void* t=nullptr;
    if(api->query_service(SCO_IPC_NAME,SCO_IPC_VERSION_1_0,&t)==SCO_OK) g_ipc=static_cast<const sco_ipc_v1*>(t);
    t=nullptr;if(api->query_service(SC_SPATIAL_SERVICE_NAME,SC_SPATIAL_SERVICE_VERSION,&t)==SCO_OK)g_sp=static_cast<const sc_spatial_v1*>(t);
    t=nullptr;if(api->query_service(SC_GAME_SPATIAL_NAME,SC_GAME_SPATIAL_VERSION,&t)==SCO_OK)g_full=static_cast<const sc_game_spatial_v1*>(t);
    const char* ids[]={"titanlink.pilot","titanlink.titan","titanlink.embark","titanlink.unlink","titanlink.status"};
    const sco_command_fn fns[]={CmdPilot,CmdTitan,CmdEmbark,CmdUnlink,CmdStatus};
    for(int i=0;i<5;++i) { sco_command c{};c.size=sizeof(c);c.name=ids[i];c.title=ids[i];c.help=ids[i];c.fn=fns[i];
        const auto r=api->register_command(self,&c);if(r!=SCO_OK)return r; }
    const auto r=api->subscribe(self,"tick",OnTick,nullptr);if(r!=SCO_OK)return r;
    t=nullptr;if(api->query_service(SCO_WIDGETS_NAME,SCO_WIDGETS_VERSION_1_0,&t)==SCO_OK)g_widgets=static_cast<const sco_widgets_v1*>(t);
    t=nullptr;if(api->query_service(SCO_UI_NAME,SCO_UI_VERSION_1_0,&t)==SCO_OK)g_ui=static_cast<const sco_ui_v1*>(t);
    if(HAS(g_ui,bind_hotkey) && HAS(g_ui,unbind_hotkey))g_hotkey=g_ui->bind_hotkey(self,"f9","titanlink.pilot",nullptr,0)==SCO_OK;
    RefreshUi();return SCO_OK;
}
void TlUnload() { if(g_panel && HAS(g_widgets,remove_panel))g_widgets->remove_panel(g_self,"titanlink.titanfall");
    if(g_hotkey && HAS(g_ui,unbind_hotkey))g_ui->unbind_hotkey(g_self,"f9");
    g_panel=g_hotkey=false;g_widgets=nullptr;g_ui=nullptr;g_reply[0]=0;if(g_ch && g_st.pilot)Push(TL_MSG_PILOT_OFF,nullptr,0);g_st.pilot=g_st.wantPilot=false;CloseLink();TlPlatformStop();TlPlatformConfigure(nullptr,nullptr);g_ipc=nullptr;g_sp=nullptr;g_full=nullptr;g_api=nullptr;g_self=nullptr;g_st={};g_anchor={};g_in={};g_launchAt=g_callAt=0; }
} // namespace
// ---- the tab's view of it (titanlink.h) ---------------------------------------------------------

const TlStatus& TlGetStatus() { return g_st; }

void TlTogglePilot(char* reply, uint32_t size) {
    if (g_st.pilot || g_st.wantPilot) {
        PilotOff(true);
        snprintf(reply, size, "Pilot mode off");
        return;
    }
    if (!OpenLink(reply, size)) return;
    g_st.wantPilot = true;
    const uint32_t now = static_cast<uint32_t>(TlPlatformNow());
    if (!g_st.linked) {
        if (!g_launchAt || now - g_launchAt > 90000) Launch();
        snprintf(reply, size, "Waiting for Titanfall 2 to load its match; pilot mode starts by itself");
    } else if (!g_st.inMatch) {
        snprintf(reply, size, "Titanfall 2 is running but not in a match yet; pilot mode starts when it is");
    } else {
        PilotOn();
        snprintf(reply, size, "Pilot mode on");
    }
}

void TlCallTitan(char* reply, uint32_t size) {
    const uint32_t now = static_cast<uint32_t>(TlPlatformNow());
    double pos[3], f[3], u[3];
    if (!g_st.pilot) { snprintf(reply, size, "Start pilot mode first (F9)"); return; }
    if (!g_st.inMatch) { snprintf(reply, size, "Titanfall's match is still loading"); return; }
    if (g_in.flags & TL_TF_IN_TITAN) { snprintf(reply, size, "You're in your Titan"); return; }
    if (g_in.flags & TL_TF_BUSY) { snprintf(reply, size, "Wait until you're out of the Titan"); return; }
    if (g_callAt && now - g_callAt < 8000) { snprintf(reply, size, "Your Titan is still on its way down"); return; }
    if (!PoseInAnchor(pos, f, u)) { snprintf(reply, size, "Your position isn't known yet"); return; }
    double d[3];
    DirToMatch(f, d);
    d[2] = 0;
    if (!BridgeNormalize(d)) { d[0] = 0; d[1] = 1; }
    tl_msg_call_titan m = {};
    ToMatch(pos, m.drop);
    for (int i = 0; i < 2; ++i) m.drop[i] += static_cast<float>(d[i] * kTitanDropAhead * TL_UNITS_PER_METRE);
    m.yaw = static_cast<float>(std::atan2(d[1], d[0]) * 180.0 / kPi);
    if (!Push(TL_MSG_CALL_TITAN, &m, sizeof(m))) { snprintf(reply, size, "Titanfall's message ring is full; try again"); return; }
    g_callAt = now;
    snprintf(reply, size, "Titan called %.0f m in front of you", kTitanDropAhead);
}

void TlEmbark(char* reply, uint32_t size) {
    if (!g_st.pilot) { snprintf(reply, size, "Start pilot mode first (F9)"); return; }
    if (g_in.flags & TL_TF_BUSY) { snprintf(reply, size, "Wait until the Titan is done"); return; }
    if (g_in.flags & TL_TF_IN_TITAN) {
        snprintf(reply, size, Push(TL_MSG_DISEMBARK, nullptr, 0) ? "Getting out of your Titan" : "Titanfall's message ring is full; try again");
        return;
    }
    if (!(g_in.flags & TL_TF_PARKED)) { snprintf(reply, size, "No Titan yet: call one (V)"); return; }
    const double dist = TitanDistance();
    if (dist < 0) { snprintf(reply, size, "Your position isn't known yet"); return; }
    if (dist >= kEmbarkRange) { snprintf(reply, size, "Walk closer to your Titan to get in (%.0f m away)", dist); return; }
    snprintf(reply, size, Push(TL_MSG_EMBARK, nullptr, 0) ? "Climbing into your Titan" : "Titanfall's message ring is full; try again");
}

void TlUnlink(char* reply, uint32_t size) {
    if (!g_ch) { snprintf(reply, size, "TitanLink isn't open"); return; }
    CloseLink();
    snprintf(reply, size, "TitanLink closed");
}


const sco_plugin_info* TitanLinkQuery(){return TlQuery();}
sco_result TitanLinkLoad(const sco_api* a,sco_plugin* s){return TlLoad(a,s);}
void TitanLinkUnload(){TlUnload();}
