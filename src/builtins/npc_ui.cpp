// Host-only archetype picker retained for Crew. NPC is command-first externally;
// its replacement UI waits for SDK renderer-neutral widgets (sco-core #139).
#include "tabs.h"
#include "../menu.h"
#include "../menu_ui.h"
#include "../third_party/imgui/imgui.h"
#include <cstdio>
#include <cstring>

// --- NPC picker, used by the Crew tab -----------------------------------------------------

int g_npcPick = 0;

bool NpcPicker() {
    static char filter[64] = "";
    const int npcs = Menu_NpcCount();
    if (npcs < 0) { Hint("Loading NPCs (you need to be in the universe)..."); return false; }
    if (npcs == 0) { Hint("No NPCs found. Check data\\npcs.txt."); return false; }
    if (g_npcPick >= npcs) g_npcPick = 0;
    if (SearchBox("##npcFilter", "Search NPCs", filter, sizeof(filter)))
        for (int i = 0; i < npcs; ++i)
            if (MatchesFilter(Menu_NpcName(i), filter)) { g_npcPick = i; break; }
    char preview[128];
    PrettyBuildName(preview, sizeof(preview), Menu_NpcName(g_npcPick));
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##npc", preview, ImGuiComboFlags_HeightLarge)) {
        for (int i = 0; i < npcs; ++i) {
            const char* name = Menu_NpcName(i);
            if (!MatchesFilter(name, filter)) continue;
            char label[160];
            PrettyBuildName(label, sizeof(label) - 16, name);
            snprintf(label + strlen(label), 16, "##n%d", i);
            if (ImGui::Selectable(label, i == g_npcPick)) g_npcPick = i;
            ImGui::SetItemTooltip("%s", name);
            if (i == g_npcPick) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return true;
}
