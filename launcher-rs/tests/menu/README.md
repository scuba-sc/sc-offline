<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Rust menu tests

Everything here is synthetic: no game, no Star Citizen process. Nothing is claimed to work in game.

| Check | Where | What it proves |
|---|---|---|
| `cargo test -p scol-menu` | `crates/scol-menu/src` | blob reader refuses bad blobs, ABI layout of the view structs, the Rust stand-in view (thread rules, key matching across a republish, coalescing, `events_done`), outbox retry, acknowledgement of local values, driver step parser |
| `examples/menu_host.rs` | `crates/scol-menu/examples` | the window runs, real egui input (pointer, text, keys) reaches the plugin side for toggle, slider (number, integer, percent), combo, text, button, list, paging; hide and show; PNGs |
| `tools/rust-menu-host` | C++ host, built against the Core integration line | the same click-through against sco-core's real `sco.widgets.view` implementation, with tags checked in the plugin callback |

`expected_events.txt` lists the events both hosts must see, in order. `run.sh` builds and runs both hosts.

Screenshots go to `D:/sc241/menu-previews/rust/` (`*.png` from the Rust stand-in, `core-*.png` from the C++ host).
