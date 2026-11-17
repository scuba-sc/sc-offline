// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Where each page of the in-game menu goes: the sidebar's sections, the name and one-line purpose a
// player sees for every page sc-offline ships, and which pages are for developers only. Pure C++
// (no ImGui, no Windows), so tools/menu_navigation_test.cpp runs it headless.
//
// Pages come from three places: sco.ui tabs (src/builtins), sco.widgets panels (plugins), and the
// menu's own Settings page. Only the page id decides placement (ids are "<plugin id>.<name>", and a
// plugin can only register ids under its own plugin id), so a third-party panel that copies a
// title is still placed by what it is: an unknown id goes to "More plugins" under its own title.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace sc_offline::menu_nav {

enum class Section : uint8_t {
    Character, Travel, Vehicles, Building, People, Squadron42, Multiplayer, Other, Settings, Developer,
};

inline std::string_view SectionTitle(Section section) {
    switch (section) {
    case Section::Character:   return "Character";
    case Section::Travel:      return "Travel";
    case Section::Vehicles:    return "Ships & vehicles";
    case Section::Building:    return "Build";
    case Section::People:      return "People";
    case Section::Squadron42:  return "Squadron 42";
    case Section::Multiplayer: return "Multiplayer";
    case Section::Other:       return "More plugins";
    case Section::Settings:    return "Settings";
    case Section::Developer:   return "Developer";
    }
    return "More plugins";
}

// The menu's own page: not a plugin id (a plugin id never starts with "sc_offline").
inline constexpr std::string_view kSettingsPage = "sc_offline.settings";

struct Known {
    std::string_view id, title, summary;
    Section section;
    int rank;   // order inside the section
};

// Every page sc-offline ships. A title here replaces the plugin's own tab title in the sidebar and
// the page header; the summary is the line under the header that says what the page is for.
inline constexpr Known kKnown[] = {
    {"loadout.player", "Gear", "What your character wears and carries: armour, clothes and weapons.", Section::Character, 10},
    {"gear_presets.main", "Saved outfits", "Save the gear you have on now and put it back on later.", Section::Character, 20},
    {"quantum.travel", "Travel", "Go to a planet, moon, station or landing zone in this star system.", Section::Travel, 10},
    {"teleport.travel", "Saved spots", "Remember where you are standing (F7) and go back there (F8).", Section::Travel, 20},
    {"spawn.vehicles", "Ships & vehicles", "Put a ship or ground vehicle next to you and get in.", Section::Vehicles, 10},
    {"build.picker", "Build", "Place props and buildings in front of you.", Section::Building, 10},
    {"build.build", "Build", "Place props and buildings in front of you.", Section::Building, 10},
    {"creative.panel", "Creative", "Free camera and creative tools for screenshots and scenes.", Section::Building, 20},
    {"crew.crew", "Crew", "Give your ship an AI crew: pilots, gunners and engineers.", Section::People, 10},
    {"npc.npcs", "Characters", "Put a character from the game near you.", Section::People, 20},
    {"loadout.sq42", "Squadron 42", "Squadron 42 outfits, ships and game settings. Contains story spoilers.", Section::Squadron42, 10},
    {"multiplayer.session", "Multiplayer", "Play together with friends in a private session.", Section::Multiplayer, 10},
    {"titanlink.titanfall", "Titanfall link", "Connection to the Titanfall bridge plugin.", Section::Other, 10},
    {"voxel_bridge.voxel", "Voxel link", "Connection to the voxel bridge plugin.", Section::Other, 20},
    {kSettingsPage, "Menu settings", "How this menu looks, developer pages, and where to report problems.", Section::Settings, 10},
    {"inspector.main", "Inspector", "Read-only view of what the game reports. For finding bugs.", Section::Developer, 10},
    {"ui_probe.main", "UI probe", "Test page for the plugin UI.", Section::Developer, 20},
};

inline const Known* FindKnown(std::string_view id) {
    for (const Known& known : kKnown)
        if (known.id == id) return &known;
    return nullptr;
}

enum class Source : uint8_t { Tab, Panel, Settings };

struct Page {
    std::string id;        // the sco.ui tab id, the sco.widgets panel id, or kSettingsPage
    std::string title;     // what the sidebar and the header show
    std::string summary;   // one line under the header; may be empty
    std::string badge;     // sco.ui badge ("" when none)
    Section section = Section::Other;
    int rank = 0;
    int arrival = 0;       // order the host listed it in; breaks ties
    Source source = Source::Panel;
};

// Describes one live page. `title` is the plugin's own; it is kept for pages sc-offline doesn't
// know. Empty titles fall back to the id, so every sidebar entry has a readable name.
inline Page Describe(std::string_view id, std::string_view title, std::string_view badge, int arrival, Source source) {
    Page page;
    page.id = id;
    page.badge = badge;
    page.arrival = arrival;
    page.source = source;
    if (const Known* known = FindKnown(id)) {
        page.title = known->title;
        page.summary = known->summary;
        page.section = known->section;
        page.rank = known->rank;
    } else {
        page.title = title.empty() ? std::string(id) : std::string(title);
        const auto dot = id.find('.');
        page.summary = "Added by the " + std::string(id.substr(0, dot)) + " plugin.";
        page.section = Section::Other;
        page.rank = 1000;
    }
    return page;
}

// Developer pages are left out unless asked for; the rest is sorted by section, then the order
// inside the section, then the order the host listed them in. A second page with an id already
// listed is dropped (the first one is the one the host draws).
inline std::vector<Page> Arrange(std::vector<Page> pages, bool developer) {
    std::vector<Page> result;
    result.reserve(pages.size());
    for (Page& page : pages) {
        if (page.section == Section::Developer && !developer) continue;
        const bool seen = std::any_of(result.begin(), result.end(), [&](const Page& p) { return p.id == page.id; });
        if (!seen) result.push_back(std::move(page));
    }
    std::stable_sort(result.begin(), result.end(), [](const Page& a, const Page& b) {
        if (a.section != b.section) return a.section < b.section;
        if (a.rank != b.rank) return a.rank < b.rank;
        return a.arrival < b.arrival;
    });
    return result;
}

// The page to show: the one asked for when it is still there, otherwise the first page.
inline std::string Choose(const std::vector<Page>& pages, std::string_view wanted) {
    for (const Page& page : pages)
        if (page.id == wanted) return page.id;
    return pages.empty() ? std::string() : pages.front().id;
}

// ---- menu-view.ini: the page that was open and the developer switch ----------------------------
//
// A separate file from menu-window.ini, whose strict reader refuses any key it doesn't know (an
// older sc-offline would otherwise reset the saved window size).

struct View {
    std::string page;
    bool developer = false;
    bool operator==(const View&) const = default;
};

inline constexpr size_t kMaxPageId = 63;   // SCO_WIDGETS_MAX_ID; sco.ui ids are no longer

inline bool ValidPageId(std::string_view id) {
    if (id.empty() || id.size() > kMaxPageId) return false;
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
    });
}

inline std::string_view TrimView(std::string_view text) {
    const auto begin = text.find_first_not_of(" \t\r");
    if (begin == text.npos) return {};
    return text.substr(begin, text.find_last_not_of(" \t\r") - begin + 1);
}

// Anything unexpected gives the defaults: first page, developer pages hidden.
inline View LoadView(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    char bytes[257];
    file.read(bytes, sizeof(bytes));
    if (file.bad() || file.gcount() > 256) return {};
    std::string_view text(bytes, static_cast<size_t>(file.gcount()));
    if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
    View view;
    bool section = false, page = false, developer = false;
    while (!text.empty()) {
        const auto end = text.find('\n');
        const auto line = TrimView(text.substr(0, end));
        text = end == text.npos ? std::string_view{} : text.substr(end + 1);
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        if (line == "[view]") { section = true; continue; }
        const auto eq = line.find('=');
        if (!section || eq == line.npos) return {};
        const auto key = TrimView(line.substr(0, eq)), value = TrimView(line.substr(eq + 1));
        if (key == "page" && !page) {
            if (!ValidPageId(value)) return {};
            view.page = value;
            page = true;
        } else if (key == "developer" && !developer) {
            if (value != "0" && value != "1") return {};
            view.developer = value == "1";
            developer = true;
        } else {
            return {};
        }
    }
    return view;
}

// A page id LoadView would refuse is left out (the menu then opens its first page); the developer
// switch is still saved.
inline bool SaveView(const std::filesystem::path& path, const View& view) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << "[view]\n";
    if (ValidPageId(view.page)) file << "page=" << view.page << '\n';
    file << "developer=" << (view.developer ? 1 : 0) << '\n';
    file.close();
    return !file.fail();
}

} // namespace sc_offline::menu_nav
