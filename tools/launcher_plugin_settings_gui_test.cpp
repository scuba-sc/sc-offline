// SPDX-License-Identifier: GPL-3.0-only
#define wmain sco_launcher_unused_entrypoint
#include "../launcher/launcher.cpp"
#undef wmain
#include "../src/third_party/imgui/imgui_internal.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)

static void Frame(float x, float y, bool down) {
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
    io.AddMouseButtonEvent(0, down);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(800, 600));
    ImGui::Begin("settings fixture", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove);
    DrawPlugins(0, 0, 800, 600);
    ImGui::End();
    ImGui::Render();
}
static void Click(float x) {
    Frame(x, 570, false);
    Frame(x, 570, true);
    Frame(x, 570, false);
}
static void Edit(size_t i, const char* text) {
    std::snprintf(g_gui.plugins.at(0).settings.at(i).edit, 256, "%s", text);
}
static void ExpectDraft(const char* a, const char* b, bool kept) {
    CHECK(g_gui.plugOpen == "fixture");
    CHECK(g_gui.plugins.size() == 1 && g_gui.plugins[0].settings.size() == 2);
    const auto& s = g_gui.plugins[0].settings;
    CHECK(std::string(s[0].edit) == a && std::string(s[1].edit) == b);
    CHECK(s[0].kept == kept && s[1].kept == kept);
}
static std::string Stored(sqlite3* db, const char* name) {
    sqlite3_stmt* st = nullptr;
    CHECK(sqlite3_prepare_v2(db, "SELECT value FROM sco_kv WHERE key=?1", -1, &st, nullptr) == SQLITE_OK);
    const std::string key = std::string("sco.settings.") + name;
    CHECK(sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK);
    std::string out;
    if (sqlite3_step(st) == SQLITE_ROW) {
        const char* value = static_cast<const char*>(sqlite3_column_blob(st, 0));
        out.assign(value, size_t(sqlite3_column_bytes(st, 0)));
    }
    CHECK(sqlite3_finalize(st) == SQLITE_OK);
    return out;
}
int wmain(int argc, wchar_t** argv) {
    CHECK(argc == 2);
    fs::path owned(argv[1]);
    owned /= L"settings-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    CHECK(fs::create_directories(owned / L"data/plugins/fixture"));
    CHECK(fs::create_directories(owned / L"data/storage"));
    {
        std::ofstream ini(owned / L"data/plugins/fixture/plugin.ini", std::ios::binary);
        CHECK(bool(ini << "name = Fixture\n[settings]\ncount = int default 1 min 0 max 100\nlabel = string default initial\n"));
    }
    g_gui.here = owned.wstring();
    g_gui.dpi = 1.0f;
    g_gui.wnd = CreateWindowExW(0, L"STATIC", L"hidden settings fixture", WS_OVERLAPPED,
        0, 0, 800, 600, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    CHECK(g_gui.wnd && !IsWindowVisible(g_gui.wnd));
    sqlite3* db = OpenPluginDb(PluginDbPath(g_gui.here, L"fixture"), SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    CHECK(db);
    CHECK(sqlite3_exec(db, "CREATE TABLE sco_kv(key TEXT PRIMARY KEY NOT NULL, value BLOB NOT NULL) WITHOUT ROWID;"
        "INSERT INTO sco_kv VALUES('sco.settings.count','int:2'),('sco.settings.label','string:old');", nullptr, nullptr, nullptr) == SQLITE_OK);
    g_gui.plugins = ReadPlugins(g_gui.here);
    g_gui.plugOpen = "fixture";
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(800, 600); io.DeltaTime = 1.0f / 60;
    LoadFonts(); StyleUi();
    unsigned char* pixels = nullptr; int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    CHECK(pixels && width && height);
    Frame(0, 0, false);

    // Invalid text and another valid edit must survive the real Save handler unchanged.
    Edit(0, "invalid"); Edit(1, "draft");
    Click(40);
    ExpectDraft("invalid", "draft", true);
    CHECK(!g_gui.note.empty() && g_gui.note.find("count") != std::string::npos);
    CHECK(Stored(db, "count") == "int:2" && Stored(db, "label") == "string:old");

    // A real competing writer makes BEGIN IMMEDIATE fail; retry after unlock succeeds.
    Edit(0, "003");
    CHECK(sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) == SQLITE_OK);
    Click(40);
    ExpectDraft("003", "draft", true);
    CHECK(!g_gui.note.empty() && g_gui.note != T(LocaleKey::SettingsSaved));
    CHECK(Stored(db, "count") == "int:2" && Stored(db, "label") == "string:old");
    CHECK(sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr) == SQLITE_OK);
    Click(40);
    ExpectDraft("3", "draft", true);
    CHECK(g_gui.note == T(LocaleKey::SettingsSaved));
    CHECK(g_gui.plugins[0].settings[0].value == "3" && g_gui.plugins[0].settings[1].value == "draft");
    CHECK(Stored(db, "count") == "int:3" && Stored(db, "label") == "string:draft");

    // Reset failure retains the selected plugin, dirty drafts and kept flags; retry restores defaults.
    Edit(0, "9"); Edit(1, "unsaved");
    CHECK(sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) == SQLITE_OK);
    Click(145);
    ExpectDraft("9", "unsaved", true);
    CHECK(!g_gui.note.empty() && g_gui.note != T(LocaleKey::SettingsReset));
    CHECK(Stored(db, "count") == "int:3" && Stored(db, "label") == "string:draft");
    CHECK(sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr) == SQLITE_OK);
    Click(145);
    ExpectDraft("1", "initial", false);
    CHECK(g_gui.note == T(LocaleKey::SettingsReset));
    CHECK(Stored(db, "count").empty() && Stored(db, "label").empty());

    // Make both controls eligible before testing either busy state.
    Edit(0, "7"); Edit(1, "saved"); Click(40);
    ExpectDraft("7", "saved", true);
    CHECK(Stored(db, "count") == "int:7" && Stored(db, "label") == "string:saved");
    // Both Save and Reset are disabled while a launcher task or game is active.
    Edit(0, "8"); Edit(1, "busy draft");
    for (int state = 0; state < 2; ++state) {
        g_gui.running = state == 0 ? L"busy" : L"";
        g_gui.gameUp = state == 1;
        const auto note = g_gui.note;
        Click(40); Click(145);
        ExpectDraft("8", "busy draft", true);
        CHECK(g_gui.note == note);
        CHECK(Stored(db, "count") == "int:7" && Stored(db, "label") == "string:saved");
    }
    g_gui.gameUp = false; g_gui.running.clear();
    CHECK(sqlite3_close(db) == SQLITE_OK);
    ImGui::DestroyContext();
    CHECK(DestroyWindow(g_gui.wnd)); g_gui.wnd = nullptr;
    CHECK(fs::remove_all(owned) > 0);
    std::cout << "actual launcher plugin settings: " << checks << " checks passed; no child/game command executed\n";
    return 0;
}
