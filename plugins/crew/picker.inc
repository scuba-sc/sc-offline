// SPDX-License-Identifier: GPL-3.0-only
struct Picker {
    sco::picker::Catalog catalog;
    std::string npc,seat="pilot"; // seat words are only used when no seat is selected: the pilot seat
    sc_vehicle_seat seats[128]{};
    uint32_t count=0,more=0,page=0,last=0;
    uint64_t ship=0;
    int32_t selected=-1;
    char reply[256]{};
} picker;
void RenderPicker();
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
void SeatAction(bool remove) {
    if(picker.selected<0 || static_cast<uint32_t>(picker.selected)>=picker.count)return;
    const auto index=picker.seats[picker.selected].index;
    const auto* v=Open<sc_vehicles_v1>(SC_VEHICLES_SERVICE_NAME);
    const auto* a=Open<sc_actors_v1>(SC_ACTORS_NAME);
    sc_vehicle_seat fresh[128]{};uint64_t ship=0;uint32_t count=0,more=0;
    auto r=Seats(v,ship,fresh,count,more);
    if(r!=SCO_OK || ship!=picker.ship) {std::snprintf(picker.reply,sizeof(picker.reply),"The seat list is out of date. Press Update seat list and try again.");return;}
    for(uint32_t i=0;i<count;++i)if(fresh[i].index==index) {
        if(!HAS_FN(v,sc_vehicles_v1,seat_occupant) || !Cap("game.vehicles.seats"))r=SCO_UNAVAILABLE;
        else {
            uint64_t occupant=0;r=v->seat_occupant(ship,index,&occupant);
            if(r==SCO_OK) {
                auto* job=Actor(occupant);
                uint64_t me=0;(void)Player(a,me);
                if(remove)r=Owned(job)?Remove(*job,a):SCO_NOT_FOUND;
                else if(occupant && (occupant==me || Owned(job)) && Cap("game.vehicles.seat") && HAS_FN(v,sc_vehicles_v1,eject)) {
                    r=v->eject(g.self,occupant);
                    if(r==SCO_OK && job && job->phase==Phase::seating){if(job->player)*job={};else job->phase=Phase::seated;}
                } else r=SCO_UNAVAILABLE;
            }
        }
        if(r==SCO_OK)std::snprintf(picker.reply,sizeof(picker.reply),remove?"Removing the crew member in that seat. They can take a moment to disappear.":"Stood up the person in that seat.");
        else std::snprintf(picker.reply,sizeof(picker.reply),remove?"Couldn't remove anyone from that seat. Only your own crew can be removed.":"Couldn't stand anyone up. Only you and your own crew can be moved.");
        return;
    }
    std::snprintf(picker.reply,sizeof(picker.reply),"That seat is no longer on the ship. Update the seat list.");
}
void PickerEvent(const char* panel,const sco_widget_event* e,void*) {
    using namespace sco::picker;
    if(!g.api || !panel || std::strcmp(panel,"crew.crew") || !e || e->size<sizeof(*e))return;
    if(Event(e,SCO_WIDGET_TEXT_INPUT) && Text(e->text,SCO_WIDGETS_MAX_TEXT)) {
        if(e->node_id==1){picker.catalog.filter=e->text;picker.catalog.page=0;}
        else if(e->node_id==3 && Text(e->text,127))picker.npc=e->text;
    } else if(Event(e,SCO_WIDGET_LIST) && e->node_id==2) {
        if(auto* value=picker.catalog.Select(e->index))picker.npc=value;
    } else if(Event(e,SCO_WIDGET_COMBO) && e->node_id==4) {
        if(e->index>=0 && static_cast<uint32_t>(e->index)+picker.page*32<picker.count) {
            picker.selected=e->index+static_cast<int32_t>(picker.page*32);
            if(Text(picker.seats[picker.selected].name,47))picker.seat=picker.seats[picker.selected].name;
        }
    } else if(Event(e,SCO_WIDGET_BUTTON)) {
        const sco_arg npc=String(picker.npc.c_str()),seat=String(picker.seat.c_str());
        switch(e->node_id) {
        case 6:(void)Target(nullptr,0,nullptr,picker.reply,sizeof(picker.reply));picker.selected=-1;break;
        case 7:if(picker.selected>=0 && static_cast<uint32_t>(picker.selected)<picker.count) {
            uint64_t ship=0;const auto* v=Open<sc_vehicles_v1>(SC_VEHICLES_SERVICE_NAME);
            if(Ship(v,ship)==SCO_OK && ship==picker.ship)(void)SitSeat(&seat,1,nullptr,picker.reply,sizeof(picker.reply),picker.seats[picker.selected].index);
            else std::snprintf(picker.reply,sizeof(picker.reply),"The seat list is out of date. Press Update seat list and try again.");
        } else (void)Sit(&seat,1,nullptr,picker.reply,sizeof(picker.reply));break;
        case 8:(void)Stand(nullptr,0,nullptr,picker.reply,sizeof(picker.reply));break;
        case 9:(void)Fill(&npc,1,nullptr,picker.reply,sizeof(picker.reply));break;
        case 10:(void)Clear(nullptr,0,nullptr,picker.reply,sizeof(picker.reply));break;
        case 11:(void)Power(nullptr,0,nullptr,picker.reply,sizeof(picker.reply));break;
        case 12:picker.catalog.Previous();break;
        case 13:picker.catalog.Next();break;
        case 14:picker.catalog.Load(g.api,g.self,"lists/npcs.txt",127);break;
        case 15:if(picker.page)--picker.page;break;
        case 16:if((picker.page+1)*32<picker.count)++picker.page;break;
        case 17:break; // Read-only refresh.
        case 18:if(picker.selected>=0 && static_cast<uint32_t>(picker.selected)<picker.count) {
            uint64_t ship=0;const auto* v=Open<sc_vehicles_v1>(SC_VEHICLES_SERVICE_NAME);
            if(Ship(v,ship)==SCO_OK && ship==picker.ship)(void)FillSeats(&npc,1,nullptr,picker.reply,sizeof(picker.reply),picker.seats[picker.selected].index);
            else std::snprintf(picker.reply,sizeof(picker.reply),"The seat list is out of date. Press Update seat list and try again.");
        }break;
        case 19:SeatAction(false);break;
        case 20:SeatAction(true);break;
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
    sc_vehicle_seat seats[128]{};uint64_t ship=0;uint32_t count=0,more=0;
    const auto r=Seats(Open<sc_vehicles_v1>(SC_VEHICLES_SERVICE_NAME),ship,seats,count,more);
    uint32_t selectedIndex=UINT32_MAX;
    if(picker.selected>=0 && static_cast<uint32_t>(picker.selected)<picker.count && ship==picker.ship)selectedIndex=picker.seats[picker.selected].index;
    picker.selected=-1;picker.ship=ship;picker.count=r==SCO_OK?count:0;picker.more=more;
    for(uint32_t i=0;i<picker.count;++i){picker.seats[i]=seats[i];if(seats[i].index==selectedIndex)picker.selected=static_cast<int32_t>(i);}
    if(picker.page*32>=picker.count)picker.page=0;
    const auto* actors=Open<sc_actors_v1>(SC_ACTORS_NAME);
    uint64_t me=0;(void)Player(actors,me);
    std::vector<std::string> cells,seatNames;std::vector<const char*> table,labels;
    const auto end=std::min(picker.count,(picker.page+1)*32);
    for(uint32_t i=picker.page*32;i<end;++i) {
        const auto& s=picker.seats[i];
        std::string name=s.name;for(auto& ch:name)if(ch=='_')ch=' ';
        const char* who=!s.occupant_id ? ((s.flags&SC_SEAT_OCCUPIED)?"Someone":"Empty") :
            s.occupant_id==me ? "You" : Owned(Actor(s.occupant_id)) ? "Your crew" : "Someone else";
        cells.push_back(name);cells.emplace_back(who);
        cells.emplace_back(Usable(s)?"Can be used":"Can't be used");seatNames.push_back(std::move(name));
    }
    for(const auto& c:cells)table.push_back(c.c_str());
    for(const auto& n:seatNames)labels.push_back(n.c_str());
    const bool shipOk=r==SCO_OK,seatSel=picker.selected>=0,npcSet=!picker.npc.empty();
    char shipLine[160],seatPage[96];
    if(!shipOk)std::snprintf(shipLine,sizeof(shipLine),"No ship found. Get into a ship, then press Update seat list.");
    else std::snprintf(shipLine,sizeof(shipLine),"Your ship has %u seat%s%s.",picker.count,picker.count==1?"":"s",more?" or more":"");
    std::snprintf(seatPage,sizeof(seatPage),"Seats %u-%u of %u",picker.count?picker.page*32+1:0,end,picker.count);
    const auto& cat=picker.catalog;
    const size_t matches=cat.visible.size(),pages=std::max<size_t>(1,(matches+cat.pageSize-1)/cat.pageSize);
    char page[96];
    if(cat.choices.empty())std::snprintf(page,sizeof(page),"No character list is available. Type a character name below.");
    else if(!matches)std::snprintf(page,sizeof(page),"No characters match your search.");
    else std::snprintf(page,sizeof(page),"Page %u of %zu (%zu characters)",cat.page+1,pages,matches);
    const auto rows=ChoiceRows(cat);
    std::vector<const char*> npcItems;
    for(const auto& row:rows)npcItems.push_back(row.c_str());
    const uint32_t off=SCO_WIDGET_DISABLED;
    std::vector<sco_widget_node> nodes;nodes.reserve(48);
    auto add=[&](uint32_t kind,uint32_t id,const char* label,uint32_t parent=0,uint32_t flags=0)->sco_widget_node& {
        nodes.push_back(Node(kind,id,label,flags));nodes.back().parent=parent;return nodes.back();};
    add(SCO_WIDGET_COLUMN,30,"Your ship");
    add(SCO_WIDGET_LABEL,31,shipLine,30);
    add(SCO_WIDGET_ROW,32,"",30);
    add(SCO_WIDGET_BUTTON,6,"Use my current ship",32);
    add(SCO_WIDGET_BUTTON,11,"Get ship ready to fly",32,shipOk?0u:off);
    add(SCO_WIDGET_BUTTON,17,"Update seat list",32);
    {auto& s=add(SCO_WIDGET_COMBO,4,"Seat",30);s.items=labels.empty()?nullptr:labels.data();s.item_count=static_cast<uint32_t>(labels.size());
     s.index=picker.selected>=static_cast<int32_t>(picker.page*32) && picker.selected<static_cast<int32_t>(end)?picker.selected-static_cast<int32_t>(picker.page*32):-1;}
    {auto& t=add(SCO_WIDGET_TABLE,21,"Seats",30);t.items=table.empty()?nullptr:table.data();t.item_count=static_cast<uint32_t>(table.size());t.columns=3;}
    if(picker.count>32) {
        add(SCO_WIDGET_ROW,33,"",30);
        add(SCO_WIDGET_BUTTON,15,"Previous",33,picker.page?0u:off);
        add(SCO_WIDGET_BUTTON,16,"Next",33,(picker.page+1)*32<picker.count?0u:off);
        add(SCO_WIDGET_LABEL,22,seatPage,30);
    }
    add(SCO_WIDGET_COLUMN,34,"Choose a character to add");
    add(SCO_WIDGET_TEXT_INPUT,1,"Search characters",34).text=cat.filter.c_str();
    {auto& l=add(SCO_WIDGET_LIST,2,"Characters",34);l.items=npcItems.empty()?nullptr:npcItems.data();l.item_count=static_cast<uint32_t>(npcItems.size());l.index=-1;}
    add(SCO_WIDGET_ROW,35,"",34);
    add(SCO_WIDGET_BUTTON,12,"Previous",35,cat.page?0u:off);
    add(SCO_WIDGET_BUTTON,13,"Next",35,(static_cast<size_t>(cat.page)+1)*cat.pageSize<matches?0u:off);
    add(SCO_WIDGET_BUTTON,14,"Reload character list",35);
    add(SCO_WIDGET_LABEL,24,page,34);
    add(SCO_WIDGET_TEXT_INPUT,3,"Character name",34).text=picker.npc.c_str();
    add(SCO_WIDGET_COLUMN,36,"Add crew");
    add(SCO_WIDGET_ROW,37,"",36);
    add(SCO_WIDGET_BUTTON,9,"Fill all empty seats",37,shipOk && npcSet?0u:off);
    add(SCO_WIDGET_BUTTON,18,"Add to selected seat",37,seatSel && npcSet?0u:off);
    if(!npcSet)add(SCO_WIDGET_LABEL,38,"Choose a character first.",36);
    add(SCO_WIDGET_COLUMN,39,"You and your crew");
    add(SCO_WIDGET_ROW,40,"",39);
    add(SCO_WIDGET_BUTTON,7,"Sit in selected seat",40,seatSel?0u:off);
    add(SCO_WIDGET_BUTTON,19,"Stand up person in seat",40,seatSel?0u:off);
    add(SCO_WIDGET_BUTTON,20,"Remove crew in seat",40,seatSel?0u:off);
    if(shipOk && !seatSel)add(SCO_WIDGET_LABEL,42,"Choose a seat above to use these buttons.",39);
    add(SCO_WIDGET_ROW,41,"",39);
    add(SCO_WIDGET_BUTTON,8,"Stand everyone up",41,shipOk?0u:off);
    add(SCO_WIDGET_BUTTON,10,"Remove all my crew",41,shipOk?0u:off);
    add(SCO_WIDGET_COLUMN,43,"Last result");
    add(SCO_WIDGET_LABEL,25,picker.reply[0]?picker.reply:"Nothing yet.",43);
    if(w->set_panel(g.self,"crew.crew","Crew",nodes.data(),static_cast<uint32_t>(nodes.size()),sizeof(nodes[0]),PickerEvent,nullptr)!=SCO_OK)g.api->log(g.self,SCO_LOG_WARN,"Crew panel refused; commands remain available");
}
void StartPicker(){picker={};picker.catalog.Load(g.api,g.self,"lists/npcs.txt",127);RenderPicker();}
void StopPicker(){const auto* w=Open<sco_widgets_v1>(SCO_WIDGETS_NAME);if(HAS_FN(w,sco_widgets_v1,remove_panel))(void)w->remove_panel(g.self,"crew.crew");picker={};}
