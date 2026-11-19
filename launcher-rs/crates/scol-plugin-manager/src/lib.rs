// SPDX-License-Identifier: GPL-3.0-only
//! Plugin management for the sc-offline Rust launcher: `plugins <list|enable|disable|install|
//! remove|catalogue|update>`.
//!
//! * [`list`]: what is in `data/plugins/`, with manifest fields and state.
//! * [`install`]: install from a folder or zip, from the signed catalogue, update, remove.
//! * [`txn`]: the journaled swap that makes each change all-or-nothing.
//! * [`fetcher`]: a local-folder [`scol_catalogue::fetch::Fetcher`] for tests and offline use.
//! * [`settings`]: a plugin's `[settings]` values in its own database `data/storage/<id>.db`.
//! * [`cli`]: the command line, as `run(args) -> i32` for the launcher CLI to call.
//!
//! The crate writes only under `data/plugins/<id>/` and `data/launcher/` (and, for
//! `remove --purge` and `settings`, the plugin's own settings store `data/storage/<id>.db`). It never loads or
//! runs plugin code and refuses to change anything while the game runs. Nothing here was run in
//! game.

pub mod cli;
pub mod fetcher;
pub mod install;
pub mod layout;
pub mod list;
pub mod settings;
pub mod store;
pub mod txn;
pub mod zipx;

pub use cli::{run, run_net, run_with, run_with_net, Capture, Console, Ctx, Io, NetFactory};
