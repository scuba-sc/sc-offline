// SPDX-License-Identifier: GPL-3.0-only
//! Settings of the launcher: `sc-offline.ini` (read, edit in place), the environment block the
//! game process gets, and the `plugin.ini` reader.
//!
//! Every function here reproduces what the C++ launcher does today, byte for byte, and the
//! golden files in `tests/golden` (written by `tools/launcher-contract` from the real C++ code)
//! prove it. That includes the `exp_*` capability keys and the environment they leave for the
//! game (`ApplyExperimentalEnv` in the C++ launcher).
//!
//! Source and synthetic evidence only. Nothing here was run in game.

pub mod capabilities;
pub mod cfmt;
pub mod config;
pub mod env;
pub mod ini_edit;
pub mod plugin_ini;

pub use capabilities::Capabilities;
pub use config::{read_config, Config, Language, LogChoice, Parsed};
pub use env::{
    apply_capabilities, full_env, play_env, render_capabilities, CapabilityApply, EnvVar, PlayPaths,
};
