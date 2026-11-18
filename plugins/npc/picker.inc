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
// The list is a startup snapshot: a retry can only help when the content service answered but the read itself
// failed. If the service is missing, or the file is simply absent or empty, a retry would only fail again.
static bool ListReadFailed(const sco::picker::Catalog& cat){
    return cat.choices.empty() && std::strstr(cat.status,"result=") && !std::strstr(cat.status,"result=0;");
}
// Player-facing list rows: the pack's display name when it has one, else the class name read as words.
// The pack is added only when the same name comes from two packs.
std::string ChoiceName(const sco::picker::Choice& c) {
    if(c.label!=c.value+" ["+c.pack+"]") {
        const size_t tail=c.value.size()+c.pack.size()+6;
        if(c.label.size()>tail)return c.label.substr(0,c.label.size()-tail);
    }
    std::string name=c.value;
    for(auto& ch:name)if(ch=='_')ch=' ';
    return name;
}
std::vector<std::string> ChoiceRows(const sco::picker::Catalog& catalog) {
    std::map<std::string,int> seen;
    for(const auto& c:catalog.choices)++seen[ChoiceName(c)];
    std::vector<std::string> rows;
    const size_t begin=static_cast<size_t>(catalog.page)*catalog.pageSize,end=std::min(begin+catalog.pageSize,catalog.visible.size());
    for(size_t i=begin;i<end;++i) {
        const auto& c=catalog.choices[catalog.visible[i]];
        auto name=ChoiceName(c);
        if(seen[name]>1)name+=" ("+c.pack+")";
        rows.push_back(std::move(name));
    }
    return rows;
}
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
        case 8:if(ListReadFailed(picker.catalog))picker.catalog.Load(g.api,g.self,"lists/npcs.txt",127);break;
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
    for(uint32_t i=picker.page*64;i<end;++i)names.push_back("Character "+std::to_string(i+1)+" - "+(g.entries[i].owned?"spawned":"being removed"));
    for(const auto& name:names)labels.push_back(name.c_str());
    const auto& cat=picker.catalog;
    const size_t matches=cat.visible.size(),pages=std::max<size_t>(1,(matches+cat.pageSize-1)/cat.pageSize);
    char page[96],ticketPage[64];
    if(cat.choices.empty())std::snprintf(page,sizeof(page),"No character list is available. Type a character name below.");
    else if(!matches)std::snprintf(page,sizeof(page),"No characters match your search.");
    else std::snprintf(page,sizeof(page),"Page %u of %zu (%zu characters)",cat.page+1,pages,matches);
    std::snprintf(ticketPage,sizeof(ticketPage),"Characters %u-%u of %u",g.count?picker.page*64+1:0,end,g.count);
    const auto rows=ChoiceRows(cat);
    std::vector<const char*> items;
    for(const auto& row:rows)items.push_back(row.c_str());
    const bool hasName=!picker.archetype.empty(),hasSelected=picker.selected>=0 && static_cast<uint32_t>(picker.selected)<g.count;
    const uint32_t off=SCO_WIDGET_DISABLED;
    std::vector<sco_widget_node> nodes;nodes.reserve(40);
    auto add=[&](uint32_t kind,uint32_t id,const char* label,uint32_t parent=0,uint32_t flags=0)->sco_widget_node& {
        nodes.push_back(Node(kind,id,label,flags));nodes.back().parent=parent;return nodes.back();};
    add(SCO_WIDGET_COLUMN,20,"Choose a character");
    add(SCO_WIDGET_TEXT_INPUT,1,"Search characters",20).text=cat.filter.c_str();
    {auto& l=add(SCO_WIDGET_LIST,2,"Characters",20);l.items=items.empty()?nullptr:items.data();l.item_count=static_cast<uint32_t>(items.size());l.index=-1;}
    add(SCO_WIDGET_ROW,21,"",20);
    add(SCO_WIDGET_BUTTON,6,"Previous",21,cat.page?0u:off);
    add(SCO_WIDGET_BUTTON,7,"Next",21,(static_cast<size_t>(cat.page)+1)*cat.pageSize<matches?0u:off);
    // The list is read once when the game starts, so reloading can only help when that first read failed.
    if(ListReadFailed(cat))add(SCO_WIDGET_BUTTON,8,"Try loading the character list again",21);
    add(SCO_WIDGET_LABEL,16,page,20);
    add(SCO_WIDGET_TEXT_INPUT,3,"Character name",20).text=picker.archetype.c_str();
    add(SCO_WIDGET_COLUMN,22,"Spawn them");
    {auto& s=add(SCO_WIDGET_SLIDER,4,"How many",22);s.value=static_cast<double>(picker.amount);s.minimum=1;s.maximum=10;}
    add(SCO_WIDGET_BUTTON,5,"Spawn characters",22,hasName?0u:off);
    add(SCO_WIDGET_LABEL,27,hasName?"They appear a few metres in front of you.":"Choose a character first.",22);
    add(SCO_WIDGET_COLUMN,23,"Characters you spawned");
    if(!g.count)add(SCO_WIDGET_LABEL,29,"You haven't spawned any characters yet.",23);
    else {
        auto& l=add(SCO_WIDGET_COMBO,9,"Character",23);l.items=labels.data();l.item_count=static_cast<uint32_t>(labels.size());
        l.index=picker.selected>=static_cast<int32_t>(picker.page*64) && picker.selected<static_cast<int32_t>(end)?picker.selected-static_cast<int32_t>(picker.page*64):-1;
        if(g.count>64) {
            add(SCO_WIDGET_ROW,25,"",23);
            add(SCO_WIDGET_BUTTON,13,"Previous",25,picker.page?0u:off);
            add(SCO_WIDGET_BUTTON,14,"Next",25,(picker.page+1)*64<g.count?0u:off);
            add(SCO_WIDGET_LABEL,28,ticketPage,23);
        }
        if(!hasSelected)add(SCO_WIDGET_LABEL,30,"Choose one of your characters to remove it.",23);
    }
    add(SCO_WIDGET_ROW,24,"",23);
    add(SCO_WIDGET_BUTTON,10,"Remove character",24,hasSelected?0u:off);
    add(SCO_WIDGET_BUTTON,11,"Remove all my characters",24,g.count?0u:off);
    add(SCO_WIDGET_BUTTON,12,"Check on them",24,g.count?0u:off);
    add(SCO_WIDGET_COLUMN,26,"Last result");
    add(SCO_WIDGET_LABEL,17,picker.reply[0]?picker.reply:"Nothing yet.",26);
    if(w->set_panel(g.self,"npc.npcs","NPCs",nodes.data(),static_cast<uint32_t>(nodes.size()),sizeof(nodes[0]),PickerEvent,nullptr)!=SCO_OK)g.api->log(g.self,SCO_LOG_WARN,"NPC panel refused; commands remain available");
}
void StartPicker(){picker={};picker.catalog.Load(g.api,g.self,"lists/npcs.txt",127);RenderPicker();}
void StopPicker(){const auto* w=Open<sco_widgets_v1>(SCO_WIDGETS_NAME);if(HAS_FN(w,sco_widgets_v1,remove_panel))(void)w->remove_panel(g.self,"npc.npcs");picker={};}
