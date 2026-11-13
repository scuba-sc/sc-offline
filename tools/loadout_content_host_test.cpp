// SPDX-License-Identifier: GPL-3.0-only
// Real Core startup, real Loadout module, synthetic lists; no game services.
#include "sco/app.h"
#include "sco/host.h"
#include "sco_content.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
struct Reply { sco_result result = SCO_FAILED; unsigned calls = 0; std::string text; };
static void Done(sco_result r, const char* text, void* context) {
    auto& reply = *static_cast<Reply*>(context);
    reply.result = r; ++reply.calls; reply.text = text ? text : "";
}
static bool Write(const fs::path& path, const char* text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary); out << text; out.close(); return bool(out);
}
static bool Contains(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
int main(int argc, char** argv) {
    if (argc != 5) return 2;
    const fs::path root = argv[1];
    if (!fs::create_directory(root)) { std::fprintf(stderr, "fixture root must not exist\n"); return 2; }
    bool ok = true;
    try {
        fs::create_directories(root / "loadout"); fs::create_directories(root / "loadout_lists/lists");
        fs::copy_file(argv[2], root / "loadout/loadout.dll");
        fs::copy_file(argv[3], root / "loadout/plugin.ini");
        fs::copy_file(argv[4], root / "loadout_lists/plugin.ini");
        ok = Write(root / "loadout_lists/lists/items.txt", "[helmet]\nFixtureHelmet\n[torso]\nFixtureTorso\n") &&
             Write(root / "loadout_lists/lists/outfits.txt", "[FixtureOutfit]\nArmor_Helmet FixtureHelmet\n");
        sco::app::Platform platform{}; platform.pluginRoot = root; platform.pluginsEnabled = true;
        if (!ok || !sco::app::Start(platform)) { std::fprintf(stderr, "host start failed\n"); return 1; }
        bool loaded = false, indexed = false;
        for (const auto& plugin : sco::app::Plugins()) {
            if (plugin.folder == "loadout") loaded = plugin.state == sco::plugins::State::Loaded;
            if (plugin.folder == "loadout_lists") indexed = plugin.state == sco::plugins::State::Loaded;
        }
        ok = loaded && indexed && sco::app::Content().FromPlugin("loadout_lists").size() == 2;
        const auto* api = sco::host::BuildApi({"synthetic"});
        auto* self = sco::host::NewPlugin("fixture_reader");
        const void* raw = nullptr;
        ok = ok && self && api->query_service(SCO_CONTENT_NAME, SCO_CONTENT_VERSION_1_0, &raw) == SCO_OK && raw;
        Reply reload;
        const auto rr = api->invoke(self, "loadout.reload_lists", nullptr, 0, Done, &reload);
        ok = ok && rr == SCO_OK && reload.calls == 1 && reload.result == SCO_OK &&
             Contains(reload.text, "items=2 from loadout_lists/lists/items.txt") &&
             Contains(reload.text, "outfits=1 from loadout_lists/lists/outfits.txt");
        sco_arg filter{}; filter.type = SCO_ARG_STRING; filter.v.s = "";
        Reply items;
        const auto ir = api->invoke(self, "loadout.items", &filter, 1, Done, &items);
        ok = ok && ir == SCO_OK && items.calls == 1 && items.result == SCO_OK &&
             Contains(items.text, "Copied gear list from loadout_lists/lists/items.txt") &&
             Contains(items.text, "FixtureHelmet") && Contains(items.text, "FixtureTorso");
        if (self) sco::Release(self);
        ok = (sco::app::Stop() == sco::Result::Ok) && ok;
        for (const auto& plugin : sco::app::Plugins())
            if (plugin.folder == "loadout") ok = ok && plugin.state == sco::plugins::State::Unloaded;
    } catch (const std::exception& e) { std::fprintf(stderr, "fixture error: %s\n", e.what()); return 1; }
    std::printf("real Core content to Loadout: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
