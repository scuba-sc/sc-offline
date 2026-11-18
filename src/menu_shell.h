// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The menu's look and its page layout: theme, fonts, the shared widgets of menu_ui.h, and the shell
// (sidebar of pages, page header, status strip). No window, device or game code, so the product
// (menu.cpp) and the offscreen preview (tools/menu_preview) draw the same menu. Call from inside an
// ImGui frame.
#include "menu_ui.h"
#include "menu_navigation.h"
#include <cstdint>
#include <string>
#include <vector>

ImVec4 MenuHex(uint32_t rgb, float a = 1.0f);

extern const ImVec4 kSoil;      // deepest green, under the image
extern const ImVec4 kPanel;     // inputs, lists, status strip
extern const ImVec4 kPanelUp;   // hover
extern const ImVec4 kLine;      // dividers, scrollbars
extern const ImVec4 kText;

constexpr float kBodySize    = 17.0f;
constexpr float kHeadingSize = 20.0f;

void MenuApplyTheme();
// Bahnschrift, then Segoe UI from the Windows font folder, then ImGui's own font.
void MenuLoadFonts();

// Draws the sidebar and the open page beside it, `height` tall. `drawPage` draws the body of
// `current` under its header. Returns the id of the page clicked in the sidebar, or "".
using MenuPageFn = void (*)(const sc_offline::menu_nav::Page& page, void* context);
std::string MenuDrawShell(const std::vector<sc_offline::menu_nav::Page>& pages, const std::string& current,
                          float height, MenuPageFn drawPage, void* context);

// The status strip's frame along the bottom of the window; draw its text between the two calls.
void MenuBeginStatusStrip(float height);
void MenuEndStatusStrip();
// Its height for two lines of text.
float MenuStatusStripHeight();
