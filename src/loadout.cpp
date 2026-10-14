#include "loadout.h"
#include "teleport.h"
#include "menu.h"
#include "spawner.h"
#include "sco/game/actors.h"
#include "sco/signatures.h"
#include <share.h>
#include <string>

static struct {
    bool       ok = false;
    uintptr_t* game = nullptr;
    int32_t    frameworkSlot = 0;
    int32_t    actorSlot = 0;
    int32_t    loadSlot = 0;
} g_lo;

namespace actors = sco::game::actors;

// The player loadout loader is sco-core's loadout.* rows (sco/game/actors.h); its slots are read
// from the instructions that row checks.
bool ResolveLoadoutApi(const Section&, const Section&) {
    if (!ActorsCapability("loadout.loader", "gear", "gear menu and outfits")) return false;
    const uint8_t* h = sco::Sig("loadout.load_player_loadout");
    g_lo.game          = reinterpret_cast<uintptr_t*>(sco::Sig("loadout.game"));
    g_lo.frameworkSlot = Rel32(h + actors::kLoadoutFrameworkSlot);
    g_lo.actorSlot     = Rel32(h + actors::kLoadoutActorSlot);
    g_lo.loadSlot      = Rel32(h + actors::kLoadoutLoadSlot);
    g_lo.ok = true;
    return true;
}

static const char* const kSlotNames[Gear_SlotCount] = {
    "undersuit", "helmet", "torso", "arms", "legs", "backpack", "primary", "sidearm", "ammo", "grenade" };
static const char* const kAttachmentSlotNames[Attachment_SlotCount] = { "optic", "barrel", "underbarrel" };
static const char* const kToolModuleSlotName = "toolmodule";
static const char* const kToolCanisterSlotName = "toolcanister";
static const char* const kToolBatterySlotName = "toolbattery";

constexpr int        kMaxGear = 3000;
static char          g_gear[kMaxGear][64];
static int           g_gearStart[Gear_SlotCount];
static int           g_gearCount[Gear_SlotCount];
static char          g_attachments[kMaxGear][64];
static int           g_attachmentStart[Attachment_SlotCount];
static int           g_attachmentCount[Attachment_SlotCount];
static char          g_toolModules[kMaxGear][64];
static int           g_toolModuleCount = 0;
static char          g_toolCanisters[kMaxGear][64];
static int           g_toolCanisterCount = 0;
static char          g_toolBatteries[kMaxGear][64];
static int           g_toolBatteryCount = 0;
static volatile LONG g_gearState = 0;
static SRWLOCK       g_gearLock = SRWLOCK_INIT;
static struct { bool pending; int picks[Gear_SlotCount]; MenuWeaponAttachments attachments; } g_equipRequest;

int Menu_GearCount(int slot) {
    if (g_gearState != 2) { InterlockedCompareExchange(&g_gearState, 1, 0); return -1; }
    return slot >= 0 && slot < Gear_SlotCount ? g_gearCount[slot] : 0;
}

const char* Menu_GearName(int slot, int index) {
    if (g_gearState != 2 || slot < 0 || slot >= Gear_SlotCount || index < 0 || index >= g_gearCount[slot]) return "";
    return g_gear[g_gearStart[slot] + index];
}

int Menu_AttachmentCount(int slot) {
    if (g_gearState != 2) { InterlockedCompareExchange(&g_gearState, 1, 0); return -1; }
    return slot >= 0 && slot < Attachment_SlotCount ? g_attachmentCount[slot] : 0;
}

const char* Menu_AttachmentName(int slot, int index) {
    if (g_gearState != 2 || slot < 0 || slot >= Attachment_SlotCount || index < 0 || index >= g_attachmentCount[slot]) return "";
    return g_attachments[g_attachmentStart[slot] + index];
}

int Menu_ToolModuleCount() {
    if (g_gearState != 2) { InterlockedCompareExchange(&g_gearState, 1, 0); return -1; }
    return g_toolModuleCount;
}

const char* Menu_ToolModuleName(int index) {
    return g_gearState == 2 && index >= 0 && index < g_toolModuleCount ? g_toolModules[index] : "";
}

int Menu_ToolCanisterCount() {
    if (g_gearState != 2) { InterlockedCompareExchange(&g_gearState, 1, 0); return -1; }
    return g_toolCanisterCount;
}

const char* Menu_ToolCanisterName(int index) {
    return g_gearState == 2 && index >= 0 && index < g_toolCanisterCount ? g_toolCanisters[index] : "";
}

void Menu_RequestEquip(const int picks[Gear_SlotCount], const MenuWeaponAttachments* attachments) {
    AcquireSRWLockExclusive(&g_gearLock);
    g_equipRequest.pending = true;
    memcpy(g_equipRequest.picks, picks, sizeof(g_equipRequest.picks));
    if (attachments) g_equipRequest.attachments = *attachments;
    else g_equipRequest.attachments = {};
    ReleaseSRWLockExclusive(&g_gearLock);
}

static int BuildGearLists() {
    char path[MAX_PATH];
    if (!DataFilePath(path, sizeof(path), "items.txt")) return 0;
    FILE* f = _fsopen(path, "r", _SH_DENYNO);
    if (!f) { Log("[gear] can't open %s", path); return 0; }
    const uintptr_t registry = VCall<uintptr_t>(*g_tp.entitySystem, actors::kEsClassRegistry);
    int slot = -1, attachmentSlot = -1, listSlot = -1, total = 0, attachmentTotal = 0, unknown = 0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n#")] = 0;
        char* name = line + strspn(line, " \t");
        if (!*name) continue;
        if (*name == '[') {
            slot = -1;
            attachmentSlot = -1;
            listSlot = -1;
            for (int s = 0; s < Gear_SlotCount; ++s)
                if (_strnicmp(name + 1, kSlotNames[s], strlen(kSlotNames[s])) == 0 && name[1 + strlen(kSlotNames[s])] == ']') slot = s;
            for (int s = 0; s < Attachment_SlotCount; ++s)
                if (_strnicmp(name + 1, kAttachmentSlotNames[s], strlen(kAttachmentSlotNames[s])) == 0 && name[1 + strlen(kAttachmentSlotNames[s])] == ']') attachmentSlot = s;
            if (_strnicmp(name + 1, kToolModuleSlotName, strlen(kToolModuleSlotName)) == 0
                && name[1 + strlen(kToolModuleSlotName)] == ']') listSlot = 1;
            if (_strnicmp(name + 1, kToolCanisterSlotName, strlen(kToolCanisterSlotName)) == 0
                && name[1 + strlen(kToolCanisterSlotName)] == ']') listSlot = 2;
            if (_strnicmp(name + 1, kToolBatterySlotName, strlen(kToolBatterySlotName)) == 0
                && name[1 + strlen(kToolBatterySlotName)] == ']') listSlot = 3;
            if (slot >= 0) { g_gearStart[slot] = total; g_gearCount[slot] = 0; }
            if (attachmentSlot >= 0) { g_attachmentStart[attachmentSlot] = attachmentTotal; g_attachmentCount[attachmentSlot] = 0; }
            if (listSlot == 1) g_toolModuleCount = 0;
            if (listSlot == 2) g_toolCanisterCount = 0;
            if (listSlot == 3) g_toolBatteryCount = 0;
            continue;
        }
        if (slot >= 0 && total < kMaxGear) {
            if (!VCall<uintptr_t>(registry, actors::kRegistryFindClass, static_cast<const char*>(name))) { ++unknown; continue; }
            strncpy_s(g_gear[total++], name, _TRUNCATE);
            ++g_gearCount[slot];
        } else if (attachmentSlot >= 0 && attachmentTotal < kMaxGear) {
            if (!VCall<uintptr_t>(registry, actors::kRegistryFindClass, static_cast<const char*>(name))) { ++unknown; continue; }
            strncpy_s(g_attachments[attachmentTotal++], name, _TRUNCATE);
            ++g_attachmentCount[attachmentSlot];
        } else if (listSlot == 1 && g_toolModuleCount < kMaxGear) {
            if (!VCall<uintptr_t>(registry, actors::kRegistryFindClass, static_cast<const char*>(name))) { ++unknown; continue; }
            strncpy_s(g_toolModules[g_toolModuleCount++], name, _TRUNCATE);
        } else if (listSlot == 2 && g_toolCanisterCount < kMaxGear) {
            if (!VCall<uintptr_t>(registry, actors::kRegistryFindClass, static_cast<const char*>(name))) { ++unknown; continue; }
            strncpy_s(g_toolCanisters[g_toolCanisterCount++], name, _TRUNCATE);
        } else if (listSlot == 3 && g_toolBatteryCount < kMaxGear) {
            if (!VCall<uintptr_t>(registry, actors::kRegistryFindClass, static_cast<const char*>(name))) { ++unknown; continue; }
            strncpy_s(g_toolBatteries[g_toolBatteryCount++], name, _TRUNCATE);
        }
    }
    fclose(f);
    Log("[gear] %d items, %d firearm attachments, %d multitool modules, %d multitool canisters and %d multitool batteries (%d unknown names skipped)",
        total, attachmentTotal, g_toolModuleCount, g_toolCanisterCount, g_toolBatteryCount, unknown);
    return total;
}

static const char* Pick(const int picks[Gear_SlotCount], int slot) {
    const int i = picks[slot];
    return i >= 0 && i < g_gearCount[slot] ? g_gear[g_gearStart[slot] + i] : nullptr;
}

static const char* MatchingMag(const char* gun);

static const char* PickAttachment(const MenuWeaponAttachments& attachments, bool primary, int slot) {
    const int index = primary ? attachments.primary[slot] : attachments.sidearm[slot];
    return index >= 0 && index < g_attachmentCount[slot] ? g_attachments[g_attachmentStart[slot] + index] : nullptr;
}

static const char* PickToolModule(const MenuWeaponAttachments& attachments, bool primary) {
    const int index = primary ? attachments.primaryToolModule : attachments.sidearmToolModule;
    return index >= 0 && index < g_toolModuleCount ? g_toolModules[index] : nullptr;
}

static const char* PickToolCanister(const MenuWeaponAttachments& attachments, bool primary) {
    const int index = primary ? attachments.primaryToolCanister : attachments.sidearmToolCanister;
    return index >= 0 && index < g_toolCanisterCount ? g_toolCanisters[index] : nullptr;
}

static bool IsSrtModule(const char* module) {
    return module && _stricmp(module, "grin_multitool_01_salvage_repair") == 0;
}

static bool IsMultitool(const char* item) {
    static const char* const prefix = "grin_multitool_01";
    const size_t length = strlen(prefix);
    return item && _strnicmp(item, prefix, length) == 0 && (item[length] == '\0' || item[length] == '_');
}

static std::string Gun(const char* port, const char* gun, const MenuWeaponAttachments& attachments, bool primary) {
    std::string children;
    const char* mag = IsMultitool(gun) ? nullptr : MatchingMag(gun);
    if (mag) children += LoadoutItem("magazine_attach", mag);

    if (IsMultitool(gun)) {
        const char* module = PickToolModule(attachments, primary);
        const char* canister = PickToolCanister(attachments, primary);
        const bool defaultSrtTool = _stricmp(gun, "grin_multitool_01_default_salvage_repair") == 0;
        const bool replaceDefaultSrt = defaultSrtTool && ((module && !IsSrtModule(module)) || canister);
        const bool preserveDefaultSrt = defaultSrtTool && !replaceDefaultSrt;
        if (replaceDefaultSrt && !module) module = "grin_multitool_01_salvage_repair";
        const bool srtActive = IsSrtModule(module) || (!module && preserveDefaultSrt);
        const char* equippedTool = replaceDefaultSrt ? "grin_multitool_01" : gun;
        if (!preserveDefaultSrt && g_toolBatteryCount > 0)
            children += LoadoutItem("magazine_attach", g_toolBatteries[0]);
        if (!preserveDefaultSrt && module)
            children += LoadoutItem("module_attach", module, std::string(), "WeaponAttachment(Utility)");
        if (srtActive && canister)
            children += LoadoutItem("canister_attach", canister, std::string(), "WeaponAttachment(Magazine)");
        return LoadoutItem(port, equippedTool, children);
    }

    static const char* const ports[Attachment_SlotCount] = { "optics_attach", "barrel_attach", "underbarrel_attach" };
    static const char* const tags[Attachment_SlotCount] = {
        "WeaponAttachment(IronSight)", "WeaponAttachment(Barrel)", "WeaponAttachment(BottomAttachment)" };
    for (int slot = 0; slot < Attachment_SlotCount; ++slot) {
        if (const char* attachment = PickAttachment(attachments, primary, slot))
            children += std::string("<Item portName=\"") + ports[slot] + "\" itemName=\"" + attachment
                + "\" tag=\"" + tags[slot] + "\"/>";
    }
    return LoadoutItem(port, gun, children);
}

static const char* MatchingMag(const char* gun) {
    if (!gun) return nullptr;
    char base[64];
    strncpy_s(base, gun, _TRUNCATE);
    for (;;) {
        char want[72];
        snprintf(want, sizeof(want), "%s_mag", base);
        for (int i = 0; i < g_gearCount[Gear_Ammo]; ++i)
            if (_stricmp(g_gear[g_gearStart[Gear_Ammo] + i], want) == 0) return g_gear[g_gearStart[Gear_Ammo] + i];
        char* cut = strrchr(base, '_');
        if (!cut) return nullptr;
        *cut = 0;
    }
}

bool LoadoutApiReady() { return g_lo.ok; }

static volatile LONG g_s42VisorHud = 0;
void Menu_SetS42VisorHud(bool on) { InterlockedExchange(&g_s42VisorHud, on ? 1 : 0); }

std::string LoadoutItem(const char* port, const char* name, const std::string& children, const char* tag) {
    std::string s = std::string("<Item portName=\"") + port + "\" itemName=\"" + name + "\"";
    if (tag && *tag) s += std::string(" tag=\"") + tag + "\"";
    return children.empty() ? s + "/>" : s + "><Items>" + children + "</Items></Item>";
}

// The player's head, as the original builds it (orig 1800265d0). A head item is always
// emitted: with no Head_ItemPort the player gets the default PU face, otherwise the named
// head gets PU_Head_Eyes / PU_Head_Teeth when the outfit names none. The lens always sits
// under the eyes; Default_LensDisplay is the SQ42 visor HUD the Settings checkbox picks.
// Accessories (hat, eye/head accessories, horns, jewellery) go under the head in both cases.
std::string HeadItem(const HeadParts& p) {
    const char* lens = InterlockedCompareExchange(&g_s42VisorHud, 0, 0) != 0
                     ? "Default_LensDisplay" : "Default_LensDisplay_PU";
    if (!p.head)
        return LoadoutItem("Head_ItemPort", "PU_Protos_Head",
                   LoadoutItem("Eyes_ItemPort", "Head_Eyes_Blue_01", LoadoutItem("Lens_ItemPort", lens))
                   + "<Item portName=\"Teeth_ItemPort\" itemName=\"Head_Teeth\" tag=\"Char_Accessory_Head\"/>"
                   + "<Item portName=\"Hair_ItemPort\" itemName=\"hair_37\" tag=\"Char_Head_Hair Male\"><Items>"
                   + LoadoutItem("Material_Variant", "Hair_Var_Brown") + "</Items></Item>"
                   + p.accessories);
    std::string children = LoadoutItem("Eyes_ItemPort", p.eyes ? p.eyes : "PU_Head_Eyes", LoadoutItem("Lens_ItemPort", lens))
                         + LoadoutItem("Teeth_ItemPort", p.teeth ? p.teeth : "PU_Head_Teeth");
    if (p.eyebrow) children += LoadoutItem("Eyebrow_ItemPort", p.eyebrow);
    if (p.hair)    children += LoadoutItem("Hair_ItemPort", p.hair, p.hairColor ? LoadoutItem("Material_Variant", p.hairColor) : std::string());
    return LoadoutItem("Head_ItemPort", p.head, children + p.accessories);
}

// MobiGlas sits under the body in both the gear menu and outfits.
std::string MobiGlasItem() {
    return LoadoutItem("mobiglas_attach", "MobiGlas",
               "<Item portName=\"mobiglas_screen_attach\" itemName=\"PersonalMobiGlas_PU\" tag=\"MobiGlas\"/>"
               "<Item portName=\"legacy_mobiglas_screen_attach\" itemName=\"LegacyMobiGlas\" tag=\"MobiGlas\"/>");
}

static std::string LoadoutXml(const int picks[Gear_SlotCount], const MenuWeaponAttachments& attachments) {
    const char* torso   = Pick(picks, Gear_Torso);
    const char* legs    = Pick(picks, Gear_Legs);
    const char* primary = Pick(picks, Gear_Primary);
    const char* sidearm = Pick(picks, Gear_Sidearm);
    const char* ammo    = Pick(picks, Gear_Ammo);
    if (!ammo) ammo = MatchingMag(primary);
    std::string onTorso, onLegs;
    if (const char* b = Pick(picks, Gear_Backpack)) onTorso += LoadoutItem("backpack", b);
    if (primary) {
        if (IsMultitool(primary)) onLegs += Gun("utility_attach_1", primary, attachments, true);
        else onTorso += Gun("wep_stocked_3", primary, attachments, true);
    }
    for (int i = 1; ammo && i <= (torso ? 4 : 2); ++i) onTorso += LoadoutItem(("magazine_attach_" + std::to_string(i)).c_str(), ammo);
    if (const char* g = Pick(picks, Gear_Grenade); g && torso)
        for (int i = 1; i <= 2; ++i) onTorso += LoadoutItem(("grenade_attach_" + std::to_string(i)).c_str(), g);
    if (sidearm) onLegs += Gun(IsMultitool(sidearm) ? "utility_attach_1" : "wep_sidearm", sidearm, attachments, false);
    onLegs += LoadoutItem("medPen_attach_1", "crlf_consumable_healing_01");

    std::string onSuit;
    if (const char* h = Pick(picks, Gear_Helmet)) onSuit += LoadoutItem("Armor_Helmet", h);
    if (torso) onSuit += LoadoutItem("Armor_Torso", torso, onTorso); else onSuit += onTorso;
    if (const char* a = Pick(picks, Gear_Arms)) onSuit += LoadoutItem("Armor_Arms", a);
    if (legs) onSuit += LoadoutItem("Armor_Legs", legs, onLegs); else onSuit += onLegs;

    const char* suit = Pick(picks, Gear_Undersuit);
    return "<Loadout><Items>" + LoadoutItem("Body_ItemPort", "body_01",
        LoadoutItem("Armor_Undersuit", suit ? suit : "rsi_odyssey_undersuit_01_01_01", onSuit)
        + HeadItem(HeadParts{}) + MobiGlasItem())
        + "</Items></Loadout>\n";
}

static const char* LoadLoadout(const char* gamePath) {
    __try {
        const uintptr_t framework = VCall<uintptr_t>(*g_lo.game, g_lo.frameworkSlot);
        const uintptr_t actor = framework ? VCall<uintptr_t>(framework, g_lo.actorSlot) : 0;
        if (!actor) return "you're not spawned yet";
        VCall<void>(actor, g_lo.loadSlot, gamePath, 8u, true);
        return nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "fault while loading the loadout";
    }
}

// Game thread only (ProcessLoadout / ProcessOutfits): writes the XML into SC_USER and has the
// player's actor load it. One counter and one "last file" for both callers, so the gear menu
// and the outfit menu can't hand out the same file name or delete each other's current file.
void EquipLoadoutXml(const std::string& xml, const char* tag) {
    static int  counter = 0;
    static char lastFile[MAX_PATH] = "";
    char dir[MAX_PATH];
    const DWORD n = GetEnvironmentVariableA("SC_USER", dir, sizeof(dir));
    if (n == 0 || n >= sizeof(dir)) { Log("[%s] SC_USER isn't set (start the game with sc-offline.exe)", tag); return; }
    char name[64], file[MAX_PATH], gamePath[96];
    snprintf(name, sizeof(name), "offline_loadout_%lu_%d.xml", GetCurrentProcessId(), ++counter);
    snprintf(file, sizeof(file), "%s\\%s", dir, name);
    snprintf(gamePath, sizeof(gamePath), "%%USER%%/%s", name);
    FILE* f = _fsopen(file, "w", _SH_DENYNO);
    if (!f) { Log("[%s] can't write %s", tag, file); return; }
    fwrite(xml.data(), 1, xml.size(), f);
    fclose(f);
    if (const char* err = LoadLoadout(gamePath)) { Log("[%s] equip failed: %s", tag, err); return; }
    Log("[%s] equipped (%s)", tag, name);
    if (lastFile[0]) DeleteFileA(lastFile);
    strcpy_s(lastFile, file);
}

static void EquipPicks(const int picks[Gear_SlotCount], const MenuWeaponAttachments& attachments) {
    EquipLoadoutXml(LoadoutXml(picks, attachments), "gear");
}

void ProcessLoadout() {
    if (!g_lo.ok || !g_tp.ok) return;
    if (g_gearState == 1) {
        uintptr_t actor, entity;
        bool live = false;
        __try { live = GetLocalPlayer(actor, entity); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        if (live) {
            __try { BuildGearLists(); } __except (EXCEPTION_EXECUTE_HANDLER) { Log("[gear] fault while reading items.txt"); }
            InterlockedExchange(&g_gearState, 2);
        }
    }
    AcquireSRWLockExclusive(&g_gearLock);
    const auto req = g_equipRequest;
    g_equipRequest.pending = false;
    ReleaseSRWLockExclusive(&g_gearLock);
    if (req.pending && g_gearState == 2) {
        MenuWeaponAttachments attachments = req.attachments;
        if (req.picks[Gear_Primary] < 0)
            for (int& pick : attachments.primary) pick = -1;
        if (req.picks[Gear_Sidearm] < 0)
            for (int& pick : attachments.sidearm) pick = -1;
        if (!IsMultitool(Pick(req.picks, Gear_Primary))) {
            attachments.primaryToolModule = -1;
            attachments.primaryToolCanister = -1;
        }
        if (!IsMultitool(Pick(req.picks, Gear_Sidearm))) {
            attachments.sidearmToolModule = -1;
            attachments.sidearmToolCanister = -1;
        }
        EquipPicks(req.picks, attachments);
    }
}
