// SPDX-License-Identifier: GPL-3.0-only
// Headless checks of the menu's sidebar model (src/menu_navigation.h): placement, order, the
// developer switch, the remembered page, and menu-view.ini. Not run in game.
#include "menu_navigation.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

static int passed = 0, failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

namespace nav = sc_offline::menu_nav;

static std::vector<std::string> Ids(const std::vector<nav::Page>& pages) {
    std::vector<std::string> ids;
    for (const auto& page : pages) ids.push_back(page.id);
    return ids;
}

// The pages a dev241 package registers, in a plugin load order that is not the player's order.
static std::vector<nav::Page> Shipped() {
    int arrival = 0;
    std::vector<nav::Page> pages;
    for (const char* id : {"build.picker", "crew.crew", "spawn.vehicles", "npc.npcs", "inspector.main", "creative.panel",
                           "gear_presets.main", "quantum.travel", "teleport.travel", "loadout.player", "loadout.sq42"})
        pages.push_back(nav::Describe(id, "plugin title", "", arrival++, nav::Source::Panel));
    pages.push_back(nav::Describe(nav::kSettingsPage, "Menu", "", arrival++, nav::Source::Settings));
    return pages;
}

static void Placement() {
    const auto arranged = nav::Arrange(Shipped(), false);
    const std::vector<std::string> expected = {
        "loadout.player", "gear_presets.main", "quantum.travel", "teleport.travel", "spawn.vehicles",
        "build.picker", "creative.panel", "crew.crew", "npc.npcs", "loadout.sq42", std::string(nav::kSettingsPage)};
    CHECK(Ids(arranged) == expected);
    // Developer pages appear only with the switch on, in their own last section.
    const auto developer = nav::Arrange(Shipped(), true);
    CHECK(developer.size() == arranged.size() + 1);
    CHECK(developer.back().id == "inspector.main");
    CHECK(developer.back().section == nav::Section::Developer);
    // Sections never interleave: each section's pages are contiguous.
    for (size_t i = 1; i < developer.size(); ++i) CHECK(developer[i - 1].section <= developer[i].section);
}

static void Names() {
    // Every shipped page has a player-facing title and a one-line summary.
    for (const auto& page : nav::Arrange(Shipped(), true)) {
        CHECK(!page.title.empty());
        CHECK(!page.summary.empty());
        CHECK(page.title != "plugin title");
        CHECK(page.summary.size() <= 120);
        CHECK(page.title.find('.') == std::string::npos);   // never an id
    }
    const auto teleport = nav::Describe("teleport.travel", "teleport", "", 0, nav::Source::Panel);
    CHECK(teleport.title == "Saved spots");
    CHECK(nav::SectionTitle(nav::Section::Vehicles) == "Ships & vehicles");
    // Every known page is in a section that has a title.
    for (const auto& known : nav::kKnown) {
        CHECK(!nav::SectionTitle(known.section).empty());
        CHECK(nav::ValidPageId(known.id));
        CHECK(!known.summary.empty() && known.summary.back() == '.');
    }
}

static void Unknown() {
    // A third-party page keeps its own title, says where it came from and goes to "More plugins",
    // after every page sc-offline knows and before Settings.
    auto pages = Shipped();
    pages.push_back(nav::Describe("weather.main", "Weather", "2", 99, nav::Source::Panel));
    pages.push_back(nav::Describe("radio.panel", "", "", 100, nav::Source::Tab));
    const auto arranged = nav::Arrange(pages, false);
    const auto ids = Ids(arranged);
    CHECK(ids[ids.size() - 3] == "weather.main");
    CHECK(ids[ids.size() - 2] == "radio.panel");
    CHECK(ids.back() == nav::kSettingsPage);
    CHECK(arranged[arranged.size() - 3].title == "Weather");
    CHECK(arranged[arranged.size() - 3].badge == "2");
    CHECK(arranged[arranged.size() - 3].summary == "Added by the weather plugin.");
    CHECK(arranged[arranged.size() - 2].title == "radio.panel");   // no title: the id, still readable
    // A copied title doesn't move a page: placement is by id.
    const auto copycat = nav::Describe("copycat.main", "Ships & vehicles", "", 0, nav::Source::Panel);
    CHECK(copycat.section == nav::Section::Other);
    // A plugin can't claim the menu's own page: its ids always start with its own plugin id.
    CHECK(nav::FindKnown("sc_offline.settings")->section == nav::Section::Settings);
}

static void Duplicates() {
    // The same id from a tab and a panel (an old built-in and its plugin): the first one is kept.
    std::vector<nav::Page> pages = {
        nav::Describe("quantum.travel", "Travel", "", 0, nav::Source::Tab),
        nav::Describe("quantum.travel", "Travel", "", 1, nav::Source::Panel),
    };
    const auto arranged = nav::Arrange(pages, false);
    CHECK(arranged.size() == 1);
    CHECK(arranged[0].source == nav::Source::Tab);
    CHECK(nav::Arrange({}, true).empty());
}

static void Choosing() {
    const auto arranged = nav::Arrange(Shipped(), false);
    CHECK(nav::Choose(arranged, "spawn.vehicles") == "spawn.vehicles");
    CHECK(nav::Choose(arranged, "inspector.main") == "loadout.player");   // hidden: back to the first page
    CHECK(nav::Choose(arranged, "") == "loadout.player");
    CHECK(nav::Choose(arranged, "gone.plugin") == "loadout.player");
    CHECK(nav::Choose({}, "spawn.vehicles").empty());
}

static void ViewFile(const std::filesystem::path& folder) {
    const auto path = folder / "menu-view.ini";
    auto write = [&](const std::string& text) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << text;
    };
    CHECK(nav::LoadView(folder / "missing.ini") == nav::View{});
    const nav::View view{"spawn.vehicles", true};
    CHECK(nav::SaveView(path, view));
    CHECK(nav::LoadView(path) == view);
    CHECK(nav::SaveView(path, nav::View{"", false}));
    CHECK(nav::LoadView(path) == nav::View{});
    // A page id that couldn't be read back is left out; the developer switch still persists.
    CHECK(nav::SaveView(path, nav::View{"Bad Id", true}));
    CHECK(nav::LoadView(path) == (nav::View{"", true}));
    CHECK(nav::SaveView(path, nav::View{std::string(64, 'a'), true}));
    CHECK(nav::LoadView(path) == (nav::View{"", true}));

    write("\xef\xbb\xbf[view]\r\npage = crew.crew\r\ndeveloper = 0\r\n");
    CHECK(nav::LoadView(path) == (nav::View{"crew.crew", false}));
    write("; comment\n[view]\ndeveloper=1\n");
    CHECK(nav::LoadView(path) == (nav::View{"", true}));
    // Anything malformed gives the defaults, never a half-read view.
    for (const char* bad : {"page=crew.crew\n", "[view]\npage=Crew\n", "[view]\ndeveloper=yes\n",
                            "[view]\npage=a\npage=b\n", "[view]\nwidth=1000\n", "[view]\npage\n",
                            "[view]\npage=../x\n", "[view]\ndeveloper=1\ndeveloper=1\n"}) {
        write(bad);
        CHECK(nav::LoadView(path) == nav::View{});
    }
    write("[view]\npage=" + std::string(300, 'a') + "\n");
    CHECK(nav::LoadView(path) == nav::View{});
}

int main(int argc, char** argv) {
    const std::filesystem::path folder = argc > 1 ? argv[1] : ".";
    Placement();
    Names();
    Unknown();
    Duplicates();
    Choosing();
    ViewFile(folder);
    std::printf("menu navigation: %d passed, %d failed (headless; not run in game)\n", passed, failed);
    return failed ? 1 : 0;
}
