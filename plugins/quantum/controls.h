// SPDX-License-Identifier: GPL-3.0-only
// SDK-only quantum policy. Engine charge, sound and effect state stays behind G8.
#pragma once
#include <sc_quantum.h>
#include <sc_gamerules.h>
#include <sco_settings.h>
#include <sco_widgets.h>
#include <sco_ui.h>
#include <cstddef>
#include <cmath>
#include <limits>
#include <cstdio>
#include <cstring>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
namespace sco::features::quantum {
#define QHAS(t, type, m) ((t) && (t)->size >= offsetof(type,m) + sizeof((t)->m) && (t)->m)
struct ControlState {
    const sco_api* api=nullptr; sco_plugin* self=nullptr;
    bool subscribed=false, hotkey=false, leased=false, input_held=false;
    bool caps_input=true, mouse_input=false, clocked=false;
    uint32_t last_tick=0, last_panel=0; uintptr_t epoch=0;
    sc_quantum_state observed{}; sco_result result=SCO_UNAVAILABLE;
    sc_gamerules_state rules{}; sco_result rules_result=SCO_UNAVAILABLE; bool rules_known=false;
    char rules_message[192]="GameRules unknown: no received parameter snapshot";
    char group_message[128]="Group QT enforcement unknown; group membership is never inferred";
    char message[256]="G8 unavailable: no admitted game-pack adapter";
};
inline ControlState g;
inline uintptr_t next_epoch=0;
inline void* CallbackContext() { return reinterpret_cast<void*>(g.epoch); }
template<class T> const T* Open(const char* name) {
    const void* table=nullptr;
    if (!QHAS(g.api,sco_api,query_service) || g.api->query_service(name,0x10000u,&table)!=SCO_OK) return nullptr;
    return static_cast<const T*>(table);
}
inline bool Cap() { return QHAS(g.api,sco_api,has) && g.api->has("game.quantum.control"); }
inline const sc_quantum_v1* Control() { return Open<sc_quantum_v1>(SC_QUANTUM_SERVICE_NAME); }
inline sco_result Answer(sco_result r,char* out,uint32_t n,const char* text) {
    std::snprintf(g.message,sizeof(g.message),"%s",text);
    if (out && n) std::snprintf(out,n,"%s",g.message);
    return r;
}
inline void ReadRules() {
    sc_gamerules_state next{}; next.size=sizeof(next);
    const bool readable=QHAS(g.api,sco_api,has) && g.api->has("game.gamerules.read");
    auto result=readable ? sc_gamerules_read(Open<sc_gamerules_v1>(SC_GAMERULES_SERVICE_NAME),g.self,&next) : SCO_UNAVAILABLE;
    if (result==SCO_OK) {
        const float thresholds[]{next.travel_target_threshold_angle,next.target_lock_angular_player,
            next.target_lock_angular_ai,next.target_lock_linear_player,next.target_lock_linear_ai,
            next.boost_group_distance_threshold};
        if (next.size<sizeof(next) || next.valid>1 || next.enforce_group_qt>1 ||
            next.quantum_travel_allowed>1 || next.quantum_boost_allowed>1) result=SCO_FAILED;
        for (float value:thresholds) if (!std::isfinite(value)) result=SCO_FAILED;
    }
    g.rules_result=result; g.rules_known=result==SCO_OK && next.valid==1;
    g.rules=g.rules_known ? next : sc_gamerules_state{};
    if (g.rules_known) {
        std::snprintf(g.rules_message,sizeof(g.rules_message),"GameRules generation=%llu: quantum travel %s; boost %s",
            static_cast<unsigned long long>(next.generation),next.quantum_travel_allowed?"allowed":"forbidden",next.quantum_boost_allowed?"allowed":"forbidden");
        std::snprintf(g.group_message,sizeof(g.group_message),"%s; group membership unknown (provider decides admission)",
            next.enforce_group_qt?"Group QT enforced":"Group QT not enforced");
    } else {
        if (result==SCO_OK) std::snprintf(g.rules_message,sizeof(g.rules_message),"GameRules unknown: parameter set not received; boost UI paused");
        else std::snprintf(g.rules_message,sizeof(g.rules_message),"GameRules unknown/unreadable (result=%d); boost UI paused",static_cast<int>(result));
        std::snprintf(g.group_message,sizeof(g.group_message),"Group QT enforcement unknown; group membership is never inferred");
    }
}
inline bool RulesAllowBoost() { return g.rules_known && g.rules.quantum_travel_allowed && g.rules.quantum_boost_allowed; }
inline bool Held() { return g.leased && g.result==SCO_OK && g.observed.held; }
inline sco_result Read() {
    sc_quantum_state next{}; next.size=sizeof(next);
    auto r=sc_quantum_read_state(Control(),g.self,&next);
    if (r==SCO_OK && (next.size<sizeof(next) || next.enabled>1 || next.held>1 ||
        next.phase>SC_QUANTUM_BOOSTING || next.block>SC_QUANTUM_BLOCK_RESTART || next.progress>SC_QUANTUM_PROGRESS_MAX)) r=SCO_FAILED;
    g.result=r; g.observed=r==SCO_OK ? next : sc_quantum_state{};
    return r;
}
inline sco_result Enable(bool on) {
    if (on && !Cap()) return SCO_UNAVAILABLE;
    const auto r=sc_quantum_enable(Control(),g.self,on?1u:0u);
    if (r==SCO_OK) { g.leased=on; if (!on) g.input_held=false; }
    Read(); return r;
}
inline sco_result Hold(uint32_t ms) {
    if (!ms || ms>SC_QUANTUM_MAX_HOLD_MS) return SCO_BAD_ARG;
    if (!Cap() || !g.leased) return SCO_UNAVAILABLE;
    // Provider independently checks current owner, game thread, drive, EAC and offline admission.
    const auto r=sc_quantum_hold(Control(),g.self,ms); Read(); return r;
}
inline sco_result Release() {
    // Never require a true capability for owner-only cancellation/restoration.
    const auto r=sc_quantum_release(Control(),g.self);
    if (r==SCO_OK || r==SCO_NOT_FOUND) g.input_held=false;
    Read(); return r;
}
inline sco_result EnableCommand(const sco_arg* a,uint32_t n,void*,char* out,uint32_t cap) noexcept {
    if ((!out && cap) || !a || n!=1 || a[0].type!=SCO_ARG_BOOL || (a[0].v.i!=0 && a[0].v.i!=1)) return SCO_BAD_ARG;
    const auto r=Enable(a[0].v.i!=0);
    return Answer(r,out,cap,r==SCO_OK ? (a[0].v.i ? "G8 controller enabled; holding is bounded and admission checked per call" : "G8 controller disabled") : "G8 enable refused; missing adapter, admission or owned cleanup readiness");
}
inline sco_result HoldCommand(const sco_arg* a,uint32_t n,void*,char* out,uint32_t cap) noexcept {
    if ((!out && cap) || !a || n!=1 || a[0].type!=SCO_ARG_INT || a[0].v.i<1 || a[0].v.i>SC_QUANTUM_MAX_HOLD_MS) return SCO_BAD_ARG;
    const auto r=Hold(static_cast<uint32_t>(a[0].v.i));
    return Answer(r,out,cap,r==SCO_OK ? "G8 hold accepted (bounded; game tick expires the lease)" : "G8 hold refused; enable this owner and supply an admitted available drive");
}
inline sco_result ReleaseCommand(const sco_arg*,uint32_t n,void*,char* out,uint32_t cap) noexcept {
    if ((!out && cap) || n) return SCO_BAD_ARG;
    const auto r=Release(); return Answer(r,out,cap,r==SCO_OK ? "G8 hold released" : "G8 release unavailable or not owned by this plugin");
}
inline sco_result BoostCommand(const sco_arg*,uint32_t n,void*,char* out,uint32_t cap) noexcept {
    if ((!out && cap) || n) return SCO_BAD_ARG;
    Read(); const auto r=g.result==SCO_OK && g.observed.held && g.leased ? Release() : Hold(SC_QUANTUM_MAX_HOLD_MS);
    return Answer(r,out,cap,r==SCO_OK ? "G8 bounded boost toggled (maximum 1000 ms without refresh)" : "G8 boost unavailable; enable an admitted controller first");
}
inline sco_result StateCommand(const sco_arg*,uint32_t n,void*,char* out,uint32_t cap) noexcept {
    if ((!out && cap) || n) return SCO_BAD_ARG;
    ReadRules(); const auto r=Read(); char text[256]{};
    if (r==SCO_OK) std::snprintf(text,sizeof(text),"G8 enabled=%u held=%u phase=%u block=%u progress=%u/1000000; owned=%u",g.observed.enabled,g.observed.held,g.observed.phase,g.observed.block,g.observed.progress,g.leased?1u:0u);
    else std::snprintf(text,sizeof(text),"G8 state unknown/unavailable (result=%d); no private fallback",static_cast<int>(r));
    return Answer(r,out,cap,text);
}
inline bool Setting(const char* name,bool fallback) {
    const auto* settings=Open<sco_settings_v1>(SCO_SETTINGS_NAME); int32_t value=0;
    return QHAS(settings,sco_settings_v1,get_bool) && settings->get_bool(g.self,name,&value)==SCO_OK && (value==0 || value==1) ? value!=0 : fallback;
}
inline bool InputDown() {
#ifdef _WIN32
    DWORD pid=0; GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    if (!pid || pid!=GetCurrentProcessId()) return false;
    return (g.caps_input && (GetAsyncKeyState(VK_CAPITAL)&0x8000)) ||
           (g.mouse_input && (GetAsyncKeyState(VK_LBUTTON)&0x8000));
#else
    return false; // Portable fixtures exercise commands, not Windows input.
#endif
}
inline void RefreshPanel();
inline void Tick(const char*,const void* data,void*) noexcept {
    if (!g.api || !data) return;
    const auto now=*static_cast<const uint32_t*>(data);
    if (g.clocked && now-g.last_tick<50u) return;
    g.clocked=true; g.last_tick=now;
    Read();
    if (g.leased && (!Cap() || g.result!=SCO_OK || !g.observed.enabled)) {
        // The provider owns admission-loss and quarantine cleanup; do not restart a lost lease.
        sc_quantum_enable(Control(),g.self,0); g.leased=false; g.input_held=false;
    }
    if (g.leased) {
        g.caps_input=Setting("caps_input",true); g.mouse_input=Setting("mouse_input",false);
        const bool pressed=InputDown();
        if (pressed) {
            const auto r=Hold(150u);
            g.input_held=r==SCO_OK;
            if (r!=SCO_OK) Answer(r,nullptr,0,"G8 input hold refused; no private boost/effect fallback");
        } else if (g.input_held) Release();
    }
    if (now-g.last_panel>=250u) { g.last_panel=now; ReadRules(); try { RefreshPanel(); } catch (...) {} }
}
inline void AppendPanel(std::vector<sco_widget_node>& nodes) {
    auto add=[&](uint32_t kind,uint32_t id,const char* label)->sco_widget_node& {
        sco_widget_node node{}; node.size=sizeof(node); node.kind=kind; node.id=id; node.index=-1; node.label=label;
        nodes.push_back(node); return nodes.back();
    };
    add(SCO_WIDGET_LABEL,100,g.message);
    add(SCO_WIDGET_LABEL,101,"G8 owns charge/audio/effects; NAV interception unavailable without game-pack adapter");
    auto& enable=add(SCO_WIDGET_TOGGLE,102,"Enable owned quantum controller"); enable.value=g.leased?1:0;
    if (!Cap() && !g.leased) enable.flags=SCO_WIDGET_DISABLED;
    auto& boost=add(SCO_WIDGET_BUTTON,103,Held()?"Release current bounded hold":RulesAllowBoost()?"Bounded boost (1000 ms maximum)":
        g.rules_known?"Boost forbidden by received GameRules":"Boost unavailable: GameRules unknown");
    if (!Held() && (!Cap() || !g.leased || !RulesAllowBoost())) boost.flags=SCO_WIDGET_DISABLED;
    auto& release=add(SCO_WIDGET_BUTTON,104,"Release quantum hold"); if (!g.leased) release.flags=SCO_WIDGET_DISABLED;
    add(SCO_WIDGET_BUTTON,105,"Refresh quantum state and GameRules");
    add(SCO_WIDGET_LABEL,106,g.rules_message); add(SCO_WIDGET_LABEL,107,g.group_message);
}
inline void Event(const sco_widget_event* event,void* context) noexcept {
    if (!g.api || reinterpret_cast<uintptr_t>(context)!=g.epoch || !event || event->size<sizeof(*event)) return;
    char reply[256]{};
    if (event->node_id==102 && event->kind==SCO_WIDGET_TOGGLE && (event->value==0 || event->value==1)) {
        sco_arg arg{}; arg.type=SCO_ARG_BOOL; arg.v.i=event->value==1 ? 1:0; EnableCommand(&arg,1,nullptr,reply,sizeof(reply));
    } else if (event->kind==SCO_WIDGET_BUTTON) {
        if (event->node_id==103) {
            Read();
            if (Held()) ReleaseCommand(nullptr,0,nullptr,reply,sizeof(reply));
            else {
                ReadRules();
                if (RulesAllowBoost()) BoostCommand(nullptr,0,nullptr,reply,sizeof(reply));
                else Answer(SCO_UNAVAILABLE,reply,sizeof(reply),g.rules_known ?
                    "Boost UI refused: received GameRules forbid quantum travel or boost" :
                    "Boost UI refused: GameRules unknown/unreadable; native provider remains authoritative");
            }
        }
        if (event->node_id==104) ReleaseCommand(nullptr,0,nullptr,reply,sizeof(reply));
        if (event->node_id==105) StateCommand(nullptr,0,nullptr,reply,sizeof(reply));
    }
}
inline sco_result Load(const sco_api* api,sco_plugin* self) {
    if (!api || !self || g.api || !QHAS(api,sco_api,register_command) ||
        !QHAS(api,sco_api,subscribe) || !QHAS(api,sco_api,unsubscribe)) return SCO_UNAVAILABLE;
    if (next_epoch==(std::numeric_limits<uintptr_t>::max)()) return SCO_TOO_MANY;
    g.api=api; g.self=self; g.epoch=++next_epoch;
    const sco_arg_def on{"on",SCO_ARG_BOOL,0,"Enable this owner's bounded G8 controller"};
    const sco_arg_def ms{"milliseconds",SCO_ARG_INT,0,"1..1000 ms; expires on game ticks"};
    const sco_command commands[]{
        {sizeof(sco_command),0,"quantum.enable","Enable quantum controller","Explicit admission-gated owner lease; never load-time mutation",nullptr,&on,1,sizeof(on),EnableCommand,nullptr},
        {sizeof(sco_command),0,"quantum.hold","Hold quantum input","Bounded G8 hold",nullptr,&ms,1,sizeof(ms),HoldCommand,nullptr},
        {sizeof(sco_command),0,"quantum.release","Release quantum input","Owner cancellation remains available when capability closes",nullptr,nullptr,0,sizeof(sco_arg_def),ReleaseCommand,nullptr},
        {sizeof(sco_command),0,"quantum.boost","Bounded quantum boost","Toggle at most 1000 ms; Caps Lock refreshes only after explicit enable",nullptr,nullptr,0,sizeof(sco_arg_def),BoostCommand,nullptr},
        {sizeof(sco_command),0,"quantum.control_status","Quantum control state","Copied G8 state; unknown is explicit",nullptr,nullptr,0,sizeof(sco_arg_def),StateCommand,nullptr}
    };
    for (const auto& c:commands) { const auto r=api->register_command(self,&c); if (r!=SCO_OK) { g={}; return r; } }
    const auto r=api->subscribe(self,"tick",Tick,nullptr); if (r!=SCO_OK) { g={}; return r; }
    g.subscribed=true;
    const auto* ui=Open<sco_ui_v1>(SCO_UI_NAME);
    if (QHAS(ui,sco_ui_v1,bind_hotkey) && QHAS(ui,sco_ui_v1,unbind_hotkey)) g.hotkey=ui->bind_hotkey(self,"ctrl+alt+b","quantum.boost",nullptr,0)==SCO_OK;
    ReadRules(); Read(); return SCO_OK; // No enable, hold, charge write or effect starts at load.
}
inline void Unload() noexcept {
    if (!g.api) return;
    if (g.subscribed && QHAS(g.api,sco_api,unsubscribe)) g.api->unsubscribe(g.self,"tick",Tick);
    if (g.hotkey) { const auto* ui=Open<sco_ui_v1>(SCO_UI_NAME); if (QHAS(ui,sco_ui_v1,unbind_hotkey)) ui->unbind_hotkey(g.self,"ctrl+alt+b"); }
    // Provider's real owner-release hook restores engine state after host quarantine/unload;
    // ordinary unload also requests immediate cancellation, never blanket other-owner writes.
    if (g.leased) { sc_quantum_release(Control(),g.self); sc_quantum_enable(Control(),g.self,0); }
    g={};
}
#undef QHAS
}
