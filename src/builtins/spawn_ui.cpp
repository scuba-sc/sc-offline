// The spawn built-in's menu tab: Vehicles (the ship list, boarding options and the current ship).
// Moved from menu.cpp unchanged; registered through sco.ui by spawn_plugin.cpp and drawn by the
// menu shell.
#include "tabs.h"
#include "../hooks.h"
#include "../menu.h"
#include "../menu_ui.h"
#include "../third_party/imgui/imgui.h"
#include <cstdio>
#include <cstring>

void DrawVehiclesTab(void*, void*) {
    static int  selected = 0;
    static char filter[64] = "";
    static MenuSpawnOptions opt;

    const int count = Menu_ShipCount();
    SectionHeading("Spawn a ship");
    if (count < 0) {
        Hint("Loading ships (you need to be in the universe)...");
    } else if (count == 0) {
        Hint("No ships found. Check data\\ships.txt.");
    } else {
        const MenuShip* ships = Menu_Ships();
        if (selected >= count) selected = 0;
        if (SearchBox("##shipFilter", "Search ships", filter, sizeof(filter)))
            for (int i = 0; i < count; ++i)
                if (MatchesFilter(ships[i].name, filter)) { selected = i; break; }
        int shown = 0;
        const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter
                                    | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("##ships", 3, flags, ImVec2(0, 230))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Ship", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 44);
            ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed, 64);
            ImGui::TableHeadersRow();
            for (int i = 0; i < count; ++i) {
                if (!MatchesFilter(ships[i].name, filter)) continue;
                ++shown;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char label[96];
                snprintf(label, sizeof(label), "%s##s%d", ships[i].name, i);
                if (ImGui::Selectable(label, i == selected, ImGuiSelectableFlags_SpanAllColumns)) selected = i;
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%d", ships[i].size);
                ImGui::TableNextColumn();
                if (ships[i].length > 0) ImGui::TextDisabled("%.0f m", ships[i].length);
            }
            ImGui::EndTable();
        }
        if (shown == 0) {
            Hint("No ships match your search.");
        } else {
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##height", &opt.height, 0.0f, 500.0f, "Spawn %.0f m above you");
            static const char* const kBoard[] = { "Don't board", "Board in the pilot seat", "Board in a seat by name", "Choose a seat after it spawns" };
            ImGui::SetNextItemWidth(-1);
            ImGui::Combo("##board", &opt.seatMode, kBoard, 4);
            if (opt.seatMode == SeatMode_Named) {
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##seatName", "Seat name, e.g. copilot or turret left", opt.seatName, sizeof(opt.seatName));
                ImGui::SetItemTooltip("Every word must appear in the seat's name. The Crew tab shows a ship's seat names.");
            }
            const bool boarding = opt.seatMode == SeatMode_Pilot || opt.seatMode == SeatMode_Named;
            ImGui::BeginDisabled(!boarding);
            ImGui::Checkbox("Remove the NPC in my seat", &opt.replaceNpc);
            ImGui::SameLine(0, 24);
            ImGui::Checkbox("Power on", &opt.flightReady);
            ImGui::SetItemTooltip("Powers the ship on once you're in a pilot seat.");
            ImGui::EndDisabled();

            char spawn[96];
            snprintf(spawn, sizeof(spawn), "Spawn %s", ships[selected].name);
            if (PrimaryButton(spawn)) {
                MenuSpawnOptions send = opt;
                if (!boarding) send.flightReady = false;
                Menu_RequestSpawn(selected, send);
                if (opt.seatMode != SeatMode_PickLater) MenuClose();
            }
        }
    }

    SectionHeading("Current ship");
    static bool shipAmmo = false;
    if (ImGui::Checkbox("Infinite ship ammo", &shipAmmo)) InvokeCreativeToggle("creative.ship_ammo", shipAmmo, &shipAmmo);
    ImGui::SetItemTooltip("Refills the magazines of the ship you're aboard. Needs the creative plugin (launcher's Plugins page).");
    if (ImGui::Button("Power on / off", ImVec2(-1, 0))) Menu_RequestFlightReady();
    Hint("Toggles Flight Ready on the ship in the Crew tab, as if you pressed R in its pilot seat.");

    SectionHeading("Ship terminals (ASOP)");
    Hint("Check the ships to show at ASOP terminals. This list is saved separately from the full spawn catalogue in ships.txt.");
    const MenuShip* asopShips = Menu_Ships();
    if (count < 0) {
        Hint("Loading ships (you need to be in the universe)...");
    } else if (count == 0) {
        Hint("No ships found. Check data\\ships.txt.");
    } else {
        if (ImGui::Button("All")) {
            const char* names[2048];
            const int n = count < 2048 ? count : 2048;
            for (int i = 0; i < n; ++i) names[i] = asopShips[i].name;
            Fleet_ReplaceAsopListed(names, n);
        }
        ImGui::SameLine();
        if (ImGui::Button("None")) Fleet_ClearAsopListed();
        ImGui::SameLine();
        ImGui::TextDisabled("%d checked", Fleet_AsopListedCount());
        static char asopFilter[64] = "";
        SearchBox("##asopFilter", "Search terminal ships", asopFilter, sizeof(asopFilter));
        if (ImGui::BeginChild("##asopShips", ImVec2(0, 220), ImGuiChildFlags_Borders)) {
            for (int i = 0; i < count; ++i) {
                if (!MatchesFilter(asopShips[i].name, asopFilter)) continue;
                ImGui::PushID(i);
                bool on = Fleet_AsopListed(asopShips[i].name);
                if (ImGui::Checkbox(asopShips[i].name, &on)) Fleet_SetAsopListed(asopShips[i].name, on);
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }
}
