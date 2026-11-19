// SPDX-License-Identifier: GPL-3.0-only
//! The Stop request channel, launcher side (`src/stop_channel.h`, `StopGame`, `EndTargets` of the C++
//! launcher). The mod creates two named events scoped to the game process:
//! `Local\sc-offline-stop-<pid>` (launcher to mod: run the exit work now) and
//! `Local\sc-offline-stopped-<pid>` (mod to launcher: it has run). The launcher signals the first in
//! each game process of this install, waits for the second for a bounded time, and only then posts
//! WM_CLOSE to the game windows. A process without the stop event is closed through its window directly.
//! Ending a process is a separate step and only touches the processes recorded at Stop.
//!
//! The Windows side is [`StopPort`]; `real::RealStopPort` is the real one and tests use
//! [`FakeStopPort`]. Nothing here was run in game.

use scol_contract::stop_channel::{stop_event_name, stopped_event_name};
use std::collections::{BTreeMap, BTreeSet};

/// A game process of this install, recorded at Stop. `created` is its creation time (FILETIME ticks): a
/// reused process id has another one and is left alone.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct GameProc {
    pub pid: u32,
    pub created: u64,
}

/// What opening and signalling the stop event gave (`Begin`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Begin {
    /// No stop event: the mod is not loaded in that process.
    NotPresent,
    /// The event exists but this process may not use it (the Windows error).
    Denied(u32),
    /// The stop event is signalled; the stopped event is being waited for.
    Signalled,
}

/// One look at the stopped event (`Poll`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Poll {
    Pending,
    Acknowledged,
    ProcessExited,
}

/// How one target's request ended.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Outcome {
    NotPresent,
    Denied(u32),
    Acknowledged(u64),
    TimedOut(u64),
    ProcessExited(u64),
}

/// The processes and windows the stop steps use.
pub trait StopPort {
    /// The StarCitizen.exe processes whose image is `bin\StarCitizen.exe`.
    fn find_game_procs(&mut self, bin: &str) -> Vec<GameProc>;
    /// Opens the events of the process and signals the stop event.
    fn begin(&mut self, target: &GameProc) -> Begin;
    /// Waits up to `slice_ms` for the stopped event of the process.
    fn poll(&mut self, target: &GameProc, slice_ms: u32) -> Poll;
    /// Closes the handles of `begin`.
    fn release(&mut self, target: &GameProc);
    /// Posts WM_CLOSE to the visible top-level windows of the targets; the number posted.
    fn post_close(&mut self, targets: &[GameProc]) -> usize;
    /// Ends one process. Ok(true) ended, Ok(false) it was already gone, Err the reason it was refused.
    fn terminate(&mut self, target: &GameProc, bin: &str) -> Result<bool, String>;
    /// True when the mod is installed in this game folder, so the game should answer.
    fn mod_expected(&mut self, bin: &str) -> bool;
    /// Milliseconds since an arbitrary start, for the bounded wait.
    fn now_ms(&mut self) -> u64;
    fn sleep(&mut self, ms: u64);
}

/// What the wait for the stopped event may last before the window is closed anyway (`kStopAckWaitMs`).
pub const STOP_ACK_WAIT_MS: u32 = 10_000;
/// How long the window waits before it offers to end the game (`kStopWaitSeconds`).
pub const STOP_WAIT_SECONDS: u32 = 60;
/// The slice of one poll.
const SLICE_MS: u32 = 50;

/// The result of [`stop_game`].
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct StopReport {
    /// The game folder is not known.
    pub no_folder: bool,
    /// The processes asked to close (empty: no process of this install was found).
    pub targets: Vec<GameProc>,
    pub outcomes: Vec<(u32, Outcome)>,
    /// The windows WM_CLOSE was posted to.
    pub posted: usize,
    /// The mod was expected to answer and did not.
    pub no_answer: bool,
}

fn describe(pid: u32, o: Outcome) -> String {
    match o {
        Outcome::NotPresent => format!(
            "pid {pid}: no stop event (the mod is not loaded in it); closing the window directly"
        ),
        Outcome::Denied(e) => format!(
            "pid {pid}: stop event not usable (error {e}); closing the window directly"
        ),
        Outcome::Acknowledged(ms) => {
            format!("pid {pid}: stop event signalled; mod acknowledged after {ms} ms")
        }
        Outcome::TimedOut(ms) => format!(
            "pid {pid}: stop event signalled; no acknowledgement after {ms} ms, closing the window anyway"
        ),
        Outcome::ProcessExited(ms) => {
            format!("pid {pid}: stop event signalled; the process exited after {ms} ms")
        }
    }
}

/// Stop: first, in each target where the mod is loaded, the mod's stop event (so its exit work, the wallet
/// save among it, runs as on a normal quit; WM_CLOSE alone skips it), then WM_CLOSE to the game's windows.
/// Refuses when the game folder is not known or no process of it is found. `log` gets one line per step
/// (the C++ launcher writes them to `data\launcher.log` as `[stop hh:mm:ss.mmm] ...`).
pub fn stop_game(
    port: &mut dyn StopPort,
    bin: &str,
    ack_wait_ms: u32,
    log: &mut dyn FnMut(&str),
) -> StopReport {
    let mut r = StopReport::default();
    if bin.is_empty() {
        log("Stop pressed: game folder unknown; nothing done");
        r.no_folder = true;
        return r;
    }
    r.targets = port.find_game_procs(bin);
    if r.targets.is_empty() {
        log("Stop pressed: no game process of this install found; nothing done");
        return r;
    }
    let pids: Vec<String> = r.targets.iter().map(|t| t.pid.to_string()).collect();
    log(&format!(
        "Stop pressed: {} target(s), pid {}",
        r.targets.len(),
        pids.join(", ")
    ));
    let expected = port.mod_expected(bin);
    let begun = port.now_ms();
    let mut pending: Vec<GameProc> = Vec::new();
    for t in r.targets.clone() {
        match port.begin(&t) {
            Begin::NotPresent => r.outcomes.push((t.pid, Outcome::NotPresent)),
            Begin::Denied(e) => r.outcomes.push((t.pid, Outcome::Denied(e))),
            Begin::Signalled => pending.push(t),
        }
    }
    while !pending.is_empty() {
        let mut still = Vec::new();
        for t in pending {
            let elapsed = port.now_ms().saturating_sub(begun);
            match port.poll(&t, SLICE_MS) {
                Poll::Acknowledged => r.outcomes.push((t.pid, Outcome::Acknowledged(elapsed))),
                Poll::ProcessExited => r.outcomes.push((t.pid, Outcome::ProcessExited(elapsed))),
                Poll::Pending if elapsed >= u64::from(ack_wait_ms) => {
                    r.outcomes.push((t.pid, Outcome::TimedOut(elapsed)))
                }
                Poll::Pending => still.push(t),
            }
        }
        pending = still;
        if !pending.is_empty() {
            port.sleep(1);
        }
    }
    for t in &r.targets {
        port.release(t);
    }
    for (pid, o) in &r.outcomes {
        log(&describe(*pid, *o));
        if expected && !matches!(o, Outcome::Acknowledged(_) | Outcome::ProcessExited(_)) {
            r.no_answer = true;
        }
    }
    if r.no_answer {
        log("the mod did not answer; closing without its save");
    }
    r.posted = port.post_close(&r.targets);
    log(&format!("WM_CLOSE posted to {} window(s)", r.posted));
    r
}

/// End: the second confirm said yes. Ends exactly the processes recorded at Stop, no others (a process is
/// only ended when it still has the recorded creation time and still runs `bin\StarCitizen.exe`). With
/// no recorded target nothing is ended. Returns (ended, failed).
pub fn end_game(
    port: &mut dyn StopPort,
    targets: &[GameProc],
    bin: &str,
    log: &mut dyn FnMut(&str),
) -> (usize, usize) {
    log(&format!(
        "End answered: yes; ending the {} recorded process(es)",
        targets.len()
    ));
    let (mut ended, mut failed) = (0, usize::from(targets.is_empty()));
    for t in targets {
        match port.terminate(t, bin) {
            Ok(true) => {
                ended += 1;
                log(&format!("pid {}: process ended", t.pid));
            }
            Ok(false) => {
                ended += 1;
                log(&format!("pid {}: already gone", t.pid));
            }
            Err(why) => {
                failed += 1;
                log(&format!("pid {}: not ended ({why})", t.pid));
            }
        }
    }
    (ended, failed)
}

/// What `stop` does when the game is still running after the wait.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EndPolicy {
    /// Say how to end it and leave it running.
    Never,
    /// Ask once (a console); no answer counts as no.
    Ask,
    /// End the recorded processes without asking (`stop --end`).
    Yes,
}

/// The settings of one [`run_stop_command`].
#[derive(Clone, Copy, Debug)]
pub struct StopCommand<'a> {
    /// The game's Bin64 folder; empty when unknown.
    pub bin: &'a str,
    /// How long the mod may take to answer its stop event (`STOP_ACK_WAIT_MS`).
    pub ack_wait_ms: u32,
    /// How long to wait for the game to be gone after Stop (`STOP_WAIT_SECONDS`). 0: do not wait; the
    /// window uses 0 because it shows the state by itself.
    pub wait_seconds: u32,
    pub end: EndPolicy,
}

/// True when one of the recorded processes still runs (same pid and creation time).
fn still_running(port: &mut dyn StopPort, bin: &str, targets: &[GameProc]) -> bool {
    let now = port.find_game_procs(bin);
    targets.iter().any(|t| now.contains(t))
}

/// The `stop` command and the window's Stop action: the stop channel, then WM_CLOSE (`stop_game`), then,
/// when `wait_seconds` is not 0, a wait for the game to be gone and the End step the C++ window offers
/// after `kStopWaitSeconds`. Output lines are the window's texts. Returns the exit code: 0 when the game
/// was asked to close and is gone (or nothing was running, or `wait_seconds` is 0), 1 when the folder is
/// unknown or the game is still running at the end.
pub fn run_stop_command(
    port: &mut dyn StopPort,
    cmd: &StopCommand<'_>,
    ask: &mut dyn FnMut(&str) -> bool,
    out: &mut dyn std::io::Write,
    log: &mut dyn FnMut(&str),
) -> i32 {
    let r = stop_game(port, cmd.bin, cmd.ack_wait_ms, log);
    if r.no_folder {
        let _ = writeln!(
            out,
            "[!] Can't tell which game to stop: the game folder isn't known yet."
        );
        return 1;
    }
    if r.targets.is_empty() {
        let _ = writeln!(
            out,
            "No Star Citizen process of this install was found to stop."
        );
        return 0;
    }
    let _ = writeln!(
        out,
        "Asked {} game process(es) to close; {} window(s) told to close.",
        r.targets.len(),
        r.posted
    );
    if r.no_answer {
        let _ = writeln!(out, "The mod didn't answer; closing without its save.");
    }
    if r.posted == 0 {
        let _ = writeln!(
            out,
            "No Star Citizen window found to close. If it is still running in a minute, you can end it."
        );
    }
    if cmd.wait_seconds == 0 {
        return 0;
    }
    let begun = port.now_ms();
    let limit = u64::from(cmd.wait_seconds) * 1000;
    while port.now_ms().saturating_sub(begun) < limit {
        if !still_running(port, cmd.bin, &r.targets) {
            let _ = writeln!(out, "The game closed.");
            return 0;
        }
        port.sleep(500);
    }
    if !still_running(port, cmd.bin, &r.targets) {
        let _ = writeln!(out, "The game closed.");
        return 0;
    }
    log(&format!(
        "game still running {} s after Stop",
        cmd.wait_seconds
    ));
    let yes = match cmd.end {
        EndPolicy::Yes => true,
        EndPolicy::Ask => ask("The game is still running. End the game process?"),
        EndPolicy::Never => false,
    };
    if !yes {
        let _ = writeln!(
            out,
            "The game is still running after {} s. Run `sc-offline.exe stop --end` to end it.",
            cmd.wait_seconds
        );
        return 1;
    }
    let (ended, failed) = end_game(port, &r.targets, cmd.bin, log);
    if failed == 0 && ended > 0 {
        let _ = writeln!(out, "Ended the game process.");
        0
    } else {
        let _ = writeln!(out, "[!] Couldn't end the game process.");
        1
    }
}

/// The names of the two events of a game process: (stop, stopped).
pub fn event_names(pid: u32) -> (String, String) {
    (stop_event_name(pid), stopped_event_name(pid))
}

/// A scripted [`StopPort`] for tests.
#[derive(Default)]
pub struct FakeStopPort {
    pub procs: Vec<GameProc>,
    /// Per pid: what `begin` gives. Missing: NotPresent.
    pub begin: BTreeMap<u32, Begin>,
    /// Per pid: at which poll the stopped event is set (missing: never).
    pub ack_after_polls: BTreeMap<u32, u32>,
    /// Pids that have exited when polled.
    pub exits: BTreeSet<u32>,
    pub windows: usize,
    pub mod_expected: bool,
    /// Per pid: what `terminate` gives. Missing: Ok(true).
    pub terminate: BTreeMap<u32, Result<bool, String>>,
    pub clock: u64,
    pub calls: Vec<String>,
    /// After this many `find_game_procs` calls the processes are gone (missing: they stay).
    pub gone_after_finds: Option<u32>,
    finds: u32,
    polls: BTreeMap<u32, u32>,
}

impl StopPort for FakeStopPort {
    fn find_game_procs(&mut self, bin: &str) -> Vec<GameProc> {
        self.calls.push(format!("find {bin}"));
        self.finds += 1;
        match self.gone_after_finds {
            Some(n) if self.finds > n => Vec::new(),
            _ => self.procs.clone(),
        }
    }
    fn begin(&mut self, t: &GameProc) -> Begin {
        self.calls.push(format!("begin {}", t.pid));
        self.begin.get(&t.pid).copied().unwrap_or(Begin::NotPresent)
    }
    fn poll(&mut self, t: &GameProc, slice_ms: u32) -> Poll {
        self.clock += u64::from(slice_ms);
        let n = self.polls.entry(t.pid).or_insert(0);
        *n += 1;
        if self.exits.contains(&t.pid) {
            return Poll::ProcessExited;
        }
        match self.ack_after_polls.get(&t.pid) {
            Some(k) if *n >= *k => Poll::Acknowledged,
            _ => Poll::Pending,
        }
    }
    fn release(&mut self, t: &GameProc) {
        self.calls.push(format!("release {}", t.pid));
    }
    fn post_close(&mut self, targets: &[GameProc]) -> usize {
        self.calls.push(format!("post_close {}", targets.len()));
        self.windows
    }
    fn terminate(&mut self, t: &GameProc, _bin: &str) -> Result<bool, String> {
        self.calls.push(format!("terminate {}", t.pid));
        self.terminate.get(&t.pid).cloned().unwrap_or(Ok(true))
    }
    fn mod_expected(&mut self, _bin: &str) -> bool {
        self.mod_expected
    }
    fn now_ms(&mut self) -> u64 {
        self.clock
    }
    fn sleep(&mut self, ms: u64) {
        self.clock += ms;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn p(pid: u32) -> GameProc {
        GameProc {
            pid,
            created: 1000 + u64::from(pid),
        }
    }

    #[test]
    fn acknowledged_then_close() {
        let mut port = FakeStopPort {
            procs: vec![p(10)],
            windows: 2,
            mod_expected: true,
            ..Default::default()
        };
        port.begin.insert(10, Begin::Signalled);
        port.ack_after_polls.insert(10, 3);
        let mut lines = Vec::new();
        let r = stop_game(&mut port, "D:\\x\\Bin64", 10_000, &mut |l| {
            lines.push(l.to_string())
        });
        assert!(matches!(r.outcomes[0].1, Outcome::Acknowledged(_)));
        assert!(!r.no_answer);
        assert_eq!(r.posted, 2);
        let close = port.calls.iter().position(|c| c == "post_close 1").unwrap();
        let rel = port.calls.iter().position(|c| c == "release 10").unwrap();
        assert!(rel < close, "the window is closed after the mod answered");
        assert!(lines.iter().any(|l| l.contains("mod acknowledged after")));
    }

    #[test]
    fn no_stop_event_closes_the_window_directly() {
        let mut port = FakeStopPort {
            procs: vec![p(11)],
            windows: 1,
            ..Default::default()
        };
        let r = stop_game(&mut port, "D:\\x\\Bin64", 10_000, &mut |_| {});
        assert_eq!(r.outcomes, vec![(11, Outcome::NotPresent)]);
        assert!(!r.no_answer, "the mod is not expected: nothing to wait for");
        assert_eq!(r.posted, 1);
    }

    #[test]
    fn timeout_when_the_mod_is_expected_flags_no_answer() {
        let mut port = FakeStopPort {
            procs: vec![p(12)],
            windows: 1,
            mod_expected: true,
            ..Default::default()
        };
        port.begin.insert(12, Begin::Signalled);
        let mut lines = Vec::new();
        let r = stop_game(&mut port, "D:\\x\\Bin64", 200, &mut |l| {
            lines.push(l.to_string())
        });
        assert!(matches!(r.outcomes[0].1, Outcome::TimedOut(ms) if ms >= 200));
        assert!(r.no_answer);
        assert!(lines
            .iter()
            .any(|l| l == "the mod did not answer; closing without its save"));
    }

    #[test]
    fn nothing_found_and_unknown_folder_do_nothing() {
        let mut port = FakeStopPort::default();
        let r = stop_game(&mut port, "", 1, &mut |_| {});
        assert!(r.no_folder);
        let r = stop_game(&mut port, "D:\\x", 1, &mut |_| {});
        assert!(r.targets.is_empty());
        assert!(!port.calls.iter().any(|c| c.starts_with("post_close")));
    }

    #[test]
    fn end_game_ends_only_recorded_targets() {
        let mut port = FakeStopPort::default();
        port.terminate
            .insert(2, Err("creation time or image no longer matches".into()));
        port.terminate.insert(3, Ok(false));
        let (ended, failed) = end_game(&mut port, &[p(1), p(2), p(3)], "D:\\x", &mut |_| {});
        assert_eq!((ended, failed), (2, 1));
        let (ended, failed) = end_game(&mut port, &[], "D:\\x", &mut |_| {});
        assert_eq!(
            (ended, failed),
            (0, 1),
            "no recorded target is a failure, not a fresh scan"
        );
    }

    fn cmd(end: EndPolicy, wait_seconds: u32) -> StopCommand<'static> {
        StopCommand {
            bin: r"D:\x\Bin64",
            ack_wait_ms: 100,
            wait_seconds,
            end,
        }
    }

    fn run_cmd(port: &mut FakeStopPort, c: &StopCommand<'_>, answer: bool) -> (i32, String, usize) {
        let mut out = Vec::new();
        let mut asked = 0usize;
        let code = run_stop_command(
            port,
            c,
            &mut |_| {
                asked += 1;
                answer
            },
            &mut out,
            &mut |_| {},
        );
        (code, String::from_utf8(out).unwrap(), asked)
    }

    #[test]
    fn stop_command_waits_until_the_game_is_gone() {
        let mut port = FakeStopPort {
            procs: vec![p(20)],
            windows: 1,
            gone_after_finds: Some(3),
            ..Default::default()
        };
        let (code, text, asked) = run_cmd(&mut port, &cmd(EndPolicy::Ask, 60), true);
        assert_eq!(code, 0);
        assert!(text.contains("Asked 1 game process(es) to close; 1 window(s) told to close."));
        assert!(text.ends_with(
            "The game closed.
"
        ));
        assert_eq!(asked, 0, "nothing to end, nothing asked");
        assert!(!port.calls.iter().any(|c| c.starts_with("terminate")));
    }

    #[test]
    fn stop_command_without_wait_returns_at_once() {
        let mut port = FakeStopPort {
            procs: vec![p(21)],
            windows: 0,
            ..Default::default()
        };
        let (code, text, _) = run_cmd(&mut port, &cmd(EndPolicy::Never, 0), false);
        assert_eq!(code, 0);
        assert!(text.contains("No Star Citizen window found to close."));
        assert_eq!(port.finds, 1, "the window action does not look again");
    }

    #[test]
    fn stop_command_never_ends_without_a_yes() {
        let mut port = FakeStopPort {
            procs: vec![p(22)],
            windows: 1,
            ..Default::default()
        };
        let (code, text, asked) = run_cmd(&mut port, &cmd(EndPolicy::Never, 2), true);
        assert_eq!(code, 1);
        assert_eq!(asked, 0);
        assert!(text.contains("Run `sc-offline.exe stop --end` to end it."));
        assert!(!port.calls.iter().any(|c| c.starts_with("terminate")));
    }

    #[test]
    fn stop_command_ask_ends_only_on_yes() {
        let mut port = FakeStopPort {
            procs: vec![p(23)],
            windows: 1,
            ..Default::default()
        };
        let (code, _, asked) = run_cmd(&mut port, &cmd(EndPolicy::Ask, 1), false);
        assert_eq!((code, asked), (1, 1));
        assert!(!port.calls.iter().any(|c| c.starts_with("terminate")));
        let (code, text, asked) = run_cmd(&mut port, &cmd(EndPolicy::Ask, 1), true);
        assert_eq!((code, asked), (0, 1));
        assert!(text.ends_with(
            "Ended the game process.
"
        ));
        assert!(port.calls.iter().any(|c| c == "terminate 23"));
    }

    #[test]
    fn stop_command_yes_ends_without_asking_and_reports_a_refusal() {
        let mut port = FakeStopPort {
            procs: vec![p(24)],
            windows: 1,
            ..Default::default()
        };
        port.terminate.insert(24, Err("denied".into()));
        let (code, text, asked) = run_cmd(&mut port, &cmd(EndPolicy::Yes, 1), false);
        assert_eq!((code, asked), (1, 0));
        assert!(text.contains("Couldn't end the game process."));
    }

    #[test]
    fn stop_command_edge_cases() {
        let mut port = FakeStopPort::default();
        let c = StopCommand {
            bin: "",
            ack_wait_ms: 1,
            wait_seconds: 1,
            end: EndPolicy::Never,
        };
        let (code, text, _) = run_cmd(&mut port, &c, false);
        assert_eq!(code, 1);
        assert!(text.contains("game folder isn't known"));
        let (code, text, _) = run_cmd(&mut port, &cmd(EndPolicy::Never, 1), false);
        assert_eq!(code, 0);
        assert!(text.contains("No Star Citizen process of this install was found to stop."));
    }

    #[test]
    fn event_names_follow_the_contract() {
        assert_eq!(
            event_names(77),
            (
                "Local\\sc-offline-stop-77".to_string(),
                "Local\\sc-offline-stopped-77".to_string()
            )
        );
    }
}
