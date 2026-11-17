// SPDX-License-Identifier: GPL-3.0-only
// SDK-only quantum policy. Engine charge, sound and effect state stays behind the game-pack provider.
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
    char message[256]=""; // plain result line shown on the Travel page
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
// text goes to the command reply; plain (default: the same text) is the sentence the page shows.
inline sco_result Answer(sco_result r,char* out,uint32_t n,const char* text,const char* plain=nullptr) {
    std::snprintf(g.message,sizeof(g.message),"%s",plain?plain:text);
    if (out && n) std::snprintf(out,n,"%s",text);
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
    return Answer(r,out,cap,r==SCO_OK ? (a[0].v.i ? "Quantum drive controls are on. Hold Caps Lock to boost." : "Quantum drive controls are off.") : "Couldn't turn on the quantum drive controls. They aren't available in this game right now.");
}
inline sco_result HoldCommand(const sco_arg* a,uint32_t n,void*,char* out,uint32_t cap) noexcept {
    if ((!out && cap) || !a || n!=1 || a[0].type!=SCO_ARG_INT || a[0].v.i<1 || a[0].v.i>SC_QUANTUM_MAX_HOLD_MS) return SCO_BAD_ARG;
    const auto r=Hold(static_cast<uint32_t>(a[0].v.i));
    return Answer(r,out,cap,r==SCO_OK ? "Boost started." : "Couldn't start boost. Turn the quantum drive controls on first.");
}
inline sco_result ReleaseCommand(const sco_arg*,uint32_t n,void*,char* out,uint32_t cap) noexcept {
    if ((!out && cap) || n) return SCO_BAD_ARG;
    const auto r=Release(); return Answer(r,out,cap,r==SCO_OK ? "Boost stopped." : "There is no boost to stop.");
}
inline sco_result BoostCommand(const sco_arg*,uint32_t n,void*,char* out,uint32_t cap) noexcept {
    if ((!out && cap) || n) return SCO_BAD_ARG;
    Read(); const bool stopping=g.result==SCO_OK && g.observed.held && g.leased;
    const auto r=stopping ? Release() : Hold(SC_QUANTUM_MAX_HOLD_MS);
    return Answer(r,out,cap,r==SCO_OK ? (stopping ? "Boost stopped." : "Boost started. It lasts about a second unless you hold Caps Lock.") :
        "Couldn't boost. Turn the quantum drive controls on first.");
}
inline sco_result StateCommand(const sco_arg*,uint32_t n,void*,char* out,uint32_t cap) noexcept {
    if ((!out && cap) || n) return SCO_BAD_ARG;
    ReadRules(); const auto r=Read(); char text[256]{};
    if (r==SCO_OK) std::snprintf(text,sizeof(text),"G8 enabled=%u held=%u phase=%u block=%u progress=%u/1000000; owned=%u",g.observed.enabled,g.observed.held,g.observed.phase,g.observed.block,g.observed.progress,g.leased?1u:0u);
    else std::snprintf(text,sizeof(text),"G8 state unknown/unavailable (result=%d); no private fallback",static_cast<int>(r));
    // The command reply keeps the technical state; the page gets a sentence.
    return Answer(r,out,cap,text,r!=SCO_OK ? "The quantum drive isn't available right now." :
        g.observed.held ? "Boost is on." : g.leased ? "Quantum drive controls are on." : "Quantum drive controls are off.");
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
            if (r!=SCO_OK) Answer(r,nullptr,0,"Couldn't boost right now.");
        } else if (g.input_held) Release();
    }
    if (now-g.last_panel>=250u) { g.last_panel=now; ReadRules(); try { RefreshPanel(); } catch (...) {} }
}
inline void AppendPanel(std::vector<sco_widget_node>& nodes) {
    // Nothing to show where the drive can't be controlled: no dead buttons.
    if (!Cap() && !g.leased) return;
    auto add=[&](uint32_t kind,uint32_t id,const char* label)->sco_widget_node& {
        sco_widget_node node{}; node.size=sizeof(node); node.kind=kind; node.id=id; node.parent=kind==SCO_WIDGET_COLUMN?0:110; node.index=-1; node.label=label;
        nodes.push_back(node); return nodes.back();
    };
    add(SCO_WIDGET_COLUMN,110,"Quantum drive");
    add(SCO_WIDGET_TOGGLE,102,"Use quantum drive controls").value=g.leased?1:0;
    add(SCO_WIDGET_LABEL,101,g.leased ? "Hold Caps Lock to boost." : "Turn the controls on to boost.");
    auto& boost=add(SCO_WIDGET_BUTTON,103,Held()?"Stop boost":"Boost");
    if (!Held() && (!Cap() || !g.leased || !RulesAllowBoost())) boost.flags=SCO_WIDGET_DISABLED;
    if (g.rules_known && !RulesAllowBoost()) add(SCO_WIDGET_LABEL,106,"Boost isn't allowed here.");
    else if (!g.rules_known && g.leased && !Held()) add(SCO_WIDGET_LABEL,108,"Boost turns on once the game says whether it's allowed here.");
    if (g.rules_known && g.rules.enforce_group_qt) add(SCO_WIDGET_LABEL,107,"Quantum travel may need a group in this game.");
    if (g.message[0]) add(SCO_WIDGET_LABEL,100,g.message);
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
                    "Boost isn't allowed here." : "Boost isn't available right now.");
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
    const sco_arg_def on{"on",SCO_ARG_BOOL,0,"Turn the quantum drive controls on (1) or off (0)"};
    const sco_arg_def ms{"milliseconds",SCO_ARG_INT,0,"How long to boost, 1 to 1000 ms"};
    const sco_command commands[]{
        {sizeof(sco_command),0,"quantum.enable","Quantum drive controls","Turn the quantum drive controls on or off. They never turn on by themselves",nullptr,&on,1,sizeof(on),EnableCommand,nullptr},
        {sizeof(sco_command),0,"quantum.hold","Hold quantum boost","Boost for up to one second",nullptr,&ms,1,sizeof(ms),HoldCommand,nullptr},
        {sizeof(sco_command),0,"quantum.release","Stop quantum boost","Stop the boost this plugin started",nullptr,nullptr,0,sizeof(sco_arg_def),ReleaseCommand,nullptr},
        {sizeof(sco_command),0,"quantum.boost","Quantum boost","Start or stop a boost of up to one second. Holding Caps Lock keeps it going once the controls are on",nullptr,nullptr,0,sizeof(sco_arg_def),BoostCommand,nullptr},
        {sizeof(sco_command),0,"quantum.control_status","Quantum drive state","Show whether the quantum drive controls are on",nullptr,nullptr,0,sizeof(sco_arg_def),StateCommand,nullptr}
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
