# launcher-rs

Rust workspace for the sc-offline launcher. Status: P0 skeleton. It holds the toolchain
pins, the dependency gate, one contract crate and a hello binary that proves the build.
It does not replace the C++ launcher in `launcher/`. Nothing here is run in game.

The design is in `docs/rust-launcher.md`.

## Build

Needs rustup and cargo on PATH. The toolchain version is pinned in `rust-toolchain.toml`
(1.99.0, target x86_64-pc-windows-msvc). On Windows the MSVC linker must be reachable
(Visual Studio 2022 or newer Build Tools).

```
cd launcher-rs
cargo build --locked --release
cargo test --locked
cargo deny check
```

`cargo deny` is a separate tool: `cargo install cargo-deny --locked`.

Release settings: `panic = "abort"`, LTO, `opt-level = "s"`, stripped. The static CRT is
set in `.cargo/config.toml`, so the exe imports no vcruntime or ucrt DLL. Check it with
`dumpbin /dependents target\release\scol-hello.exe`.

## Layout

- `crates/scol-contract`: frozen constants (environment names, stop event names, marker file,
  backup suffix, exit codes, ini keys). Each item cites its C++ source line.
- `crates/scol-cli`: P0 hello binary only.

Add a crate by creating `crates/<name>/`. The root `Cargo.toml` uses `members = ["crates/*"]`.
`Cargo.lock` is committed.

## Rules

- Public GPL-3.0-only repo. New source files start with `SPDX-License-Identifier: GPL-3.0-only`.
- No personal names, e-mails, private paths or private repo names in any file.
- Capability switches default ON in release packages, each one a visible setting the player
  can turn off, each one's text says "not run in game".
