// Host Build picker: grouped legacy data list, rendering and public command forwarding only.
// Registered as loadout.build; external build owns placement/undo and refuses disabled G5.
#include "tabs.h"
#include "build_menu.h"
#include "../menu.h"
#include "../menu_ui.h"
#include "../third_party/imgui/imgui.h"
#include <cstdio>
#include <cstring>

void DrawBuildTab(void*, void*) {
    static int  build = 0, buildTab = 0;
    static char buildFilter[64] = "";
    const int buildables = Menu_BuildCount();
    SectionHeading("Objects");
    if (buildables < 0) { Hint("Loading build objects (you need to be in the universe)..."); return; }
    if (buildables == 0) { Hint("No build objects found. Check data\\buildables.txt."); return; }
    if (build >= buildables) build = 0;
    if (ImGui::BeginTabBar("##buildTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (int c = 0; c < Menu_BuildCategoryCount(); ++c) {
            char tab[40];
            snprintf(tab, sizeof(tab), "%s##cat%d", Menu_BuildCategoryName(c), c);
            if (tab[0] >= 'a' && tab[0] <= 'z') tab[0] -= 'a' - 'A';
            if (ImGui::BeginTabItem(tab)) { buildTab = c; ImGui::EndTabItem(); }
        }
        ImGui::EndTabBar();
    }
    SearchBox("##buildFilter", "Search all objects", buildFilter, sizeof(buildFilter));
    const bool searching = buildFilter[strspn(buildFilter, " _")] != 0;
    if (ImGui::BeginChild("##buildList", ImVec2(0, 260), ImGuiChildFlags_Borders)) {
        for (int i = 0; i < buildables; ++i) {
            const char* name = Menu_BuildName(i);
            if (searching ? !MatchesFilter(name, buildFilter) : Menu_BuildCategoryOf(i) != buildTab) continue;
            char label[160];
            PrettyBuildName(label, sizeof(label) - 16, name);
            if (searching) {
                char tagged[160];
                snprintf(tagged, sizeof(tagged), "%s   (%s)", label, Menu_BuildCategory(i));
                strcpy_s(label, sizeof(label) - 16, tagged);
            }
            snprintf(label + strlen(label), 16, "##%d", i);
            if (ImGui::Selectable(label, i == build)) build = i;
            ImGui::SetItemTooltip("%s", name);
        }
    }
    ImGui::EndChild();

    SectionHeading("Placing");
    char picked[128];
    PrettyBuildName(picked, sizeof(picked), Menu_BuildName(build));
    ImGui::Text("Selected: %s", picked);
    float reach = Menu_BuildReach();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##reach", &reach, 5.0f, 300.0f, "Reach %.0f m");
    ImGui::SetItemTooltip("How far ahead objects are placed. They land on the ground where you look, or under the point this far out.");
    Menu_SetBuild(build, reach);
    const bool building = Menu_BuildModeActive();
    if (PrimaryButton(building ? "Stop building (F6)" : "Start building (F6)")) {
        Menu_ToggleBuildMode();
        if (!building) MenuClose();
    }
    if (ImGui::Button("Place selection near player", ImVec2(-1, 0)))
        Menu_RequestPlace(build, true, reach <= 100.0f ? reach : 100.0f);
    ImGui::SetItemTooltip("Public grounded placement (up to 100 m); does not require G5 freecam.");
    const float half = Columns(2);
    if (ImGui::Button("Undo last", ImVec2(half, 0))) Menu_BuildUndo();
    ImGui::SameLine();
    char clearLabel[48];
    snprintf(clearLabel, sizeof(clearLabel), "Clear base (%d)###clearBase", Menu_BuildPlacedCount());
    if (ImGui::Button(clearLabel, ImVec2(half, 0))) Menu_BuildClear();
    Hint("Aim/freecam requires enabled public G5 (currently unavailable; no private fallback). Placement retains undo authority while loaded; unload removes it.");
}
