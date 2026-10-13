// SPDX-License-Identifier: GPL-3.0-only
struct Picker {
    sco::picker::Catalog catalog;
    std::string npc,seat="pilot";
    sc_vehicle_seat seats[128]{};
    uint32_t count=0,more=0,page=0,last=0;
    uint64_t ship=0;
    int32_t selected=-1;
    char reply[256]{};
} picker;
void RenderPicker();
void SeatAction(bool remove) {
    if(picker.selected<0 || static_cast<uint32_t>(picker.selected)>=picker.count)return;
    const auto index=picker.seats[picker.selected].index;
    const auto* v=Open<sc_vehicles_v1>(SC_VEHICLES_SERVICE_NAME);
    const auto* a=Open<sc_actors_v1>(SC_ACTORS_NAME);
    sc_vehicle_seat fresh[128]{};uint64_t ship=0;uint32_t count=0,more=0;
    auto r=Seats(v,ship,fresh,count,more);
    if(r!=SCO_OK || ship!=picker.ship) {std::snprintf(picker.reply,sizeof(picker.reply),"Seat snapshot changed/unavailable; refresh before acting");return;}
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
        std::snprintf(picker.reply,sizeof(picker.reply),"Selected seat %s result=%u; only local player/crew-owned occupants",remove?"removal requested (not disappearance)":"stand",static_cast<unsigned>(r));return;
    }
    std::snprintf(picker.reply,sizeof(picker.reply),"Selected seat absent from copied batch; no action");
}
void PickerEvent(const char* panel,const sco_widget_event* e,void*) {
    using namespace sco::picker;
    if(!g.api || !panel || std::strcmp(panel,"crew.crew") || !e || e->size<sizeof(*e))return;
    if(Event(e,SCO_WIDGET_TEXT_INPUT) && Text(e->text,SCO_WIDGETS_MAX_TEXT)) {
        if(e->node_id==1){picker.catalog.filter=e->text;picker.catalog.page=0;}
        else if(e->node_id==3 && Text(e->text,127))picker.npc=e->text;
        else if(e->node_id==5 && Text(e->text,47)){picker.seat=e->text;picker.selected=-1;}
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
            else std::snprintf(picker.reply,sizeof(picker.reply),"Ship snapshot changed; refresh before acting");
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
            else std::snprintf(picker.reply,sizeof(picker.reply),"Ship snapshot changed; refresh before acting");
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
    std::vector<std::string> cells;std::vector<const char*> table,labels;
    const auto end=std::min(picker.count,(picker.page+1)*32);
    for(uint32_t i=picker.page*32;i<end;++i) {
        const auto& s=picker.seats[i];
        cells.push_back(std::to_string(s.index));cells.emplace_back(s.name);
        cells.push_back(s.occupant_id?std::to_string(s.occupant_id):"unknown/empty");
        cells.emplace_back(Usable(s)?"usable":"unknown/unusable");labels.push_back(s.name);
    }
    for(const auto& c:cells)table.push_back(c.c_str());
    char status[192];std::snprintf(status,sizeof(status),"Seat result=%u; copied=%u; more=%u (no SDK pagination); page=%u",static_cast<unsigned>(r),picker.count,more,picker.page+1);
    char page[96];std::snprintf(page,sizeof(page),"Archetype page %u; matches=%zu",picker.catalog.page+1,picker.catalog.visible.size());
    sco_widget_node nodes[]={Node(SCO_WIDGET_TEXT_INPUT,1,"Filter NPC archetypes"),Node(SCO_WIDGET_LIST,2,"NPC archetype"),
        Node(SCO_WIDGET_TEXT_INPUT,3,"Archetype (manual or selected)"),Node(SCO_WIDGET_COMBO,4,"Ship seat"),Node(SCO_WIDGET_TEXT_INPUT,5,"Seat name words (manual)"),
        Node(SCO_WIDGET_BUTTON,6,"Target current ship"),Node(SCO_WIDGET_BUTTON,7,"Sit"),Node(SCO_WIDGET_BUTTON,8,"Stand up all owned crew/player"),
        Node(SCO_WIDGET_BUTTON,9,"Fill empty usable seats"),Node(SCO_WIDGET_BUTTON,10,"Clear owned crew"),Node(SCO_WIDGET_BUTTON,11,"Flight Ready"),
        Node(SCO_WIDGET_BUTTON,12,"Previous archetypes"),Node(SCO_WIDGET_BUTTON,13,"Next archetypes"),Node(SCO_WIDGET_BUTTON,14,"Refresh catalog"),
        Node(SCO_WIDGET_BUTTON,15,"Previous seats"),Node(SCO_WIDGET_BUTTON,16,"Next seats"),Node(SCO_WIDGET_BUTTON,17,"Refresh seats"),
        Node(SCO_WIDGET_BUTTON,18,"Add owned NPC to selected seat"),Node(SCO_WIDGET_BUTTON,19,"Stand selected occupant"),Node(SCO_WIDGET_BUTTON,20,"Remove selected owned crew"),
        Node(SCO_WIDGET_TABLE,21,"Index / seat / occupant / usability"),Node(SCO_WIDGET_LABEL,22,status),Node(SCO_WIDGET_LABEL,23,picker.catalog.status),
        Node(SCO_WIDGET_LABEL,24,page),Node(SCO_WIDGET_LABEL,25,picker.reply)};
    nodes[0].text=picker.catalog.filter.c_str();nodes[1].items=picker.catalog.labels.data();nodes[1].item_count=static_cast<uint32_t>(picker.catalog.labels.size());nodes[1].index=-1;
    nodes[2].text=picker.npc.c_str();nodes[3].items=labels.data();nodes[3].item_count=static_cast<uint32_t>(labels.size());
    nodes[3].index=picker.selected>=static_cast<int32_t>(picker.page*32) && picker.selected<static_cast<int32_t>(end)?picker.selected-static_cast<int32_t>(picker.page*32):-1;
    nodes[4].text=picker.seat.c_str();nodes[20].items=table.data();nodes[20].item_count=static_cast<uint32_t>(table.size());nodes[20].columns=4;
    if(w->set_panel(g.self,"crew.crew","Crew",nodes,25,sizeof(nodes[0]),PickerEvent,nullptr)!=SCO_OK)g.api->log(g.self,SCO_LOG_WARN,"Crew panel refused; commands remain available");
}
void StartPicker(){picker={};picker.catalog.Load(g.api,g.self,"lists/npcs.txt",127);RenderPicker();}
void StopPicker(){const auto* w=Open<sco_widgets_v1>(SCO_WIDGETS_NAME);if(HAS_FN(w,sco_widgets_v1,remove_panel))(void)w->remove_panel(g.self,"crew.crew");picker={};}
