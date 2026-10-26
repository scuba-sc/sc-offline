// SPDX-License-Identifier: GPL-3.0-only
#define wmain sco_launcher_unused_entrypoint
#include "../launcher/launcher.cpp"
#undef wmain
#include "../src/third_party/imgui/imgui_internal.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using sco::launcher::locale::Language;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)

static std::wstring Title(HWND window) {
    wchar_t text[1024]{};
    CHECK(GetWindowTextW(window, text, 1024) > 0);
    return text;
}

int wmain(int argc, wchar_t** argv) {
    CHECK(argc == 3);
    std::string catalogue;
    CHECK(sco::launcher::locale::ReadLocaleFile(argv[1], catalogue) == sco::launcher::locale::FileResult::Ready);
    fs::path owned(argv[2]);
    owned.make_preferred();
    owned /= L"gui-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    CHECK(fs::create_directories(owned));
    g_gui.here = owned.wstring();
    g_gui.dpi = 1.0f;
    g_gui.wnd = CreateWindowExW(0, L"STATIC", L"owned hidden locale fixture", WS_OVERLAPPED,
                               0, 0, 800, 800, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    CHECK(g_gui.wnd != nullptr && !IsWindowVisible(g_gui.wnd));
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(1024, 2048);
    io.DeltaTime = 1.0f / 60;
    LoadFonts();
    StyleUi();
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    CHECK(pixels && width > 0 && height > 0);

    // The real startup admission must refresh the hidden native title before ShowWindow.
    for (const auto language : {Language::Russian, Language::Auto}) {
        g_gui.locale = sco::launcher::locale::Localizer(true);
        CHECK(g_gui.locale.LoadRussian(catalogue));
        g_gui.locale.Select(language);
        g_gui.localeGlyphsChecked = false;
        RefreshWindowTitle();
        const auto english = Title(g_gui.wnd);
        ImGui::NewFrame();
        AdmitLocaleGlyphs();
        CHECK(g_gui.locale.RussianActive());
        CHECK(Title(g_gui.wnd) == Wide(F(LocaleKey::WindowTitle, {{"version", SCO_VERSION}})));
        CHECK(Title(g_gui.wnd) != english && !IsWindowVisible(g_gui.wnd));
        ImGui::EndFrame();
    }

    const char* controls[] = {"Play", "Status", "Update", "Install", "Uninstall", "Play##act",
        "Yes", "No", "Reload##plug", "Turn on##plugson", "Open folder##plugempty",
        "Back##plugback", "Save##plugsave", "Reset##plugreset", "Copy"};
    for (const auto language : {Language::English, Language::Russian, Language::Auto}) {
        g_gui.locale.Select(language);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("actual launcher controls", nullptr, ImGuiWindowFlags_NoSavedSettings);
        float y = 10;
        for (const auto* id : controls) {
            const auto expected = ImGui::GetID(id);
            CHECK(!UiButton(id, ImVec2(10, y), ImVec2(180, 30), kPlain, true, -1, T(LocaleKey::Play)));
            CHECK(GImGui->LastItemData.ID == expected);
            y += 36;
        }
        ImGui::End();
        ImGui::Render();
        CHECK(ImGui::GetDrawData()->TotalVtxCount > 0);
    }

    g_gui.tail.clear();
    g_gui.question.clear();
    AppendOutput(L"translated parent [y/N] ", false);
    CHECK(g_gui.tail.empty() && g_gui.question.empty());
    AppendOutput(L"real child question [y/N] ");
    CHECK(g_gui.question == "real child question");
    const auto tail = g_gui.tail;
    AppendOutput(L"\nparent notice [y/N] ", false);
    CHECK(g_gui.tail == tail && g_gui.question == "real child question");
    AppendOutput(L"\nchild reply continues\n");
    CHECK(g_gui.question.empty());

    g_gui.locale.Select(Language::English);
    CHECK(SelectLanguage(Language::Russian));
    CHECK(g_gui.locale.Choice() == Language::Russian);
    Config config;
    CHECK(ReadConfig(g_gui.here + L"\\sc-offline.ini", config));
    CHECK(config.language == Language::Russian);
    const auto original = Title(g_gui.wnd);
    g_gui.running = L"fixture busy marker";
    CHECK(!SelectLanguage(Language::English));
    CHECK(g_gui.locale.Choice() == Language::Russian && Title(g_gui.wnd) == original);
    g_gui.running.clear();
    g_gui.gameUp = true;
    CHECK(!SelectLanguage(Language::English));
    CHECK(g_gui.locale.Choice() == Language::Russian);
    g_gui.gameUp = false;
    g_gui.here = (owned / L"missing").wstring();
    CHECK(!SelectLanguage(Language::English));
    CHECK(g_gui.locale.Choice() == Language::Russian && Title(g_gui.wnd) == original);
    CHECK(g_gui.note == T(LocaleKey::LanguageWriteError));

    ImGui::DestroyContext();
    CHECK(DestroyWindow(g_gui.wnd));
    g_gui.wnd = nullptr;
    CHECK(fs::remove_all(owned) > 0);
    std::cout << "actual launcher locale: " << checks << " checks passed; no child/game command executed\n";
    return 0;
}
