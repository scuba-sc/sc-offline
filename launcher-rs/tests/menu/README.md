<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Rust menu tests

Everything here is synthetic: no game, no Star Citizen process. None of this has been run in game; see [docs/status.md](../../../docs/status.md).

| Check | Where | What it proves |
|---|---|---|
| `cargo test -p scol-menu` | `crates/scol-menu/src` | blob reader refuses bad blobs, ABI layout of the view structs, the Rust stand-in view (thread rules, key matching across a republish, coalescing, `events_done`), outbox retry, acknowledgement of local values, driver step parser; page sections, titles, summaries and developer pages equal `src/menu_navigation.h` (the test parses that header); `menu-view.ini` and `menu-window.ini` are read and written in the C++ format; number text (`30`, `0.5`, `0.125`, `1.235`); the exports refuse a bad start block and never panic |
| `examples/menu_host.rs` | `crates/scol-menu/examples` | the window runs, real egui input reaches the plugin side for every control of every page: toggle, slider (number, integer, percent), combo, text, button, list, list and table paging; the Settings page, the developer switch, window size reset and the classic entry; hide and show; PNGs |
| `tools/rust-menu-host` | C++ host, built against a Core checkout that has `include/sco/widgets_view.h` | the same click-through against sco-core's real `sco.widgets.view` implementation, with tags checked in the plugin callback |

## Panel fixture

`panels.txt` holds one state of each real page shape, in the format of `tools/panel_dump.h`: the most complete state of `spawn.vehicles`, `spawn.asop`, `crew.crew`, `npc.npcs`, `quantum.travel`, `loadout.player`, `loadout.sq42`, `build.picker` and `gear_presets.main`. The records come from the plugins' synthetic workflow test binaries run with `SCO_PANEL_DUMP=<file>` (the same route `tools/menu_preview/run.py` uses). The content is the tests' made-up data, not game data. The dump has no node tags, so the readers name node N `nN`.

To refresh it, run the workflow tests with `SCO_PANEL_DUMP` set, concatenate the files, and keep the most complete record per panel id (both readers do that selection themselves, so the whole dump also works as input).

## Run

`run.sh` builds and runs both hosts with the fixture. `expected_events.txt` lists the events both hosts must see from the hand-built demo pages, in order; the dump pages are checked by rule (right kind, node tag, value inside the range, whole number for integer presentation, row for paging).

Screenshots go to `$OUT/shots/rust` (`*.png` from the Rust stand-in) and `$OUT/shots/core` (`core-*.png` from the C++ host against real Core). Look at them: they are how clipped or overlapping content is found.

## Not covered here

The M key polling, the cursor clip, the focus hand-back to the game and the DPI behaviour need a real foreground game window and monitors. They follow the rules of `src/menu.cpp`. The list of what the maintainer must run is in [docs/rust-menu.md](../../../docs/rust-menu.md#not-run-in-game).
