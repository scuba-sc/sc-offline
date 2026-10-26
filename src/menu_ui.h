#pragma once
// The menu shell's shared look and widgets (menu.cpp), for the tabs the built-ins draw through
// sco.ui (src/builtins/*_ui.cpp). Call them only from a draw callback: on the game thread, inside
// the menu's ImGui frame.
#include "third_party/imgui/imgui.h"
#include <cstddef>

constexpr ImGuiTabBarFlags kMenuTabBarFlags = ImGuiTabBarFlags_FittingPolicyScroll;

extern const ImVec4 kMuted;      // hints
extern const ImVec4 kLeaf;       // accent
extern const ImVec4 kLeafDeep;

void  SectionHeading(const char* title);
void  Hint(const char* text);
bool  PrimaryButton(const char* label, float height = 36.0f);
float Columns(int n);             // the width of one of n equal columns in the row
bool  SearchBox(const char* id, const char* hint, char* buf, size_t n);
bool  MatchesFilter(const char* name, const char* filter);   // every word of filter, any case
// "PlayerDeco_Crate_{1234...}.socpak" -> "Crate (1234)": a game class or file name for a list.
void  PrettyBuildName(char* out, size_t n, const char* name);

// Closes the menu after this frame (a spawn or equip that should show the game).
void  MenuClose();
// Selects the first tab in the next frame (the Squadron 42 spoiler gate's Back).
void  MenuSelectFirstTab();
