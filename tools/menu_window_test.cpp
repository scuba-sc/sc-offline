// SPDX-License-Identifier: GPL-3.0-only
#include "menu_ui.h"
#include "menu_window.h"
#include "third_party/imgui/imgui_internal.h"
#include <cstdio>
#include <climits>
#include <cstring>
#include <cstdlib>
#include <cmath>

static int passed = 0, failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

static void EscapeRouting() {
    using sc_offline::menu_window::EscapeInput;
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    EscapeInput input;
    int closes = 0, forwarded = 0;
    bool focused = true;
    auto key = [&](bool escape, bool down, bool repeated = false) {
        if (!input.Key(escape, down, repeated, focused, [&] { ++closes; })) {
            ++forwarded;
            io.AddKeyEvent(escape ? ImGuiKey_Escape : ImGuiKey_A, down);
        }
    };
    auto character = [&](bool escape) {
        if (!input.Character(escape)) { ++forwarded; io.AddInputCharacter(escape ? 27 : 'a'); }
    };
    input.visible = true;
    key(true, true); key(true, true, true); character(true);
    CHECK(input.pending && input.visible && closes == 0 && forwarded == 0);
    CHECK(GImGui->InputEventsQueue.Size == 0);
    key(true, false);
    CHECK(!input.visible && !input.pending && closes == 1);
    character(true); // Already queued by TranslateMessage before dispatch.
    CHECK(forwarded == 0 && GImGui->InputEventsQueue.Size == 0);
    input.visible = true; // Reopen: the same visibility state drives the loop.
    key(true, false);
    CHECK(input.visible && closes == 1 && !input.pending);
    key(false, true); character(false);
    CHECK(forwarded == 2 && GImGui->InputEventsQueue.Size == 2);
    key(true, true);
    input.Reset(); // WM_KILLFOCUS
    focused = false; key(true, false);
    CHECK(input.visible && closes == 1 && !input.pending);
    focused = true; key(true, true, true); key(true, false);
    CHECK(input.visible && closes == 1 && !input.pending);
    key(true, true); input.visible = false; input.Reset(); // M/frame hide.
    input.visible = true; key(true, true, true); key(true, false);
    CHECK(input.visible && closes == 1 && !input.pending);
    key(true, true); key(true, false);
    CHECK(!input.visible && closes == 2);
    ImGui::DestroyContext();
}

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

// The window as the menu thread sees it: an outer rect (what GetWindowRect returns and SetWindowPos
// takes) and a frame that only changes the client area. The loop in Sizing mirrors WM_EXITSIZEMOVE
// and ShowMenu in src/menu.cpp.
struct SimWindow {
    int frame;
    sc_offline::menu_window::Size outer;
    sc_offline::menu_window::Size Client() const { return {outer.width - 2 * frame, outer.height - 2 * frame}; }
};

static void Sizing() {
    using namespace sc_offline::menu_window;
    const auto file = std::filesystem::temp_directory_path() / "menu-window-sizing.ini";
    const int dpis[] = {96, 120, 144, 192};
    const int frames[] = {0, 8, 16};
    for (const int dpi : dpis) {
        const double scale = dpi / 96.0;
        const Bounds work{0, 0, static_cast<int>(std::lround(1920 * scale)), static_cast<int>(std::lround(1080 * scale))};
        const Size normal{static_cast<int>(std::lround(1090 * scale)), static_cast<int>(std::lround(890 * scale))};
        const Size starts[] = {normal, {200, 100}, {20000, 20000}};   // normal, below the minimum, beyond the work area
        for (const int frame : frames) for (const Size start : starts) {
            SimWindow window{frame, start};
            Size firstOuter{}, firstSaved{};
            for (int i = 0; i < 50; ++i) {
                // WM_EXITSIZEMOVE: the outer size is remembered at 96 dpi and written.
                CHECK(Save(file, Remember(window.outer, dpi, work)));
                // The menu is hidden and shown: the file is read and the size restored.
                const Size saved = Load(file);
                const Bounds shown = Fit(Restore(saved, dpi, work), 60, 60, work, dpi);
                window.outer = {shown.right - shown.left, shown.bottom - shown.top};
                if (i == 0) {
                    firstOuter = window.outer;
                    firstSaved = saved;
                    if (start.width == normal.width)   // a normal size survives to within rounding
                        CHECK(std::abs(window.outer.width - start.width) <= 2 && std::abs(window.outer.height - start.height) <= 2);
                }
                CHECK(window.outer.width == firstOuter.width && window.outer.height == firstOuter.height);
                CHECK(saved.width == firstSaved.width && saved.height == firstSaved.height);
                CHECK(window.Client().width == firstOuter.width - 2 * frame);   // the frame never feeds back
            }
            CHECK(window.outer.width <= work.right * kMaxPercent / 100 && window.outer.height <= work.bottom * kMaxPercent / 100);
            CHECK(window.outer.width >= ScaleDpi(kMinWidth, kBaseDpi, dpi) && window.outer.height >= ScaleDpi(kMinHeight, kBaseDpi, dpi));
        }
    }
    // The same saved size is the same logical window on monitors of different scale.
    const Bounds w96{0, 0, 1920, 1080}, w144{0, 0, 2880, 1620};
    const Size at144 = Remember({1500, 1200}, 144, w144);
    CHECK(at144.width == 1000 && at144.height == 800);
    const Size at96 = Restore(at144, 96, w96);
    CHECK(at96.width == 1000 && at96.height == 800);
    // Out-of-range saved values are clamped to the monitor, never trusted.
    const Size old = Restore({2144, 1297}, 96, w96);
    CHECK(old.width == 1728 && old.height == 972);
    const Size huge = Restore({8192, 8192}, 96, {0, 0, 1280, 720});
    CHECK(huge.width == 1152 && huge.height == 648);
    const Size small = Restore({kMinWidth, kMinHeight}, 96, {0, 0, 640, 480});
    CHECK(small.width == 576 && small.height == 432);   // the minimum yields to a small work area
    // Not a size this menu wrote: the default, which fits.
    for (const Size bad : {Size{100, 100}, Size{9000, 800}, Size{-5, 800}, Size{0, 0}, Size{40000, 40000}}) {
        const Size restored = Restore(bad, 96, w96);
        CHECK(restored.width == Size{}.width && restored.height == Size{}.height);
        CHECK(!Save(file, bad));
    }
    { std::ofstream out(file, std::ios::binary); out << "[window]\nwidth=2144\nheight=1297\n"; }
    CHECK(Load(file).width == 2144 && Load(file).height == 1297);   // readable; Restore clamps it
    std::filesystem::remove(file);
    CHECK(Load(file).width == Size{}.width && Load(file).height == Size{}.height);   // absent: the default
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
    const Bounds fitted = Fit({3000, 2000}, -500, 900, {0, 0, 1920, 1080});   // at most 90% of the work area
    CHECK(fitted.left == 0 && fitted.top == 108 && fitted.right == 1728 && fitted.bottom == 1080);
    const Bounds minimum = Fit({1, 1}, 20, 30, {0, 0, 1920, 1080});
    CHECK(minimum.right - minimum.left == kMinWidth && minimum.bottom - minimum.top == kMinHeight);
    const Bounds tiny = Fit({1140, 720}, -300, 500, {100, 50, 220, 130});
    CHECK(tiny.left == 100 && tiny.top == 58 && tiny.right == 208 && tiny.bottom == 130);
    const Bounds secondary = Fit({1140, 720}, -300, -500, {-1920, -300, 0, 780});
    CHECK(secondary.left == -1140 && secondary.top == -300 && secondary.right == 0 && secondary.bottom == 420);
    const Bounds extreme = Fit({32768, 32768}, INT_MAX, INT_MIN, {-1000, -1000, 1000, 1000});
    CHECK(extreme.left == -800 && extreme.top == -1000 && extreme.right == 1000 && extreme.bottom == 800);
    CHECK(!Save(file, {-1, 720}));
    CHECK(Load(file).width == 1140 && Load(file).height == 720);
    CHECK(!Save(dir / "missing" / "settings.ini", {1140, 720}));
    CHECK(Load(dir / "missing.ini").width == Size{}.width && Load(dir / "missing.ini").height == Size{}.height);
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
        CHECK(fallback.width == Size{}.width && fallback.height == Size{}.height);
    }
    { std::ofstream out(malformed, std::ios::binary); out << "\xef\xbb\xbf[window]\r\n# saved size\r\n width = 800 \r\n height = 600 \r\n"; }
    CHECK(Load(malformed).width == 800 && Load(malformed).height == 600);
    for (const char* text : {"[window]\nwidth=100\nheight=100\n", "[window]\nwidth=9000\nheight=800\n"}) {   // not a size the menu wrote
        { std::ofstream out(malformed, std::ios::binary); out << text; }
        CHECK(Load(malformed).width == Size{}.width && Load(malformed).height == Size{}.height);
    }
    Sizing();
    EscapeRouting();
    LongTabTitles();
    std::printf("menu window: %d passed, %d failed (headless; not run in game)\n", passed, failed);
    return failed ? 1 : 0;
}
