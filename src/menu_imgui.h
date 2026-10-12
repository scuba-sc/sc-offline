#pragma once
// sc-offline's ImGui contract for plugins that draw a menu tab or an overlay through sco.ui
// (docs/plugin-ui.md). Header-only; a plugin includes it with sc-offline's src/ on its include path.
//
// The frame a draw callback gets is sc-offline's ImGuiContext*, valid only during that call, on the
// game thread. A plugin draws with ImGui by linking the same ImGui sc-offline ships
// (src/third_party/imgui, IMGUI_VERSION_NUM below; its imgui.cpp, imgui_draw.cpp,
// imgui_tables.cpp and imgui_widgets.cpp, no backends) and calling Bind(frame) first in every draw
// callback. Bind points the plugin's copy of ImGui at sc-offline's context and at the allocator
// sc-offline's own ImGui uses (the process heap), so either side can free its allocations.
// This optional raw-context integration is third-party ABI coupling, NOT the frozen sco_api C ABI.
// A matching version number alone is insufficient: match the exact source/configuration pin
// (tools/imgui-pin.py), ABI-affecting definitions and host build settings, then rebuild drawing
// plugins when that identity changes. Binding does not negotiate or validate context versions.
// Another ImGui version or imconfig.h is undefined behaviour; crash guards cannot guarantee
// recovery from corrupted native process state.
#include "third_party/imgui/imgui.h"
#include <windows.h>

namespace sc_offline_imgui {

constexpr int kVersionNum = IMGUI_VERSION_NUM;   // the ImGui this header was built with

inline void* Alloc(size_t size, void*) { return HeapAlloc(GetProcessHeap(), 0, size ? size : 1); }
inline void  Free(void* p, void*) { if (p) HeapFree(GetProcessHeap(), 0, p); }

// First thing in every draw callback: this module's ImGui now draws into sc-offline's frame.
inline ImGuiContext* Bind(void* frame) {
    ImGui::SetAllocatorFunctions(Alloc, Free);
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(frame));
    return static_cast<ImGuiContext*>(frame);
}

}  // namespace sc_offline_imgui
