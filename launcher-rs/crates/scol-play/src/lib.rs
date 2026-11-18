// SPDX-License-Identifier: GPL-3.0-only
//! The `play`, `install` and `uninstall` commands of the sc-offline Rust launcher, and the hidden
//! `--helper`, `--delete-logs` and `--self-test` modes.
//!
//! This is the port of `wmain` (the play path), `Helper`, `StartHelper`, `StartGame`, `CleanSessionLogs`,
//! `OfferCrashReport`, `SettlePreviousUpdate` and `SelfTest` of `launcher/launcher.cpp`, plus the launcher
//! side of the Stop channel of `src/stop_channel.h`. The text and the exit codes are the C++ launcher's.
//!
//! Every process and system effect goes through [`host::Host`] (and [`host::Signals`] for the helper);
//! the PC changes go through `scol_pcchanges::System`. `real::RealHost` (feature `real`) is the real
//! implementation; [`fake::FakeHost`] is the one every test uses, with a stand-in game process under a
//! scratch folder. No test starts the real game, asks for administrator rights or touches the firewall,
//! the hosts file or the EAC exe. Nothing here was run in game.

#![cfg(windows)]

pub mod fake;
pub mod flow;
pub mod helper;
pub mod host;
pub mod log;
pub mod post;
#[cfg(feature = "real")]
pub mod real;
pub mod selftest;
pub mod settle;
pub mod stop;

pub use flow::{run_play, Mode, PlayArgs};
pub use helper::{helper_main, split_args, FileHooks};
pub use host::{Host, LogAnswer, Ready, Signals};
pub use post::{delete_session_logs, session_logs};
pub use selftest::self_test;

/// Now as Windows FILETIME ticks: 100 ns since 1601-01-01.
pub fn now_ticks() -> u64 {
    const UNIX_TO_FILETIME_SECS: u64 = 11_644_473_600;
    let d = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default();
    (d.as_secs() + UNIX_TO_FILETIME_SECS) * 10_000_000 + u64::from(d.subsec_nanos()) / 100
}

#[cfg(test)]
mod tests {
    #[test]
    fn ticks_are_after_2025() {
        // 2025-01-01 in FILETIME ticks
        assert!(super::now_ticks() > 133_800_000_000_000_000);
    }
}
