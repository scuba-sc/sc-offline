// SPDX-License-Identifier: GPL-3.0-only
//! Game discovery and the read-only status checks of the sc-offline launcher, in Rust.
//!
//! This is the port of the parts of `launcher/launcher.cpp` that `sc-offline.exe status` and the
//! window's Status panel run: `ToBin64`, `DetectBin64` (with `install_discovery.h` and
//! `install_identity_win32.h`), `Identify`, `GameBuild`, `EacState`, `PcChangesLeft`,
//! `CountPlugins`, `CollectStatus`, `SelfChecks` and `RunStatus`. The text it prints is the C++
//! launcher's, line for line (`launcher-rs/tests/status/compare_status.py` checks that against the
//! built `sc-offline.exe`).
//!
//! Nothing here writes a file, opens a network connection, starts a process or changes a setting.
//! It reads files, folders, the registry, the process list and the environment. It is Windows only,
//! like the launcher. Nothing here was run in game.

#![cfg(windows)]

pub mod checks;
pub mod discovery;
pub mod identity;
pub mod paths;
pub mod status;
pub mod version;
mod win;

pub use status::{collect_status, run_status, StatusReport};
