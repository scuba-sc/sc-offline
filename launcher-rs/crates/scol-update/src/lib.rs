// SPDX-License-Identifier: GPL-3.0-only
//! Self-update of the sc-offline Rust launcher, matching the C++ launcher (`launcher/launcher.cpp`,
//! "Self-update"): the same release API, the same manifest format 1, the same journal
//! (`data\update\applied.txt`), the same `--self-test <tag>` contract, so an old C++ launcher can
//! update to a Rust one and the other way round.
//!
//! The flow, in the order it runs:
//!
//! 1. [`flow::check_latest`]: ask the hard-coded GitHub release API (HTTPS only, byte cap, redirects
//!    host-checked) which release is on offer on the player's channel ([`release::pick`]).
//! 2. [`flow::stage_update`]: download the zip, check it against GitHub's SHA-256, then
//!    [`stage::stage_zip`]: read `manifest.json`, check tag and version, refuse unlisted files and
//!    unsafe paths, extract only listed files with size and digest checks.
//! 3. [`swap::swap_staged`]: never while the game runs; re-check the staged files; write each as
//!    `.update-new`, journal, move the old file to `.update-old`, move the new one in, re-hash; then
//!    run the new exe's `--self-test`; any failure rolls the old files back. `sc-offline.ini` and
//!    player files are kept ([`swap::player_owned`]); new ini keys are appended commented out.
//! 4. [`swap::settle_previous`] at every start finishes or rolls back what a crash left.
//!
//! Network access goes through [`scol_catalogue::fetch::Fetcher`]; `http::WinHttp` is the real one
//! (WinHTTP through `windows-sys`, no TLS library of our own) and also serves the plugin catalogue
//! (`http::catalogue_fetcher`). Machine access (game running, self-test, elevation) goes through
//! [`platform::Platform`]. Tests use fakes for both. Nothing here was run in game.

pub mod cli;
pub mod flow;
#[cfg(windows)]
pub mod http;
pub mod manifest;
pub mod platform;
pub mod release;
pub mod stage;
pub mod swap;
pub mod version;
pub mod zipx;

pub use cli::{run, run_with, Context};
