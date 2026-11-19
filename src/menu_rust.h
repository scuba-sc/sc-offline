// SPDX-License-Identifier: GPL-3.0-only
// Opt-in hook for the Rust in-game menu (launcher-rs/crates/scol-menu, sc_offline_menu.dll).
//
// Compiled only when the Core in use has sco/widgets_view.h and the host menu client is on
// (SCO_HOST_MENU_CLIENT); CMake then defines SCO_MENU_RUST. The ImGui menu stays the default:
// the hook does nothing unless SC_OFFLINE_MENU=rust (the launcher sets it from the ini key
// `menu = imgui|rust`). Any failure leaves the ImGui menu running and writes one mod.log line.
//
// Not run in game. Source and build checks only.
#pragma once
#if defined(SCO_HOST_MENU_CLIENT) && defined(__has_include)
#if __has_include(<sco/widgets_view.h>) && __has_include(<sco/host_menu.h>)
#define SCO_MENU_RUST 1
#include "sco/host_menu.h"
#include <windows.h>

namespace sc_offline::menu_rust {

// True when SC_OFFLINE_MENU is "rust" (case-insensitive). Anything else, or unset, is "imgui".
bool Requested();

// Wraps the host menu client descriptor. With the ImGui menu selected it returns `inner`
// unchanged, so there is no behaviour change. With "rust" selected the wrapper forwards every
// callback to `inner`, and after a successful load it loads sc_offline_menu.dll (next to this
// module), hands it the widgets view table and client, and installs a game-thread message hook
// that calls dispatch_events. The ImGui menu keeps running as the fallback for sco.ui tabs.
sco::app::HostMenuClient Wrap(const sco::app::HostMenuClient& inner);

// True while the Rust menu thread runs and has not reported a fault. While it is true the Rust menu owns
// the M key: the ImGui window does not open on M (it still closes on M when it was opened as the
// classic pages). When the Rust menu stops or reports a fault this turns false and M is the ImGui
// menu's again, with one mod.log line.
bool Active();

// The Rust menu's "Classic pages" entry asked for the ImGui window (it hides itself). Menu thread of the
// ImGui menu: returns true once per request.
bool TakeClassicRequest();

// The ImGui menu tells the Rust menu whether its window is up, so one M press never closes the classic
// window and opens the Rust one in the same breath. Menu thread of the ImGui menu.
void NoteClassicWindow(bool visible);

// The game window as found by the message hook, or null before the first game message.
HWND GameWindow();

} // namespace sc_offline::menu_rust
#endif
#endif
