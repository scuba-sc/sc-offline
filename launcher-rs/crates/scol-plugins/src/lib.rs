// SPDX-License-Identifier: GPL-3.0-only
//! Plugin package verification and `plugin.ini` parsing for the sc-offline Rust launcher.
//!
//! * [`verify`] checks a plugin zip without extracting it (port of the SDK `sco-plugin-verify.py`).
//! * [`manifest`] parses `plugin.ini` with the host rules.
//!
//! This crate never loads or runs plugin code, never touches the network and never writes files.
//! Nothing here was run in game.

pub mod manifest;
pub mod sha256;
pub mod unicode;
pub mod verify;

mod unicode_data;
