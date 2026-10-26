// SPDX-License-Identifier: GPL-3.0-only
#include "menu_ui.h"
#include "menu_window.h"
#include "third_party/imgui/imgui_internal.h"
#include <cstdio>
#include <climits>
#include <cstring>

static int passed = 0, failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

static void LongTabTitles() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(480, 360);
    io.DeltaTime = 1.0f / 60;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    for (int frame = 0; frame < 3; ++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("synthetic menu", nullptr, ImGuiWindowFlags_NoSavedSettings);
        if (ImGui::BeginTabBar("tabs", kMenuTabBarFlags)) {
            const char* labels[] = {
                "Vehicles with a long translated title",
                "Crew with a long translated title",
                "Locations with a long translated title",
                "Settings with a long translated title"
            };
            for (const char* label : labels) {
                if (ImGui::BeginTabItem(label)) ImGui::EndTabItem();
            }
            if (frame == 2) {
                const ImGuiTabBar* bar = GImGui->CurrentTabBar;
                CHECK(bar->Tabs.Size == 4);
                CHECK(bar->WidthAllTabs > bar->BarRect.GetWidth());
                CHECK(bar->ScrollButtonEnabled);
                for (const auto& tab : bar->Tabs) CHECK(tab.Width + 0.1f >= tab.ContentWidth);
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
        ImGui::Render();
    }
    ImGui::DestroyContext();
}

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) return 2;
    const auto dir = std::filesystem::path(argv[1]) / "menu-window-fixture";
    std::filesystem::create_directories(dir);
    const auto file = dir / "menu-window.ini";
    using namespace sc_offline::menu_window;
    if (argc == 3) {
        if (std::strcmp(argv[2], "--restore")) return 2;
        const Size restored = Load(file);
        CHECK(restored.width == 1140 && restored.height == 720);
        std::printf("menu restore in a new process: %d passed, %d failed\n", passed, failed);
        return failed ? 1 : 0;
    }
    CHECK(Save(file, {1140, 720}));
    const Size loaded = Load(file);
    CHECK(loaded.width == 1140 && loaded.height == 720);
    const Bounds fitted = Fit({3000, 2000}, -500, 900, {0, 0, 1920, 1080});
    CHECK(fitted.left == 0 && fitted.top == 0 && fitted.right == 1920 && fitted.bottom == 1080);
    const Bounds minimum = Fit({1, 1}, 20, 30, {0, 0, 640, 480});
    CHECK(minimum.right - minimum.left == kMinWidth && minimum.bottom - minimum.top == kMinHeight);
    const Bounds tiny = Fit({1140, 720}, -300, 500, {100, 50, 220, 130});
    CHECK(tiny.left == 100 && tiny.top == 50 && tiny.right == 220 && tiny.bottom == 130);
    const Bounds secondary = Fit({1140, 720}, -300, -500, {-1920, -300, 0, 780});
    CHECK(secondary.left == -1140 && secondary.top == -300 && secondary.right == 0 && secondary.bottom == 420);
    const Bounds extreme = Fit({32768, 32768}, INT_MAX, INT_MIN, {-1000, -1000, 1000, 1000});
    CHECK(extreme.left == -1000 && extreme.top == -1000 && extreme.right == 1000 && extreme.bottom == 1000);
    CHECK(!Save(file, {-1, 720}));
    CHECK(Load(file).width == 1140 && Load(file).height == 720);
    CHECK(!Save(dir / "missing" / "settings.ini", {1140, 720}));
    CHECK(Load(dir / "missing.ini").width == 600 && Load(dir / "missing.ini").height == 900);
    const auto malformed = dir / "bad.ini";
    for (const auto& text : {
             std::string{}, std::string("[window]\nwidth=800\n"),
             std::string("[window]\nwidth=800\nheight=0\n"),
             std::string("[window]\nwidth=-800\nheight=720\n"),
             std::string("[window]\nwidth=9999999999999999999999\nheight=720\n"),
             std::string("[window]\nwidth=32769\nheight=720\n"),
             std::string("[window]\nwidth=800garbage\nheight=720\n"),
             std::string("[window]\nwidth=800\nheight=720\nwidth=900\n"),
             std::string("width=800\nheight=720\n"), std::string(257, 'x')}) {
        { std::ofstream out(malformed, std::ios::binary); out << text; }
        const Size fallback = Load(malformed);
        CHECK(fallback.width == 600 && fallback.height == 900);
    }
    { std::ofstream out(malformed, std::ios::binary); out << "\xef\xbb\xbf[window]\r\n# saved size\r\n width = 640 \r\n height = 480 \r\n"; }
    CHECK(Load(malformed).width == 640 && Load(malformed).height == 480);
    LongTabTitles();
    std::printf("menu window: %d passed, %d failed (headless; not run in game)\n", passed, failed);
    return failed ? 1 : 0;
}
