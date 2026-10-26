// SPDX-License-Identifier: GPL-3.0-only
// Synthetic ImGui input, using the product's vendored renderer without a graphics backend.
#include "sco/widgets_imgui.h"
#include "imgui.h"
#include <cstdio>
#include <vector>

namespace {
sco::widgets::Panel panel;
sco::widgets::ImGuiRenderer renderer(false);
ImVec2 itemMin;
int events = 0;

void Frame() {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_Always);
    ImGui::Begin("numeric test", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetNextItemWidth(250);
    std::vector<sco::widgets::Event> emitted;
    renderer.Render(panel, emitted);
    itemMin = ImGui::GetItemRectMin();
    for (const auto& event : emitted) {
        panel.nodes[0].value = event.value;
        ++events;
    }
    ImGui::End();
    ImGui::Render();
}

bool Typed(const char* text, double expected, bool moveCursor = false) {
    auto& io = ImGui::GetIO();
    const int before = events;
    const float x = itemMin.x + 60;
    const float y = itemMin.y + ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.y + 6;
    io.AddMousePosEvent(x, y); Frame();
    io.AddKeyEvent(ImGuiKey_LeftCtrl, true); io.AddKeyEvent(ImGuiMod_Ctrl, true); Frame();
    io.AddMouseButtonEvent(0, true); Frame();
    io.AddMouseButtonEvent(0, false); Frame();
    io.AddKeyEvent(ImGuiKey_A, true); Frame();
    io.AddKeyEvent(ImGuiKey_A, false); io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false); Frame();
    io.AddInputCharactersUTF8(text); Frame();
    if (moveCursor) {
        io.AddKeyEvent(ImGuiKey_LeftArrow, true); Frame();
        io.AddKeyEvent(ImGuiKey_LeftArrow, false); Frame();
        io.AddInputCharactersUTF8("2"); Frame();
    }
    io.AddKeyEvent(ImGuiKey_Enter, true); Frame();
    io.AddKeyEvent(ImGuiKey_Enter, false); Frame();
    const bool ok = events > before && panel.nodes[0].value == expected;
    if (!ok) std::printf("typed %s: expected %.0f, got %.17g (%d events)\n",
                         text, expected, panel.nodes[0].value, events - before);
    return ok;
}
}

int main() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigNavCursorVisibleAlways = true;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigMacOSXBehaviors = false; // Exercise the Windows product's Ctrl-click behavior.
    io.DisplaySize = ImVec2(800, 600);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    panel.id = "numeric.main";
    sco::widgets::Node node;
    node.id = 1; node.kind = SCO_WIDGET_SLIDER; node.label = "Darkness";
    node.minimum = -100; node.maximum = 100; node.value = 74;
    node.presentation = SCO_WIDGET_NUMERIC_INTEGER_PERCENT;
    panel.nodes.push_back(node);
    Frame(); Frame();
    const bool positive = Typed("74.5", 75);
    const bool negative = Typed("-0.5", -1);
    const bool maximum = Typed("300", 100);
    const bool minimum = Typed("-300", -100);
    const bool cursor = Typed("74.5", 74, true);
    panel.nodes[0].value = 0;
    Frame(); Frame();
    io.AddKeyEvent(ImGuiKey_Space, true); Frame();
    io.AddKeyEvent(ImGuiKey_Space, false); Frame();
    io.AddKeyEvent(ImGuiKey_RightArrow, true); Frame();
    io.AddKeyEvent(ImGuiKey_RightArrow, false); Frame();
    const bool oneStep = panel.nodes[0].value == 1;
    if (!oneStep) std::printf("right arrow: expected 1, got %.17g\n", panel.nodes[0].value);
    io.AddKeyEvent(ImGuiKey_Space, true); Frame();
    io.AddKeyEvent(ImGuiKey_Space, false); Frame();
    panel.nodes[0].value = 0;
    io.AddMousePosEvent(itemMin.x + 230, itemMin.y + ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.y + 6); Frame();
    io.AddMouseButtonEvent(0, true); Frame();
    const bool mouseRange = panel.nodes[0].value > 1;
    io.AddMouseButtonEvent(0, false); Frame();
    if (!mouseRange) std::printf("mouse slider: expected movement over one, got %.17g\n", panel.nodes[0].value);
    ImGui::DestroyContext();
    if (!(positive && negative && maximum && minimum && cursor && oneStep && mouseRange)) return 1;
    std::puts("real ImGui numeric entry, keyboard step and mouse input passed");
    return 0;
}
