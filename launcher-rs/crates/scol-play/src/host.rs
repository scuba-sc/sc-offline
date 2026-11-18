// SPDX-License-Identifier: GPL-3.0-only
//! Every process and system effect of the play flow goes through [`Host`]; the helper's waits and
//! named events go through [`Signals`]. There is one real implementation (`real`, behind the `real`
//! feature) and one fake ([`crate::fake`]); the tests use the fake and the stand-in game process.

use scol_install::checks::game_may_be_running_in;
use scol_pcchanges::{HelperStart, System};
use std::io::Write;

/// What `wait_ready` found.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Ready {
    /// The helper copied the mod in and signalled.
    Ready,
    /// The helper exited first, with this code (the copy failed; it left nothing half done).
    HelperGone(i32),
}

/// The helper's side of the session: the parent process, the ready and closed events, the game.
pub trait Signals {
    /// `OpenProcess(SYNCHRONIZE)` on the launcher pid. False when it cannot be opened.
    fn open_parent(&mut self, pid: u32) -> bool;
    /// Releases what `open_parent` opened.
    fn close_parent(&mut self);
    /// Sets the ready event the launcher waits on.
    fn signal_ready(&mut self, event: &str);
    /// Waits until the launcher sets `<event>-closed` or the launcher process exits.
    fn wait_session_end(&mut self, event: &str);
    /// Milliseconds between two looks at the process list while the helper waits for the game.
    fn poll_ms(&self) -> u64 {
        2000
    }
    /// `GameMayBeRunningIn` (the conservative guard: an unidentifiable StarCitizen.exe counts).
    fn game_may_be_running(&mut self, bin: &str) -> bool {
        game_may_be_running_in(bin, &mut std::io::sink())
    }
    fn sleep(&mut self, ms: u64) {
        std::thread::sleep(std::time::Duration::from_millis(ms));
    }
}

/// The launcher's side.
pub trait Host {
    /// This process's id (the helper gets it on its command line).
    fn pid(&self) -> u32;
    /// An environment variable of this process; `Some("")` for one that is set and empty.
    fn env(&self, name: &str) -> Option<String>;
    /// `SetVar`: sets the variable for this process and the game it starts; an empty value removes it.
    fn set_env(&mut self, name: &str, value: &str);
    /// The system trait the PC changes use.
    fn system(&mut self) -> &mut dyn System;
    fn is_elevated(&mut self) -> bool;
    /// `OwnsConsoleInput`: a console to answer questions in.
    fn owns_console_input(&mut self) -> bool;
    /// One line typed by the player; None at the end of input.
    fn read_line(&mut self) -> Option<String>;

    /// Creates the ready event (and, when `with_closed`, `<event>-closed`). Err is the Windows error.
    fn create_session(&mut self, event: &str, with_closed: bool) -> Result<(), u32>;
    /// `StartHelper`: starts the helper process described by `start` (hidden with normal rights, or
    /// elevated with one UAC prompt). Err is the Windows error.
    fn start_helper(&mut self, start: &HelperStart) -> Result<(), u32>;
    /// Blocks until the ready event is set or the helper exits.
    fn wait_ready(&mut self) -> Ready;
    /// Sets `<event>-closed`: the game closed, the helper may clean up now.
    fn signal_closed(&mut self);
    /// Waits for the helper to exit: `None` waits as long as it takes. Returns its exit code, or None
    /// when `ms` passed first.
    fn wait_helper(&mut self, ms: Option<u32>) -> Option<i32>;
    /// The helper has exited (a zero-time wait).
    fn helper_finished(&mut self) -> bool;
    /// Closes the session's handles.
    fn end_session(&mut self);

    /// `StartGame`: the game with normal rights, in `dir`. Prints its notes to `out`.
    fn start_game(&mut self, exe: &str, dir: &str, out: &mut dyn Write) -> Result<(), u32>;
    /// Blocks until the process `start_game` started exits.
    fn wait_game(&mut self);
    /// `GameMayBeRunningIn`.
    fn game_may_be_running(&mut self, bin: &str, out: &mut dyn Write) -> bool {
        game_may_be_running_in(bin, out)
    }
    /// Milliseconds between two looks at the process list while the launcher waits for the game to be gone.
    fn game_poll_ms(&self) -> u64 {
        2000
    }
    fn sleep(&mut self, ms: u64) {
        std::thread::sleep(std::time::Duration::from_millis(ms));
    }
    /// Now as Windows FILETIME ticks (100 ns since 1601).
    fn now_ticks(&mut self) -> u64;
    /// Ctrl+C would end only this window; the helper still cleans up. Says so instead of dying.
    fn ignore_ctrl_c(&mut self) {}
    /// Opens a web address (the crash report's bug form). Nothing is sent by the launcher itself.
    fn open_url(&mut self, _url: &str) {}
    /// `--delete-logs <channel folder> <since>` run elevated (one UAC prompt); its exit code.
    fn delete_logs_elevated(&mut self, _channel_dir: &str, _since: u64) -> i32 {
        1
    }
    /// Shows a file selected in the file manager.
    fn reveal_file(&mut self, _path: &str) {}
    /// Asks Windows for a folder the player picks. Not part of this port: None.
    fn pick_folder(&mut self) -> Option<String> {
        None
    }
    /// The update check of `play` (`check_updates`). The Rust launcher has no network code in this
    /// crate: the default does nothing, the caller that owns the updater overrides it.
    fn check_update(&mut self, _cfg: &scol_config::Config, _out: &mut dyn Write) {}

    /// `AskYes`: prints the question and reads one line; only a first letter y or Y says yes.
    fn ask_yes(&mut self, out: &mut dyn Write, question: &str) -> bool {
        let _ = write!(out, "{question} [y/N] ");
        let _ = out.flush();
        match self.read_line() {
            Some(l) => l.starts_with(['y', 'Y']),
            None => false,
        }
    }
    /// `AskLogs`: the session-log question.
    fn ask_logs(&mut self, out: &mut dyn Write, question: &str) -> LogAnswer {
        let _ = write!(out, "{question} [y/N/always/never] ");
        let _ = out.flush();
        match self.read_line() {
            Some(l) => parse_log_answer(&l),
            None => LogAnswer::Keep,
        }
    }
}

/// The answer to the session-log question.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LogAnswer {
    Keep,
    Delete,
    Always,
    Never,
}

/// `ParseLogAnswer`: y deletes (after one more "sure?"), a or always deletes and remembers, never keeps
/// and remembers; anything else, a bare Enter included, keeps this once.
pub fn parse_log_answer(line: &str) -> LogAnswer {
    let word: String = line
        .trim_start_matches([' ', '\t'])
        .chars()
        .take_while(|c| !matches!(c, ' ' | '\t' | '\r' | '\n'))
        .map(|c| c.to_ascii_lowercase())
        .collect();
    if word == "a" || word == "always" {
        LogAnswer::Always
    } else if word == "never" {
        LogAnswer::Never
    } else if word.starts_with('y') {
        LogAnswer::Delete
    } else {
        LogAnswer::Keep
    }
}
