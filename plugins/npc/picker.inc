// SPDX-License-Identifier: GPL-3.0-only
struct Picker {
    sco::picker::Catalog catalog;
    std::string archetype;
    int64_t amount=1;
    int32_t selected=-1;
    uint32_t page=0;
    char reply[256]{};
} picker;
void RenderPicker();
void PickerEvent(const char* panel,const sco_widget_event* e,void*) {
    using namespace sco::picker;
    if(!g.api || !panel || std::strcmp(panel,"npc.npcs") || !e || e->size<sizeof(*e))return;
    if(Event(e,SCO_WIDGET_TEXT_INPUT) && Text(e->text,SCO_WIDGETS_MAX_TEXT)) {
        if(e->node_id==1){picker.catalog.filter=e->text;picker.catalog.page=0;}
        else if(e->node_id==3 && Text(e->text,127))picker.archetype=e->text;
    } else if(Event(e,SCO_WIDGET_LIST) && e->node_id==2) {
        if(auto* value=picker.catalog.Select(e->index))picker.archetype=value;
    } else if(Event(e,SCO_WIDGET_COMBO) && e->node_id==9) {
        if(e->index>=0 && static_cast<uint32_t>(e->index)+picker.page*64<g.count)picker.selected=e->index+static_cast<int32_t>(picker.page*64);
    } else if(Event(e,SCO_WIDGET_SLIDER) && e->node_id==4 && std::isfinite(e->value) && e->value>=1 && e->value<=10)picker.amount=static_cast<int64_t>(e->value);
    else if(Event(e,SCO_WIDGET_BUTTON)) {
        const sco_arg args[]={String(picker.archetype.c_str()),Int(picker.amount)};
        switch(e->node_id) {
        case 5:(void)Spawn(args,2,nullptr,picker.reply,sizeof(picker.reply));break;
        case 6:picker.catalog.Previous();break;
        case 7:picker.catalog.Next();break;
        case 8:picker.catalog.Load(g.api,g.self,"lists/npcs.txt",127);break;
        case 10:if(picker.selected>=0 && static_cast<uint32_t>(picker.selected)<g.count) {
            auto id=Int(static_cast<int64_t>(g.entries[picker.selected].id));(void)Remove(&id,1,nullptr,picker.reply,sizeof(picker.reply));}break;
        case 11:(void)Clear(nullptr,0,nullptr,picker.reply,sizeof(picker.reply));break;
        case 12:(void)Observe(nullptr,0,nullptr,picker.reply,sizeof(picker.reply));break;
        case 13:if(picker.page)--picker.page;break;
        case 14:if((picker.page+1)*64<g.count)++picker.page;break;
        default:break;
        }
    }
    RenderPicker();
}
void RenderPicker() {
    using namespace sco::picker;
    const auto* w=Open<sco_widgets_v1>(SCO_WIDGETS_NAME);
    if(!HAS_FN(w,sco_widgets_v1,set_panel) || !HAS_FN(w,sco_widgets_v1,remove_panel))return;
    picker.catalog.View();
    if(picker.page*64>=g.count)picker.page=0;
    const auto end=std::min(g.count,(picker.page+1)*64);
    std::vector<std::string> names;std::vector<const char*> labels;
    for(uint32_t i=picker.page*64;i<end;++i)names.push_back(std::to_string(g.entries[i].id)+(g.entries[i].owned?" (owned/pending visibility)":" (removal started/authority relinquished)"));
    for(const auto& name:names)labels.push_back(name.c_str());
    char page[96];std::snprintf(page,sizeof(page),"Archetype page=%u; matches=%zu; ticket page=%u",picker.catalog.page+1,picker.catalog.visible.size(),picker.page+1);
    sco_widget_node nodes[]={Node(SCO_WIDGET_TEXT_INPUT,1,"Filter NPC archetypes"),Node(SCO_WIDGET_LIST,2,"NPC archetype"),
        Node(SCO_WIDGET_TEXT_INPUT,3,"Archetype (manual or selected)"),Node(SCO_WIDGET_SLIDER,4,"NPC count"),Node(SCO_WIDGET_BUTTON,5,"Spawn NPCs"),
        Node(SCO_WIDGET_BUTTON,6,"Previous archetypes"),Node(SCO_WIDGET_BUTTON,7,"Next archetypes"),Node(SCO_WIDGET_BUTTON,8,"Refresh catalog"),
        Node(SCO_WIDGET_COMBO,9,"Owned NPC session ticket"),Node(SCO_WIDGET_BUTTON,10,"Remove selected owned NPC"),Node(SCO_WIDGET_BUTTON,11,"Clear owned NPCs"),
        Node(SCO_WIDGET_BUTTON,12,"Observe owned NPCs"),Node(SCO_WIDGET_BUTTON,13,"Previous tickets"),Node(SCO_WIDGET_BUTTON,14,"Next tickets"),
        Node(SCO_WIDGET_LABEL,15,picker.catalog.status),Node(SCO_WIDGET_LABEL,16,page),Node(SCO_WIDGET_LABEL,17,picker.reply)};
    nodes[0].text=picker.catalog.filter.c_str();nodes[1].items=picker.catalog.labels.data();nodes[1].item_count=static_cast<uint32_t>(picker.catalog.labels.size());nodes[1].index=-1;
    nodes[2].text=picker.archetype.c_str();nodes[3].value=static_cast<double>(picker.amount);nodes[3].minimum=1;nodes[3].maximum=10;
    nodes[8].items=labels.data();nodes[8].item_count=static_cast<uint32_t>(labels.size());
    nodes[8].index=picker.selected>=static_cast<int32_t>(picker.page*64) && picker.selected<static_cast<int32_t>(end)?picker.selected-static_cast<int32_t>(picker.page*64):-1;
    if(w->set_panel(g.self,"npc.npcs","NPCs",nodes,17,sizeof(nodes[0]),PickerEvent,nullptr)!=SCO_OK)g.api->log(g.self,SCO_LOG_WARN,"NPC panel refused; commands remain available");
}
void StartPicker(){picker={};picker.catalog.Load(g.api,g.self,"lists/npcs.txt",127);RenderPicker();}
void StopPicker(){const auto* w=Open<sco_widgets_v1>(SCO_WIDGETS_NAME);if(HAS_FN(w,sco_widgets_v1,remove_panel))(void)w->remove_panel(g.self,"npc.npcs");picker={};}
