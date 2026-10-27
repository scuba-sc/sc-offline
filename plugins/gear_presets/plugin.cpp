// SPDX-License-Identifier: GPL-3.0-only
#include "model.h"
#include "store.h"
#include <scosdk/game/inventory_layout.hpp>
#include <sco_widgets.h>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace sco::features::gear_presets {
namespace {
#define HAS(t, type, member) ((t) && (t)->size >= offsetof(type, member) + sizeof((t)->member) && (t)->member)
struct State {
    const sco_api* api = nullptr; sco_plugin* self = nullptr;
    Preset captured; bool fresh = false;
    std::string name = "Preset", status = "Capture a complete layout to save a preset.";
    std::vector<SavedPreset> items, published;
    std::vector<Difference> differences;
    int32_t selection = -1; uintptr_t cookie = 0;
} g;
uintptr_t next_cookie = 0;
const sco_plugin_info info{sizeof(sco_plugin_info),SCO_API_MAJOR,SCO_API_MINOR,"gear_presets","1.0.0","sc-offline"};
const sco_widgets_v1* Widgets() {
    if (!HAS(g.api,sco_api,query_service)) return nullptr;
    const void* raw=nullptr;
    return g.api->query_service(SCO_WIDGETS_NAME,SCO_WIDGETS_VERSION_1_0,&raw)==SCO_OK ? static_cast<const sco_widgets_v1*>(raw) : nullptr;
}
sco_result CaptureNow(Preset& out) {
    sco::sdk::game::InventoryLayout layout;
    auto result=layout.Open(g.api,g.self); if (result!=SCO_OK) return result;
    sc_inventory_layout_snapshot snapshot{}; std::array<sc_inventory_layout_entry,128> rows{};
    result=layout.Snapshot(snapshot,rows); if (result!=SCO_OK) return result;
    return Capture("Captured",snapshot,rows,out) ? SCO_OK : SCO_UNAVAILABLE;
}
sco_result Reload() {
    PresetStore store; auto result=store.Open(g.api,g.self); if (result!=SCO_OK) return result;
    std::vector<SavedPreset> next; result=store.List(next); if (result!=SCO_OK) return result;
    g.items=std::move(next); g.selection=-1; return SCO_OK;
}
sco_result DoCapture() {
    g.fresh=false; g.differences.clear(); Preset next; const auto result=CaptureNow(next);
    if (result!=SCO_OK) { g.status="Layout unavailable or incomplete; no capture saved."; return result; }
    g.captured=std::move(next); g.fresh=true;
    g.status="Captured complete layout. Categories are coarse; exact item classes are unknown.";
    return SCO_OK;
}
sco_result DoSave() {
    if (!g.fresh) { g.status="Capture a complete layout before saving."; return SCO_UNAVAILABLE; }
    Preset next=g.captured; next.name=g.name;
    PresetStore store; auto result=store.Open(g.api,g.self); if (result!=SCO_OK) { g.status="Preset storage unavailable."; return result; }
    uint64_t id=0; result=store.Save(next,id);
    if (result!=SCO_OK) { g.status="Save failed; previous presets are unchanged."; return result; }
    g.fresh=false;
    // Presentation must not turn a committed write into a reported failure.
    try {
        result=Reload();
        g.status=result==SCO_OK ? "Preset saved. Select it to compare a fresh layout." : "Preset saved, but the picker could not refresh.";
    } catch (...) { /* the committed result remains successful */ }
    return SCO_OK;
}
sco_result DoCompare(uint64_t id) {
    g.differences.clear(); PresetStore store; auto result=store.Open(g.api,g.self); if(result!=SCO_OK) { g.status="Preset storage unavailable."; return result; }
    Preset saved,current; result=store.Load(id,saved); if(result!=SCO_OK) { g.status="Preset could not be read."; return result; }
    result=CaptureNow(current); if(result!=SCO_OK) { g.status="Current layout unavailable or incomplete."; return result; }
    if(!Compare(saved,current,g.differences)) { g.status="Layouts differ; capture a new preset for this layout."; return SCO_UNAVAILABLE; }
    g.status="Category comparison only. Review gear manually; no changes are applied."; return SCO_OK;
}
const char* AdviceText(Advice a) {
    switch(a) {
    case Advice::ExactClassUnknown: return "same category; exact class unknown";
    case Advice::EquipCategory: return "manually equip required category";
    case Advice::RemoveItem: return "captured empty; inspect or remove manually";
    case Advice::ReplaceCategory: return "different category; inspect or replace manually";
    case Advice::Unclassified: return "unclassified port; no gear advice";
    default: return "unknown; inspect manually";
    }
}
void Publish();
void Event(const char* panel, const sco_widget_event* event, void* ctx) {
    if (!g.api || !panel || std::strcmp(panel,"gear_presets.main") || !event || event->size<sizeof(*event) ||
        reinterpret_cast<uintptr_t>(ctx)!=g.cookie) return;
    try {
        if (event->node_id==1 && event->kind==SCO_WIDGET_TEXT_INPUT && event->text) {
            size_t n=0; while(n<=80 && event->text[n]) ++n;
            if(n<=80) g.name.assign(event->text,n);
        } else if (event->node_id==2 && event->kind==SCO_WIDGET_BUTTON) (void)DoCapture();
        else if (event->node_id==3 && event->kind==SCO_WIDGET_BUTTON) (void)DoSave();
        else if (event->node_id==4 && event->kind==SCO_WIDGET_BUTTON) {
            if (Reload()!=SCO_OK) g.status="Preset picker unavailable.";
        } else if (event->node_id==5 && event->kind==SCO_WIDGET_COMBO && event->index>=0 &&
                   static_cast<size_t>(event->index)<g.published.size()) {
            const auto id=g.published[static_cast<size_t>(event->index)].id;
            g.selection=-1;
            for(size_t i=0;i<g.items.size();++i) if(g.items[i].id==id) g.selection=static_cast<int32_t>(i);
        } else if (event->node_id==6 && event->kind==SCO_WIDGET_BUTTON && g.selection>=0 &&
                   static_cast<size_t>(g.selection)<g.items.size()) (void)DoCompare(g.items[static_cast<size_t>(g.selection)].id);
        Publish();
    } catch (...) { g.status="Preset operation failed."; }
}
void Publish() {
    const auto* w=Widgets(); if (!HAS(w,sco_widgets_v1,set_panel) || next_cookie==std::numeric_limits<uintptr_t>::max()) return;
    std::vector<sco_widget_node> nodes; std::vector<std::string> labels; std::vector<const char*> names;
    nodes.reserve(140); labels.reserve(g.differences.size()); names.reserve(g.items.size());
    for (const auto& item:g.items) names.push_back(item.name.c_str());
    auto add=[&](uint32_t kind,uint32_t id,const char* label,uint32_t flags=0)->sco_widget_node& {
        sco_widget_node node{}; node.size=sizeof(node); node.kind=kind; node.id=id; node.label=label; node.flags=flags; nodes.push_back(node); return nodes.back();
    };
    add(SCO_WIDGET_TEXT_INPUT,1,"Preset name").text=g.name.c_str();
    add(SCO_WIDGET_BUTTON,2,"Capture complete layout");
    add(SCO_WIDGET_BUTTON,3,"Save captured preset",g.fresh ? 0u : SCO_WIDGET_DISABLED);
    add(SCO_WIDGET_BUTTON,4,"Refresh saved presets");
    auto& picker=add(SCO_WIDGET_COMBO,5,"Saved preset"); picker.items=names.empty()?nullptr:names.data(); picker.item_count=static_cast<uint32_t>(names.size()); picker.index=g.selection;
    add(SCO_WIDGET_BUTTON,6,"Compare current layout",g.selection>=0 ? 0u : SCO_WIDGET_DISABLED);
    add(SCO_WIDGET_LABEL,7,g.status.c_str());
    add(SCO_WIDGET_LABEL,8,"Category-level advice only; no equipment or movement commands are executed.");
    uint32_t id=100;
    for (const auto& d:g.differences) {
        char line[240];
        if (d.advice==Advice::EquipCategory)
            std::snprintf(line,sizeof(line),"%s: %s (saved category %u)",d.key.c_str(),AdviceText(d.advice),d.wanted);
        else if (d.advice==Advice::ReplaceCategory || d.advice==Advice::ExactClassUnknown)
            std::snprintf(line,sizeof(line),"%s: %s (saved %u, current %u)",d.key.c_str(),AdviceText(d.advice),d.wanted,d.current);
        else std::snprintf(line,sizeof(line),"%s: %s",d.key.c_str(),AdviceText(d.advice));
        labels.emplace_back(line); add(SCO_WIDGET_LABEL,id++,labels.back().c_str());
    }
    const uintptr_t cookie=++next_cookie;
    if(w->set_panel(g.self,"gear_presets.main","Gear presets",nodes.data(),static_cast<uint32_t>(nodes.size()),sizeof(nodes[0]),Event,reinterpret_cast<void*>(cookie))==SCO_OK) {
        g.cookie=cookie; g.published=g.items;
    }
}
sco_result Reply(sco_result result,char* reply,uint32_t size) { if(reply && size) std::snprintf(reply,size,"%s",g.status.c_str()); try { Publish(); } catch (...) {} return result; }
sco_result CaptureCommand(const sco_arg*,uint32_t n,void*,char* reply,uint32_t size) { try { return n ? SCO_BAD_ARG : Reply(DoCapture(),reply,size); } catch (...) { return SCO_TOO_MANY; } }
sco_result SaveCommand(const sco_arg* args,uint32_t n,void*,char* reply,uint32_t size) {
    try { if(n!=1 || !args || args[0].type!=SCO_ARG_STRING || !args[0].v.s) return SCO_BAD_ARG;
        size_t len=0; while(len<=80 && args[0].v.s[len]) ++len; if(len>80) return SCO_BAD_ARG;
        g.name.assign(args[0].v.s,len); return Reply(DoSave(),reply,size);
    } catch (...) { return SCO_TOO_MANY; }
}
sco_result ListCommand(const sco_arg*,uint32_t n,void*,char* reply,uint32_t size) {
    try {
        if(n) return SCO_BAD_ARG;
        auto r=Reload();
        g.status=r==SCO_OK ? "Saved preset IDs:" : "Preset picker unavailable.";
        if (r==SCO_OK) {
            for (const auto& item:g.items) {
                const auto next=" "+std::to_string(item.id);
                if (g.status.size()+next.size()>220) { g.status+=" ..."; break; }
                g.status+=next;
            }
            if (g.items.empty()) g.status+=" none";
        }
        return Reply(r,reply,size);
    }
    catch (...) { return SCO_TOO_MANY; }
}
sco_result CompareCommand(const sco_arg* args,uint32_t n,void*,char* reply,uint32_t size) {
    try { if(n!=1 || !args || args[0].type!=SCO_ARG_INT || args[0].v.i<=0) return SCO_BAD_ARG;
        auto r=DoCompare(static_cast<uint64_t>(args[0].v.i)); return Reply(r,reply,size);
    } catch (...) { return SCO_TOO_MANY; }
}
}
extern "C" SCO_EXPORT const sco_plugin_info* sco_plugin_query() { return &info; }
extern "C" SCO_EXPORT sco_result sco_plugin_load(const sco_api* api,sco_plugin* self) {
    try {
        if(g.api || !self || !HAS(api,sco_api,register_command) || !HAS(api,sco_api,query_service) || !HAS(api,sco_api,has)) return SCO_BAD_ARG;
        g=State{}; g.api=api; g.self=self;
        const sco_arg_def name{"name",SCO_ARG_STRING,0,"UTF-8 preset name"}, id{"id",SCO_ARG_INT,0,"Saved preset ID"};
        struct Command { const char* name; const char* title; sco_command_fn fn; const sco_arg_def* arg; uint32_t count; };
        const Command commands[]{ {"gear_presets.capture","Capture gear layout",CaptureCommand,nullptr,0},
            {"gear_presets.save","Save captured gear preset",SaveCommand,&name,1},
            {"gear_presets.list","List gear presets",ListCommand,nullptr,0},
            {"gear_presets.compare","Compare gear preset",CompareCommand,&id,1} };
        for(const auto& c:commands) {
            sco_command command{}; command.size=sizeof(command); command.name=c.name; command.title=c.title;
            command.help="Read-only category comparison; no equipment changes."; command.args=c.arg; command.nargs=c.count;
            command.arg_def_size=sizeof(sco_arg_def); command.fn=c.fn;
            const auto result=api->register_command(self,&command); if(result!=SCO_OK) { g=State{}; return result; }
        }
        (void)Reload(); Publish(); return SCO_OK;
    } catch (...) { g=State{}; return SCO_TOO_MANY; }
}
extern "C" SCO_EXPORT void sco_plugin_unload() {
    try { const auto* w=Widgets(); if (HAS(w,sco_widgets_v1,remove_panel)) (void)w->remove_panel(g.self,"gear_presets.main"); g=State{}; }
    catch (...) { g=State{}; }
}
}
