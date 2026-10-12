// The loadout built-in's menu tabs: Player (noclip, god mode, infinite ammo and the gear menu) and
// Squadron 42 (outfits, the SQ42 settings, the [sq42] spawn list, SQ42 ships and the console).
// Moved from menu.cpp; registered through sco.ui by loadout_plugin.cpp and drawn by the menu shell
// on the game thread. The Player tab's noclip, god mode and infinite ammo are the optional creative
// plugin's (data/plugins/creative): the checkboxes run its commands through sco_api's invoke.
#include "tabs.h"
#include "../menu.h"
#include "../menu_ui.h"
#include "../third_party/imgui/imgui.h"
#include "../build.h"
#include "../cvars.h"
#include "sc_spawn.h"
#include <cstdio>
#include <cstring>

static const sco_api* g_api = nullptr;
static sco_plugin*    g_self = nullptr;

void RegisterPlayerTab(const sco_api* api, sco_plugin* self) {
    g_api = api;
    g_self = self;
    RegisterBuiltinTab(api, self, "loadout.player", "Player", kTabPlayer, DrawPlayerTab);
}

// The command's reply in the status strip (which also logs it). A refusal puts the checkbox back; a
// missing command means the creative plugin isn't loaded. SCO_FAILED (the command ran and failed)
// shows the command's own reply; SCO_UNAVAILABLE means the capability isn't on this build.
static void CreativeReply(sco_result r, const char* reply, void* ctx) {
    if (r == SCO_OK) {
        SetMenuStatus("%s", reply ? reply : "");
        return;
    }
    if (ctx) *static_cast<bool*>(ctx) = !*static_cast<bool*>(ctx);
    if (reply && reply[0]) SetMenuStatus("%s", reply);
    else if (r == SCO_NOT_FOUND) SetMenuStatus("Needs the creative plugin: turn it on in the launcher's Plugins page.");
    else if (r == SCO_UNAVAILABLE) SetMenuStatus("Not available on this game build.");
    else SetMenuStatus("The creative plugin refused (result %d).", static_cast<int>(r));
}

void InvokeCreativeToggle(const char* command, bool on, bool* flag) {
    if (!g_api || !g_self) { if (flag) *flag = !on; return; }
    sco_arg arg = {};
    arg.type = SCO_ARG_BOOL;
    arg.v.i = on ? 1 : 0;
    g_api->invoke(g_self, command, &arg, 1, CreativeReply, flag);
}

void InvokeCreativeSpeed(float metresPerSecond) {
    if (!g_api || !g_self) return;
    sco_arg arg = {};
    arg.type = SCO_ARG_FLOAT;
    arg.v.f = metresPerSecond;
    g_api->invoke(g_self, "creative.noclip_speed", &arg, 1, CreativeReply, nullptr);
}

static void GearCombo(int slot, const char* label, const char* none, int& pick, const char* filter, float width) {
    const int n = Menu_GearCount(slot);
    if (pick >= n) pick = -1;
    char preview[112], id[32];
    snprintf(preview, sizeof(preview), "%s: %s", label, pick >= 0 ? Menu_GearName(slot, pick) : none);
    snprintf(id, sizeof(id), "##gear%d", slot);
    ImGui::SetNextItemWidth(width);
    if (!ImGui::BeginCombo(id, preview, ImGuiComboFlags_HeightLarge)) return;
    if (ImGui::Selectable(none, pick < 0)) pick = -1;
    for (int i = 0; i < n; ++i) {
        const char* name = Menu_GearName(slot, i);
        if (!MatchesFilter(name, filter)) continue;
        ImGui::PushID(i);
        if (ImGui::Selectable(name, i == pick)) pick = i;
        if (i == pick) ImGui::SetItemDefaultFocus();
        ImGui::PopID();
    }
    ImGui::EndCombo();
}

void DrawPlayerTab(void*, void*) {
    SectionHeading("Movement");
    static bool  noclip = false;
    static float speed = 30.0f;
    if (ImGui::Checkbox("Noclip", &noclip)) {
        InvokeCreativeSpeed(speed);
        InvokeCreativeToggle("creative.noclip", noclip, &noclip);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##noclipSpeed", &speed, 1.0f, 500.0f, "Speed %.0f", ImGuiSliderFlags_Logarithmic))
        InvokeCreativeSpeed(speed);
    Hint("F7 saves where you're standing and F8 takes you back. The Travel tab has named spots and places.");

    SectionHeading("Protection");
    static bool god = false, ammo = false;
    if (ImGui::Checkbox("God mode", &god)) InvokeCreativeToggle("creative.god", god, &god);
    ImGui::SameLine(0, 24);
    if (ImGui::Checkbox("Infinite ammo", &ammo)) InvokeCreativeToggle("creative.ammo", ammo, &ammo);
    Hint("Noclip, god mode and infinite ammo come from the creative plugin. Turn it on in the launcher's Plugins page.");

    SectionHeading("Gear");
    static int  gear[Gear_SlotCount] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    static char gearFilter[64] = "";
    static const struct { const char* label; const char* none; } kGear[Gear_SlotCount] = {
        { "Undersuit", "Default" }, { "Helmet", "None" }, { "Body", "None" }, { "Arms", "None" }, { "Legs", "None" },
        { "Backpack", "None" }, { "Primary", "None" }, { "Sidearm", "None" }, { "Ammo", "Matches weapon" }, { "Grenades", "None" } };
    if (Menu_GearCount(0) < 0) { Hint("Loading gear (you need to be in the universe)..."); return; }
    SearchBox("##gearFilter", "Search gear", gearFilter, sizeof(gearFilter));
    const float half = Columns(2);
    for (int s = 0; s < Gear_SlotCount; ++s) {
        if (s % 2) ImGui::SameLine();
        GearCombo(s, kGear[s].label, kGear[s].none, gear[s], gearFilter, half);
    }
    if (PrimaryButton("Equip gear")) { Menu_RequestEquip(gear); MenuClose(); }
}

void DrawSq42Tab(void*, void*) {
    static bool spoilerOk = false;
    if (!spoilerOk) {
        ImGui::SeparatorText("Spoiler warning");
        ImGui::TextWrapped("This tab could have Squadron 42 spoilers.");
        ImGui::TextWrapped("Press OK to continue.");
        if (ImGui::Button("OK", ImVec2(120, 0))) spoilerOk = true;
        ImGui::SameLine();
        if (ImGui::Button("Back", ImVec2(120, 0))) MenuSelectFirstTab();
        return;
    }

    ImGui::SeparatorText("Outfits");
    const int outfits = Menu_OutfitCount();
    static int outfit = 0;
    if (outfits < 0) {
        ImGui::TextWrapped("Loading outfits... (you need to be spawned in the universe)");
    } else if (outfits == 0) {
        ImGui::TextWrapped("No outfits found - check outfits.txt.");
    } else {
        static char filter[64] = "";
        if (outfit >= outfits) outfit = 0;
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##outfitFilter", "search outfits...", filter, sizeof(filter));
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##outfit", Menu_OutfitName(outfit), ImGuiComboFlags_HeightLargest)) {
            for (int i = 0; i < outfits; ++i) {
                const char* name = Menu_OutfitName(i);
                if (!MatchesFilter(name, filter)) continue;
                ImGui::PushID(i);
                if (ImGui::Selectable(name, i == outfit)) outfit = i;
                if (i == outfit) ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Wear SQ42 outfit", ImVec2(-1, 0))) {
            Menu_RequestWearOutfit(outfit);
            MenuClose();
        }
    }
    static bool visor = false;
    if (ImGui::Checkbox("SQ42 visor HUD (applies on the next Equip or outfit)", &visor))
        Menu_SetS42VisorHud(visor);

    ImGui::SeparatorText("Settings");
    for (int i = 0; i < Menu_S42SettingCount(); ++i) {
        bool on = Menu_S42SettingOn(i);
        ImGui::PushID(i);
        const bool known = Menu_S42SettingKnown(i);   // greyed until the game thread has read the cvar
        ImGui::BeginDisabled(!known);
        if (ImGui::Checkbox(Menu_S42SettingLabel(i), &on)) Menu_RequestS42Setting(i, on);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", known ? Menu_S42SettingTip(i) : "Reading this setting from the game...");
        ImGui::PopID();
    }

    ImGui::SeparatorText("Spawn");
    {
        static int   thing = -1;
        static char  thingFilter[64] = "";
        static bool  inFront = true;
        static float ahead = 8.0f;
        const int buildables = Menu_BuildCount();
        if (buildables < 0) {
            ImGui::TextWrapped("Loading... (you need to be spawned in the universe)");
        } else if (buildables == 0) {
            ImGui::TextWrapped("No buildables found - check buildables.txt.");
        } else {
            if (thing < 0) {
                thing = 0;
                for (int i = 0; i < buildables; ++i)
                    if (_stricmp(Menu_BuildCategory(i), "sq42") == 0) { thing = i; break; }
            }
            if (thing >= buildables) thing = 0;
            ImGui::Checkbox("Spawn in front of you", &inFront);
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##thingFilter", "search the [sq42] group...", thingFilter, sizeof(thingFilter));
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##sq42thing", Menu_BuildName(thing), ImGuiComboFlags_HeightLargest)) {
                for (int i = 0; i < buildables; ++i) {
                    // Only the [sq42] group, like the original; the text filter narrows it further.
                    if (_stricmp(Menu_BuildCategory(i), "sq42") != 0) continue;
                    const char* name = Menu_BuildName(i);
                    if (!MatchesFilter(name, thingFilter)) continue;
                    ImGui::PushID(i);
                    if (ImGui::Selectable(name, i == thing)) thing = i;
                    if (i == thing) ImGui::SetItemDefaultFocus();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            if (inFront) {
                ImGui::SetNextItemWidth(200);
                ImGui::SliderFloat("ahead (m)", &ahead, 1.0f, 50.0f, "%.0f");
            }
            if (ImGui::Button("Spawn it", ImVec2(-1, 0))) {
                Menu_RequestPlace(thing, inFront, ahead);
                MenuClose();
            }
            ImGui::SetItemTooltip("Undo and Clear base in the build section remove these too.");
        }
    }

    ImGui::SeparatorText("Ships");
    struct Entry { const char* label; const char* cls; bool enemyWing; float height; bool sit; };
    static const struct { const char* label; const char* cls; } kSq42Ships[] = {
        { "Idris-P (the Stanton's class)", "AEGS_Idris_P" },
        { "Gladius (SQ42 fighter)",        "AEGS_Gladius" },
        { "Retaliator (has an S42 HUD)",   "AEGS_Retaliator" },
        { "Starfarer (ch 5, 7, 9)",        "MISC_Starfarer" },
        { "Avenger Stalker (S42 wreck)",   "AEGS_Avenger_Stalker" },
        { "Hornet (Cal Mason's ship)",     "ANVL_Hornet_F7C" },
        { "Vanduul Blade (AI)",            "VNCL_Blade_PU_AI_VAN" },
        { "Vanduul Scythe (AI)",           "VNCL_Scythe_PU_AI_VAN" },
        { "Vanduul Glaive (AI)",           "VNCL_Glaive_PU_AI_VAN" },
        { "Vanduul Stinger (AI)",          "VNCL_Stinger_PU_AI_VAN" },
    };
    Entry list[16];
    static_assert(sizeof(kSq42Ships) / sizeof(kSq42Ships[0]) + 2 <= sizeof(list) / sizeof(list[0]),
                  "SQ42 ship table outgrew Entry list[]");
    int   n = 0;
    for (size_t i = 0; i < sizeof(kSq42Ships) / sizeof(kSq42Ships[0]); ++i)
        list[n++] = { kSq42Ships[i].label, kSq42Ships[i].cls, false, 0.0f, true };
    for (int i = 0; i < n; ++i)
        if (strncmp(list[i].cls, "VNCL_", 5) == 0) { list[i].height = 300.0f; list[i].sit = false; }

    // Not the original's dormant "[battle]" mode (two Bengals fighting each other): these
    // spawn one UEE Bengal, the second with a Vanduul wing when an enemy side was found.
    list[n++] = { "Bengal (UEE)", "RSI_Bengal_PU_AI_UEE", false, 1500.0f, false };
    list[n++] = { "Bengal + wing (deferred)", "RSI_Bengal_PU_AI_UEE", true, 1500.0f, false };

    static int   pick = 0;
    static char  sqFilter[64] = "";
    static float height = 30.0f;   // the original's fixed spawn height for your own ships
    static bool  sit = true;
    if (pick >= n) pick = 0;
    // Greys out classes this game build doesn't have, like the original, instead of failing
    // after the click with "unknown entity class".
    auto known = [](const char* cls) {
        const sc_spawn_service_v1* spawn = nullptr;
        if (!g_api || g_api->size < offsetof(sco_api, query_service) + sizeof(g_api->query_service) ||
            !g_api->query_service || !g_api->has || !g_api->has("spawn.entities")) return false;
        if (g_api->query_service(SC_SPAWN_SERVICE_NAME, SC_SPAWN_SERVICE_VERSION,
                reinterpret_cast<const void**>(&spawn)) != SCO_OK || !spawn ||
            spawn->size < offsetof(sc_spawn_service_v1, class_exists) + sizeof(spawn->class_exists) ||
            !spawn->class_exists) return false;
        return spawn->class_exists(cls) != 0;
    };

    ImGui::TextWrapped("Ordinary ships use the external spawn plugin. AI/enemy-wing orchestration is deferred; no private fallback.");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##sq42shipFilter", "search ships...", sqFilter, sizeof(sqFilter));
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##sq42ships", list[pick].label, ImGuiComboFlags_HeightLargest)) {
        for (int i = 0; i < n; ++i) {
            if (sqFilter[0] && !MatchesFilter(list[i].label, sqFilter)) continue;
            ImGui::PushID(i);
            const bool have = !list[i].enemyWing && known(list[i].cls);
            if (ImGui::Selectable(list[i].label, i == pick, have ? 0 : ImGuiSelectableFlags_Disabled)) pick = i;
            if (!have) ImGui::SetItemTooltip("Unavailable class/provider, or deferred enemy-wing orchestration: %s", list[i].cls);
            if (i == pick) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    const bool pickKnown = !list[pick].enemyWing && known(list[pick].cls);
    if (list[pick].sit) {
        ImGui::SliderFloat("height above me (m)", &height, 0.0f, 500.0f, "%.0f");
        ImGui::Checkbox("put me in the pilot seat", &sit);
    }
    ImGui::BeginDisabled(!pickKnown);
    const bool spawnClicked = ImGui::Button("Spawn", ImVec2(-1, 42));
    ImGui::EndDisabled();
    if (spawnClicked) {
        if (!g_api || !g_self || !g_api->invoke) {
            SetMenuStatus("The external spawn plugin is unavailable.");
        } else {
            sco_arg args[4] = {};
            args[0].type = SCO_ARG_STRING; args[0].v.s = list[pick].cls;
            args[1].type = SCO_ARG_FLOAT; args[1].v.f = list[pick].sit ? height : list[pick].height;
            args[2].type = SCO_ARG_STRING; args[2].v.s = list[pick].sit && sit ? "pilot" : "none";
            args[3].type = SCO_ARG_BOOL; args[3].v.i = list[pick].sit && sit ? 1 : 0;
            const sco_result queued = g_api->invoke(g_self, "spawn.ship_options", args, 4,
                [](sco_result result, const char* reply, void*) {
                    if (reply && *reply) SetMenuStatus("%s", reply);
                    else SetMenuStatus("External spawn result %d (missing/disabled plugin has no fallback).", static_cast<int>(result));
                }, nullptr);
            if (queued != SCO_OK) SetMenuStatus("External spawn unavailable (result %d).", static_cast<int>(queued));
            else MenuClose();
        }
    }

    ImGui::SeparatorText("Console");
    static char cmd[256] = "";
    const bool consoleReady = Menu_ConsoleReady();
    ImGui::BeginDisabled(!consoleReady);
    const float runWidth = ImGui::CalcTextSize("Run").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SetNextItemWidth(-(runWidth + ImGui::GetStyle().ItemSpacing.x));
    bool run = ImGui::InputTextWithHint("##console",
                                        "a console command, e.g. i_target_selector.targeting2_enabled 1",
                                        cmd, sizeof(cmd), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    run |= ImGui::Button("Run");
    ImGui::EndDisabled();
    if (run && cmd[0]) {
        Menu_RunConsole(cmd);
        cmd[0] = 0;
    }
    if (!consoleReady) ImGui::TextDisabled("The game's console wasn't found yet.");
    ImGui::TextWrapped("Runs in the game's own console. What it did shows in the game's log, not here.");
}
