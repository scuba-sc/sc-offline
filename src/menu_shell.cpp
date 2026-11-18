// SPDX-License-Identifier: GPL-3.0-only
// The menu's look and page layout (menu_shell.h): shared by the product's menu (menu.cpp) and the
// offscreen preview (tools/menu_preview).
#include "menu_shell.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace Nav = sc_offline::menu_nav;

// =============================================================================================
// Look: dark Tegridy green. Soil-green behind everything, leaf green only on things you click,
// tractor yellow for warnings. An optional background image sits under a
// green wash so text stays readable.
// =============================================================================================

ImVec4 MenuHex(uint32_t rgb, float a) {
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
}

const ImVec4 kSoil      = MenuHex(0x0E1811);
const ImVec4 kPanel     = MenuHex(0x172A1B);
const ImVec4 kPanelUp   = MenuHex(0x1F3523);
const ImVec4 kLine      = MenuHex(0x2F4B32);
const ImVec4 kText      = MenuHex(0xE6EFDD);
const ImVec4 kMuted     = MenuHex(0x9FB59A);
const ImVec4 kLeaf      = MenuHex(0x86C64B);   // accent
const ImVec4 kLeafDeep  = MenuHex(0x3E6B27);

void MenuApplyTheme() {
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowPadding = ImVec2(16, 12);
    st.FramePadding = ImVec2(9, 5);
    st.ItemSpacing = ImVec2(8, 7);
    st.ItemInnerSpacing = ImVec2(6, 4);
    st.CellPadding = ImVec2(8, 4);
    st.ScrollbarSize = 11;
    st.WindowRounding = 0;
    st.ChildRounding = 4;
    st.FrameRounding = 4;
    st.PopupRounding = 4;
    st.GrabRounding = 4;
    st.TabRounding = 4;
    st.ScrollbarRounding = 4;
    st.WindowBorderSize = 0;
    st.PopupBorderSize = 1;
    st.DisabledAlpha = 0.8f;   // ImGui's 0.6 over this dark green is unreadable
    st.ChildBorderSize = 1;
    st.FrameBorderSize = 1; // outlines checkboxes, inputs and combos against the dark panels
    st.TabBorderSize = 0;
    st.SeparatorTextBorderSize = 1;
    st.SeparatorTextPadding = ImVec2(0, 6);
    st.SeparatorTextAlign = ImVec2(0, 0.5f);
    st.SelectableTextAlign = ImVec2(0, 0.5f);

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text]                 = kText;
    c[ImGuiCol_TextDisabled]         = kMuted;
    c[ImGuiCol_WindowBg]             = ImVec4(0, 0, 0, 0);          // the background is drawn underneath
    c[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]              = MenuHex(0x1C3321, 1.0f);     // lighter than the soil-green window
    c[ImGuiCol_Border]               = MenuHex(0x4F7F51);           // visible on popups and child panels
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = MenuHex(0x172A1B, 0.88f);
    c[ImGuiCol_FrameBgHovered]       = MenuHex(0x1F3523, 0.95f);
    c[ImGuiCol_FrameBgActive]        = MenuHex(0x26412A, 1.0f);
    c[ImGuiCol_TitleBg]              = MenuHex(0x0B140D, 0.95f);
    c[ImGuiCol_TitleBgActive]        = MenuHex(0x0B140D, 0.95f);
    c[ImGuiCol_TitleBgCollapsed]     = MenuHex(0x0B140D, 0.95f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]        = kLine;
    c[ImGuiCol_ScrollbarGrabHovered] = MenuHex(0x3C5E3F);
    c[ImGuiCol_ScrollbarGrabActive]  = kLeafDeep;
    c[ImGuiCol_CheckMark]            = kLeaf;
    c[ImGuiCol_CheckboxSelectedBg]   = kLeafDeep;             // checked box; ImGui's default is stock blue
    c[ImGuiCol_SliderGrab]           = kLeafDeep;
    c[ImGuiCol_SliderGrabActive]     = kLeaf;
    c[ImGuiCol_Button]               = MenuHex(0x1F3523, 0.92f);
    c[ImGuiCol_ButtonHovered]        = MenuHex(0x2A4730);
    c[ImGuiCol_ButtonActive]         = kLeafDeep;
    c[ImGuiCol_Header]               = MenuHex(0x3F7A2A, 0.90f);    // selected row: leaf green, text stays above 4.5:1
    c[ImGuiCol_HeaderHovered]        = MenuHex(0x2D5832, 1.0f);     // hovered row: clearly lighter than the panels
    c[ImGuiCol_HeaderActive]         = MenuHex(0x4F9433, 1.0f);
    c[ImGuiCol_Separator]            = kLine;
    c[ImGuiCol_SeparatorHovered]     = kLeafDeep;
    c[ImGuiCol_SeparatorActive]      = kLeaf;
    c[ImGuiCol_ResizeGrip]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Tab]                  = MenuHex(0x0B140D, 0.70f);
    c[ImGuiCol_TabHovered]           = kPanelUp;
    c[ImGuiCol_TabSelected]          = MenuHex(0x172A1B, 0.95f);
    c[ImGuiCol_TabSelectedOverline]  = kLeaf;
    c[ImGuiCol_TabDimmed]            = MenuHex(0x0B140D, 0.70f);
    c[ImGuiCol_TabDimmedSelected]    = MenuHex(0x172A1B, 0.95f);
    c[ImGuiCol_TableHeaderBg]        = MenuHex(0x172A1B, 0.95f);
    c[ImGuiCol_TableBorderStrong]    = kLine;
    c[ImGuiCol_TableBorderLight]     = MenuHex(0x2F4B32, 0.6f);
    c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = MenuHex(0x172A1B, 0.35f);
    c[ImGuiCol_TextSelectedBg]       = MenuHex(0x3E6B27, 0.7f);
    c[ImGuiCol_NavCursor]            = kLeaf;
}

void MenuLoadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    char dir[MAX_PATH] = "C:\\Windows";
    GetWindowsDirectoryA(dir, sizeof(dir));
    static const char* const kFonts[] = { "bahnschrift.ttf", "segoeui.ttf" };
    ImFont* font = nullptr;
    for (const char* name : kFonts) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s\\Fonts\\%s", dir, name);
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) continue;
        if ((font = io.Fonts->AddFontFromFileTTF(path, kBodySize)) != nullptr) break;
    }
    ImGuiStyle& st = ImGui::GetStyle();
    st.FontSizeBase = font ? kBodySize : 13.0f;
    st.FontScaleMain = font ? 1.0f : 1.3f;
}

// --- small building blocks (menu_ui.h) --------------------------------------------------------

static bool ContainsNoCase(const char* s, const char* needle, size_t n) {
    for (; *s; ++s) {
        size_t i = 0;
        while (i < n && s[i] && tolower(static_cast<unsigned char>(s[i])) == tolower(static_cast<unsigned char>(needle[i]))) ++i;
        if (i == n) return true;
    }
    return false;
}

bool MatchesFilter(const char* name, const char* filter) {
    for (const char* p = filter; *p; ) {
        p += strspn(p, " _");
        const size_t n = strcspn(p, " _");
        if (n && !ContainsNoCase(name, p, n)) return false;
        p += n;
    }
    return true;
}

void SectionHeading(const char* title) {
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::PushFont(nullptr, kHeadingSize);
    ImGui::SeparatorText(title);
    ImGui::PopFont();
}

void Hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

bool PrimaryButton(const char* label, float height) {
    ImGui::PushStyleColor(ImGuiCol_Button, kLeafDeep);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, MenuHex(0x4F8732));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kLeaf);
    const bool pressed = ImGui::Button(label, ImVec2(-1, height));
    ImGui::PopStyleColor(3);
    return pressed;
}

float Columns(int n) {
    return (ImGui::GetContentRegionAvail().x - (n - 1) * ImGui::GetStyle().ItemSpacing.x) / n;
}

bool SearchBox(const char* id, const char* hint, char* buf, size_t n) {
    ImGui::SetNextItemWidth(-1);
    return ImGui::InputTextWithHint(id, hint, buf, n);
}

void PrettyBuildName(char* out, size_t n, const char* name) {
    static const char* const kPrefixes[] = { "PlayerDeco_", "Turret_Automated_", "BaseBuilding_Interactables_", "BaseBuilding_",
                                             "PU_Human_Enemy_GroundCombat_NPC_", "PU_Human-", "NPC_Archetypes-", "AIShip_CrewProfiles-" };
    if (const char* slash = strrchr(name, '/')) name = slash + 1;
    for (const char* p : kPrefixes)
        if (_strnicmp(name, p, strlen(p)) == 0) { name += strlen(p); break; }
    strncpy_s(out, n, name, _TRUNCATE);
    if (char* ext = strstr(out, ".socpak")) *ext = 0;
    if (char* guid = strstr(out, "_{")) {
        char tag[8];
        snprintf(tag, sizeof(tag), " (%.4s)", guid + 2);
        *guid = 0;
        strncat_s(out, n, tag, _TRUNCATE);
    }
    for (char* c = out; *c; ++c) if (*c == '_' || *c == '-') *c = ' ';
}

// =============================================================================================
// The shell: sidebar of pages, the open page under its header, the status strip
// =============================================================================================

// ImGui hides what follows "##" in a label; a plugin's title must stay literal in the sidebar.
static std::string LiteralLabel(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '#' && !out.empty() && out.back() == '#') out.push_back(' ');
        out.push_back(c);
    }
    return out;
}

// The left column: every page under its section's name. Returns the page clicked, or "".
static std::string DrawSidebar(const std::vector<Nav::Page>& pages, const std::string& current, float width, float height) {
    std::string clicked;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, MenuHex(0x0B140D, 0.55f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    ImGui::BeginChild("##sidebar", ImVec2(width, height), ImGuiChildFlags_AlwaysUseWindowPadding);
    bool first = true;
    Nav::Section section{};
    for (const Nav::Page& page : pages) {
        if (first || page.section != section) {
            if (!first) ImGui::Dummy(ImVec2(0, 6));
            const std::string_view name = Nav::SectionTitle(page.section);
            ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
            ImGui::TextUnformatted(name.data(), name.data() + name.size());
            ImGui::PopStyleColor();
            section = page.section;
            first = false;
        }
        std::string label = "  " + LiteralLabel(page.title);
        if (!page.badge.empty()) label += "  (" + LiteralLabel(page.badge) + ")";
        label += "###" + page.id;
        if (ImGui::Selectable(label.c_str(), page.id == current)) clicked = page.id;
        if (!page.summary.empty()) ImGui::SetItemTooltip("%s", page.summary.c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    return clicked;
}

// The page header: its name, then one line on what it is for.
static void DrawPageHeader(const Nav::Page& page) {
    ImGui::PushFont(nullptr, kHeadingSize + 4);
    ImGui::TextUnformatted(page.title.c_str());
    ImGui::PopFont();
    if (!page.summary.empty()) Hint(page.summary.c_str());
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 2));
}

std::string MenuDrawShell(const std::vector<Nav::Page>& pages, const std::string& current, float height,
                          MenuPageFn drawPage, void* context) {
    const Nav::Page* page = nullptr;
    for (const Nav::Page& p : pages)
        if (p.id == current) page = &p;
    const float sidebarWidth = std::clamp(ImGui::GetFontSize() * 11.0f, 170.0f, 260.0f);
    const std::string clicked = DrawSidebar(pages, current, sidebarWidth, height);
    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 8));
    if (ImGui::BeginChild("##page", ImVec2(0, height), ImGuiChildFlags_AlwaysUseWindowPadding) && page) {
        DrawPageHeader(*page);
        // "##body" + the page id: each page keeps its own scroll position.
        const std::string body = "##body" + page->id;
        if (ImGui::BeginChild(body.c_str(), ImVec2(0, 0)) && drawPage) drawPage(*page, context);
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    return clicked;
}

float MenuStatusStripHeight() { return ImGui::GetTextLineHeight() * 2 + 18; }

void MenuBeginStatusStrip(float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, MenuHex(0x0B140D, 0.92f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 8));
    ImGui::BeginChild("##status", ImVec2(0, height), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    const ImVec2 p = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + 4, p.y + height), ImGui::GetColorU32(kLeaf));
}

void MenuEndStatusStrip() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}
