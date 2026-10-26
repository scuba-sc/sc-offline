// SPDX-License-Identifier: GPL-3.0-only
// Synthetic public-service workflow; no renderer, host implementation or game.
#include "loadout.h"
#include "sco_content.h"
#include "sco_widgets.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {
struct File { const char* name; std::string bytes; };
struct Panel {
    sco_widget_event_fn event = nullptr;
    void* context = nullptr;
    unsigned accepted = 0;
    std::map<uint32_t,uint32_t> combos;
};
std::vector<File> files;
Panel player, sq42;
unsigned rejected = 0, removed = 0, unsubscribed = 0;
sco_content_v1 content{};
sco_widgets_v1 widgets{};

void Require(bool condition,const char* message) {
    if (!condition) { std::fprintf(stderr,"loadout widgets: %s\n",message); std::exit(1); }
}
sco_result Item(sco_plugin*,sco_content_kind kind,uint32_t ordinal,sco_content_item* out) {
    if (kind!=SCO_CONTENT_LIST || !out || out->size<sizeof(*out)) return SCO_BAD_ARG;
    if (ordinal>=files.size()) return SCO_NOT_FOUND;
    *out={}; out->size=sizeof(*out); out->kind=kind; out->id=ordinal+1;
    std::snprintf(out->pack,sizeof(out->pack),"fixture");
    std::snprintf(out->name,sizeof(out->name),"%s",files[ordinal].name);
    return SCO_OK;
}
sco_result Read(sco_plugin*,uint64_t id,void* out,uint32_t* size) {
    if (!size || (!out && *size)) return SCO_BAD_ARG;
    if (!id || id>files.size()) return SCO_NOT_FOUND;
    const auto& bytes=files[static_cast<size_t>(id-1)].bytes;
    if (*size<bytes.size()) { *size=static_cast<uint32_t>(bytes.size()); return SCO_TOO_MANY; }
    if (!bytes.empty()) std::memcpy(out,bytes.data(),bytes.size());
    *size=static_cast<uint32_t>(bytes.size()); return SCO_OK;
}
sco_result SetPanel(sco_plugin*,const char* id,const char*,const sco_widget_node* nodes,
                    uint32_t count,uint32_t stride,sco_widget_event_fn event,void* context) {
    Require(nodes && count && stride==sizeof(sco_widget_node),"invalid panel extent");
    std::map<uint32_t,uint32_t> combos;
    for (uint32_t i=0;i<count;++i) {
        const auto& node=nodes[i];
        Require(node.size==sizeof(node),"invalid node extent");
        // sco.widgets requires a NULL array exactly when the item count is zero.
        // A rejected replacement must preserve the previous panel and callback.
        if ((node.item_count && !node.items) || (!node.item_count && node.items)) {
            ++rejected; return SCO_BAD_ARG;
        }
        if (node.kind==SCO_WIDGET_COMBO) {
            Require(node.index>=-1 && (node.index<0 || static_cast<uint32_t>(node.index)<node.item_count),"invalid combo selection");
            for (uint32_t j=0;j<node.item_count;++j) Require(node.items[j]!=nullptr,"missing combo option");
            combos[node.id]=node.item_count;
        }
    }
    Require(!std::strcmp(id,"loadout.player") || !std::strcmp(id,"loadout.sq42"),"unexpected panel id");
    auto& panel=!std::strcmp(id,"loadout.player") ? player : sq42;
    panel.event=event; panel.context=context; ++panel.accepted; panel.combos=std::move(combos);
    return SCO_OK;
}
sco_result RemovePanel(sco_plugin*,const char*) { ++removed; return SCO_OK; }
sco_result Query(const char* name,uint32_t version,const void** out) {
    Require(version==0x10000u,"consumer changed service version");
    *out=nullptr;
    if (!std::strcmp(name,SCO_CONTENT_NAME)) *out=&content;
    if (!std::strcmp(name,SCO_WIDGETS_NAME)) *out=&widgets;
    return *out ? SCO_OK : SCO_NOT_FOUND;
}
sco_result Register(sco_plugin*,const sco_command*) { return SCO_OK; }
sco_result Subscribe(sco_plugin*,const char*,sco_event_fn,void*) { return SCO_OK; }
sco_result Unsubscribe(sco_plugin*,const char*,sco_event_fn) { ++unsubscribed; return SCO_OK; }
int Has(const char*) { return 0; }
void Send(const char* id,Panel& panel,uint32_t node,uint32_t kind,const char* text=nullptr) {
    Require(panel.event!=nullptr,"missing panel callback");
    sco_widget_event event{}; event.size=sizeof(event); event.node_id=node; event.kind=kind; event.text=text;
    panel.event(id,&event,panel.context);
}
void Workflow(bool populated) {
    files={{"lists/items.txt","[helmet]\nFixtureHelmet\n"}};
    if (populated) {
        files.push_back({"lists/outfits.txt","[Fixture outfit]\nArmor_Helmet FixtureHelmet\n"});
        files.push_back({"lists/buildables.txt","[sq42]\nFixtureObject\n"});
    }
    player={}; sq42={}; rejected=0; removed=0; unsubscribed=0;
    sco_api api{}; api.size=sizeof(api); api.major=SCO_API_MAJOR; api.minor=SCO_API_MINOR;
    api.has=Has; api.register_command=Register; api.query_service=Query;
    api.subscribe=Subscribe; api.unsubscribe=Unsubscribe;
    unsigned char owner=0; auto* self=reinterpret_cast<sco_plugin*>(&owner);
    Require(sco::features::loadout::Load(&api,self)==SCO_OK,"load failed");
    Require(!rejected && player.accepted==1 && sq42.accepted==1,"initial panels rejected");
    Require(player.combos.at(2)==1 && sq42.combos.empty(),"initial picker/confirmation mismatch");
    Send("loadout.sq42",sq42,100,SCO_WIDGET_BUTTON);
    Require(!rejected && sq42.accepted==2,"SQ42 panel rejected after spoiler confirmation");
    Require(sq42.combos.at(101)==(populated ? 1u : 0u),"outfit picker count mismatch");
    Require(sq42.combos.at(130)==(populated ? 1u : 0u),"build picker count mismatch");
    Send("loadout.player",player,1,SCO_WIDGET_TEXT_INPUT,"no matching gear");
    Require(!rejected && player.combos.at(2)==0,"empty filtered picker rejected");
    Send("loadout.player",player,1,SCO_WIDGET_TEXT_INPUT,"");
    Require(!rejected && player.combos.at(2)==1,"restored gear picker rejected");
    sco::features::loadout::Unload(); sco::features::loadout::Unload();
    Require(removed==2 && unsubscribed==1,"unload cleanup mismatch");
}
}
int main() {
    content.size=sizeof(content); content.item=Item; content.read=Read;
    widgets.size=sizeof(widgets); widgets.set_panel=SetPanel; widgets.remove_panel=RemovePanel;
    Workflow(false); Workflow(true);
    std::puts("loadout widget workflow: PASS (synthetic public services; not run in game)");
}
