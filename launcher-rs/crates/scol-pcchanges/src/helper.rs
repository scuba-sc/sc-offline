// SPDX-License-Identifier: GPL-3.0-only
//! The `--helper` mode (`Helper`, `PcFlags`, `ParsePcFlags`, `StartHelper`'s argument line and
//! elevation choice). The mod copy and the process waits are not PC changes and live in the
//! launcher; the helper reaches them through [`HelperHooks`].

use crate::changes::{apply_pc_changes, game_exe_in, pc_changes_left, undo_pc_changes, PcWanted};
use crate::system::{parent_dir, System};
use std::io::Write;

/// Exit code: bad arguments.
pub const EXIT_BAD_ARGS: i32 = 4;

/// What the helper needs from the rest of the launcher.
pub trait HelperHooks {
    /// `OpenProcess(SYNCHRONIZE)` on the launcher pid. False when it cannot be opened.
    fn open_parent(&mut self, pid: u32) -> bool;
    /// Releases what `open_parent` opened.
    fn close_parent(&mut self);
    /// `TakeMod`: 0 ok, 3 removal failed.
    fn take_mod(&mut self, bin: &str) -> i32;
    /// `PutMod` with mode `play` or `install`: 0 ok, 2 copy failed.
    fn put_mod(&mut self, bin: &str, mode: &str) -> i32;
    /// Signals the ready event the launcher waits on.
    fn signal_ready(&mut self, event: &str);
    /// Waits until the launcher says the game closed (`<event>-closed`) or the launcher exits.
    fn wait_for_session_end(&mut self, event: &str);
    /// Waits while a StarCitizen.exe of this install may still be running.
    fn wait_game_closed(&mut self, bin: &str);
}

/// Everything `run` touches.
pub struct Io<'a> {
    pub sys: &'a mut dyn System,
    pub hooks: &'a mut dyn HelperHooks,
    pub out: &'a mut dyn Write,
}

/// `PcFlags`: f firewall, h hosts, e EAC rename, l the LAN stays open, `-` none (and under Wine).
pub fn pc_flags(w: &PcWanted, on_wine: bool) -> String {
    if on_wine {
        return "-".to_string();
    }
    let mut f = String::new();
    if w.firewall {
        f.push('f');
    }
    if w.eac_hosts {
        f.push('h');
    }
    if w.eac_rename {
        f.push('e');
    }
    if w.lan {
        f.push('l');
    }
    if f.is_empty() {
        "-".to_string()
    } else {
        f
    }
}

/// `ParsePcFlags`. `allow` is left empty; the helper sets it from its last argument.
pub fn parse_pc_flags(f: &str) -> PcWanted {
    PcWanted {
        firewall: f.contains('f'),
        eac_hosts: f.contains('h'),
        eac_rename: f.contains('e'),
        lan: f.contains('l'),
        allow: String::new(),
    }
}

/// `NeedsPcChanges`.
pub fn needs_pc_changes(w: &PcWanted, on_wine: bool) -> bool {
    !on_wine && (w.firewall || w.eac_hosts || w.eac_rename)
}

/// How the launcher starts the helper (`StartHelper`).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct HelperStart {
    /// The helper's command line after the exe, as `StartHelper` builds it.
    pub args: String,
    /// True: start it with `runas` (one UAC prompt). False: hidden, with normal rights.
    pub elevated: bool,
    /// The note printed before the prompt, when elevated.
    pub notice: Option<&'static str>,
}

const NOTICE_PC: &str = "[i] Windows will ask for administrator rights once: the helper blocks the game's network and\n    turns Easy Anti-Cheat off while you play, and undoes both when the game closes.\n    Only the helper runs as administrator, not the game.\n";
const NOTICE_MOD: &str = "[i] the game folder needs administrator rights for the mod; Windows will ask once.\n    Only the copy/remove helper runs as administrator, not the game.\n";

/// The helper's argument line and whether it must run elevated. The choice is the one
/// `StartHelper` makes: elevated whenever PC changes are wanted or left over, or the game folder
/// is not writable. Starting the process is left to the caller.
pub fn plan_helper_start(
    sys: &mut dyn System,
    op: &str,
    bin: &str,
    launcher_pid: u32,
    event: &str,
    pc: &PcWanted,
) -> HelperStart {
    let on_wine = sys.on_wine();
    let mut args = format!(
        "--helper {op} \"{bin}\" {launcher_pid} {event} {}",
        pc_flags(pc, on_wine)
    );
    if pc.lan && !pc.allow.is_empty() {
        args.push(' ');
        args.push_str(&pc.allow);
    }
    let is_play = op.eq_ignore_ascii_case("play");
    let pc_changes = (is_play && needs_pc_changes(pc, on_wine))
        || ((is_play || op.eq_ignore_ascii_case("uninstall")) && !pc_changes_left(sys).is_empty());
    let parent = parent_dir(sys, bin);
    if !pc_changes && sys.can_write_to(bin) && sys.can_write_to(&parent) {
        return HelperStart {
            args,
            elevated: false,
            notice: None,
        };
    }
    HelperStart {
        args,
        elevated: true,
        notice: Some(if pc_changes { NOTICE_PC } else { NOTICE_MOD }),
    }
}

/// The condition `wmain` uses to dispatch to the helper: `--helper` and 6 or 7 arguments after
/// the exe name.
pub fn is_helper_invocation(args: &[String]) -> bool {
    (args.len() == 6 || args.len() == 7) && args[0].eq_ignore_ascii_case("--helper")
}

/// `wcstoul(s, nullptr, 10)` for the digits a pid has: leading digits, 0 when there are none.
fn parse_pid(s: &str) -> u32 {
    let digits: String = s.chars().take_while(char::is_ascii_digit).collect();
    digits.parse().unwrap_or(0)
}

/// The helper: `--helper <play|install|uninstall> <Bin64> <launcher pid> <event name> <pc flags>
/// [<multiplayer_allow>]`; `args` are the arguments after the exe name.
///
/// play: make the PC changes, put the mod in, signal the event, wait for the game to close, take
/// the mod out, undo the PC changes. install: put the mod in. uninstall: take the mod out and undo
/// leftover PC changes. Exit codes: 0 ok, 2 copy failed, 3 removal failed, 4 bad args, 5 PC
/// change failed.
pub fn run(args: &[String], io: &mut Io<'_>) -> i32 {
    if !is_helper_invocation(args) {
        return EXIT_BAD_ARGS;
    }
    let op = args[1].as_str();
    let bin = args[2].as_str();
    let parent_pid = parse_pid(&args[3]);
    let event = args[4].as_str();
    let flags = args[5].as_str();
    let allow = args.get(6).map(String::as_str).unwrap_or("");

    if op.eq_ignore_ascii_case("uninstall") {
        let a = io.hooks.take_mod(bin);
        let b = undo_pc_changes(io.sys, io.out, false);
        return if a != 0 { a } else { b };
    }
    let play = op.eq_ignore_ascii_case("play");
    if !play && !op.eq_ignore_ascii_case("install") {
        return EXIT_BAD_ARGS;
    }
    if play && !io.hooks.open_parent(parent_pid) {
        return EXIT_BAD_ARGS;
    }
    if play {
        let mut wanted = parse_pc_flags(flags);
        wanted.allow = allow.to_string();
        let pc = apply_pc_changes(io.sys, io.out, &wanted, &game_exe_in(bin), false);
        if pc != 0 {
            undo_pc_changes(io.sys, io.out, false);
            io.hooks.close_parent();
            return pc;
        }
    }
    let rc = io.hooks.put_mod(bin, if play { "play" } else { "install" });
    if rc != 0 {
        if play {
            undo_pc_changes(io.sys, io.out, false);
            io.hooks.close_parent();
        }
        return rc;
    }
    io.hooks.signal_ready(event);
    if !play {
        return 0;
    }
    // Clean up when the launcher says the game closed, or when the launcher exits.
    io.hooks.wait_for_session_end(event);
    io.hooks.close_parent();
    io.hooks.wait_game_closed(bin);
    let taken = io.hooks.take_mod(bin);
    let had_pc = !pc_changes_left(io.sys).is_empty();
    let undone = undo_pc_changes(io.sys, io.out, false);
    if had_pc && undone == 0 {
        let _ = writeln!(io.out, "[+] PC changes undone");
    }
    if taken != 0 {
        taken
    } else {
        undone
    }
}
