# Rust in-game menu: reference for maintainers

The Rust menu is a DLL, `sc_offline_menu.dll`, built from the crate `scol-menu` in `launcher-rs/`. It opens on **M** in its own window, reads the panels that plugins publish through `sco.widgets`, draws them with egui, and sends the player's clicks back to the plugins. The C++ host (`dinput8.dll`, hooks, Core host kit) stays C++ and loads the DLL. The ImGui menu stays compiled as the fallback and as the home of `sco.ui` pages. Player text: [plugin-ui.md](plugin-ui.md#the-new-menu-opt-in). The launcher is in [rust-launcher.md](rust-launcher.md).

Evidence: source, build and synthetic tests only. Not run in game. See [status.md](status.md).

## How the new menu is enabled

The package ships two host DLLs built from the same sources.

| File in the package | Built with | Installed |
|---|---|---|
| `dinput8.dll` | `SCO_HOST_MENU_CLIENT=OFF`, the configuration the owner ran in game | by default |
| `dinput8-rust-menu.dll` | `SCO_HOST_MENU_CLIENT=ON`, with the Rust menu hook (`src/menu_rust.cpp`) | only for `menu = rust` |

The ON build also changes how the classic menu gets its pages, at about a dozen `#ifdef` sites. That has never run in game either, which is why it is not the default. The path from the ini to the screen:

1. The ini key `menu = imgui|rust` in `sc-offline.ini`. Default `imgui`. Both launchers parse it case-insensitively. A bad value keeps the old one and prints `[!] sc-offline.ini line N: menu must be imgui or rust`. The Rust window's Settings page writes it (Classic is `imgui`, New is `rust`).
2. The Rust launcher on Play picks the DLL. It installs `dinput8-rust-menu.dll` into the game's `Bin64` as `dinput8.dll` only when the ini says `menu = rust` and that file is beside the launcher. Otherwise it installs `dinput8.dll`. If the installed DLL differs in bytes from the selected source, `play` replaces it. So a change of `menu` takes effect on the next Play. The launcher does not copy `sc_offline_menu.dll` into the game folder: the hook loads it from beside the launcher (the folder above the `data` folder that holds `mod.log`), and falls back to the folder of the host DLL.
3. Both launchers export `SC_OFFLINE_MENU=imgui|rust` last in the game's environment, always, so a value from the player's shell cannot choose.
4. The hook (`sc_offline::menu_rust`, compiled only in the ON build, and only when Core has `sco/widgets_view.h`) acts only when `SC_OFFLINE_MENU` is `rust`. Otherwise `Wrap` returns the host menu client unchanged and nothing differs from the ImGui menu.
5. When the host menu client loads, the hook starts the Rust menu (below). While the Rust menu runs and is healthy, it owns M. The ImGui window does not open on M; it opens only from the Rust menu's "Classic pages" entry and closes on M.
6. `SC_OFFLINE_MENU_KEY` (a letter or `F1` to `F12`, set in Windows before starting the launcher) changes the key. Used for testing next to the ImGui menu.

Fallback. Any failure leaves the ImGui menu running and writes one `[menu]` line to `mod.log`. The lines, from `src/menu_rust.cpp`, all end with "the ImGui menu stays" unless noted:

| Cause | Line starts with |
|---|---|
| Core gives no widgets view (`HostWidgetsView` false) | `[menu] rust menu: the Core in use gives no widgets view` |
| Table size or major version is wrong | `[menu] rust menu: widgets view table has an unexpected size or version` |
| `sc_offline_menu.dll` found neither beside the launcher nor next to the host module | `[menu] rust menu: sc_offline_menu.dll not found beside the launcher or the host (<error>)` |
| An export is missing | `[menu] rust menu: sc_offline_menu.dll lacks an export` |
| ABI is not 2 | `[menu] rust menu: sc_offline_menu.dll has ABI N, this host needs 2` |
| The game-thread hook could not be installed | `[menu] rust menu: could not hook the game thread (<error>)` |
| `scol_menu_start` returned nonzero | `[menu] rust menu: start answered N` |
| `dispatch_events` faulted (the Rust menu stops, the ImGui menu takes M back) | `[menu] rust menu: dispatch_events faulted` |
| The DLL reports a fault (same hand-back) | `[menu] rust menu reports a fault` |

On success the hook logs `[menu] rust menu <version> started (SC_OFFLINE_MENU=rust); open key 0x4D; ...`. The DLL itself logs `[menu] view capabilities: ...`, and on exit `[menu] window closed` or `[menu] stopped: <reason>`. A panic on the menu thread logs `[menu] stopped: a panic in the menu thread; the menu stays off for this session`. After a stop or a fault the menu does not restart. The ImGui menu has M back for the rest of the session.

The classic launcher (`sc-offline-classic.exe`) always installs `dinput8.dll`. With `menu = rust` it therefore shows the classic menu, and the OFF build writes one `mod.log` line saying the new menu needs the Rust launcher.

## The DLL and the host

`src/menu_rust.cpp` loads `sc_offline_menu.dll` from the folder of the host module with `LoadLibraryExW` (`LOAD_WITH_ALTERED_SEARCH_PATH`) and never frees it, because its thread can outlive the client's stop. It resolves five exports and passes one start block. The block is product-private, not an SDK ABI. `StartV1` in `crates/scol-menu/src/lib.rs` and the C++ mirror must match; `kAbi` is 2.

Exports: `scol_menu_abi`, `scol_menu_version`, `scol_menu_start`, `scol_menu_request_stop`, `scol_menu_is_broken` (all used by the host). Also exported: `scol_menu_drain`, `scol_menu_is_typing`, `scol_menu_is_visible`, `scol_menu_show` and `scol_menu_test_step` (test driver, needs the test flag). No work runs in `DllMain`. Every export body runs under `catch_unwind`.

Start block: `size`, `abi`, `game_window`, `view` (the `sco_widgets_view_v1` table), `client`, `log`, `nudge_game`, `shot_dir` (tests), `flags` (1 start visible, 2 test mode), `hotkey` (virtual-key code, 0 none), `title` (`sc-offline v<version>`), `view_path` and `size_path` (full paths of `menu-view.ini` and `menu-window.ini` next to `mod.log`), and four optional callbacks: `open_classic`, `game_window_fn`, `asop_available`, `classic_visible`. Each callback only stores a request or reads an atomic. None waits for the game thread. `scol_menu_start` returns 0, or -1 bad block, -2 already started, -3 the view table was refused, -4 no thread.

Game-thread side: the hook installs a `WH_GETMESSAGE` hook on the game thread. On each message it calls `dispatch_events` through `sco::plugins::Guarded`, one pass at a time (`inPump`), after clearing the nudge flag. Every 500 ms it also asks Core whether `game.asop.selection.file` is available, for the vehicles page. `nudge_game` posts one `WM_NULL` to the game window, so a click does not wait for the game's next message. `SCO_UNAVAILABLE` from `dispatch_events` means Core is stopping and ends the pump. `Stop` never waits: it clears `running` and calls `scol_menu_request_stop`; the Rust thread sees `SCO_UNAVAILABLE`, closes its window and leaves the view itself.

## The widgets view

The Rust menu reads panels through `sco.widgets.view` 1.0, a C table in sco-core (`include/sco/widgets_view.h`; the rules are in sco-core `docs/widgets-view.md`, and the header comment is normative). It is a host-source seam, not a plugin service: `query_service` never returns it, and the host gets it from `sco::app::HostWidgetsView` for the reserved host-menu identity. The table and client are static for the life of the process. `crates/scol-menu/src/ffi.rs` holds hand-written `#[repr(C)]` mirrors with layout tests, so no bindgen or C compiler is part of a build.

What the menu relies on:

- Threads. One menu thread calls `menu_thread_enter` before anything else and `menu_thread_leave` from a drop guard on every exit path. That thread alone calls `list_panels`, `copy_panel` and `submit_event`. `dispatch_events` runs only on the game thread. `capabilities` and `stamps` work on any thread. After Core stops, every call answers `SCO_UNAVAILABLE`.
- Reading. `stamps` gives `global_revision`, `list_revision`, `event_drops` and `events_done`. `copy_panel` returns one panel as a flat blob: a header, nodes in preorder, an item table and a string table, all by offset, no pointers. The menu checks magic, version, sizes, stride, offset alignment and overlap, every offset and length, and UTF-8 before it reads a byte. A bad blob shows as a refused page. A password node arrives with empty text, so the field shows no dots for an existing value.
- Writing. `submit_event` queues one event (kind, node id, value or index or text or row, the epoch of the copy, and the node key copied from the blob). The host validates it again against the live node when `dispatch_events` runs, and takes the callback tag from the live node, never from the menu.
- Republish. Each node has a 64-bit key: kind and tag for a tagged node; kind, id, parent and label for an untagged one. If a plugin replaces its panel between copy and dispatch, the click goes to the live node with the same key. A tagged control survives a relabel or renumbering. An untagged control that is relabelled loses that click; the loss is counted in `event_drops`.
- Queue. `submit_event` returns a sequence number. A slider or text event merges into the newest queued slot for the same node. A full queue answers `SCO_TOO_MANY`, except for a merge. `events_done` reaches a sequence number when that event is finished, delivered or dropped, and the effect is in the panel revision.
- No render pass. Core does not render panels in this mode, so a plugin gets no per-draw signal. Callbacks come only from `dispatch_events`. A plugin that refreshed state only while drawn must refresh from its own events or a timer.

## Menu internals

Source files in `crates/scol-menu/src`: `lib.rs` (exports, threads, `Handle`), `ffi.rs` (C mirrors), `view.rs` (safe wrapper), `blob.rs` (blob reader and a builder for tests), `app.rs` (the egui app), `render.rs` (panel renderer), `nav.rs` (pages, sections, `menu-view.ini`, `menu-window.ini`), `theme.rs`, `win.rs` (Win32 calls), `dump.rs`, `samples.rs`, `fake.rs` (a Rust stand-in view), `script.rs` (test driver), `tests.rs`. Dependencies: `eframe` 0.37 with the glow renderer only, `egui`, `winit` 0.30, `png`, and `windows-sys` on Windows. It does not depend on any other `scol-*` crate.

Threads. `scol-menu` owns the window and every view call except `dispatch_events`. `scol-menu-keys` polls `GetAsyncKeyState` for the open key every 25 ms and toggles the window on a key-down edge, only while a window of this process is in front, the ImGui window is not up (`classic_visible`) and no text box has the keyboard. The window is an eframe window on its own thread with `any_thread` and `with_dpi_aware(false)`, so it leaves the game's DPI awareness alone.

Frame. Each frame reads `stamps`. `list_panels` runs only when `global_revision` or `list_revision` moved, and `copy_panel` only for the open page, the vehicles extra and the status panel when their revision or epoch moved. The loop repaints every 33 ms while visible and 50 ms while hidden.

Local values. While the player drags, types or toggles, egui holds the value. The app records the value, the node key and the `seq` that `submit_event` returned. A host revision does not replace that value until `events_done` reaches the `seq` and one frame passed. Then the app forces one `copy_panel` and shows the copied value. If the host dropped the event, the node snaps to the plugin's value and the status strip says "A panel changed; try again" for three seconds.

Outbox. Slider and text events for the same node merge while waiting, last value wins. When the queue is full, buttons, toggles, combos, lists and paging events wait for the next frame and are never discarded.

Pages. `nav.rs` ports `src/menu_navigation.h`: sections (Character, Travel, Ships & vehicles, Build, People, Squadron 42, Multiplayer, More plugins, Settings, Developer), the known pages with titles, summaries and ranks, and developer pages hidden unless `menu-view.ini` says `developer=1`. A test parses the C++ table and compares it with `KNOWN`, so they cannot drift. `menu-view.ini` (`[view]`, keys `page` and `developer`, at most 256 bytes) and `menu-window.ini` (size at 96 dpi) are the ImGui menu's files in the same format. The vehicles page adds "Your own ships" from `spawn.asop` when `asop_available` says so. The Settings page shows the host's `menu.main` panel (background and About settings), then "Developer pages", "Window" (Reset window size) and "Classic pages" (Open classic pages). The status strip along the bottom shows the host's `menu.status` panel. Panels flagged host-owned are not pages.

Classic pages. `sco.ui` tabs still draw in the ImGui window. The Rust sidebar has a "Classic pages" entry that hides the Rust window and sets a request the ImGui menu takes (`TakeClassicRequest`); the ImGui window closes on M. `NoteClassicWindow` tells the Rust side whether the ImGui window is up, plus 300 ms after it closes, so one M press never closes one and opens the other. Overlays (`register_overlay`) draw only in the ImGui window.

Renderer. `render.rs` ports Core's ImGui adapter rules: label column at most 40 percent of the width and at least 100 px, control width at least 140 px, capped at 560 px in a container wider than 900 px, severity written out as text, labelled ROW and COLUMN nodes as section headers, a ROW of COLUMNs shares its width, tables and lists page with "Rows a-b of n" and Previous and Next (a paging event is `SCO_WIDGET_EVENT_PAGE` with `row`), numbers shown as number, integer or integer percent, disabled state inherited from ancestors. Node tags stay in the model so tests can address nodes.

Window. No system frame: the menu draws its own title bar and resizes by dragging the outer 5 points. It is always on top, has no taskbar button, and its minimum size is 720 x 520 (96 dpi) and at most 90 percent of the work area. Size is saved when a resize ends. While the window is in front the cursor is clipped to it; the clip is released while moving or resizing, on focus loss and on hide, and always when the thread ends, whatever way. Hide (Esc when no text box has the keyboard, the open key, or the close button) gives the foreground back to the game window. Esc and the key do nothing while a text box has the keyboard. `scol_menu_is_typing` reports that state to the host.

Theme and fonts. Colours and metrics are transcribed from `ApplyTheme()` in `src/menu_shell.cpp` (soil green, leaf-green accent; body 17, heading 20, page title 24, at 96 dpi). The window is opaque, with no background picture. Fonts are read from the Windows font folder at run time: Bahnschrift, Segoe UI, Arial, Tahoma, Calibri, Segoe symbols. None is embedded. With none present the menu does not start and the ImGui menu keeps M.

Not in this build: the capability readout on the Settings page (the switches are shown in the launcher's Capabilities page), a background image, a software cursor, text-input method support, right-to-left text.

## Build and test

```
cd launcher-rs
cargo build --release -p scol-menu            # target\release\sc_offline_menu.dll
cargo test -p scol-menu
cargo run -p scol-menu --example menu_host -- [--dump <panel dump>] [--out <folder>]
CORE=<core checkout> OUT=<scratch> bash tests/menu/run.sh
```

The release profile uses `panic = unwind` (see [rust-launcher.md](rust-launcher.md#crates)). The DLL uses the static CRT.

The C++ host side builds from the top-level CMake. `-DSCO_HOST_MENU_CLIENT=ON` adds `src/menu_widgets.cpp` and `src/menu_rust.cpp` and defines `SCO_HOST_MENU_CLIENT`; `SCO_MENU_RUST` is defined only when Core has `sco/widgets_view.h` and `sco/host_menu.h`. Build the ON variant for `dinput8-rust-menu.dll` and the OFF variant for `dinput8.dll`.

| Check | What it shows |
|---|---|
| `cargo test -p scol-menu` | The blob reader refuses bad blobs. The C layout matches the header. Against the Rust stand-in view: thread rules, key matching across a republish, slider merging, `events_done`, queue-full retry, acknowledgement of local values, a stopped Core. Page sections equal `src/menu_navigation.h`. `menu-view.ini` and `menu-window.ini` use the C++ format. The exports refuse a bad start block and never panic. |
| `examples/menu_host.rs` | The window runs, and real egui input reaches the plugin side for every control of every page, and for the Settings page, the classic entry, hide and show. Saves PNGs. |
| `tools/rust-menu-host` (C++, against sco-core's real view) | The same click-through against Core's real `sco.widgets.view`, with tags checked in the plugin callback. |
| `tests/menu/panels.txt` | One state of each real page shape, from the plugins' synthetic workflow tests (`SCO_PANEL_DUMP`). Made-up data. |

`run.sh` saves screenshots under `$OUT/shots`. Look at them: they are how clipped or overlapping content is found. Sco-core has its own tests for the table (`tests/test_widgets_view.cpp`, `tests/abi_widgets_view.c`).

## What is built

- The DLL, the exports, the start block (ABI 2), the view wrapper, the blob reader, the pages and sidebar, the renderer for every widget kind, local values with acknowledgement, the outbox, the Settings page, the status strip, window size memory, the cursor clip, hide and focus hand-back, and the classic-pages handoff.
- The host hook (`src/menu_rust.cpp`) and its fallbacks.
- The view interface in sco-core, with a Rust stand-in (`fake.rs`) and a C++ host test.

## Not built

- Capability readout in the Settings page and a background picture.
- A shape B: a separate menu process over shared memory. It was the fallback if a menu inside the game could not work.
- Porting the in-tree plugins that still draw with `sco.ui` and ImGui (`src/builtins/multiplayer_ui.cpp`, `quantum_ui.cpp`, and the plugins `build`, `quantum`, `spatial`, `titanlink`, `voxel_bridge`). They keep working in the classic window. `ui_probe` stays on `sco.ui` as the test of that path.

## Not run in game

Loading the DLL in the game at all. The M handoff, the cursor clip, giving the foreground back, resize by edge, DPI and two-monitor placement, typing in a text box with game hotkeys quiet, the classic-pages handoff, overlays the owner runs next to the game, clicks across a republish on a page that republishes on a timer (the loadout pages republish from a tick and have untagged nodes), game exit with the menu open, and the ON host DLL itself. The default build (`dinput8.dll`, OFF) does not contain the menu hook.

The owner's list for the first run, each recorded as pass, fail or not run with the game build, the sc-offline build and the DLL digests:

1. M opens the menu above the game, windowed, borderless and exclusive fullscreen; Esc, M and the close button hide it and return input.
2. The cursor is clipped while open and free after hide, Alt+Tab, size and move.
3. Typing does not fire game hotkeys; M and Esc behave while typing.
4. 100, 125 and 150 percent scale and two monitors; the saved size comes back.
5. Each shipped page changes the game state it names, or says plainly that it cannot.
6. A page that republishes on a timer: press a button 20 times, then once a second for a minute; each press acts once. Write down lost presses.
7. Overlays the owner normally runs do not break the menu.
8. With `sc_offline_menu.dll` deleted, the ImGui menu stays and `mod.log` has the line.
9. A `sco.ui` plugin opens in the classic window and returns.
10. The game exits cleanly with the menu open and closed.
11. Wine or Proton, best effort.

## Decisions

- Rust scope: the menu is Rust, the host stays C++. The menu reads panels through a new C interface in sco-core, so no C++ type crosses.
- In-process DLL (shape A), not a separate process. It matches what the ImGui menu already does (own thread and window inside the game), adds no second wire format and no second executable. Reconsider only if an in-game run shows a conflict with the game or overlays, an uncontainable fault, or too much `unsafe`.
- The renderer is eframe with glow, from the launcher spike, not a custom Direct3D 11 painter. The window sets no process DPI awareness.
- Two host DLLs, chosen by the launcher from `menu`. The configuration that ran in game is the default, and the Rust menu cannot change it. Switching takes effect on the next Play.
- `menu` defaults to `imgui`. It changes only after the owner records an in-game run.
- The interface is a host-source seam (`core-source` profile, outside the SDK catalogue), not offered to plugins. A plugin that could read it could read other plugins' panels and inject clicks.
- A relabelled untagged control loses a click that races a republish, counted and shown, rather than being sent to a control that reused its id. Tag any control that must survive a republish.
- Password text never leaves Core, so the Rust field shows no dots for an existing value.
- The menu is a catch-and-stop design. A panic is caught at every export and on the menu thread; the menu stays off for the session and the ImGui menu has M back. An access violation or stack overflow inside a driver is not caught.

## Open work

- Capability readout on the Settings page: one row per switch with On or Off and "Not run in game.", and `exp_entities_diag` shown apart as a diagnostic. The host would pass a read-only copy of the switch state at start.
- Port the in-tree `sco.ui` pages to `sco.widgets` and delete the dead `src/builtins/build_ui.cpp`.
- Golden pictures through a headless renderer, and a Cyrillic check with an approved font.
- The owner's in-game run above, then the default may change.
