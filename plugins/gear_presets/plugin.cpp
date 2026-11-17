// SPDX-License-Identifier: GPL-3.0-only
#if __has_include(<scosdk/game/inventory_layout.hpp>)
#include "model.h"
#include "store.h"
#include <algorithm>
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
    std::string name = "My outfit", status = "Check what you're wearing, then save it as an outfit.";
    std::vector<SavedPreset> items, published;
    std::vector<Difference> differences;
    int32_t selection = -1; uintptr_t cookie = 0; uint64_t published_selection = 0;
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
    if (result!=SCO_OK) { g.status="Couldn't read what you're wearing right now, so nothing was saved."; return result; }
    g.captured=std::move(next); g.fresh=true;
    g.status="Checked what you're wearing. Give it a name and press Save outfit.";
    return SCO_OK;
}
sco_result DoSave() {
    if (!g.fresh) { g.status="Check what you're wearing before saving."; return SCO_UNAVAILABLE; }
    Preset next=g.captured; next.name=g.name;
    PresetStore store; auto result=store.Open(g.api,g.self); if (result!=SCO_OK) { g.status="Saved outfits can't be reached right now."; return result; }
    uint64_t id=0; result=store.Save(next,id);
    if (result!=SCO_OK) { g.status="Couldn't confirm the save. Refresh the list before trying again."; return result; }
    g.fresh=false;
    // Presentation must not turn a committed write into a reported failure.
    try {
        result=Reload();
        g.status=result==SCO_OK ? "Outfit saved. Pick it from the list to compare it with what you're wearing." : "Outfit saved, but the list couldn't refresh.";
    } catch (...) { /* the committed result remains successful */ }
    return SCO_OK;
}
sco_result DoDelete(uint64_t id) {
    // Prepare messages before the write so later allocation cannot change its result.
    std::string failed="Couldn't confirm the delete. Refresh the list before trying again.";
    std::string deleted="Outfit deleted.";
    std::string refresh_failed="Outfit deleted, but the list couldn't refresh.";
    std::string missing="That outfit wasn't found. Refresh the list and pick a saved outfit.";
    g.cookie=0; g.selection=-1; g.differences.clear();
    PresetStore store; auto result=store.Open(g.api,g.self);
    if(result!=SCO_OK) { g.status="Saved outfits can't be reached right now, so nothing was deleted."; return result; }
    g.status.swap(failed);
    result=store.Delete(id);
    if(result==SCO_OK) g.status.swap(deleted);
    else if(result==SCO_NOT_FOUND) g.status.swap(missing);
    // A failed provider call may have applied the deletion. Refresh is best effort.
    try {
        if(Reload()!=SCO_OK) {
            g.items.clear();
            if(result==SCO_OK) g.status.swap(refresh_failed);
        }
    } catch (...) {
        g.items.clear();
        if(result==SCO_OK) g.status.swap(refresh_failed);
    }
    return result;
}
// A slot the player has to do something about. Same-kind and uncompared slots are not differences.
bool Actionable(const Difference& d) { return d.advice!=Advice::ExactClassUnknown && d.advice!=Advice::Unclassified; }
sco_result DoCompare(uint64_t id) {
    g.differences.clear(); PresetStore store; auto result=store.Open(g.api,g.self); if(result!=SCO_OK) { g.status="Saved outfits can't be reached right now."; return result; }
    Preset saved,current; result=store.Load(id,saved); if(result!=SCO_OK) { g.status="Couldn't read that saved outfit."; return result; }
    result=CaptureNow(current); if(result!=SCO_OK) { g.status="Couldn't read what you're wearing right now, so there is nothing to compare."; return result; }
    if(!Compare(saved,current,g.differences)) { g.status="This outfit doesn't line up with your current gear, so it can't be compared. Save a new outfit instead."; return SCO_UNAVAILABLE; }
    g.status=std::none_of(g.differences.begin(),g.differences.end(),Actionable)
        ? "Compared with what you're wearing. Everything is the same kind of item as when you saved."
        : "Compared with what you're wearing. The differences are below. Nothing was changed, so swap items yourself.";
    return SCO_OK;
}
// What to do about one slot, in the player's words. Only the kind of item is compared, never the exact item.
const char* AdviceText(Advice a) {
    switch(a) {
    case Advice::ExactClassUnknown: return "same kind of item as when you saved.";
    case Advice::EquipCategory: return "you had an item here when you saved, and nothing now. Put one on.";
    case Advice::RemoveItem: return "this was empty when you saved. Take off what you have here.";
    case Advice::ReplaceCategory: return "a different kind of item than when you saved. Swap it yourself.";
    case Advice::Unclassified: return "not compared.";
    default: return "couldn't be compared. Check it yourself.";
    }
}
// "helmet" -> "Helmet", "left_arm" -> "Left arm": slot names as a player reads them.
std::string SlotName(const std::string& key) {
    std::string name;
    for (const char c:key) name+=(c=='_' || c=='-' || c=='.') ? ' ' : c;
    if (!name.empty() && name[0]>='a' && name[0]<='z') name[0]=static_cast<char>(name[0]-('a'-'A'));
    return name;
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
            if (Reload()!=SCO_OK) g.status="The saved outfit list isn't available right now.";
        } else if (event->node_id==5 && event->kind==SCO_WIDGET_COMBO && event->index>=0 &&
                   static_cast<size_t>(event->index)<g.published.size()) {
            const auto id=g.published[static_cast<size_t>(event->index)].id;
            g.selection=-1;
            for(size_t i=0;i<g.items.size();++i) if(g.items[i].id==id) g.selection=static_cast<int32_t>(i);
        } else if (event->node_id==6 && event->kind==SCO_WIDGET_BUTTON && g.selection>=0 &&
                   static_cast<size_t>(g.selection)<g.items.size()) (void)DoCompare(g.items[static_cast<size_t>(g.selection)].id);
        else if (event->node_id==9 && event->kind==SCO_WIDGET_BUTTON && g.selection>=0 &&
                   static_cast<size_t>(g.selection)<g.items.size() && g.published_selection!=0 &&
                   g.items[static_cast<size_t>(g.selection)].id==g.published_selection) (void)DoDelete(g.published_selection);
    } catch (...) {
        try { g.status="That didn't work. Try again."; } catch (...) {}
    }
    // Presentation failure must not overwrite the operation's result or escape the callback.
    try { Publish(); } catch (...) {}
}
void Publish() {
    const auto* w=Widgets(); if (!HAS(w,sco_widgets_v1,set_panel) || next_cookie==std::numeric_limits<uintptr_t>::max()) return;
    std::vector<sco_widget_node> nodes; std::vector<std::string> labels; std::vector<const char*> names;
    nodes.reserve(140); labels.reserve(g.differences.size()); names.reserve(g.items.size());
    for (const auto& item:g.items) names.push_back(item.name.c_str());
    auto add=[&](uint32_t kind,uint32_t id,const char* label,uint32_t flags=0)->sco_widget_node& {
        sco_widget_node node{}; node.size=sizeof(node); node.kind=kind; node.id=id; node.label=label; node.flags=flags; nodes.push_back(node); return nodes.back();
    };
    auto under=[&](uint32_t parent,uint32_t kind,uint32_t id,const char* label,uint32_t flags=0)->sco_widget_node& {
        auto& node=add(kind,id,label,flags); node.parent=parent; return node;
    };
    // In the order a player uses it: check what you wear, save it, then compare or delete saved outfits.
    add(SCO_WIDGET_COLUMN,20,"Save what you're wearing");
    under(20,SCO_WIDGET_TEXT_INPUT,1,"Outfit name").text=g.name.c_str();
    add(SCO_WIDGET_ROW,23,"Save actions").parent=20;
    under(23,SCO_WIDGET_BUTTON,2,"Check what I'm wearing");
    under(23,SCO_WIDGET_BUTTON,3,"Save outfit",g.fresh ? 0u : SCO_WIDGET_DISABLED);
    add(SCO_WIDGET_COLUMN,21,"Saved outfits");
    auto& picker=under(21,SCO_WIDGET_COMBO,5,"Saved outfit"); picker.items=names.empty()?nullptr:names.data(); picker.item_count=static_cast<uint32_t>(names.size()); picker.index=g.selection;
    add(SCO_WIDGET_ROW,24,"Saved outfit actions").parent=21;
    under(24,SCO_WIDGET_BUTTON,6,"Compare with what I'm wearing",g.selection>=0 ? 0u : SCO_WIDGET_DISABLED);
    under(24,SCO_WIDGET_BUTTON,9,"Delete outfit",g.selection>=0 ? 0u : SCO_WIDGET_DISABLED);
    under(24,SCO_WIDGET_BUTTON,4,"Refresh list");
    under(21,SCO_WIDGET_LABEL,8,"Comparing only tells you what differs. It never changes your gear.");
    add(SCO_WIDGET_LABEL,7,g.status.c_str());
    if (std::any_of(g.differences.begin(),g.differences.end(),Actionable)) add(SCO_WIDGET_COLUMN,22,"What is different");
    uint32_t id=100;
    for (const auto& d:g.differences) {
        if (!Actionable(d)) continue;
        labels.push_back(SlotName(d.key)+": "+AdviceText(d.advice));
        under(22,SCO_WIDGET_LABEL,id++,labels.back().c_str());
    }
    auto published=g.items; // Allocate before accepting a new callback.
    const uint64_t selected=g.selection>=0 && static_cast<size_t>(g.selection)<published.size()
        ? published[static_cast<size_t>(g.selection)].id : 0;
    const uintptr_t cookie=++next_cookie;
    if(w->set_panel(g.self,"gear_presets.main","Gear presets",nodes.data(),static_cast<uint32_t>(nodes.size()),sizeof(nodes[0]),Event,reinterpret_cast<void*>(cookie))==SCO_OK) {
        g.published.swap(published); g.published_selection=selected; g.cookie=cookie;
    }
}
// `detail` replaces the panel's sentence in the command reply only (the console lists IDs there).
sco_result Reply(sco_result result,char* reply,uint32_t size,const std::string* detail=nullptr) {
    if(reply && size) std::snprintf(reply,size,"%s",(detail ? *detail : g.status).c_str());
    try { Publish(); } catch (...) {} return result;
}
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
        g.status=r==SCO_OK ? "Refreshed your saved outfits." : "The saved outfit list isn't available right now.";
        std::string ids=r==SCO_OK ? "Saved preset IDs:" : g.status;
        if (r==SCO_OK) {
            for (const auto& item:g.items) {
                const auto next=" "+std::to_string(item.id);
                if (ids.size()+next.size()>220) { ids+=" ..."; break; }
                ids+=next;
            }
            if (g.items.empty()) ids+=" none";
        }
        return Reply(r,reply,size,&ids);
    }
    catch (...) { return SCO_TOO_MANY; }
}
sco_result DeleteCommand(const sco_arg* args,uint32_t n,void*,char* reply,uint32_t size) {
    try { if(n!=1 || !args || args[0].type!=SCO_ARG_INT || args[0].v.i<=0) return SCO_BAD_ARG;
        return Reply(DoDelete(static_cast<uint64_t>(args[0].v.i)),reply,size);
    } catch (...) { return SCO_TOO_MANY; }
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
            {"gear_presets.compare","Compare gear preset",CompareCommand,&id,1},
            {"gear_presets.delete","Delete saved gear preset",DeleteCommand,&id,1} };
        for(const auto& c:commands) {
            sco_command command{}; command.size=sizeof(command); command.name=c.name; command.title=c.title;
            command.help=c.fn==DeleteCommand ? "Delete one saved preset by ID." : "Category comparison; no equipment changes."; command.args=c.arg; command.nargs=c.count;
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

#endif // SDK inventory layout is available
