// SPDX-License-Identifier: GPL-3.0-only
#include "../launcher/localization_ui.h"
#include "../src/third_party/imgui/imgui_internal.h"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <limits>

using namespace sco::launcher::locale;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)

int main(int argc, char** argv) {
    CHECK(argc == 3);
    std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
    CHECK(input.good());
    const auto length = input.tellg();
    CHECK(length >= 0 && length <= static_cast<std::streamoff>(kCatalogueLimit));
    std::string bytes(static_cast<size_t>(length), '\0');
    input.seekg(0);
    CHECK(bool(input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))));
    Catalogue russian;
    CHECK(russian.Load(bytes) && russian.Diagnostic().missing.empty());
    Localizer locale(true);
    CHECK(locale.LoadRussian(bytes));

    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(720, 2048);
    io.DeltaTime = 1.0f / 60;
    ImFontGlyphRangesBuilder builder;
    for (size_t key = 0; key < kMessageCount; ++key) {
        builder.AddText(kMessages[key].english);
        builder.AddText(russian.Text(static_cast<Key>(key)));
    }
    ImVector<ImWchar> ranges;
    builder.BuildRanges(&ranges);
    ImFont* font = argv[2][0] ? io.Fonts->AddFontFromFileTTF(argv[2], 15.0f, nullptr, ranges.Data) : io.Fonts->AddFontDefault();
    CHECK(font != nullptr);
    io.FontDefault = font;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    CHECK(pixels != nullptr && width > 0 && height > 0);
    const bool covered = CatalogueGlyphs(russian, font);
    CHECK(!CatalogueGlyphs(russian, nullptr));
    if (argv[2][0]) CHECK(covered);
    else CHECK(!covered);
    locale.SetGlyphCoverage(covered);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("headless locale semantics", nullptr, ImGuiWindowFlags_NoSavedSettings);
    struct Control { const char* id; Key key; };
    const Control controls[] = {
        {"Play", Key::Play}, {"Status", Key::Status}, {"Update", Key::Update},
        {"Install", Key::Install}, {"Uninstall", Key::Uninstall},
        {"Play##act", Key::Play}, {"Status##act", Key::Status}, {"Update##act", Key::Update},
        {"Install##act", Key::Install}, {"Uninstall##act", Key::Uninstall},
        {"Yes", Key::Yes}, {"No", Key::No}, {"Reload##plug", Key::Reload},
        {"Turn on##plugson", Key::TurnOn}, {"Open folder##plugempty", Key::OpenFolder},
        {"Back##plugback", Key::Back}, {"Save##plugsave", Key::Save},
        {"Reset##plugreset", Key::Reset}, {"Copy", Key::Copy}
    };
    for (const auto language : {Language::English, Language::Russian, Language::Auto}) {
        locale.Select(language);
        for (const auto& control : controls) {
            const auto expected = ImGui::GetID(control.id);
            ImGui::InvisibleButton(control.id, ImVec2(180, 24));
            CHECK(GImGui->LastItemData.ID == expected);
            const auto position = ImGui::GetItemRectMin();
            const auto text = FitText(locale.Text(control.key), 172, false);
            ImGui::GetWindowDrawList()->AddText(position, IM_COL32_WHITE, text.c_str());
            CHECK(GImGui->LastItemData.ID == expected);
        }
        CHECK(ImGui::GetID((std::string(locale.Text(Key::LanguageAuto)) + "###language-auto").c_str()) ==
              ImGui::GetID("Auto###language-auto"));
    }
    CHECK(ImGui::GetID("Yes") != ImGui::GetID("Да"));
    const auto original = ImGui::GetID("Play");
    ImGui::PushID("Play");
    CHECK(ImGui::GetID("Играть") != original);
    ImGui::PopID();
    for (size_t key = 0; key < kMessageCount; ++key) {
        for (const float limit : {0.0f, 1.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f, 256.0f}) {
            for (const bool left : {false, true}) {
                const auto fitted = FitText(russian.Text(static_cast<Key>(key)), limit, left);
                CHECK(ValidUtf8(fitted, true));
                CHECK(ImGui::CalcTextSize(fitted.c_str()).x <= limit + 0.1f);
            }
        }
    }
    CHECK(FitText("a", std::numeric_limits<float>::quiet_NaN(), false).empty());
    CHECK(FitText("a", std::numeric_limits<float>::infinity(), false).empty());
    CHECK(FitText("\xC0\xAF", 100, false).empty());
    CHECK(FitText("abc", -1, false).empty());
    ImGui::End();
    ImGui::Render();
    CHECK(ImGui::GetDrawData()->TotalVtxCount > 0);
    ImGui::DestroyContext();
    std::cout << "launcher locale ImGui: " << checks << " checks passed; catalogue glyph coverage=" << covered
              << "; system font=" << bool(argv[2][0]) << " (headless)\n";
}
