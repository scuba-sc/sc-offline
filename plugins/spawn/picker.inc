// SPDX-License-Identifier: GPL-3.0-only
// Included in the owning module, sharing its command handlers and session jobs.
struct Picker {
    sco::picker::Catalog catalog;
    std::string ship, seat="pilot";
    double height=30;
    bool power=true,ammo=false;
    int32_t owned=-1;
    char reply[256]{};
} picker;
void RenderPicker();
void PickerEvent(const char* panel,const sco_widget_event* e,void*) {
    using namespace sco::picker;
    if(!g.api || !panel || std::strcmp(panel,"spawn.vehicles") || !e || e->size<sizeof(*e))return;
    if(Event(e,SCO_WIDGET_TEXT_INPUT) && Text(e->text,SCO_WIDGETS_MAX_TEXT)) {
        if(e->node_id==1){picker.catalog.filter=e->text;picker.catalog.page=0;}
        else if(e->node_id==3 && Text(e->text,63))picker.ship=e->text;
        else if(e->node_id==5 && Text(e->text,63))picker.seat=e->text;
    } else if(Event(e,SCO_WIDGET_LIST) && e->node_id==2) {
        if(auto* value=picker.catalog.Select(e->index))picker.ship=value;
    } else if(Event(e,SCO_WIDGET_COMBO) && e->node_id==11) {
        if(e->index>=0 && static_cast<uint32_t>(e->index)<g.count)picker.owned=e->index;
    } else if(Event(e,SCO_WIDGET_SLIDER) && e->node_id==4 && std::isfinite(e->value) && e->value>=0 && e->value<=10000)picker.height=e->value;
    else if(Event(e,SCO_WIDGET_TOGGLE) && (e->value==0 || e->value==1)) {
        if(e->node_id==6)picker.power=e->value!=0;
        else if(e->node_id==16) {
            sco_arg a=Bool(e->value!=0);
            if(HAS(g.api,sco_api,invoke) && g.api->invoke(g.self,"creative.ship_ammo",&a,1,[](sco_result,const char* reply,void*){std::snprintf(picker.reply,sizeof(picker.reply),"%s",reply?reply:"");},nullptr)==SCO_OK)picker.ammo=e->value!=0;
            else std::snprintf(picker.reply,sizeof(picker.reply),"Needs the creative plugin; setting unchanged");
        }
    } else if(Event(e,SCO_WIDGET_BUTTON)) {
        const sco_arg options[]={String(picker.ship.c_str()),Float(picker.height),String(picker.seat.c_str()),Bool(picker.power)};
        switch(e->node_id) {
        case 7:(void)Options(options,4,nullptr,picker.reply,sizeof(picker.reply));break;
        case 8:picker.catalog.Previous();break;
        case 9:picker.catalog.Next();break;
        case 10:picker.catalog.Load(g.api,g.self,"lists/ships.txt",63);break;
        case 12:if(picker.owned>=0 && static_cast<uint32_t>(picker.owned)<g.count) {
            const sco_arg args[]={Int(static_cast<int64_t>(g.tickets[picker.owned].id)),String(picker.seat.c_str()),Bool(picker.power)};
            (void)Board(args,3,nullptr,picker.reply,sizeof(picker.reply));}break;
        case 13:if(picker.owned>=0 && static_cast<uint32_t>(picker.owned)<g.count) {
            const auto id=Int(static_cast<int64_t>(g.tickets[picker.owned].id));(void)Remove(&id,1,nullptr,picker.reply,sizeof(picker.reply));}break;
        case 14:(void)Clear(nullptr,0,nullptr,picker.reply,sizeof(picker.reply));break;
        case 15:(void)Status(nullptr,0,nullptr,picker.reply,sizeof(picker.reply));break;
        case 17:(void)Startup(nullptr,0,nullptr,picker.reply,sizeof(picker.reply));break;
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
    const char* titles[64]{};char ids[64][64]{};
    for(uint32_t i=0;i<g.count;++i){std::snprintf(ids[i],sizeof(ids[i]),"%llu (phase %u)",static_cast<unsigned long long>(g.tickets[i].id),static_cast<unsigned>(g.tickets[i].phase));titles[i]=ids[i];}
    if(picker.owned>=0 && static_cast<uint32_t>(picker.owned)>=g.count)picker.owned=-1;
    char page[96];std::snprintf(page,sizeof(page),"Page %u: %zu matching classes; copied catalog order",picker.catalog.page+1,picker.catalog.visible.size());
    sco_widget_node nodes[]={Node(SCO_WIDGET_TEXT_INPUT,1,"Filter classes"),Node(SCO_WIDGET_LIST,2,"Ship class"),
        Node(SCO_WIDGET_TEXT_INPUT,3,"Class (manual or selected)"),Node(SCO_WIDGET_SLIDER,4,"Height (metres)"),
        Node(SCO_WIDGET_TEXT_INPUT,5,"Seat: pilot, name words, or none"),Node(SCO_WIDGET_TOGGLE,6,"Flight Ready (pilot only)"),
        Node(SCO_WIDGET_BUTTON,7,"Spawn"),Node(SCO_WIDGET_BUTTON,8,"Previous classes"),Node(SCO_WIDGET_BUTTON,9,"Next classes"),
        Node(SCO_WIDGET_BUTTON,10,"Refresh catalog"),Node(SCO_WIDGET_COMBO,11,"Owned ship"),Node(SCO_WIDGET_BUTTON,12,"Board selected ship"),
        Node(SCO_WIDGET_BUTTON,13,"Cancel/remove selected ship"),Node(SCO_WIDGET_BUTTON,14,"Clear owned ships"),Node(SCO_WIDGET_BUTTON,15,"Refresh spawn status"),
        Node(SCO_WIDGET_TOGGLE,16,"Infinite ship ammo (creative plugin)"),Node(SCO_WIDGET_BUTTON,17,"Startup destination (unavailable)"),
        Node(SCO_WIDGET_LABEL,18,picker.catalog.status),Node(SCO_WIDGET_LABEL,19,page),Node(SCO_WIDGET_LABEL,20,picker.reply)};
    nodes[0].text=picker.catalog.filter.c_str();nodes[1].items=picker.catalog.labels.data();nodes[1].item_count=static_cast<uint32_t>(picker.catalog.labels.size());nodes[1].index=-1;
    nodes[2].text=picker.ship.c_str();nodes[3].value=picker.height;nodes[3].maximum=10000;
    nodes[4].text=picker.seat.c_str();nodes[5].value=picker.power?1:0;
    nodes[10].items=titles;nodes[10].item_count=g.count;nodes[10].index=picker.owned;nodes[15].value=picker.ammo?1:0;
    auto r=w->set_panel(g.self,"spawn.vehicles","Vehicles",nodes,20,sizeof(nodes[0]),PickerEvent,nullptr);
    if(r!=SCO_OK)g.api->log(g.self,SCO_LOG_WARN,"Vehicles panel refused; spawn commands remain available");
}
void StartPicker(){picker={};picker.catalog.Load(g.api,g.self,"lists/ships.txt",63);RenderPicker();}
void StopPicker(){const auto* w=Open<sco_widgets_v1>(SCO_WIDGETS_NAME);if(HAS(w,sco_widgets_v1,remove_panel))(void)w->remove_panel(g.self,"spawn.vehicles");picker={};}
