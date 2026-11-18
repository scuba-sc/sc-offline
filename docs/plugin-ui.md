# Plugin UI: menu tabs, overlays and hotkeys

sc-offline's menu (**M**) is a shell over sco-core's `sco.ui` service ([sco-core docs/ui.md](https://github.com/scuba-sc/sco-core/blob/main/docs/ui.md)). The host registers no feature tabs. Feature panels (Vehicles, Crew, NPCs, Player, Squadron 42, Build and the inspector) come from the external plugins through `sco.widgets` and are drawn by the host's ImGui adapter. A plugin can also add its own tab, overlay and hotkeys through `sco.ui`. This page covers what sc-offline adds to sco-core's `sco.ui` contract.

## Pages and the sidebar

The menu is a sidebar of pages on the left and the open page on the right, with the status strip along the bottom. A page is a `sco.widgets` panel (what every shipped plugin uses), a `sco.ui` tab (ImGui drawing, below), or the menu's own **Menu settings**. Each page opens under a header: its name, then one line that says what it is for.

[`src/menu_navigation.h`](../src/menu_navigation.h) decides where a page goes, by its id only (a title can't move a page). The pages sc-offline ships, in the order a player sees them:

| Section | Page | Id |
| --- | --- | --- |
| Character | Gear | `loadout.player` |
| | Saved outfits | `gear_presets.main` |
| Travel | Travel | `quantum.travel` |
| | Saved spots | `teleport.travel` |
| Ships & vehicles | Ships & vehicles (with **Your own ships**, `spawn.asop`, under it) | `spawn.vehicles` |
| Build | Build | `build.picker` |
| | Creative | `creative.panel` |
| People | Crew | `crew.crew` |
| | Characters | `npc.npcs` |
| Squadron 42 | Squadron 42 | `loadout.sq42` |
| Multiplayer | Multiplayer | `multiplayer.session` |
| More plugins | any page sc-offline doesn't know, under the plugin's own title | |
| More plugins | Titanfall link, Voxel link (optional bridges, off by default) | `titanlink.titanfall`, `voxel_bridge.voxel` |
| Settings | Menu settings | (the menu's own) |
| Developer | Inspector, UI probe (test plugin) | `inspector.main`, `ui_probe.main` |

Pages in **Developer** appear only when **Menu settings > Show developer pages** is on. The open page and that switch are remembered in `menu-view.ini`, next to `mod.log`. A badge appears after a page's name, as in `UI probe (3)`. A page sc-offline doesn't know goes under **More plugins**, after the known ones; its header line says which plugin added it. Unknown pages keep the order the host lists them in: `sco.ui` tabs by their `order`, then widget panels. Known pages always take the place the table above gives them, whatever their `order`.

`teleport` (F7/F8), `contracts` and `mining` run mostly from keys and commands. `inspector` is read-only and for developers.

## Writing a page for players

Players open this menu in the middle of a game. Every page has to make sense to someone who has never seen the source, the SDK or the logs. These rules apply to every visible string: panel titles, labels, buttons, list rows, hints and status lines. Logs and command names keep their technical names.

1. **Say what it does, in the player's words.** "Spawn ship", "Get in", "Remove all my ships". Buttons start with a verb. Sentence case.
2. **No internals on screen.** No ids or command names (`build.place`, `spawn.ship_options`), capability names (`game.inventory.read`), versions (`(1.2)`, `requires loadout 1.1`), gate or task numbers (`G2`, `S1`), or words from the code: archetype, owned, ticket, session ticket, phase, catalog, pagination, payload, slot id, job, retained, preview, orchestration, SDK, adapter, GameRules. Show a game class name only where there is nothing better. Then clean it for reading (`PrettyBuildName`); the raw name can go in a tooltip on a page that submits 1.1 nodes.
3. **No button that can only fail.** A control that can never work on this build is left out. A control that works only sometimes is shown disabled (`SCO_WIDGET_DISABLED`), with the reason in one plain sentence ("Choose a ship first.", "Ready to fly needs the pilot seat."): in its tooltip on a page that submits 1.1 nodes, otherwise as a label line right next to it. The shipped pages submit 1.0 nodes and use the label line.
4. **Group the page.** Put related controls in a labelled `COLUMN` (it renders as a section heading), in the order the player uses them: choose, adjust, do, then manage what was made. Put buttons that belong together in one `ROW`. Keep the main action next to what it acts on.
5. **Results speak plainly.** A status line says what happened and what to do next ("Spawned a Cutlass Black 30 m ahead. Press F to get in."). A refusal says why, without the internal reason code. On a page that submits 1.1 nodes, give problems severity `WARNING` or `ERROR`, so they don't read like normal text. Keep two texts for every result: the technical one for the command reply and `mod.log`, and the plain one for the page.
6. **Lists the player chooses from** show readable names, with a search box above them when they are long.

Three checks keep this true:

- `tools/check_menu_wording.py` (CI) reads the string literals in the page sources for the internal words above. A technical string that never reaches a page goes in its allow-list, with the reason.
- The plugins' workflow tests scan the words of every panel they receive, which covers the result sentences built at run time.
- Every fixture that receives panels checks them against the host's tree rules ([`tools/panel_dump.h`](../tools/panel_dump.h)). The host refuses a tree that breaks them, and the page then disappears in game.

To look at the pages without the game, run the workflow tests with `SCO_PANEL_DUMP=<file>`, then `python tools/menu_preview/run.py --dump <file> --out <folder>` (Windows). It draws every page with the menu's own shell, theme and widget renderer into PNG files. The content is the tests' synthetic data, so it is not a screenshot of the game.

## Drawing: ImGui through sc-offline's frame

A draw callback gets `frame`, which is sc-offline's `ImGuiContext*`. It is valid **only during that call** and only on the game thread, the only thread sco-core runs draws on. The callback draws the page's body inside the menu window, below the page header and above the status strip, and the page's scrolling child is already open. Overlays draw after the menu window, once per menu frame.

ImGui is **optional for plugins**: commands, events, settings and hotkeys work without linking it. A plugin that cannot establish this host-specific drawing contract must skip tabs/overlays and keep its non-UI features working. The frozen `sco_api.h` 1.1 C ABI and `sco.ui` table cover callout registration and layout, **not** the memory behind `frame`. Sharing an `ImGuiContext*` is **third-party ABI coupling**, not compatibility across arbitrary ImGui versions.

A plugin that draws with ImGui has to use **the exact ImGui source/configuration as sc-offline**:

1. Compile sc-offline's vendored ImGui into the plugin: `src/third_party/imgui/` at the sc-offline version you target (`imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`, with its `imconfig.h` unchanged and no backends). This branch ships ImGui 1.92.8 WIP (`IMGUI_VERSION_NUM` 19277).
2. Include [`src/menu_imgui.h`](../src/menu_imgui.h), and call `sc_offline_imgui::Bind(frame)` first in every draw callback. It points the plugin's copy of ImGui at sc-offline's context and at the allocator sc-offline's ImGui uses (the process heap), so either side can free memory the other allocated, whichever C runtime each one links.
3. Use plain ImGui calls after that. Don't keep the context or any ImGui pointer past the call, and don't call `NewFrame`, `Render` or a backend.

```cpp
#include "sco_ui.h"
#include "menu_imgui.h"

static void DrawMyTab(void* frame, void*) {
    sc_offline_imgui::Bind(frame);
    ImGui::SeparatorText("My plugin");
    if (ImGui::Button("Wave")) { /* invoke a command */ }
}
// in sco_plugin_load: ui->register_tab(self, "myplugin.main", "My plugin", 900, DrawMyTab, NULL);
```

[`tools/test-plugins/ui_probe`](../tools/test-plugins/ui_probe/ui_probe.cpp) is a complete example; `CMakeLists.txt` builds it against the `imgui` library target. A different ImGui version or `imconfig.h` is undefined behaviour.

We chose direct ImGui calls rather than a C drawing shim, so a plugin draws with the same widgets as the host. This is a deliberately version-locked product contract: Dear ImGui does not guarantee backward or forward ABI compatibility. Context/allocator binding does not negotiate versions, detect incompatible layouts or make arbitrary DLL builds safe. Rebuild drawing plugins when the host's ImGui snapshot, configuration or ABI-affecting build settings change; do not rely on crash guards to recover from corrupted native state.

The immutable host source pin is `1ee0727cc63866a95e709ae61dd5dd355b511253`. [`tools/imgui-pin.py`](../tools/imgui-pin.py) pins SHA-256 for all 15 vendored implementation, backend, configuration, header and licence inputs, and separately compares the three public SDK files with the pinned sco-core submodule. The exact bytes—not just **1.92.8 WIP** / **19277**—must match. CI runs the pin gate and [`tools/test_imgui_pin.py`](../tools/test_imgui_pin.py), which rejects same-label source drift, matching drift on both sides, SDK-only drift and missing inputs. Intentional upgrades must hand-edit the pin and provenance together; there is no auto-update mode. The implementation/private headers are not added to the public SDK. These source checks are not game-runtime or compiler-configuration certification.

**Crashes.** Each draw runs as a callout of its plugin under sco-core's crash guard. When a draw faults, sco-core disables that plugin and removes its tabs, badges, overlays and hotkeys. Around every draw, the menu stores ImGui's stack state and restores it afterwards (`ErrorRecoveryStoreState`), so a draw that faulted half-way, or forgot an `End*()`, doesn't break the rest of the frame. `mod.log` records `[menu] tab <id> (<title>) crashed; plugin '<id>' is disabled`, with sco-core's own crash line next to it.

**Where the frame is built.** The menu window and its Direct3D device live on the menu's own thread. Because `sco.ui` draws run on the game thread only, the game thread builds each ImGui frame: `NewFrame`, the shell, every tab and `Render`. The menu thread then renders it, and the two threads never use ImGui at the same time. While the game thread is busy (a loading screen), the menu keeps showing its last picture.

**Overlays.** sc-offline has no drawing surface over the game itself, only the menu window. Overlays therefore draw over the menu while it is open, not over the game while it is closed.

## Hotkeys

When a key is pressed while the game or the menu is in front (and the menu isn't typing into a text box), sc-offline builds the chord (`ctrl+alt+9`, `f6`). If the chord is bound, it runs the bound command through `sco::ui::Dispatch`, and `mod.log` shows `[hotkey] Ctrl+Alt+9 -> ui_probe.ping: OK "ui_probe ping 1"`. A plugin binds any free chord with `bind_hotkey`. The first plugin to bind a chord keeps it, and a later bind is refused with the holder's name.

| Key | Does | How |
| --- | --- | --- |
| **M** | Opens and closes the menu | Reserved by sc-offline |
| **F6** | `build.toggle` | Bound by the `build` plugin |
| **F7** | `teleport.save` | Bound by the `teleport` plugin |
| **F8** | `teleport.go` | Bound by the `teleport` plugin |
| **R**, **[**, **]**, **Backspace** | Build mode: rotate, reach closer, reach farther, undo | Reserved by sc-offline (used only while build mode is on) |
| **Caps Lock** (hold) | Quantum boost while held | Read by the quantum plugin. It is a held key, not a chord, and the chord grammar has no Caps Lock |
| Left mouse button | Build mode: place | Read by build mode, not a chord |

Binding a reserved chord is refused with `<chord> is reserved by the host`.
