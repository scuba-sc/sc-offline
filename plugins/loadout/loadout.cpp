// SPDX-License-Identifier: GPL-3.0-only
// Product policy only: copied pack lists, typed equip requests and read-only SQ42 observations.
#include "loadout.h"
#include "readable_name.h"
#include "sc_loadout.h"
#include "sc_cvars.h"
#include "sco_content.h"
#include "sco_widgets.h"
#include "slot_catalogue.h"
#include "weapon_types.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sco::features::loadout {
namespace {
#define HAS(t, type, m) ((t) && (t)->size >= offsetof(type, m) + sizeof((t)->m) && (t)->m)
struct Piece { std::string port, name; uint32_t slot = 0; };
struct Outfit { std::string name; std::vector<Piece> pieces; };
struct ContentSource {
    bool present = false;
    uint64_t id = 0;
    std::string pack, name;
};
struct Lists {
    std::vector<Piece> items;
    std::vector<Outfit> outfits;
    std::vector<std::string> buildables;
    ContentSource items_source, outfits_source, buildables_source;
    uint64_t generation = 0;
};
struct State {
    const sco_api* api = nullptr; sco_plugin* self = nullptr;
    Lists lists; bool lists_known = false, spoilers = false, clocked = false;
    uint32_t last_read = 0, page = 0, outfit_page = 0, build_page = 0; int32_t outfit = -1, ship = -1, build = -1;
    uint64_t content_generation = 0, outfit_generation = 0;
    int cvars[4]{-1,-1,-1,-1};
    std::string filter, outfit_filter, ship_filter, equip;
    std::vector<size_t> published_items, published_outfits, published_ships, published_builds;
    ContentSource published_items_source, published_outfits_source, published_buildables_source;
    uint64_t player_content_generation = 0, sq_content_generation = 0;
    uint64_t job = 0; std::vector<Piece> job_pieces; bool job_poll_paused = false;
    // The last valid progress report said the request has finished (complete or failed). The record is
    // only kept so the result can be read; it no longer blocks a new equip request.
    bool job_done = false;
    char job_status[256] = "Nothing is being equipped.";
    std::shared_ptr<const catalogue::Data> curated, published_curated;
    std::array<int32_t,std::size(catalogue::kSlots)> curated_picks{};
    std::array<std::vector<int32_t>,std::size(catalogue::kSlots)> published_rows;
    std::array<std::string,2> weapon_filters, attachment_filters;
    std::array<weapon_types::Type,2> weapon_type{};
    std::array<std::vector<weapon_types::Type>,2> published_types;
    bool player_events = false, sq_events = false; uintptr_t player_cookie = 0, sq_cookie = 0;
    char curated_status[256]{}, curated_plain[256]{};
    double height = 30, ahead = 8; bool sit = true;
    char status[2048]{};
} g;
uintptr_t next_player_cookie = 0, next_sq_cookie = 0;
bool job_observing = false, job_submitting = false, job_cancelling = false;
uint64_t job_revision = 0;
// A request still in progress. A finished one (job_done) is only a result left to read.
bool JobBusy() { return g.job && !g.job_done; }
const sco_plugin_info info{sizeof(sco_plugin_info),SCO_API_MAJOR,SCO_API_MINOR,"loadout","1.0.0","sc-offline"};
const char* const cvar_names[]{"i_target_selector.targeting2_enabled","pl_lensdisplay.minimap_enabled",
    "pl_lensdisplay.greebles_enabled","g_squadron_frontend"};
const char* const cvar_labels[]{"Auto targeting","Visor mini-map","Visor surface detail","Squadron 42 menus (experimental)"};
// Existing product reference names/labels, not fixtures or extracted assets.
struct Ship { const char* label; const char* cls; double height; bool board; };
const Ship ships[]{
    {"Idris-P (the Stanton's class)","AEGS_Idris_P",30,true},
    {"Gladius (Squadron 42 fighter)","AEGS_Gladius",30,true},
    {"Retaliator (has the Squadron 42 HUD)","AEGS_Retaliator",30,true},
    {"Starfarer (chapters 5, 7 and 9)","MISC_Starfarer",30,true},
    {"Avenger Stalker (the Squadron 42 wreck)","AEGS_Avenger_Stalker",30,true},
    {"Hornet (Cal Mason's ship)","ANVL_Hornet_F7C",30,true},
    {"Vanduul Blade (enemy)","VNCL_Blade_PU_AI_VAN",300,false},
    {"Vanduul Scythe (enemy)","VNCL_Scythe_PU_AI_VAN",300,false},
    {"Vanduul Glaive (enemy)","VNCL_Glaive_PU_AI_VAN",300,false},
    {"Vanduul Stinger (enemy)","VNCL_Stinger_PU_AI_VAN",300,false},
    {"Bengal (UEE)","RSI_Bengal_PU_AI_UEE",1500,false},
};
template<class T> const T* Open(const char* name,uint32_t version=0x10000u) {
    const void* table = nullptr;
    if (!HAS(g.api,sco_api,query_service) || g.api->query_service(name,version,&table) != SCO_OK) return nullptr;
    return static_cast<const T*>(table);
}
bool Cap(const char* name) { return HAS(g.api,sco_api,has) && g.api->has(name); }
// Whether Core's outfit slots (22 to 31) are on right now. Read once per decision, not once per piece.
struct OutfitCapabilities { bool outfit=false; };
OutfitCapabilities OutfitCaps() { return {Cap("game.loadout.outfit")}; }
bool SlotOffered(uint32_t slot,const OutfitCapabilities& caps) {
    return caps.outfit && slot>=22 && slot<=31;
}
// `message` is the detailed text for the command reply and the log. `plain` is the sentence the player
// reads in the panel; it never carries command names, versions or reason codes.
sco_result Answer(sco_result result,char* reply,uint32_t size,const char* message,const char* plain) {
    std::snprintf(g.status,sizeof(g.status),"%s",plain);
    if (reply && size) std::snprintf(reply,size,"%s",message);
    if (HAS(g.api,sco_api,log)) g.api->log(g.self,result == SCO_OK ? SCO_LOG_INFO : SCO_LOG_WARN,message);
    return result;
}
std::string Trim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t\r");
    if (first == s.npos) return {};
    return std::string(s.substr(first,s.find_last_not_of(" \t\r")-first+1));
}
bool Same(std::string_view a,std::string_view b) {
    if (a.size()!=b.size()) return false;
    for (size_t i=0;i<a.size();++i) {
        const auto lower=[](unsigned char c) { return c>='A' && c<='Z' ? c+('a'-'A') : c; };
        if (lower(static_cast<unsigned char>(a[i]))!=lower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}
bool SameSource(const ContentSource& a,const ContentSource& b) {
    // pack/path is the deterministic source identity; id is only the SDK's
    // process-local read handle for this copied item.
    return a.present==b.present && (!a.present || (a.pack==b.pack && a.name==b.name));
}
bool ValidPackId(std::string_view pack) {
    if (pack.empty() || pack.size()>31) return false;
    for (unsigned char c:pack) if (!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='_')) return false;
    return true;
}
std::string SourceKey(const ContentSource& source) {
    return source.present ? source.pack+"/"+source.name : "absent";
}
std::string ListsSummary() {
    return "Loadout copied lists ready: items="+std::to_string(g.lists.items.size())+" from "+SourceKey(g.lists.items_source)+
        "; outfits="+std::to_string(g.lists.outfits.size())+" from "+SourceKey(g.lists.outfits_source)+
        "; buildables="+std::to_string(g.lists.buildables.size())+" from "+SourceKey(g.lists.buildables_source)+".";
}
bool Class(std::string_view name) {
    if (name.empty() || name.size()>=SC_LOADOUT_ITEM_NAME_BYTES) return false;
    for (unsigned char c:name) if (!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_')) return false;
    return true;
}
// Core 1.3 outfit slots, same numbers as sc_loadout.h (SC_LOADOUT_SLOT_CLOTHING_TORSO_0 .. SC_LOADOUT_SLOT_JEWELLERY).
// Written as numbers so this file still builds against an SDK kit that predates them. Core has no slot for
// hair, hair colour, eyebrows, head, body, eyes or teeth: those ports map to slot 0 and an outfit using one is refused whole.
constexpr uint32_t kFirstOutfitSlot=22, kLastOutfitSlot=31;
struct ClothingPort { const char* port; uint32_t slot; };
constexpr ClothingPort kClothingPorts[]{
    {"Clothing_Torso_0",22},{"Clothing_Torso_1",23},{"Clothing_Torso2",24},{"Clothing_Legs",25},{"Clothing_Feet",26},
    {"Clothing_Hands",27},{"Hat_ItemPort",28},{"Head_Accessory_ItemPort",29},{"Head_Horn_ItemPort",30},{"Jewellery_ItemPort",31}};
#ifdef SC_LOADOUT_VERSION_1_3
static_assert(SC_LOADOUT_SLOT_CLOTHING_TORSO_0==kFirstOutfitSlot && SC_LOADOUT_SLOT_CLOTHING_TORSO_1==23 && SC_LOADOUT_SLOT_CLOTHING_TORSO_2==24 &&
              SC_LOADOUT_SLOT_CLOTHING_LEGS==25 && SC_LOADOUT_SLOT_CLOTHING_FEET==26 && SC_LOADOUT_SLOT_CLOTHING_HANDS==27 &&
              SC_LOADOUT_SLOT_HAT==28 && SC_LOADOUT_SLOT_HEAD_ACCESSORY==29 && SC_LOADOUT_SLOT_HEAD_HORN==30 &&
              SC_LOADOUT_SLOT_JEWELLERY==kLastOutfitSlot,"outfit slot numbers follow sc_loadout.h");
#endif
uint32_t Slot(std::string_view name,bool outfit) {
    struct Map { const char* item; const char* port; uint32_t slot; };
    // These are logical consumer selections only; the game pack must verify actual engine mapping.
    const Map map[]{ {"undersuit","Armor_Undersuit",SC_LOADOUT_SLOT_UNDERSUIT},
        {"helmet","Armor_Helmet",SC_LOADOUT_SLOT_HEAD},{"torso","Armor_Torso",SC_LOADOUT_SLOT_TORSO},
        {"arms","Armor_Arms",SC_LOADOUT_SLOT_ARMS},{"legs","Armor_Legs",SC_LOADOUT_SLOT_LEGS},
        {"backpack","backpack",SC_LOADOUT_SLOT_BACKPACK},{"visor","Lens_ItemPort",SC_LOADOUT_SLOT_VISOR} };
    for (const auto& m:map) if (Same(name,outfit ? m.port : m.item)) return m.slot;
    // Outfit-only ports. Core 1.3 gives these logical slots (22-31); whether one can be sent is decided
    // when the request is made, from the capabilities Core reports, not here.
    if (outfit) for (const auto& c:kClothingPorts) if (Same(name,c.port)) return c.slot;
    if (!outfit && Same(name,"primary")) return SC_LOADOUT_SLOT_PRIMARY;
    if (!outfit && Same(name,"sidearm")) return SC_LOADOUT_SLOT_SIDEARM;
    return 0;
}
const char* ItemSupport(const Piece& p) {
    return p.slot==SC_LOADOUT_SLOT_PRIMARY || p.slot==SC_LOADOUT_SLOT_SIDEARM ? " [requires 1.1]" : p.slot ? "" : " [unsupported]";
}
sco_result Parse(std::string_view bytes,int kind,Lists& lists) {
    if (bytes.find('\0')!=bytes.npos) return SCO_BAD_ARG;
    std::string section; Outfit* outfit=nullptr;
    while (!bytes.empty()) {
        const auto end=bytes.find('\n'); const auto raw=bytes.substr(0,end);
        bytes=end==bytes.npos ? std::string_view{} : bytes.substr(end+1);
        const auto line=Trim(raw.substr(0,raw.find('#')));
        if (line.empty()) continue;
        if (line.size()>255) return SCO_TOO_MANY;
        if (line.front()=='[') {
            if (line.size()<3 || line.back()!=']') return SCO_BAD_ARG;
            section=Trim(std::string_view(line).substr(1,line.size()-2));
            if (section.empty() || section.size()>127) return SCO_BAD_ARG;
            if (kind==1) {
                if (lists.outfits.size()>=256) return SCO_TOO_MANY;
                lists.outfits.push_back({section,{}}); outfit=&lists.outfits.back();
            }
            continue;
        }
        if (section.empty()) return SCO_BAD_ARG;
        if (kind==0) {
            if (!Class(line)) return SCO_BAD_ARG;
            if (lists.items.size()>=3000) return SCO_TOO_MANY;
            lists.items.push_back({section,line,Slot(section,false)});
        } else if (kind==1) {
            const auto sep=line.find_first_of(" \t");
            if (!outfit || sep==line.npos) return SCO_BAD_ARG;
            const auto port=line.substr(0,sep),name=Trim(std::string_view(line).substr(sep));
            if (!Class(port) || !Class(name)) return SCO_BAD_ARG;
            if (outfit->pieces.size()>=32) return SCO_TOO_MANY;
            outfit->pieces.push_back({port,name,Slot(port,true)});
        } else if (Same(section,"sq42")) {
            // buildables uses a separate retained list format; first name before a pipe is its command key.
            const auto name=Trim(std::string_view(line).substr(0,line.find('|')));
            if (lists.buildables.size()>=256) return SCO_TOO_MANY;
            if (!name.empty()) lists.buildables.push_back(name);
        }
    }
    return SCO_OK;
}
sco_result ReadLists(std::string* failure=nullptr) {
    auto fail=[&](sco_result result,const char* message) {
        if (failure) *failure=message;
        return result;
    };
    // A reload withdraws the old snapshot before reading. Callbacks and selections
    // from that generation cannot act while a replacement is being assembled.
    g.player_events=false; g.sq_events=false; g.lists_known=false;
    g.lists={}; g.published_items.clear(); g.published_outfits.clear(); g.published_ships.clear(); g.published_builds.clear();
    g.published_items_source={}; g.published_outfits_source={}; g.published_buildables_source={};
    g.player_content_generation=0; g.sq_content_generation=0; g.outfit_generation=0;
    g.outfit=-1; g.build=-1; g.page=0; g.outfit_page=0; g.build_page=0; g.equip.clear();
    if (failure) failure->clear();
    if (g.content_generation==std::numeric_limits<uint64_t>::max())
        return fail(SCO_TOO_MANY,"Loadout content generation limit reached; restart the host before reloading.");
    const auto generation=++g.content_generation;

    const auto* content=Open<sco_content_v1>(SCO_CONTENT_NAME);
    if (!HAS(content,sco_content_v1,item) || !HAS(content,sco_content_v1,read))
        return fail(SCO_UNAVAILABLE,"The public content service is unavailable; no private-file fallback.");
    Lists candidate; candidate.generation=generation;
    std::vector<uint64_t> source_ids;
    uint32_t total=0; bool found=false;
    for (uint32_t ordinal=0;ordinal<=SCO_CONTENT_MAX_ITEMS;++ordinal) {
        sco_content_item item{}; item.size=sizeof(item);
        const auto r=content->item(g.self,SCO_CONTENT_LIST,ordinal,&item);
        if (r==SCO_NOT_FOUND) {
            if (!found)
                return fail(SCO_NOT_FOUND,"No supported copied list sources were present; no private-file fallback.");
            g.lists=std::move(candidate); g.lists_known=true;
            return SCO_OK;
        }
        if (r!=SCO_OK) return fail(r,"The copied-content index could not be read completely.");
        if (ordinal==SCO_CONTENT_MAX_ITEMS || item.size!=sizeof(item) ||
            item.kind!=SCO_CONTENT_LIST || !std::memchr(item.name,0,sizeof(item.name)))
            return fail(SCO_FAILED,"The content service returned an invalid list record; no snapshot was published.");
        const std::string_view name(item.name);
        const int kind=Same(name,"lists/items.txt") ? 0 : Same(name,"lists/outfits.txt") ? 1 :
            Same(name,"lists/buildables.txt") ? 2 : -1;
        if (kind<0) continue;

        const auto* pack_end=static_cast<const char*>(std::memchr(item.pack,0,sizeof(item.pack)));
        if (!pack_end) return fail(SCO_FAILED,"The content service returned an unterminated pack identity.");
        const std::string_view pack(item.pack,static_cast<size_t>(pack_end-item.pack));
        if (!ValidPackId(pack))
            return fail(SCO_FAILED,"The content service returned an invalid pack identity.");
        ContentSource* source=kind==0 ? &candidate.items_source :
            kind==1 ? &candidate.outfits_source : &candidate.buildables_source;
        if (source->present)
            return fail(SCO_BAD_ARG,"Ambiguous copied list source; enable exactly one pack for each supported list path.");
        if (std::find(source_ids.begin(),source_ids.end(),item.id)!=source_ids.end())
            return fail(SCO_FAILED,"The content service reused an item identity; no snapshot was published.");
        source->present=true; source->id=item.id; source->pack.assign(pack); source->name.assign(name);
        source_ids.push_back(item.id);

        uint32_t needed=0; const auto asked=content->read(g.self,item.id,nullptr,&needed);
        if (asked!=SCO_TOO_MANY && !(asked==SCO_OK && needed==0))
            return fail(asked==SCO_OK ? SCO_FAILED : asked,"The copied list size could not be read.");
        if (needed>SCO_CONTENT_MAX_FILE_BYTES || total>SCO_CONTENT_MAX_TOTAL_BYTES-needed)
            return fail(SCO_TOO_MANY,"Copied lists exceed the public per-file or total-byte limit.");
        total+=needed;
        std::string bytes(needed,'\0'); uint32_t count=needed;
        if (needed) {
            const auto read=content->read(g.self,item.id,bytes.data(),&count);
            if (read!=SCO_OK) return fail(read,"Copied list bytes could not be read completely.");
        }
        if (count!=needed) return fail(SCO_FAILED,"The copied list read returned an incomplete byte count.");
        const auto parsed=Parse(bytes,kind,candidate);
        if (parsed!=SCO_OK)
            return fail(parsed,"A copied list is malformed or incomplete; the whole candidate snapshot was refused.");
        found=true;
    }
    return fail(SCO_TOO_MANY,"Copied-content enumeration exceeded the public item limit.");
}
sco_result Ready(char* out,uint32_t size) {
    if (g.lists_known) return SCO_OK;
    std::string failure;
    const auto r=ReadLists(&failure);
    if (r!=SCO_OK) Answer(r,out,size,failure.empty() ? "Loadout lists unavailable/refused; no private-file fallback." : failure.c_str(),"The gear lists couldn't be loaded.");
    return r;
}
sco_result Validate(const sco_arg* args,uint32_t n,uint32_t expected,char* out,uint32_t size) {
    if (!g.api || !g.self) return SCO_UNAVAILABLE;
    if ((!out && size) || n!=expected || (expected && !args)) return SCO_BAD_ARG;
    return SCO_OK;
}
bool String(const sco_arg& arg,size_t max) { return arg.type==SCO_ARG_STRING && arg.v.s && std::strlen(arg.v.s)<=max; }
const char* Status(uint32_t s) {
    switch(s) { case SC_LOADOUT_APPLIED:return "applied"; case SC_LOADOUT_UNSUPPORTED:return "unsupported";
        case SC_LOADOUT_ITEM_MISSING:return "item-missing"; case SC_LOADOUT_FAILED:return "failed";
        case SC_LOADOUT_UNAVAILABLE:return "unavailable"; default:return "invalid"; }
}
sco_result Apply(const std::vector<Piece>& pieces,char* out,uint32_t size,uint32_t version=SC_LOADOUT_VERSION_1_0) {
    if (JobBusy() || job_submitting || job_cancelling) return Answer(SCO_UNAVAILABLE,out,size,"An equip request is already pending: check its progress or cancel it before sending another.","Finish or cancel the current equipping first.");
    if (pieces.empty()) return Answer(SCO_BAD_ARG,out,size,"No selected pieces; unspecified slots are unchanged (not cleared/defaulted).","Nothing is selected.");
    if (pieces.size()>SC_LOADOUT_MAX_ITEMS) return Answer(SCO_TOO_MANY,out,size,"More than 16 pieces: entire request refused before equip; no split/retry.","That is too many items at once. Choose 16 or fewer.");
    sc_loadout_item items[SC_LOADOUT_MAX_ITEMS]{};
    for (size_t i=0;i<pieces.size();++i) {
        const auto& p=pieces[i];
        if (!p.slot) {
            const auto message="Unsupported legacy slot/port '"+p.port+"': whole selection refused; this consumer does not map that slot.";
            return Answer(SCO_UNAVAILABLE,out,size,message.c_str(),"That item can't be equipped from here.");
        }
        if (p.slot>SC_LOADOUT_SLOT_SIDEARM_TOOL_BATTERY)
            return Answer(SCO_UNAVAILABLE,out,size,"Outfit slots (22-34) go only through the background equip route; the synchronous apply reports them unsupported. Nothing sent.","That item can't be equipped from here.");
        if (!Class(p.name) || Same(p.name,"none") || Same(p.name,"default"))
            return Answer(SCO_BAD_ARG,out,size,"Invalid or clear/default class; entire selection refused.","That item isn't valid.");
        for (size_t j=0;j<i;++j) if (pieces[j].slot==p.slot) return Answer(SCO_BAD_ARG,out,size,"Duplicate logical slot: entire selection refused before equip.","Two of the chosen items go in the same place.");
        items[i].slot=p.slot; std::memcpy(items[i].item_class,p.name.c_str(),p.name.size()+1);
    }
    const auto* loadout=Open<sc_loadout_v1>(SC_LOADOUT_SERVICE_NAME,version);
    if (!Cap("game.loadout.apply") || (version>=SC_LOADOUT_VERSION_1_1 && !Cap("game.loadout.weapons")) || !HAS(loadout,sc_loadout_v1,apply))
        return Answer(SCO_UNAVAILABLE,out,size,"Required typed loadout version/method/capability unavailable; no XML/private equip fallback.","Changing gear isn't available yet.");
    sc_loadout_request request{sizeof(request),static_cast<uint32_t>(pieces.size()),sizeof(sc_loadout_item),0,items};
    sc_loadout_report report{}; report.size=sizeof(report);
    const auto result=loadout->apply(g.self,&request,&report);
    if (result!=SCO_OK && result!=SCO_FAILED && result!=SCO_UNAVAILABLE) return Answer(result,out,size,"Loadout provider rejected batch; report not interpreted, no retry.","The game refused to change your gear.");
    if (report.size<sizeof(report) || report.count!=request.count || report.applied>report.count) return Answer(SCO_FAILED,out,size,"Invalid loadout report; state unknown, no retry.","Couldn't tell what changed. Check your gear.");
    uint32_t applied=0;
    for (uint32_t i=0;i<report.count;++i) {
        if (report.slots[i].slot!=items[i].slot || static_cast<uint32_t>(report.slots[i].status)>SC_LOADOUT_UNAVAILABLE) return Answer(SCO_FAILED,out,size,"Invalid slot report; state unknown, no retry.","Couldn't tell what changed. Check your gear.");
        applied+=report.slots[i].status==SC_LOADOUT_APPLIED;
    }
    if (applied!=report.applied || (result==SCO_OK && applied!=report.count) || (result==SCO_UNAVAILABLE && applied)) return Answer(SCO_FAILED,out,size,"Inconsistent loadout report; state unknown, no retry.","Couldn't tell what changed. Check your gear.");
    std::string message="applied="+std::to_string(applied)+"/"+std::to_string(report.count)+"; no rollback/automatic retry";
    for (uint32_t i=0;i<report.count;++i) message+="; "+pieces[i].port+"="+Status(report.slots[i].status);
    const std::string summary=applied==report.count ? "Equipped "+std::to_string(applied)+(applied==1 ? " item." : " items.") :
        applied ? "Equipped "+std::to_string(applied)+" of "+std::to_string(report.count)+" items. The rest couldn't be equipped." :
        "Couldn't equip those items.";
    return Answer(result,out,size,message.c_str(),summary.c_str());
}
const auto& weapon_slots=catalogue::kSlots;
sco_result SlotPieces(const sco_arg* args,uint32_t n,char* out,uint32_t size,std::vector<Piece>& pieces) {
    const auto refuse=[&] {
        return Answer(SCO_BAD_ARG,out,size,"Manual slots refused: use unique public slot=Class pairs (at most 2047 bytes). Classes need 1-95 ASCII letters, digits or underscores. Slots set to None/default are skipped (never cleared); no new request.","Those weapon choices aren't valid.");
    };
    // Nothing chosen is not a malformed request: say what to do, send nothing, and log it as info.
    const auto choose=[&] {
        constexpr const char* message="Choose at least one slot as slot=Class; slots left at None/default are skipped and nothing was sent.";
        std::snprintf(g.status,sizeof(g.status),"%s","Choose at least one weapon or attachment.");
        if (out && size) std::snprintf(out,size,"%s",message);
        if (HAS(g.api,sco_api,log)) g.api->log(g.self,SCO_LOG_INFO,message);
        return SCO_BAD_ARG;
    };
    const auto r=Validate(args,n,1,out,size); if (r!=SCO_OK) return r==SCO_BAD_ARG ? refuse() : r;
    if (!String(args[0],2047)) return refuse();
    std::string_view input=args[0].v.s;
    if (Trim(input).empty()) return choose();
    while (!input.empty()) {
        const auto comma=input.find(','); const auto assignment=input.substr(0,comma);
        const auto equal=assignment.find('='); if (equal==assignment.npos) return refuse();
        const auto name=Trim(assignment.substr(0,equal)),cls=Trim(assignment.substr(equal+1));
        const bool unset=Same(cls,"none") || Same(cls,"default");   // an unset slot is skipped, never sent
        if (!unset && !Class(cls)) return refuse();
        uint32_t slot=0;
        for (const auto& entry:weapon_slots) if (name==entry.name) slot=entry.slot;
        if (!slot) return refuse();
        if (unset) { if (comma==input.npos) break; input.remove_prefix(comma+1); if (input.empty()) return refuse(); continue; }
        for (const auto& piece:pieces) if (piece.slot==slot) return refuse();
        pieces.push_back({name,cls,slot});
        if (comma==input.npos) break;
        input.remove_prefix(comma+1); if (input.empty()) return refuse();
    }
    if (pieces.empty()) return choose();
    // Parents 8/9 precede every attachment in the same batch, regardless of input order.
    std::sort(pieces.begin(),pieces.end(),[](const Piece& a,const Piece& b) { return a.slot<b.slot; });
    return SCO_OK;
}
sco_result Slots(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    std::vector<Piece> pieces; const auto r=SlotPieces(args,n,out,size,pieces);
    return r==SCO_OK ? Apply(pieces,out,size,SC_LOADOUT_VERSION_1_1) : r;
}
sco_result Cancel(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size);
sco_result SubmitPieces(std::vector<Piece> pieces,char* out,uint32_t size) {
    // A finished request is only a result left to read: release it so the next one can start. If Core
    // refuses the release, the record stays and the gate below refuses the new request.
    if (g.job && g.job_done && !job_submitting && !job_cancelling) {
        (void)Cancel(nullptr,0,nullptr,nullptr,0);
        if (g.job) return Answer(SCO_UNAVAILABLE,out,size,"The finished equip request could not be released; nothing was sent.","Couldn't clear the last result. Try again.");
    }
    if (g.job || job_submitting || job_cancelling) return Answer(SCO_UNAVAILABLE,out,size,"An equip request is already pending or being sent: check its progress or cancel it before sending another.","Finish or cancel the current equipping first.");
    if (pieces.empty()) return Answer(SCO_BAD_ARG,out,size,"No selected pieces; nothing was sent.","Nothing is selected.");
    if (pieces.size()>SC_LOADOUT_MAX_ITEMS) return Answer(SCO_TOO_MANY,out,size,"More than 16 pieces; nothing was sent.","That is too many items at once. Choose 16 or fewer.");
    const auto caps=OutfitCaps();
    for (size_t i=0;i<pieces.size();++i) {
        const auto& p=pieces[i];
        if (!p.slot || (p.slot>SC_LOADOUT_SLOT_SIDEARM_TOOL_BATTERY && !SlotOffered(p.slot,caps)))
            return Answer(SCO_UNAVAILABLE,out,size,"Unsupported legacy slot/port; entire selection refused.","That item can't be equipped from here.");
        if (!Class(p.name) || Same(p.name,"none") || Same(p.name,"default")) return Answer(SCO_BAD_ARG,out,size,"Invalid or clear/default class; entire selection refused.","That item isn't valid.");
        for (size_t j=0;j<i;++j) if (pieces[j].slot==p.slot)
            return Answer(SCO_BAD_ARG,out,size,"Duplicate logical slot; entire selection refused.","Two of the chosen items go in the same place.");
    }
    struct Guard { Guard(){job_submitting=true;} ~Guard(){job_submitting=false;} } guard;
    const auto* api=g.api; auto* owner=g.self; const auto revision=job_revision;
    const auto* service=Open<sc_loadout_v1_2>(SC_LOADOUT_SERVICE_NAME,SC_LOADOUT_VERSION_1_2);
    if (g.api!=api || g.self!=owner || job_revision!=revision) return SCO_UNAVAILABLE;
    const bool admitted=Cap("game.loadout.async");
    if (g.api!=api || g.self!=owner || job_revision!=revision) return SCO_UNAVAILABLE;
    if (!admitted || !HAS(service,sc_loadout_v1_2,submit) ||
        !HAS(service,sc_loadout_v1_2,status) || !HAS(service,sc_loadout_v1_2,cancel))
        return Answer(SCO_UNAVAILABLE,out,size,"Background equipping is unavailable (service or capability missing); no synchronous fallback.","Changing gear isn't available yet.");
    if (g.api!=api || g.self!=owner || job_revision!=revision) return SCO_UNAVAILABLE;
    sc_loadout_item items[SC_LOADOUT_MAX_ITEMS]{};
    for (size_t i=0;i<pieces.size();++i) { items[i].slot=pieces[i].slot; std::memcpy(items[i].item_class,pieces[i].name.c_str(),pieces[i].name.size()+1); }
    sc_loadout_request request{sizeof(request),static_cast<uint32_t>(pieces.size()),sizeof(sc_loadout_item),0,items};
    uint64_t job=0; const auto result=service->submit(owner,&request,&job);
    if (g.api!=api || g.self!=owner || job_revision!=revision) return SCO_UNAVAILABLE;
    if (result!=SCO_OK) return Answer(result,out,size,"Loadout submit refused; no retry or synchronous fallback.","The game refused the request.");
    if (!job) return Answer(SCO_FAILED,out,size,"Invalid zero request id; state unknown, no retry.","Couldn't start equipping.");
    g.job=job; g.job_pieces=std::move(pieces); g.job_poll_paused=false; g.job_done=false; ++job_revision;
    std::snprintf(g.job_status,sizeof(g.job_status),"Equipping: waiting for the game.");
    return Answer(SCO_OK,out,size,"Equip request sent, not confirmed equipped. Check its progress; cancelling releases pending work without rollback.","Equipping started. Progress shows below.");
}
sco_result Submit(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    std::vector<Piece> pieces; const auto parsed=SlotPieces(args,n,out,size,pieces);
    return parsed==SCO_OK ? SubmitPieces(std::move(pieces),out,size) : parsed;
}
// A status observation is copied only for this call; no service table survives a lookup.
// `message` is the detailed reply and log text; `plain` is the sentence the panel shows.
sco_result ObserveJob(std::string& message, std::string& plain, bool& malformed, bool& obsolete) {
    malformed=false; obsolete=false; plain="Can't check progress right now.";
    if (!g.job) { message="No equip request is pending."; plain="Nothing is being equipped."; return SCO_NOT_FOUND; }
    const auto* api=g.api; auto* owner=g.self; const auto job=g.job; const auto revision=job_revision;
    const auto pieces=g.job_pieces;
    struct Guard { Guard() { job_observing=true; } ~Guard() { job_observing=false; } } guard;
    const auto* service=Open<sc_loadout_v1_2>(SC_LOADOUT_SERVICE_NAME,SC_LOADOUT_VERSION_1_2);
    if (job_revision!=revision || g.api!=api || g.self!=owner || g.job!=job) { obsolete=true; message="The equip request changed during the check; status unknown."; plain="Progress changed. Check again."; return SCO_UNAVAILABLE; }
    if (!service || service->size<sizeof(sc_loadout_v1_2) || !service->status) {
        message="Status service unavailable; the request stays pending until cancelled; no equip retry."; return SCO_UNAVAILABLE;
    }
    sc_loadout_job_report progress{}; progress.size=sizeof(progress); progress.report.size=sizeof(progress.report);
    const auto result=service->status(owner,job,&progress);
    if (job_revision!=revision || g.api!=api || g.self!=owner || g.job!=job) { obsolete=true; message="The equip request changed during the check; status unknown."; plain="Progress changed. Check again."; return SCO_UNAVAILABLE; }
    if (result!=SCO_OK) { message="Loadout status refused; no completion inferred or automatic equip retry."; return result; }
    if (job_revision!=revision || g.api!=api || g.self!=owner || g.job!=job) { obsolete=true; message="The equip request changed during the check; status unknown."; plain="Progress changed. Check again."; return SCO_UNAVAILABLE; }
    const auto& report=progress.report;
    if (progress.size<sizeof(progress) || report.size<sizeof(report) || report.count!=pieces.size() ||
        progress.completed>report.count || report.applied>progress.completed || static_cast<uint32_t>(progress.state)>SC_LOADOUT_JOB_FAILED ||
        (progress.state==SC_LOADOUT_JOB_COMPLETE && progress.completed!=report.count)) malformed=true;
    uint32_t applied=0;
    if (!malformed) for (uint32_t i=0;i<progress.completed;++i) {
        if (report.slots[i].slot!=pieces[i].slot || static_cast<uint32_t>(report.slots[i].status)>SC_LOADOUT_UNAVAILABLE) { malformed=true; break; }
        applied+=report.slots[i].status==SC_LOADOUT_APPLIED;
    }
    if (malformed || applied!=report.applied) {
        malformed=true; message="Invalid progress report; polling paused. Check again or cancel; the request stays pending.";
        plain="Couldn't read progress. Cancel and try again."; return SCO_FAILED;
    }
    const char* state=progress.state==SC_LOADOUT_JOB_PENDING ? "pending" : progress.state==SC_LOADOUT_JOB_COMPLETE ? "complete" : "failed";
    message=std::string(state)+"; completed="+std::to_string(progress.completed)+"/"+std::to_string(report.count)+
        "; applied="+std::to_string(applied)+"/"+std::to_string(report.count)+"; no rollback/automatic equip retry; cancel to release the pending request";
    for (uint32_t i=0;i<progress.completed;++i) message+="; "+pieces[i].port+"="+Status(report.slots[i].status);
    const auto done=std::to_string(applied), all=std::to_string(report.count);
    plain=progress.state==SC_LOADOUT_JOB_PENDING ? "Equipping: "+std::to_string(progress.completed)+" of "+all+" done." :
        progress.state==SC_LOADOUT_JOB_COMPLETE ? (applied==report.count ? "Done. Equipped "+done+" of "+all+" items." :
            applied==0 ? std::string("Finished, but nothing could be equipped.") :
            "Finished. Equipped "+done+" of "+all+" items; the rest couldn't be equipped.") :
        "Equipping failed. Equipped "+done+" of "+all+" items.";
    g.job_done=progress.state!=SC_LOADOUT_JOB_PENDING;
    return SCO_OK;
}
void JobLabel(const std::string& plain) { std::snprintf(g.job_status,sizeof(g.job_status),"%.255s",plain.c_str()); }
sco_result JobReply(sco_result result,char* out,uint32_t size,const char* message) {
    if (out && size) std::snprintf(out,size,"%s",message);
    try { if (HAS(g.api,sco_api,log)) g.api->log(g.self,result==SCO_OK ? SCO_LOG_INFO : SCO_LOG_WARN,message); } catch (...) { /* logging is best effort */ }
    return result;
}
sco_result Progress(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    const auto valid=Validate(args,n,0,out,size); if (valid!=SCO_OK) return valid;
    if (job_observing) return SCO_UNAVAILABLE;
    const auto revision=job_revision;
    try {
        std::string message,plain; bool malformed=false,obsolete=false; const auto result=ObserveJob(message,plain,malformed,obsolete);
        if (obsolete) { if (out && size) std::snprintf(out,size,"%s",message.c_str()); return result; }
        // An explicit valid inspection resumes polling; refusal leaves a malformed-report pause in place.
        if (malformed) g.job_poll_paused=true; else if (result==SCO_OK) g.job_poll_paused=false;
        JobLabel(plain); return JobReply(result,out,size,message.c_str());
    } catch (...) {
        if (revision!=job_revision) return SCO_UNAVAILABLE;
        g.job_poll_paused=true;
        std::snprintf(g.job_status,sizeof(g.job_status),"Couldn't read progress. Cancel and try again.");
        return JobReply(SCO_FAILED,out,size,"Loadout status unknown; polling paused. Inspect again or cancel.");
    }
}
sco_result Cancel(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    const auto valid=Validate(args,n,0,out,size); if (valid!=SCO_OK) return valid;
    if (job_cancelling) return SCO_UNAVAILABLE;
    if (!g.job) return Answer(SCO_NOT_FOUND,out,size,"No equip request is pending.","Nothing is being equipped.");
    struct Guard { Guard(){job_cancelling=true;} ~Guard(){job_cancelling=false;} } guard;
    const auto* api=g.api; auto* owner=g.self; const auto job=g.job; const auto revision=job_revision;
    const auto current=[&] { return g.api==api && g.self==owner && g.job==job && job_revision==revision; };
    const auto* service=Open<sc_loadout_v1_2>(SC_LOADOUT_SERVICE_NAME,SC_LOADOUT_VERSION_1_2);
    if (!current()) return SCO_UNAVAILABLE;
    if (!HAS(service,sc_loadout_v1_2,cancel)) return Answer(SCO_UNAVAILABLE,out,size,"Cancellation unavailable; the request stays pending, owner unload remains the host cleanup boundary.","Couldn't cancel right now. Try again.");
    // Cleanup remains callable after mutation capability revocation; never undo confirmed equipment.
    const auto result=service->cancel(owner,job);
    if (!current()) return SCO_UNAVAILABLE;
    const bool finished=g.job_done;
    if (result==SCO_OK || result==SCO_NOT_FOUND) { g.job=0; g.job_pieces.clear(); g.job_poll_paused=false; g.job_done=false; ++job_revision; JobLabel("Nothing is being equipped."); }
    return Answer(result,out,size,result==SCO_OK ? "Equip request released; confirmed equipment remains, no rollback." : "Loadout cancellation refused/not found; no rollback or retry.",
        result==SCO_OK ? (finished ? "Cleared the last result. Anything equipped stays on." : "Stopped equipping. Anything already equipped stays on.") :
        finished ? "Couldn't clear the last result. Try again." : "Couldn't cancel. Try again.");
}
sco_result ReadSlots() {
    g.player_events=false;g.weapon_type.fill(weapon_types::All);g.weapon_filters={};g.attachment_filters={};g.curated_picks.fill(0);g.curated.reset();g.published_curated.reset();
    catalogue::Data data;const auto result=catalogue::Read(g.api,g.self,data);
    if(result==SCO_OK) {
        g.curated=std::make_shared<const catalogue::Data>(std::move(data));
        std::snprintf(g.curated_status,sizeof(g.curated_status),"Curated slots: %u copied choices; compatibility/equip is decided by the provider.",g.curated->count);
        if (g.curated->count) std::snprintf(g.curated_plain,sizeof(g.curated_plain),"%u weapons, tools and attachments to choose from.",g.curated->count);
        else std::snprintf(g.curated_plain,sizeof(g.curated_plain),"The weapon list is empty, so there is nothing to choose.");
    } else {
        const char* reason=result==SCO_NOT_FOUND?"missing enabled List":result==SCO_BAD_ARG?"ambiguous or malformed weapon list":result==SCO_TOO_MANY?"weapon list limits exceeded":"public content unavailable/refused";
        std::snprintf(g.curated_status,sizeof(g.curated_status),"Curated slots unavailable: %s. Use exactly one lists/loadout_slots.txt; no raw-file import.",reason);
        std::snprintf(g.curated_plain,sizeof(g.curated_plain),"No weapon list is available, so there is nothing to choose.");
    }
    return result;
}
sco_result ReloadSlots(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    const auto valid=Validate(args,n,0,out,size);if(valid!=SCO_OK)return valid;
    const auto result=ReadSlots();return Answer(result,out,size,g.curated_status,result==SCO_OK ? "Weapon list refreshed." : "The weapon list couldn't be loaded.");
}
void Curated(bool asynchronous) {
    if(!g.published_curated){Answer(SCO_UNAVAILABLE,nullptr,0,g.curated_status,"No weapon list is available.");return;}
    std::string assignments;
    for(size_t i=0;i<std::size(weapon_slots);++i) {
        const auto pick=g.curated_picks[i];if(!pick)continue;
        const auto& choices=g.published_curated->classes[i];
        if(pick<0||static_cast<size_t>(pick)>choices.size()){Answer(SCO_BAD_ARG,nullptr,0,"Curated selection invalid; no equip requested.","That weapon choice isn't valid.");return;}
        if(!assignments.empty())assignments+=',';
        assignments+=std::string(weapon_slots[i].name)+'='+choices[static_cast<size_t>(pick)-1];
    }
    if(assignments.empty()){Answer(SCO_BAD_ARG,nullptr,0,"No curated choices selected. Unchanged omits the slot; equipment is not cleared.","Choose at least one weapon or attachment.");return;}
    sco_arg arg{};arg.type=SCO_ARG_STRING;arg.v.s=assignments.c_str();
    if(asynchronous)(void)Submit(&arg,1,nullptr,nullptr,0);else (void)Slots(&arg,1,nullptr,nullptr,0);
}
sco_result EquipSelection(const sco_arg* args,uint32_t n,char* out,uint32_t size,bool async) {
    auto r=Validate(args,n,1,out,size); if (r!=SCO_OK) return r;
    if (!String(args[0],1023) || !*args[0].v.s) return SCO_BAD_ARG;
    r=Ready(out,size); if (r!=SCO_OK) return r;
    if (!g.lists.items_source.present)
        return Answer(SCO_NOT_FOUND,out,size,"No enabled data pack supplies lists/items.txt; no private-file fallback.","No gear list is installed.");
    std::string_view names=args[0].v.s; std::vector<Piece> pieces;
    while (!names.empty()) {
        const auto start=names.find_first_not_of(" ,\t"); if (start==names.npos) break;
        names.remove_prefix(start); const auto end=names.find_first_of(" ,\t"); const auto name=names.substr(0,end);
        names=end==names.npos ? std::string_view{} : names.substr(end);
        const Piece* match=nullptr;
        for (const auto& p:g.lists.items) if (Same(name,p.name)) {
            if (match && !Same(match->port,p.port)) return Answer(SCO_BAD_ARG,out,size,"Item appears in multiple slots; ambiguous selection refused.","That item fits more than one place, so it can't be used.");
            if (!match) match=&p;
        }
        if (!match) return Answer(SCO_NOT_FOUND,out,size,("Item not in copied items.txt: "+std::string(name)).c_str(),"That item isn't in the gear list.");
        if (pieces.size()==SC_LOADOUT_MAX_ITEMS) return Answer(SCO_TOO_MANY,out,size,"Too many selected items; no equip requested.","That is too many items at once. Choose 16 or fewer.");
        pieces.push_back(*match);
    }
    const bool weapons=std::any_of(pieces.begin(),pieces.end(),[](const Piece& p) {
        return p.slot==SC_LOADOUT_SLOT_PRIMARY || p.slot==SC_LOADOUT_SLOT_SIDEARM;
    });
    return async ? SubmitPieces(std::move(pieces),out,size) :
        Apply(pieces,out,size,weapons ? SC_LOADOUT_VERSION_1_1 : SC_LOADOUT_VERSION_1_0);
}
sco_result Equip(const sco_arg* a,uint32_t n,void*,char* o,uint32_t z) { return EquipSelection(a,n,o,z,false); }
sco_result SubmitGear(const sco_arg* a,uint32_t n,void*,char* o,uint32_t z) { return EquipSelection(a,n,o,z,true); }
// An outfit can be worn only when every piece goes in a slot this menu can set, no two pieces share a
// slot and the whole outfit fits one request. Clothing, hat, head accessory, head horn and jewellery have a
// slot only while Core reports game.loadout.outfit. Hair, hair colour, eyebrows, body, head, eyes and teeth
// have no slot at all, so an outfit with one of those is never offered.
bool PiecesMapped(const Outfit& outfit,const OutfitCapabilities& caps) {
    return !outfit.pieces.empty() && std::all_of(outfit.pieces.begin(),outfit.pieces.end(),[&](const Piece& p) {
        return p.slot && (p.slot<=SC_LOADOUT_SLOT_SIDEARM_TOOL_BATTERY || SlotOffered(p.slot,caps)); });
}
// The player's word for an outfit part, for a refusal that names it.
std::string PartName(const std::string& port) {
    static const struct { const char* port; const char* name; } names[]{
        {"Hair_ItemPort","hair"},{"Hair_Color","hair colour"},{"Eyebrow_ItemPort","eyebrows"},{"Body_ItemPort","body"},
        {"Head_ItemPort","head"},{"Eyes_ItemPort","eyes"},{"Teeth_ItemPort","teeth"},{"Eye_Accessories_ItemPort","eye accessories"}};
    for (const auto& n:names) if (Same(port,n.port)) return n.name;
    auto text=Readable(port);
    for (auto& c:text) if (c>='A' && c<='Z') c=static_cast<char>(c+('a'-'A'));
    return text;
}
// First piece that has no slot at all in this Core (hair, eyebrows, face, body and similar), or null.
const Piece* PieceWithoutSlot(const Outfit& outfit) {
    for (const auto& p:outfit.pieces) if (!p.slot) return &p;
    return nullptr;
}
bool Wearable(const Outfit& outfit,const OutfitCapabilities& caps) {
    if (!PiecesMapped(outfit,caps) || outfit.pieces.size()>SC_LOADOUT_MAX_ITEMS) return false;
    for (size_t i=0;i<outfit.pieces.size();++i) for (size_t j=0;j<i;++j) if (outfit.pieces[j].slot==outfit.pieces[i].slot) return false;
    return true;
}
sco_result WearSelection(const sco_arg* args,uint32_t n,char* out,uint32_t size,bool async) {
    auto r=Validate(args,n,1,out,size); if (r!=SCO_OK) return r;
    if (!String(args[0],127) || !*args[0].v.s) return SCO_BAD_ARG;
    if (!g.spoilers) return Answer(SCO_UNAVAILABLE,out,size,"SQ42 spoiler confirmation required: confirm spoilers first (session-only).","Confirm the spoiler warning first.");
    r=Ready(out,size); if (r!=SCO_OK) return r;
    if (!g.lists.outfits_source.present)
        return Answer(SCO_NOT_FOUND,out,size,"No enabled data pack supplies lists/outfits.txt; no private-file fallback.","No outfits are installed.");
    const Outfit* found=nullptr;
    for (const auto& outfit:g.lists.outfits) if (Same(outfit.name,args[0].v.s)) {
        if (found) return Answer(SCO_BAD_ARG,out,size,("Duplicate outfit name in "+SourceKey(g.lists.outfits_source)+"; ambiguous selection refused.").c_str(),"Two outfits share that name, so it can't be used.");
        found=&outfit;
    }
    if (!found) return Answer(SCO_NOT_FOUND,out,size,"Outfit not in copied outfits.txt.","That outfit isn't in the list.");
    if (const auto* missing=PieceWithoutSlot(*found))
        return Answer(SCO_UNAVAILABLE,out,size,("Outfit piece "+missing->port+" has no public loadout slot in this Core; whole outfit refused, nothing sent.").c_str(),
            ("This outfit includes "+PartName(missing->port)+", and the game has nowhere to put it yet. Nothing was changed.").c_str());
    if (!found->pieces.empty() && !PiecesMapped(*found,OutfitCaps())) return Answer(SCO_UNAVAILABLE,out,size,"Outfit slots are switched off in Core; whole outfit refused, nothing sent.","Changing outfits isn't turned on.");
    return async ? SubmitPieces(found->pieces,out,size) : Apply(found->pieces,out,size);
}
sco_result Wear(const sco_arg* a,uint32_t n,void*,char* o,uint32_t z) { return WearSelection(a,n,o,z,false); }
sco_result SubmitOutfit(const sco_arg* a,uint32_t n,void*,char* o,uint32_t z) { return WearSelection(a,n,o,z,true); }
sco_result Spoilers(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    const auto r=Validate(args,n,1,out,size); if (r!=SCO_OK) return r;
    if (args[0].type!=SCO_ARG_BOOL || (args[0].v.i!=0 && args[0].v.i!=1)) return SCO_BAD_ARG;
    g.spoilers=args[0].v.i!=0;
    for (auto& value:g.cvars) value=-1;
    return Answer(SCO_OK,out,size,g.spoilers ? "SQ42 spoilers confirmed for this loaded session." : "SQ42 spoiler confirmation cleared.",
        g.spoilers ? "Spoilers are shown for this session." : "Spoilers are hidden.");
}
sco_result Visor(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    const auto r=Validate(args,n,1,out,size); if (r!=SCO_OK) return r;
    if (!String(args[0],95) || !Class(args[0].v.s)) return SCO_BAD_ARG;
    if (!g.spoilers) return Answer(SCO_UNAVAILABLE,out,size,"SQ42 spoiler confirmation required.","Confirm the spoiler warning first.");
    return Apply({{"visor",args[0].v.s,SC_LOADOUT_SLOT_VISOR}},out,size);
}
void ReadCvars() {
    const auto* cv=Open<sc_cvars_v1>(SC_CVARS_NAME);
    for (uint32_t i=0;i<4;++i) {
        g.cvars[i]=-1; uint32_t value=2;
        if (Cap("game.cvars.read") && HAS(cv,sc_cvars_v1,get_bool) && cv->get_bool(g.self,cvar_names[i],SC_CVAR_BOOL,&value)==SCO_OK && value<=1)
            g.cvars[i]=static_cast<int>(value);
    }
}
sco_result Cvars(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    const auto r=Validate(args,n,0,out,size); if (r!=SCO_OK) return r;
    ReadCvars(); std::string message="SQ42 READ-ONLY (writes not approved)"; bool unknown=false;
    for (uint32_t i=0;i<4;++i) { unknown|=g.cvars[i]<0; message+="; "+std::string(cvar_names[i])+"="+(g.cvars[i]<0 ? "unknown/unavailable" : g.cvars[i] ? "on" : "off"); }
    return Answer(unknown ? SCO_UNAVAILABLE : SCO_OK,out,size,message.c_str(),unknown ? "Some Squadron 42 settings can't be read yet." : "Squadron 42 settings can be viewed but not changed.");
}
sco_result Refuse(const sco_arg*,uint32_t,void*,char* out,uint32_t size) {
    if ((!out && size) || !g.api || !g.self) return SCO_BAD_ARG;
    return Answer(SCO_UNAVAILABLE,out,size,"SQ42 cvar writes and arbitrary console execution are NOT approved; read-only, no private fallback/USER.cfg changes.","These settings can't be changed.");
}
sco_result Reload(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    const auto r=Validate(args,n,0,out,size); if (r!=SCO_OK) return r;
    std::string failure;
    const auto read=ReadLists(&failure);
    std::string message=read==SCO_OK ? ListsSummary() :
        failure.empty() ? "Copied lists unavailable/refused; no private-file fallback." : std::move(failure);
    return Answer(read,out,size,message.c_str(),read==SCO_OK ? "Lists refreshed." : "The lists couldn't be loaded.");
}
sco_result Items(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    auto r=Validate(args,n,1,out,size); if (r!=SCO_OK) return r;
    if (!String(args[0],63)) return SCO_BAD_ARG;
    r=Ready(out,size); if (r!=SCO_OK) return r;
    if (!g.lists.items_source.present)
        return Answer(SCO_NOT_FOUND,out,size,"No enabled data pack supplies lists/items.txt; no private-file fallback.","No gear list is installed.");
    std::string message="Copied gear list from "+SourceKey(g.lists.items_source)+" (first 12 matches; use UI filter/pages)"; uint32_t count=0;
    for (const auto& p:g.lists.items) if (p.name.find(args[0].v.s)!=p.name.npos) {
        if (count++==12) break;
        message+="; "+p.port+":"+p.name+ItemSupport(p);
    }
    return Answer(SCO_OK,out,size,message.c_str(),"The matching gear is listed in the console.");
}
const char* ResultName(sco_result result) {
    switch(result) { case SCO_OK:return "SCO_OK"; case SCO_UNAVAILABLE:return "SCO_UNAVAILABLE"; case SCO_NOT_FOUND:return "SCO_NOT_FOUND";
        case SCO_BAD_ARG:return "SCO_BAD_ARG"; case SCO_CRASHED:return "SCO_CRASHED"; case SCO_WRONG_THREAD:return "SCO_WRONG_THREAD";
        case SCO_TOO_MANY:return "SCO_TOO_MANY"; case SCO_FAILED:return "SCO_FAILED"; default:return "unknown result"; }
}
// Names the external command and the result code, so a refusal with no provider reply is identifiable.
std::string ExternalOutcome(const char* name,sco_result result,const char* tail) {
    return std::string("External command ")+name+(result==SCO_OK ? " accepted: " : " refused: ")+ResultName(result)+"; "+tail;
}
void ForwardReply(sco_result result,const char* reply,void* name) {
    if (!g.api) return;
    // spawn writes its replies for the player; build's replies are technical (its own page shows
    // a separate plain sentence), so this page says the outcome in its own words.
    if (reply && *reply && std::strcmp(static_cast<const char*>(name),"build.place")) { Answer(result,nullptr,0,reply,reply); return; }
    if (reply && *reply) {
        Answer(result,nullptr,0,reply,result==SCO_OK ? "Placed the object. It can take a moment to appear."
                                                      : "Couldn't place that object here. Try somewhere else.");
        return;
    }
    Answer(result,nullptr,0,ExternalOutcome(static_cast<const char*>(name),result,"provider supplied no reply; no fallback.").c_str(),
        result==SCO_OK ? "Done." : "That didn't work. Try again.");
}
sco_result Forward(const char* name,const sco_arg* args,uint32_t n,char* out,uint32_t size) {
    if (!HAS(g.api,sco_api,invoke)) return Answer(SCO_UNAVAILABLE,out,size,"External command invocation unavailable.","Couldn't reach that feature.");
    g.status[0]=0;
    const auto r=g.api->invoke(g.self,name,args,n,ForwardReply,const_cast<char*>(name));
    // Public game-thread invocation completes synchronously; preserve its actual reply.
    if (g.status[0]) {
        if (out && size) std::snprintf(out,size,"%s",g.status);
        return r;
    }
    return Answer(r,out,size,ExternalOutcome(name,r,r==SCO_OK ? "provider supplied no reply." : "no host fallback.").c_str(),
        r==SCO_OK ? "Done." : "That didn't work. Try again.");
}
sco_result Spawn(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    const auto r=Validate(args,n,4,out,size); if (r!=SCO_OK) return r;
    if (!g.spoilers) return Answer(SCO_UNAVAILABLE,out,size,"SQ42 spoiler confirmation required.","Confirm the spoiler warning first.");
    if (!String(args[0],63) || !Class(args[0].v.s) || args[1].type!=SCO_ARG_FLOAT || !std::isfinite(args[1].v.f) || args[1].v.f<0 || args[1].v.f>10000 ||
        !String(args[2],63) || args[3].type!=SCO_ARG_BOOL || (args[3].v.i!=0 && args[3].v.i!=1)) return SCO_BAD_ARG;
    return Forward("spawn.ship_options",args,n,out,size);
}
sco_result Build(const sco_arg* args,uint32_t n,void*,char* out,uint32_t size) {
    const auto r=Validate(args,n,2,out,size); if (r!=SCO_OK) return r;
    if (!g.spoilers) return Answer(SCO_UNAVAILABLE,out,size,"SQ42 spoiler confirmation required.","Confirm the spoiler warning first.");
    if (!String(args[0],127) || !*args[0].v.s || args[1].type!=SCO_ARG_FLOAT || !std::isfinite(args[1].v.f) || args[1].v.f<0 || args[1].v.f>100) return SCO_BAD_ARG;
    return Forward("build.place",args,n,out,size);
}
void Publish();
sco_arg Text(const char* text) { sco_arg a{}; a.type=SCO_ARG_STRING; a.v.s=text; return a; }
sco_arg Number(double value) { sco_arg a{}; a.type=SCO_ARG_FLOAT; a.v.f=value; return a; }
sco_arg Boolean(bool value) { sco_arg a{}; a.type=SCO_ARG_BOOL; a.v.i=value ? 1 : 0; return a; }
std::vector<size_t> Filtered() {
    std::vector<size_t> matches;
    for (size_t i=0;i<g.lists.items.size();++i) {
        const auto& p=g.lists.items[i];
        if (!p.slot) continue;   // not a slot this menu can equip, so it is not offered
        if (catalogue::Matches(p.name,g.filter) || catalogue::Contains(p.port,g.filter) || catalogue::Contains(Readable(p.port),g.filter)) matches.push_back(i);
    }
    return matches;
}
bool OutfitMatch(const Outfit& outfit,const OutfitCapabilities& caps) {
    if (!Wearable(outfit,caps)) return false;   // never offer an outfit that can only be refused
    return catalogue::Contains(outfit.name,g.outfit_filter) || catalogue::Contains(Readable(outfit.name),g.outfit_filter);
}
size_t OutfitMatches() {
    size_t count=0; const auto caps=OutfitCaps();
    for (const auto& outfit:g.lists.outfits) if (OutfitMatch(outfit,caps)) ++count;
    return count;
}
// "Helmet: Fixture Helmet": the slot heading and the item, spelled for reading.
std::string ItemLabel(const Piece& p) { return Readable(p.port)+": "+Readable(p.name); }
// The selected classes in g.equip, as the player reads them.
std::string SelectionText() {
    std::string text; std::string_view names=g.equip;
    while (!names.empty()) {
        const auto end=names.find(','); const auto name=names.substr(0,end);
        names=end==names.npos ? std::string_view{} : names.substr(end+1);
        const Piece* match=nullptr;
        for (const auto& p:g.lists.items) if (Same(name,p.name)) { match=&p; break; }
        if (!text.empty()) text+=", ";
        text+=match ? ItemLabel(*match) : Readable(name);
    }
    return text;
}
// Slot indices are catalogue identities, not the order used to draw the groups.
size_t WeaponGroup(size_t slot) { return slot==1 || (slot>=5&&slot<=7) || slot>=11 ? 1u : 0u; }
void ClearAttachments(size_t group) {
    for(size_t i=2;i<std::size(weapon_slots);++i)
        if(WeaponGroup(i)==group)g.curated_picks[i]=0;
}
bool PickerText(const sco_widget_event* e,std::string& out) {
    if(e->kind!=SCO_WIDGET_TEXT_INPUT||!e->text)return false;
    size_t length=0;while(length<=63&&e->text[length])++length;
    if(length>63)return false;
    for(size_t i=0;i<length;++i)if(static_cast<unsigned char>(e->text[i])<32||static_cast<unsigned char>(e->text[i])>126)return false;
    out.assign(e->text,length);return true;
}
void Event(const char* panel,const sco_widget_event* e,void* context) {
    if (!g.api || !panel || !e || e->size<sizeof(*e)) return;
    if (std::strcmp(panel,"loadout.player") && std::strcmp(panel,"loadout.sq42")) return;
    const bool sq=!std::strcmp(panel,"loadout.sq42");
    if(!sq&&(!g.player_events||reinterpret_cast<uintptr_t>(context)!=g.player_cookie))return;
    if(sq&&(!g.sq_events||reinterpret_cast<uintptr_t>(context)!=g.sq_cookie))return;
    const bool current_content=g.lists_known && g.lists.generation==g.content_generation;
    if(!sq && e->node_id>=2 && e->node_id<=4 &&
       (!current_content || g.player_content_generation!=g.lists.generation ||
        !SameSource(g.published_items_source,g.lists.items_source)))return;
    if(sq && (e->node_id==101 || e->node_id==106) &&
       (!current_content || g.sq_content_generation!=g.lists.generation ||
        !SameSource(g.published_outfits_source,g.lists.outfits_source)))return;
    if(sq && (e->node_id==130 || e->node_id==132) &&
       (!current_content || g.sq_content_generation!=g.lists.generation ||
        !SameSource(g.published_buildables_source,g.lists.buildables_source)))return;
    if (sq && !g.spoilers && e->node_id!=100) return;
    if(!sq&&e->node_id>=200&&e->node_id<200+std::size(weapon_slots)) {
        const auto slot=static_cast<size_t>(e->node_id-200);
        if(e->kind==SCO_WIDGET_COMBO&&e->index>=0&&g.published_curated&&static_cast<size_t>(e->index)<g.published_rows[slot].size()) {
            const auto selected=g.published_rows[slot][static_cast<size_t>(e->index)];
            // Unchanged omits only this assignment; selecting a parent for the first
            // time must preserve independently prepared attachments.
            if(slot<2&&selected>0&&g.curated_picks[slot]>0&&selected!=g.curated_picks[slot])ClearAttachments(slot);
            g.curated_picks[slot]=selected;
        }
    }
    else if(!sq&&e->node_id>=240&&e->node_id<=243) {
        const auto group=static_cast<size_t>((e->node_id-240)%2);
        (void)PickerText(e,e->node_id<242?g.weapon_filters[group]:g.attachment_filters[group]);
    }
    else if(!sq&&e->node_id>=244&&e->node_id<=245) {
        const auto group=static_cast<size_t>(e->node_id-244);
        if(e->kind==SCO_WIDGET_COMBO&&e->index>=0&&static_cast<size_t>(e->index)<g.published_types[group].size()) {
            g.weapon_type[group]=g.published_types[group][static_cast<size_t>(e->index)];
            const auto selected=g.curated_picks[group];
            if(g.curated&&selected>0&&static_cast<size_t>(selected)<=g.curated->classes[group].size()&&
               !weapon_types::Matches(g.weapon_type[group],g.curated->classes[group][static_cast<size_t>(selected)-1])) {
                g.curated_picks[group]=0;ClearAttachments(group);
            }
        }
    }
    else if (!sq) switch(e->node_id) {
        case 1:
            if(e->kind==SCO_WIDGET_TEXT_INPUT&&e->text) {
                size_t length=0;while(length<=63&&e->text[length])++length;
                bool valid=length<=63;
                for(size_t i=0;i<length&&valid;++i)valid=static_cast<unsigned char>(e->text[i])>=32&&static_cast<unsigned char>(e->text[i])<=126;
                if(valid){g.filter.assign(e->text,length);g.page=0;}
            }
            break;
        case 2: if (e->kind==SCO_WIDGET_COMBO && e->index>=0 && static_cast<size_t>(e->index)<g.published_items.size()) {
            const auto index=g.published_items[static_cast<size_t>(e->index)];
            if (index<g.lists.items.size()) {
                const auto& p=g.lists.items[index];
                if (g.equip.size()+p.name.size()+1<=1023) { if (!g.equip.empty()) g.equip+=','; g.equip+=p.name; }
            }
        } break;
        case 3: if (e->kind==SCO_WIDGET_BUTTON && g.page) --g.page; break;
        case 4: if (e->kind==SCO_WIDGET_BUTTON && (g.page+1)*64<Filtered().size()) ++g.page; break;
        case 27: if (e->kind==SCO_WIDGET_BUTTON) { auto a=Text(g.equip.c_str()); SubmitGear(&a,1,nullptr,nullptr,0); } break;
        case 7: if (e->kind==SCO_WIDGET_BUTTON) { g.equip.clear(); } break;
        case 8: if (e->kind==SCO_WIDGET_BUTTON) Reload(nullptr,0,nullptr,nullptr,0); break;
        case 23: if (e->kind==SCO_WIDGET_BUTTON) Progress(nullptr,0,nullptr,nullptr,0); break;
        case 24: if (e->kind==SCO_WIDGET_BUTTON) Cancel(nullptr,0,nullptr,nullptr,0); break;
        case 221: if(e->kind==SCO_WIDGET_BUTTON)Curated(true);break;
        case 222: if(e->kind==SCO_WIDGET_BUTTON){g.curated_picks.fill(0);Answer(SCO_OK,nullptr,0,"Curated selection cleared; equipped items and pending request unchanged.","Choices cleared. Your gear is unchanged.");}break;
        case 223: if(e->kind==SCO_WIDGET_BUTTON)(void)ReloadSlots(nullptr,0,nullptr,nullptr,0);break;
        default: break;
    }
    else switch(e->node_id) {
        case 100: if (e->kind==SCO_WIDGET_BUTTON) { g.spoilers=true; ReadCvars(); } break;
        case 101: if (e->kind==SCO_WIDGET_COMBO) {
            if (e->index==-1) { g.outfit=-1; g.outfit_generation=0; }
            else if (e->index>=0 && static_cast<size_t>(e->index)<g.published_outfits.size()) {
                g.outfit=static_cast<int32_t>(g.published_outfits[static_cast<size_t>(e->index)]);
                g.outfit_generation=g.sq_content_generation;
            }
        } break;
        case 135: (void)PickerText(e,g.outfit_filter); g.outfit_page=0; break;
        case 137: if (e->kind==SCO_WIDGET_BUTTON && g.outfit_page) --g.outfit_page; break;
        case 138: if (e->kind==SCO_WIDGET_BUTTON && static_cast<size_t>(g.outfit_page+1)*64<OutfitMatches()) ++g.outfit_page; break;
        case 139: if (e->kind==SCO_WIDGET_BUTTON && g.build_page) --g.build_page; break;
        case 140: if (e->kind==SCO_WIDGET_BUTTON && static_cast<size_t>(g.build_page+1)*64<g.lists.buildables.size()) ++g.build_page; break;
        case 106: if (e->kind==SCO_WIDGET_BUTTON && g.outfit_generation==g.lists.generation && g.outfit>=0 && static_cast<size_t>(g.outfit)<g.lists.outfits.size() && std::find(g.published_outfits.begin(),g.published_outfits.end(),static_cast<size_t>(g.outfit))!=g.published_outfits.end()) {
            auto a=Text(g.lists.outfits[static_cast<size_t>(g.outfit)].name.c_str()); SubmitOutfit(&a,1,nullptr,nullptr,0);
        } break;
        case 107: if (e->kind==SCO_WIDGET_BUTTON) Progress(nullptr,0,nullptr,nullptr,0); break;
        case 108: if (e->kind==SCO_WIDGET_BUTTON) Cancel(nullptr,0,nullptr,nullptr,0); break;
        case 120: if (e->kind==SCO_WIDGET_COMBO) {
            if (e->index==-1) g.ship=-1;
            else if (e->index>=0 && static_cast<size_t>(e->index)<g.published_ships.size()) g.ship=static_cast<int32_t>(g.published_ships[static_cast<size_t>(e->index)]);
        } break;
        case 136: (void)PickerText(e,g.ship_filter); break;
        case 121: if (e->kind==SCO_WIDGET_SLIDER && std::isfinite(e->value) && e->value>=0 && e->value<=500) g.height=e->value; break;
        case 122: if (e->kind==SCO_WIDGET_TOGGLE && (e->value==0 || e->value==1)) g.sit=e->value!=0; break;
        case 123: if (e->kind==SCO_WIDGET_BUTTON && g.ship>=0 && static_cast<size_t>(g.ship)<std::size(ships) && std::find(g.published_ships.begin(),g.published_ships.end(),static_cast<size_t>(g.ship))!=g.published_ships.end()) {
            const auto& ship=ships[g.ship]; const bool sit=ship.board && g.sit;
            sco_arg a[]{Text(ship.cls),Number(ship.board ? g.height : ship.height),Text(sit ? "pilot" : "none"),Boolean(sit)};
            Spawn(a,4,nullptr,nullptr,0);
        } break;
        case 130: if (e->kind==SCO_WIDGET_COMBO && e->index>=0 && static_cast<size_t>(e->index)<g.published_builds.size()) g.build=static_cast<int32_t>(g.published_builds[static_cast<size_t>(e->index)]); break;
        case 131: if (e->kind==SCO_WIDGET_SLIDER && std::isfinite(e->value) && e->value>=0 && e->value<=100) g.ahead=e->value; break;
        case 132: if (e->kind==SCO_WIDGET_BUTTON && g.build>=0 && static_cast<size_t>(g.build)<g.lists.buildables.size() && std::find(g.published_builds.begin(),g.published_builds.end(),static_cast<size_t>(g.build))!=g.published_builds.end()) {
            sco_arg a[]{Text(g.lists.buildables[static_cast<size_t>(g.build)].c_str()),Number(g.ahead)}; Build(a,2,nullptr,nullptr,0);
        } break;
        default: break;
    }
    Publish();
}
bool AsyncAvailable() {
    const auto* api=g.api; auto* owner=g.self; const auto revision=job_revision;
    const auto current=[&] { return g.api==api && g.self==owner && job_revision==revision; };
    const auto* service=Open<sc_loadout_v1_2>(SC_LOADOUT_SERVICE_NAME,SC_LOADOUT_VERSION_1_2);
    if (!current()) return false;
    const bool cap=Cap("game.loadout.async");
    if (!current()) return false;
    return cap && HAS(service,sc_loadout_v1_2,submit) && HAS(service,sc_loadout_v1_2,status) && HAS(service,sc_loadout_v1_2,cancel);
}
// True when a loaded plugin has registered the command. A host that cannot list commands gives no answer,
// so the command is assumed present and any refusal is reported in plain words.
bool CommandPresent(const char* name) {
    if (!HAS(g.api,sco_api,list_commands)) return true;
    std::array<const sco_command*,1024> list{};
    const auto count=g.api->list_commands(list.data(),static_cast<uint32_t>(list.size()));
    for (uint32_t i=0;i<count && i<list.size();++i) if (list[i] && list[i]->name && !std::strcmp(list[i]->name,name)) return true;
    return false;
}
// Headings for the weapon controls, in catalogue slot order.
const char* const weapon_labels[]{"Weapon","Weapon","Optic","Barrel","Underbarrel","Optic","Barrel","Underbarrel",
    "Tool attachment","Tool canister","Tool battery","Tool attachment","Tool canister","Tool battery"};
static_assert(std::size(weapon_labels)==std::size(catalogue::kSlots),"one label per public slot");
void Publish() {
    g.player_events=false; g.sq_events=false;
    const auto* api=g.api; auto* owner=g.self; const auto revision=job_revision;
    const auto content_generation=g.lists.generation;
    const auto items_source=g.lists.items_source;
    const auto outfits_source=g.lists.outfits_source;
    const auto buildables_source=g.lists.buildables_source;
    const bool async_available=AsyncAvailable();
    if (g.api!=api || g.self!=owner || job_revision!=revision) return;
    const auto* w=Open<sco_widgets_v1>(SCO_WIDGETS_NAME);
    if (!HAS(w,sco_widgets_v1,set_panel)) return; // command-first when optional renderer-neutral UI is absent
    std::vector<sco_widget_node> nodes; std::vector<std::string> text;
    text.reserve(256); // node string pointers survive construction, then set_panel copies them
    auto add=[&](uint32_t kind,uint32_t id,const char* label,uint32_t flags=0) -> sco_widget_node& {
        sco_widget_node n{}; n.size=sizeof(n); n.kind=kind; n.id=id; n.label=label; n.flags=flags; nodes.push_back(n); return nodes.back();
    };
    auto under=[&](uint32_t parent,uint32_t kind,uint32_t id,const char* label,uint32_t flags=0) -> sco_widget_node& {
        auto& node=add(kind,id,label,flags); node.parent=parent; return node;
    };
    // Player page, in the order a player uses it: choose gear, choose weapons, then follow the job.
    const auto matches=Filtered(); if (g.page*64>=matches.size()) g.page=0;
    std::vector<const char*> options;
    std::vector<size_t> item_rows;
    for (size_t i=static_cast<size_t>(g.page)*64;i<matches.size() && options.size()<64;++i) {
        const auto row=matches[i];
        text.push_back(ItemLabel(g.lists.items[row]));
        options.push_back(text.back().c_str()); item_rows.push_back(row);
    }
    // Without the game's async loadout route nothing on this page can work, so it shows only why (test card
    // step 10: no buttons that only fail). A job already running keeps its progress and Cancel below.
    const bool can_equip=async_available || JobBusy();
    if (!can_equip) item_rows.clear();   // no gear picker is shown, so no gear row can be chosen
    if (!async_available) add(SCO_WIDGET_LABEL,25,JobBusy() ? "Gear changes here have stopped working. You can still cancel the one in progress."
                                                       : "Changing gear isn't turned on.");
    if (can_equip) {
    add(SCO_WIDGET_COLUMN,300,"Choose gear");
    under(300,SCO_WIDGET_TEXT_INPUT,1,"Search gear").text=g.filter.c_str();
    auto& picker=under(300,SCO_WIDGET_COMBO,2,"Gear"); picker.items=options.empty() ? nullptr : options.data(); picker.item_count=static_cast<uint32_t>(options.size()); picker.index=-1;
    add(SCO_WIDGET_ROW,301,"").parent=300;
    under(301,SCO_WIDGET_BUTTON,3,"Previous page",g.page ? 0u : SCO_WIDGET_DISABLED);
    under(301,SCO_WIDGET_BUTTON,4,"Next page",(g.page+1)*64<matches.size() ? 0u : SCO_WIDGET_DISABLED);
    under(301,SCO_WIDGET_BUTTON,8,"Refresh gear list");
    // Widget text is bounded to 255 bytes; the command takes the full 1023-byte selection.
    text.push_back(g.equip.empty() ? "Selected: nothing yet" : "Selected: "+SelectionText());
    if (text.back().size()>250) text.back()=text.back().substr(0,247)+"...";
    under(300,SCO_WIDGET_LABEL,5,text.back().c_str());
    add(SCO_WIDGET_ROW,302,"").parent=300;
    under(302,SCO_WIDGET_BUTTON,27,"Equip selected gear",JobBusy() || !async_available || g.equip.empty() ? SCO_WIDGET_DISABLED : 0u);
    under(302,SCO_WIDGET_BUTTON,7,"Clear selection",g.equip.empty() ? SCO_WIDGET_DISABLED : 0u);
    }
    std::array<std::vector<const char*>,std::size(catalogue::kSlots)> curated_options;
    std::array<std::vector<std::string>,std::size(catalogue::kSlots)> curated_labels;
    std::array<std::vector<int32_t>,std::size(catalogue::kSlots)> curated_rows;
    bool chosen=false;
    std::array<std::vector<weapon_types::Type>,2> type_rows;
    std::array<std::vector<const char*>,2> type_options;
    if (can_equip) {
    add(SCO_WIDGET_COLUMN,310,"Weapons and tools");
    add(SCO_WIDGET_ROW,260,"").parent=310;   // the two weapon columns side by side, under the section heading
    constexpr size_t order[2][7]{{0,2,3,4,8,9,10},{1,5,6,7,11,12,13}};
    for(size_t group=0;group<2;++group) {
        const auto parent=261u+static_cast<uint32_t>(group);
        add(SCO_WIDGET_COLUMN,parent,group?"Sidearm or secondary tool":"Primary weapon or tool").parent=260;
        auto grouped=[&](uint32_t kind,uint32_t id,const char* label,uint32_t flags=0)->sco_widget_node& {
            auto& node=add(kind,id,label,flags);node.parent=parent;return node;
        };
        auto& types=type_rows[group];auto& labels=type_options[group];types.push_back(weapon_types::All);
        if(g.curated)for(int type=weapon_types::All+1;type<weapon_types::Count;++type) {
            const auto value=static_cast<weapon_types::Type>(type);
            for(const auto& cls:g.curated->classes[group])if(weapon_types::Matches(value,cls)){types.push_back(value);break;}
        }
        int32_t type_index=0;
        for(size_t i=0;i<types.size();++i){labels.push_back(weapon_types::Label(types[i]));if(types[i]==g.weapon_type[group])type_index=static_cast<int32_t>(i);}
        auto& type=grouped(SCO_WIDGET_COMBO,244u+static_cast<uint32_t>(group),"Type");
        type.items=labels.data();type.item_count=static_cast<uint32_t>(labels.size());type.index=type_index;
        grouped(SCO_WIDGET_TEXT_INPUT,240u+static_cast<uint32_t>(group),"Find weapon").text=g.weapon_filters[group].c_str();
        for(const auto i:order[group]) {
            if(i==order[group][1])grouped(SCO_WIDGET_TEXT_INPUT,242u+static_cast<uint32_t>(group),"Find attachment").text=g.attachment_filters[group].c_str();
            auto& choices=curated_options[i];choices.push_back("Keep current");
            auto& rows=curated_rows[i];rows={0};int32_t selected=0;
            if(g.curated) {
                const auto& classes=g.curated->classes[i];
                for(size_t c=0;c<classes.size();++c) {
                    const auto pick=static_cast<int32_t>(c+1);
                    const auto& filter=i<2?g.weapon_filters[group]:g.attachment_filters[group];
                    if(i<2&&!weapon_types::Matches(g.weapon_type[group],classes[c]))continue;
                    if(pick==g.curated_picks[i]||(catalogue::Matches(classes[c],g.filter)&&catalogue::Matches(classes[c],filter)))rows.push_back(pick);
                }
                auto& display=curated_labels[i];display.reserve(rows.size()-1);
                for(size_t row=1;row<rows.size();++row) {
                    display.push_back(catalogue::Display(classes[static_cast<size_t>(rows[row])-1]));
                    if(rows[row]==g.curated_picks[i])selected=static_cast<int32_t>(row);
                }
                for(const auto& label:display)choices.push_back(label.c_str());
            }
            auto& slot=grouped(SCO_WIDGET_COMBO,200+static_cast<uint32_t>(i),weapon_labels[i],choices.size()>1?0u:SCO_WIDGET_DISABLED);
            slot.items=choices.data();slot.item_count=static_cast<uint32_t>(choices.size());slot.index=selected;chosen|=g.curated_picks[i]>0;
        }
    }
    add(SCO_WIDGET_ROW,311,"").parent=310;
    under(311,SCO_WIDGET_BUTTON,221,"Equip chosen weapons",chosen&&!JobBusy()&&async_available?0u:SCO_WIDGET_DISABLED);
    under(311,SCO_WIDGET_BUTTON,222,"Clear choices",chosen?0u:SCO_WIDGET_DISABLED);
    under(311,SCO_WIDGET_BUTTON,223,"Refresh weapon list");
    under(310,SCO_WIDGET_LABEL,224,"Anything left on Keep current stays as it is.");
    under(310,SCO_WIDGET_LABEL,225,g.curated_plain);
    }
    if (g.job || async_available) {
        add(SCO_WIDGET_COLUMN,320,"Progress");
        under(320,SCO_WIDGET_LABEL,26,g.job_status);
        add(SCO_WIDGET_ROW,321,"").parent=320;
        under(321,SCO_WIDGET_BUTTON,23,"Check progress",g.job ? 0u : SCO_WIDGET_DISABLED);
        under(321,SCO_WIDGET_BUTTON,24,g.job_done ? "Clear result" : "Cancel equipping",g.job ? 0u : SCO_WIDGET_DISABLED);
    }
    char status[256]{}; std::snprintf(status,sizeof(status),"%.255s",g.status); if (*status) add(SCO_WIDGET_LABEL,14,status);
    if(next_player_cookie<std::numeric_limits<uintptr_t>::max()) {
        const auto cookie=++next_player_cookie;
        if(w->set_panel(g.self,"loadout.player","Player",nodes.data(),static_cast<uint32_t>(nodes.size()),sizeof(sco_widget_node),Event,reinterpret_cast<void*>(cookie))==SCO_OK) {
            g.published_curated=g.curated;g.published_rows=std::move(curated_rows);g.published_types=std::move(type_rows);
            g.published_items=std::move(item_rows); g.published_items_source=items_source;
            g.player_content_generation=content_generation; g.player_cookie=cookie;g.player_events=true;
        }
    }
    nodes.clear(); options.clear(); text.clear();
    std::vector<size_t> outfit_rows, ship_rows, build_rows;
    const auto publish_sq=[&] {
        if(next_sq_cookie<std::numeric_limits<uintptr_t>::max()) {
            const auto cookie=++next_sq_cookie;
            if(w->set_panel(g.self,"loadout.sq42","Squadron 42",nodes.data(),static_cast<uint32_t>(nodes.size()),sizeof(sco_widget_node),Event,reinterpret_cast<void*>(cookie))==SCO_OK) {
                g.published_outfits=std::move(outfit_rows); g.published_ships=std::move(ship_rows); g.published_builds=std::move(build_rows);
                g.published_outfits_source=outfits_source; g.published_buildables_source=buildables_source;
                g.sq_content_generation=content_generation; g.sq_cookie=cookie; g.sq_events=true;
            }
        }
    };
    if (!g.spoilers) { add(SCO_WIDGET_LABEL,99,"This page may spoil Squadron 42 story details."); add(SCO_WIDGET_BUTTON,100,"Show spoilers"); }
    else {
        // Outfit
        add(SCO_WIDGET_COLUMN,400,"Outfit");
        // Same rule as the Gear page: without the async route nothing in this section can work, so it shows only
        // why. A job already running keeps its progress and Cancel. Ships, Objects and the settings list do not
        // need async and stay.
        const bool can_wear=async_available || JobBusy();
        if (!async_available) under(400,SCO_WIDGET_LABEL,115,JobBusy() ? "Outfit changes here have stopped working. You can still cancel the one in progress."
                                                                  : "Changing outfits isn't turned on.");
        // Outfits that use parts Core has no slot for (clothing, hats, faces) can never be worn, so they are
        // not offered; the page says so in plain words, and shows no picker at all when none can be worn.
        const size_t outfit_total=g.lists.outfits.size();
        const auto outfit_caps=OutfitCaps();
        const size_t outfit_wearable=static_cast<size_t>(std::count_if(g.lists.outfits.begin(),g.lists.outfits.end(),
            [&](const Outfit& o) { return Wearable(o,outfit_caps); }));
        const size_t outfit_hidden=outfit_total-outfit_wearable;
        if (can_wear && outfit_total && !outfit_wearable)
            under(400,SCO_WIDGET_LABEL,116,"None of these outfits can be worn yet. They use parts the game can't change yet.");
        if (can_wear && (!outfit_total || outfit_wearable)) {
        under(400,SCO_WIDGET_TEXT_INPUT,135,"Search outfits").text=g.outfit_filter.c_str();
        int32_t selected_row=-1;
        std::vector<size_t> outfit_matches;   // paged like the Player gear list: at most 64 rows per combo
        for (size_t i=0;i<g.lists.outfits.size();++i) if (OutfitMatch(g.lists.outfits[i],outfit_caps)) outfit_matches.push_back(i);
        if (static_cast<size_t>(g.outfit_page)*64>=outfit_matches.size()) g.outfit_page=0;
        for (size_t i=static_cast<size_t>(g.outfit_page)*64;i<outfit_matches.size() && outfit_rows.size()<64;++i) {
            if (g.outfit>=0 && static_cast<size_t>(g.outfit)==outfit_matches[i]) selected_row=static_cast<int32_t>(outfit_rows.size());
            outfit_rows.push_back(outfit_matches[i]); text.push_back(Readable(g.lists.outfits[outfit_matches[i]].name)); options.push_back(text.back().c_str());
        }
        auto& outfit=under(400,SCO_WIDGET_COMBO,101,"Outfit"); outfit.items=options.empty() ? nullptr : options.data(); outfit.item_count=static_cast<uint32_t>(options.size()); outfit.index=selected_row;
        add(SCO_WIDGET_ROW,401,"").parent=400;
        under(401,SCO_WIDGET_BUTTON,137,"Previous page",g.outfit_page ? 0u : SCO_WIDGET_DISABLED);
        under(401,SCO_WIDGET_BUTTON,138,"Next page",static_cast<size_t>(g.outfit_page+1)*64<outfit_matches.size() ? 0u : SCO_WIDGET_DISABLED);
        under(400,SCO_WIDGET_BUTTON,106,"Wear selected outfit",selected_row>=0 && !JobBusy() && async_available ? 0u : SCO_WIDGET_DISABLED);
        if (outfit_hidden) {
            text.push_back(std::to_string(outfit_hidden)+(outfit_hidden==1 ? " outfit isn't shown because it uses" : " outfits aren't shown because they use")+" parts the game can't change yet.");
            under(400,SCO_WIDGET_LABEL,117,text.back().c_str());
        }
        }
        if (g.job || async_available) {
            under(400,SCO_WIDGET_LABEL,109,g.job_status);
            add(SCO_WIDGET_ROW,402,"").parent=400;
            under(402,SCO_WIDGET_BUTTON,107,"Check progress",g.job ? 0u : SCO_WIDGET_DISABLED);
            under(402,SCO_WIDGET_BUTTON,108,g.job_done ? "Clear result" : "Cancel equipping",g.job ? 0u : SCO_WIDGET_DISABLED);
        }
        // Game settings: they can be read when the game allows it, never changed from here.
        add(SCO_WIDGET_COLUMN,410,"Game settings");
        bool any_setting=false;
        for (uint32_t i=0;i<4;++i) any_setting|=g.cvars[i]>=0;
        if (!any_setting) under(410,SCO_WIDGET_LABEL,114,"These settings can't be read or changed yet.");
        else {
            under(410,SCO_WIDGET_LABEL,114,"These settings can be seen here but not changed.");
            for (uint32_t i=0;i<4;++i) if (g.cvars[i]>=0) {
                text.push_back(std::string(cvar_labels[i])+": "+(g.cvars[i] ? "on" : "off"));
                under(410,SCO_WIDGET_LABEL,110+i,text.back().c_str());
            }
        }
        // Ships: spawning is another feature's command.
        add(SCO_WIDGET_COLUMN,420,"Ships");
        int32_t ship_row=-1;
        std::vector<const char*> ship_labels, builds;   // node arrays must outlive the set_panel call below
        if (!CommandPresent("spawn.ship_options")) under(420,SCO_WIDGET_LABEL,124,"Spawning ships needs the Ships & vehicles feature, which is missing.");
        else {
            under(420,SCO_WIDGET_TEXT_INPUT,136,"Search ships").text=g.ship_filter.c_str();
            for (size_t i=0;i<std::size(ships);++i) if (catalogue::Contains(ships[i].label,g.ship_filter)) {
                if (g.ship>=0 && static_cast<size_t>(g.ship)==i) ship_row=static_cast<int32_t>(ship_rows.size());
                ship_rows.push_back(i); ship_labels.push_back(ships[i].label);
            }
            auto& ship=under(420,SCO_WIDGET_COMBO,120,"Ship"); ship.items=ship_labels.empty() ? nullptr : ship_labels.data(); ship.item_count=static_cast<uint32_t>(ship_labels.size()); ship.index=ship_row;
            auto& height=under(420,SCO_WIDGET_SLIDER,121,"Height above me (m)"); height.value=g.height; height.minimum=0; height.maximum=500;
            under(420,SCO_WIDGET_TOGGLE,122,"Start in the pilot seat, ready to fly").value=g.sit ? 1 : 0;
            under(420,SCO_WIDGET_BUTTON,123,"Spawn ship",ship_row>=0 ? 0u : SCO_WIDGET_DISABLED);
        }
        // Objects: placing is another feature's command.
        add(SCO_WIDGET_COLUMN,430,"Objects");
        if (!CommandPresent("build.place")) under(430,SCO_WIDGET_LABEL,141,"Placing objects needs the Build feature, which is missing.");
        else {
            int32_t build_row=-1;   // at most 64 rows per page
            if (static_cast<size_t>(g.build_page)*64>=g.lists.buildables.size()) g.build_page=0;
            for (size_t i=static_cast<size_t>(g.build_page)*64;i<g.lists.buildables.size() && builds.size()<64;++i) {
                if (g.build>=0 && static_cast<size_t>(g.build)==i) build_row=static_cast<int32_t>(builds.size());
                build_rows.push_back(i); text.push_back(Readable(g.lists.buildables[i])); builds.push_back(text.back().c_str());
            }
            auto& build=under(430,SCO_WIDGET_COMBO,130,"Object"); build.items=builds.empty() ? nullptr : builds.data(); build.item_count=static_cast<uint32_t>(builds.size()); build.index=build_row;
            add(SCO_WIDGET_ROW,431,"").parent=430;
            under(431,SCO_WIDGET_BUTTON,139,"Previous page",g.build_page ? 0u : SCO_WIDGET_DISABLED);
            under(431,SCO_WIDGET_BUTTON,140,"Next page",static_cast<size_t>(g.build_page+1)*64<g.lists.buildables.size() ? 0u : SCO_WIDGET_DISABLED);
            auto& ahead=under(430,SCO_WIDGET_SLIDER,131,"Distance ahead (m, 0 = at my feet)"); ahead.value=g.ahead; ahead.minimum=0; ahead.maximum=100;
            under(430,SCO_WIDGET_BUTTON,132,"Place object",build_row>=0 ? 0u : SCO_WIDGET_DISABLED);
        }
        char sq_status[256]{}; std::snprintf(sq_status,sizeof(sq_status),"%.255s",g.status); if (*sq_status) add(SCO_WIDGET_LABEL,134,sq_status);
        publish_sq(); return;
    }
    publish_sq();
}
void Tick(const char*,const void* data,void*) {
    if (!g.api || !data || job_observing) return;
    uint32_t now=0; std::memcpy(&now,data,sizeof(now));
    if (g.clocked && static_cast<uint32_t>(now-g.last_read)<200) return;
    g.clocked=true; g.last_read=now;
    if (g.spoilers) ReadCvars();
    if (g.job && !g.job_poll_paused) {
        const auto revision=job_revision;
        try {
            std::string message,plain; bool malformed=false,obsolete=false; (void)ObserveJob(message,plain,malformed,obsolete);
            if (obsolete) return;
            if (malformed) g.job_poll_paused=true;
            JobLabel(plain);
        } catch (...) {
            if (revision!=job_revision) return;
            g.job_poll_paused=true;
            std::snprintf(g.job_status,sizeof(g.job_status),"Couldn't read progress. Cancel and try again.");
        }
    }
    Publish();
}
sco_result Command(const char* name,sco_command_fn fn,const sco_arg_def* args,uint32_t count) {
    sco_command c{}; c.size=sizeof(c); c.name=name; c.title=name; c.help=name;
    c.args=args; c.nargs=count; c.arg_def_size=sizeof(sco_arg_def); c.fn=fn;
    // No capability gate: commands provide precise missing-service/refusal replies themselves.
    return g.api->register_command(g.self,&c);
}
sco_arg_def Arg(const char* name,sco_arg_type type) { sco_arg_def a{}; a.name=name; a.type=type; return a; }
}
const sco_plugin_info* Query() { return &info; }
sco_result Load(const sco_api* api,sco_plugin* self) {
    if (g.api || !self || !HAS(api,sco_api,register_command) || !HAS(api,sco_api,query_service) || !HAS(api,sco_api,subscribe) ||
        !HAS(api,sco_api,unsubscribe) || !HAS(api,sco_api,has)) return SCO_BAD_ARG;
    ++job_revision; g=State{}; g.api=api; g.self=self;
    const auto items=Arg("items",SCO_ARG_STRING),outfit=Arg("outfit",SCO_ARG_STRING),spoilers=Arg("confirmed",SCO_ARG_BOOL),visor=Arg("class",SCO_ARG_STRING),filter=Arg("filter",SCO_ARG_STRING);
    const sco_arg_def spawn[]{Arg("class",SCO_ARG_STRING),Arg("height",SCO_ARG_FLOAT),Arg("seat",SCO_ARG_STRING),Arg("power",SCO_ARG_BOOL)};
    const sco_arg_def build[]{Arg("object",SCO_ARG_STRING),Arg("ahead",SCO_ARG_FLOAT)};
    const sco_arg_def write[]{Arg("name",SCO_ARG_STRING),Arg("on",SCO_ARG_BOOL)};
    struct Registration { const char* name; sco_command_fn fn; const sco_arg_def* args; uint32_t count; };
    const Registration commands[]{ {"loadout.equip",Equip,&items,1},{"loadout.wear",Wear,&outfit,1},{"loadout.submit_gear",SubmitGear,&items,1},{"loadout.submit_outfit",SubmitOutfit,&outfit,1},{"loadout.spoilers",Spoilers,&spoilers,1},
        {"loadout.slots",Slots,&items,1},{"loadout.submit",Submit,&items,1},{"loadout.progress",Progress,nullptr,0},{"loadout.cancel",Cancel,nullptr,0},
        {"loadout.visor",Visor,&visor,1},{"loadout.items",Items,&filter,1},{"loadout.reload_lists",Reload,nullptr,0},
        {"loadout.reload_slots",ReloadSlots,nullptr,0},
        {"loadout.cvars",Cvars,nullptr,0},{"loadout.cvar_set",Refuse,write,2},{"loadout.console",Refuse,&items,1},
        {"loadout.spawn",Spawn,spawn,4},{"loadout.build",Build,build,2} };
    for (const auto& c:commands) { const auto r=Command(c.name,c.fn,c.args,c.count); if (r!=SCO_OK) { g=State{}; return r; } }
    const auto r=api->subscribe(self,"tick",Tick,nullptr); if (r!=SCO_OK) { g=State{}; return r; }
    std::string list_status;
    const auto lists=ReadLists(&list_status);
    (void)ReadSlots();
    const auto summary=lists==SCO_OK ? ListsSummary() : list_status;
    Answer(lists==SCO_OK ? SCO_OK : SCO_UNAVAILABLE,nullptr,0,lists==SCO_OK ? summary.c_str() :
        list_status.empty() ? "Loadout copied lists unavailable; no private fallback. Reload the lists after installing packs and restarting." : list_status.c_str(),
        lists==SCO_OK ? "" : "The gear lists couldn't be loaded.");
    Publish(); return SCO_OK;
}
void Unload() {
    if (!g.api) return;
    if (g.job) (void)Cancel(nullptr,0,nullptr,nullptr,0);
    const auto* w=Open<sco_widgets_v1>(SCO_WIDGETS_NAME);
    if (HAS(w,sco_widgets_v1,remove_panel)) { (void)w->remove_panel(g.self,"loadout.player"); (void)w->remove_panel(g.self,"loadout.sq42"); }
    if (HAS(g.api,sco_api,unsubscribe)) (void)g.api->unsubscribe(g.self,"tick",Tick);
    // Equip is player state, not entity ownership; ordinary unload does not undo equipped gear.
    ++job_revision; g=State{};
}
}
