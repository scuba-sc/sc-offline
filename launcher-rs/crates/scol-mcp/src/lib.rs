// SPDX-License-Identifier: GPL-3.0-only
//! The read-only MCP server behind `sc-offline.exe mcp`, in Rust.
//!
//! This is the port of `launcher/mcp_server.h`: JSON-RPC 2.0 over stdio, one message per line, four tools
//! (`get_status`, `list_plugins`, `read_log`, `get_settings`) and no command, console, start/stop or settings
//! write. Tool names, titles, descriptions, schemas, limits, error codes and redaction results are those of
//! the C++ server. The shared vectors in `launcher-rs/tests/mcp-vectors`, written by running the C++ code,
//! are replayed by `tests/vectors.rs` and must match byte for byte.
//!
//! The protocol code is portable and never opens a file: a [`Platform`] supplies the status fields, plugin
//! folders, log tails and ini text. On Windows, [`host::HostPlatform`] reads the real install (status through
//! `scol-install`). Redaction is `scol-redact`.
//!
//! Intentional differences from the C++ launcher are listed in `launcher-rs/tests/mcp-vectors/README.md`.
//! Nothing here was run in game; the checks are synthetic.

#![forbid(unsafe_code)]

pub mod json;
mod run;
pub mod server;
pub mod tools;

#[cfg(windows)]
pub mod host;

#[cfg(windows)]
pub use run::{run, run_in};
pub use run::{run_with, Io, USAGE};
pub use server::{
    folder_less, serve, IniRead, LogRead, LogScan, Platform, PluginFolder, Server, StatusInfo,
    ToolResult,
};
