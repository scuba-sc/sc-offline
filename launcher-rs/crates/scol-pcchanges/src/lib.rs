// SPDX-License-Identifier: GPL-3.0-only
//! The PC changes of the sc-offline launcher, in Rust.
//!
//! While you play, the launcher's helper makes up to four kinds of change on the PC and undoes
//! them when the game closes: Windows Firewall rules that block StarCitizen.exe, CrashHandler.exe
//! and RSI Launcher.exe (plus one LAN allow rule for multiplayer), one hosts-file line for the
//! Easy Anti-Cheat download server, and the rename of EasyAntiCheat_EOS.exe to .bak. Each change
//! is recorded in `%ProgramData%\sc-offline\pc-changes.txt` and only recorded changes are undone.
//!
//! This is a port of that code in `launcher/launcher.cpp`, copied exactly and never widened. Every
//! system effect goes through [`System`]. The tests use [`fake::FakeSystem`] and check the exact
//! command lines and file contents; none of them runs the real implementation. Nothing here was
//! run in game.

#![deny(unsafe_code)]

pub mod allow;
pub mod changes;
pub mod fake;
pub mod helper;
#[cfg(all(windows, feature = "real-system"))]
pub mod real;
pub mod system;

pub use changes::{
    apply_pc_changes, describe_pc_changes, pc_changes_left, record_add, undo_pc_changes, PcWanted,
};
pub use helper::{
    is_helper_invocation, parse_pc_flags, pc_flags, plan_helper_start, run, HelperHooks,
    HelperStart, Io,
};
pub use system::{LocalTime, System};
