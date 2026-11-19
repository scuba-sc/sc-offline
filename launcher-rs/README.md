# launcher-rs

Rust workspace of the sc-offline launcher and the in-game Rust menu. It builds `sc-offline.exe`, the launcher of the 0.8.0 package, and `sc_offline_menu.dll`. The C++ launcher in `launcher/` ships beside it as `sc-offline-classic.exe`. The crates, the commands and how they work are in [docs/rust-launcher.md](../docs/rust-launcher.md). The menu is in [docs/rust-menu.md](../docs/rust-menu.md).

Not run in game. See [docs/status.md](../docs/status.md).

## Build

Needs rustup and cargo on `PATH`. The toolchain is pinned in `rust-toolchain.toml` (1.99.0, target `x86_64-pc-windows-msvc`). On Windows the MSVC linker must be reachable (Visual Studio 2022 or newer Build Tools). `cargo deny` is a separate tool: `cargo install cargo-deny --locked`.

```
cd launcher-rs
cargo build --release --workspace --locked
cargo test --workspace --locked
cargo clippy --workspace --all-targets -- -D warnings
cargo deny check
```

Outputs in `target\release\`: `sc-offline.exe`, `scol.exe` (the same code under the name the parity scripts use), `scol-pm.exe`, `scol-ui.exe`, `scol-hello.exe`, `scol-play-standin.exe` (a stand-in game for tests) and `sc_offline_menu.dll`.

Release settings: LTO, `opt-level = "s"`, stripped, `panic = "unwind"` (the menu DLL catches panics at its exports). `.cargo/config.toml` sets the static CRT, so no exe imports a vcruntime or ucrt DLL. Check it with `dumpbin /dependents target\release\sc-offline.exe`.

## Layout

- `crates/`: `scol-contract`, `scol-config`, `scol-install`, `scol-pcchanges`, `scol-play`, `scol-update`, `scol-plugins`, `scol-catalogue`, `scol-plugin-manager`, `scol-redact`, `scol-mcp`, `scol-ui`, `scol-cli`, `scol-menu`. Add a crate by creating `crates/<name>/`; the root `Cargo.toml` takes `crates/*`. `Cargo.lock` is committed.
- `tests/`: parity scripts, golden files and test vectors. Each folder has a README.
- `spike/`: a throwaway UI experiment, outside the workspace (see `spike/egui-launcher/REPORT.md`).

## Rules

- Public GPL-3.0-only repository. New source files start with `SPDX-License-Identifier: GPL-3.0-only`.
- No personal names, e-mails, private paths or private repository names in any file.
- No test starts the game, asks for administrator rights or changes the firewall, the hosts file or the EAC exe. `scol-play` and `scol-pcchanges` compile their real implementations only with the features `real` and `real-system`, which only `scol-cli` turns on.
- Capability switches default on in release packages (`exp_entities_diag` off). Each is a visible setting the player can turn off, and its text says it has not been run in game.
