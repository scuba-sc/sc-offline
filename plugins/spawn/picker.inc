// SPDX-License-Identifier: GPL-3.0-only
// Included in the owning module, sharing its command handlers and session jobs.
struct Picker {
    sco::picker::Catalog catalog;
    std::string ship, seat="pilot"; // seat is "pilot" or "none" (stay outside)
    double height=30;
    bool power=true;
    int32_t owned=-1;
    char reply[256]{};
} picker;
void RenderPicker();
// Flight Ready is only meaningful with the pilot seat; spawn.ship_options still refuses it otherwise.
bool PickerPower(){return picker.power && picker.seat=="pilot";}
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
    if(!g.api || !panel || std::strcmp(panel,"spawn.vehicles") || !e || e->size<sizeof(*e))return;
    if(Event(e,SCO_WIDGET_TEXT_INPUT) && Text(e->text,SCO_WIDGETS_MAX_TEXT)) {
        if(e->node_id==1){picker.catalog.filter=e->text;picker.catalog.page=0;}
        else if(e->node_id==3 && Text(e->text,63))picker.ship=e->text;
    } else if(Event(e,SCO_WIDGET_LIST) && e->node_id==2) {
        if(auto* value=picker.catalog.Select(e->index))picker.ship=value;
    } else if(Event(e,SCO_WIDGET_COMBO) && e->node_id==5) {
        if(e->index==0)picker.seat="pilot";
        else if(e->index==1)picker.seat="none";
    } else if(Event(e,SCO_WIDGET_COMBO) && e->node_id==11) {
        if(e->index>=0 && static_cast<uint32_t>(e->index)<g.count)picker.owned=e->index;
    } else if(Event(e,SCO_WIDGET_SLIDER) && e->node_id==4 && std::isfinite(e->value) && e->value>=0 && e->value<=10000)picker.height=e->value;
    else if(Event(e,SCO_WIDGET_TOGGLE) && (e->value==0 || e->value==1)) {
        if(e->node_id==6)picker.power=e->value!=0;
    } else if(Event(e,SCO_WIDGET_BUTTON)) {
        const sco_arg options[]={String(picker.ship.c_str()),Float(picker.height),String(picker.seat.c_str()),Bool(PickerPower())};
        switch(e->node_id) {
        case 7:(void)Options(options,4,nullptr,picker.reply,sizeof(picker.reply));break;
        case 8:picker.catalog.Previous();break;
        case 9:picker.catalog.Next();break;
        case 10:picker.catalog.Load(g.api,g.self,"lists/ships.txt",63);break;
        case 12:if(picker.owned>=0 && static_cast<uint32_t>(picker.owned)<g.count) {
            // "Get in" always means the pilot seat; "Stay outside" only applies to a new spawn.
            const sco_arg args[]={Int(static_cast<int64_t>(g.tickets[picker.owned].id)),String("pilot"),Bool(PickerPower())};
            (void)Board(args,3,nullptr,picker.reply,sizeof(picker.reply));}break;
        case 13:if(picker.owned>=0 && static_cast<uint32_t>(picker.owned)<g.count) {
            const auto id=Int(static_cast<int64_t>(g.tickets[picker.owned].id));(void)Remove(&id,1,nullptr,picker.reply,sizeof(picker.reply));}break;
        case 14:(void)Clear(nullptr,0,nullptr,picker.reply,sizeof(picker.reply));break;
        case 15:(void)Status(nullptr,0,nullptr,picker.reply,sizeof(picker.reply));break;
        default:break;
        }
    }
    RenderPicker();
}
void RenderPicker() {
    using namespace sco::picker;
    const auto* w=Open<sco_widgets_v1>(SCO_WIDGETS_NAME);
    if(!HAS(w,sco_widgets_v1,set_panel) || !HAS(w,sco_widgets_v1,remove_panel))return;
    picker.catalog.View();
    const auto rows=ChoiceRows(picker.catalog);
    std::vector<const char*> shipItems;
    for(const auto& row:rows)shipItems.push_back(row.c_str());
    std::vector<std::string> owned;
    for(uint32_t i=0;i<g.count;++i)owned.push_back("Ship "+std::to_string(i+1)+" - "+PhaseWord(g.tickets[i].phase));
    std::vector<const char*> ownedItems;
    for(const auto& row:owned)ownedItems.push_back(row.c_str());
    if(picker.owned>=0 && static_cast<uint32_t>(picker.owned)>=g.count)picker.owned=-1;
    const bool hasShip=!picker.ship.empty(),hasOwned=picker.owned>=0;
    const auto& cat=picker.catalog;
    const size_t matches=cat.visible.size(),pages=std::max<size_t>(1,(matches+cat.pageSize-1)/cat.pageSize);
    char page[96];
    if(cat.choices.empty())std::snprintf(page,sizeof(page),"No ship list is available. Type a ship name below.");
    else if(!matches)std::snprintf(page,sizeof(page),"No ships match your search.");
    else std::snprintf(page,sizeof(page),"Page %u of %zu (%zu ships)",cat.page+1,pages,matches);
    static const char* const seatItems[]={"Get in as pilot","Stay outside"};
    const uint32_t off=SCO_WIDGET_DISABLED;
    std::vector<sco_widget_node> nodes;nodes.reserve(48);
    auto add=[&](uint32_t kind,uint32_t id,const char* label,uint32_t parent=0,uint32_t flags=0)->sco_widget_node& {
        nodes.push_back(Node(kind,id,label,flags));nodes.back().parent=parent;return nodes.back();};
    add(SCO_WIDGET_COLUMN,30,"Choose a ship");
    add(SCO_WIDGET_TEXT_INPUT,1,"Search ships",30).text=cat.filter.c_str();
    {auto& list=add(SCO_WIDGET_LIST,2,"Ships",30);list.items=shipItems.empty()?nullptr:shipItems.data();list.item_count=static_cast<uint32_t>(shipItems.size());list.index=-1;}
    add(SCO_WIDGET_ROW,31,"",30);
    add(SCO_WIDGET_BUTTON,8,"Previous",31,cat.page?0u:off);
    add(SCO_WIDGET_BUTTON,9,"Next",31,(static_cast<size_t>(cat.page)+1)*cat.pageSize<matches?0u:off);
    add(SCO_WIDGET_BUTTON,10,"Reload ship list",31);
    add(SCO_WIDGET_LABEL,19,page,30);
    add(SCO_WIDGET_TEXT_INPUT,3,"Ship name",30).text=picker.ship.c_str();
    add(SCO_WIDGET_COLUMN,33,"Spawn it");
    {auto& s=add(SCO_WIDGET_SLIDER,4,"Height above you (m)",33);s.value=picker.height;s.maximum=10000;}
    {auto& s=add(SCO_WIDGET_COMBO,5,"After spawning",33);s.items=seatItems;s.item_count=2;s.index=picker.seat=="none"?1:0;}
    add(SCO_WIDGET_TOGGLE,6,"Start ready to fly",33,picker.seat=="pilot"?0u:off).value=PickerPower()?1:0;
    if(picker.seat!="pilot")add(SCO_WIDGET_LABEL,21,"Ready to fly needs the pilot seat.",33);
    add(SCO_WIDGET_BUTTON,7,"Spawn ship",33,hasShip?0u:off);
    if(!hasShip)add(SCO_WIDGET_LABEL,34,"Choose a ship first.",33);
    add(SCO_WIDGET_COLUMN,35,"Ships you spawned");
    if(!g.count)add(SCO_WIDGET_LABEL,37,"You haven't spawned any ships yet.",35);
    else {
        auto& list=add(SCO_WIDGET_COMBO,11,"Ship",35);list.items=ownedItems.data();list.item_count=g.count;list.index=picker.owned;
        if(!hasOwned)add(SCO_WIDGET_LABEL,37,"Choose one of your ships to get in or remove it.",35);
    }
    add(SCO_WIDGET_ROW,36,"",35);
    add(SCO_WIDGET_BUTTON,12,"Get in",36,hasOwned?0u:off);
    add(SCO_WIDGET_BUTTON,13,"Remove ship",36,hasOwned?0u:off);
    add(SCO_WIDGET_BUTTON,14,"Remove all my ships",36,g.count?0u:off);
    add(SCO_WIDGET_BUTTON,15,"Check status",36);
    add(SCO_WIDGET_COLUMN,38,"Last result");
    add(SCO_WIDGET_LABEL,20,picker.reply[0]?picker.reply:"Nothing yet.",38);
    auto r=w->set_panel(g.self,"spawn.vehicles","Vehicles",nodes.data(),static_cast<uint32_t>(nodes.size()),sizeof(nodes[0]),PickerEvent,nullptr);
    if(r!=SCO_OK)g.api->log(g.self,SCO_LOG_WARN,"Vehicles panel refused; spawn commands remain available");
}
void StartPicker(){picker={};picker.catalog.Load(g.api,g.self,"lists/ships.txt",63);RenderPicker();}
void StopPicker(){const auto* w=Open<sco_widgets_v1>(SCO_WIDGETS_NAME);if(HAS(w,sco_widgets_v1,remove_panel))(void)w->remove_panel(g.self,"spawn.vehicles");picker={};}
