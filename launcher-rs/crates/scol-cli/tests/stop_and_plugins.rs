// SPDX-License-Identifier: GPL-3.0-only
//! `sc-offline.exe stop` over a fake stop port (the stop channel, then WM_CLOSE, then the wait and the End
//! step), and `plugins ...` through the built exe: the catalogue without `--source` on a build with no
//! trust key, `plugins settings`, and the usage. Synthetic folders under the temp dir; no game, no network.
//! Nothing here was run in game.
#![cfg(windows)]

use scol_cli::testing::FakeBackend;
use scol_cli::{run_with, Streams};
use scol_play::fake::{FakeHost, SharedSystem};
use scol_play::stop::{Begin, GameProc};
use std::path::{Path, PathBuf};

fn args(a: &[&str]) -> Vec<String> {
    a.iter().map(|s| s.to_string()).collect()
}

fn scratch(tag: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("scol-cli-sp-{tag}-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

/// A launcher folder with an ini naming a fake game folder, and that game folder.
fn world(tag: &str) -> (PathBuf, String) {
    let root = scratch(tag);
    let here = root.join("here");
    let bin = root
        .join("lib")
        .join("StarCitizen")
        .join("LIVE")
        .join("Bin64");
    std::fs::create_dir_all(here.join("data")).unwrap();
    std::fs::create_dir_all(&bin).unwrap();
    std::fs::write(bin.join("StarCitizen.exe"), b"x").unwrap();
    std::fs::write(
        here.join("sc-offline.ini"),
        format!(
            "game = {}\r\n",
            root.join("lib").join("StarCitizen").display()
        ),
    )
    .unwrap();
    std::fs::write(here.join("data").join("launcher.log"), b"").unwrap();
    (here, bin.to_string_lossy().into_owned())
}

fn run_stop_cli(be: &mut FakeBackend, here: &Path, a: &[&str]) -> (i32, String) {
    let h = here.to_string_lossy().into_owned();
    let (mut out, mut err) = (Vec::new(), Vec::new());
    let mut input: &[u8] = b"";
    let code = run_with(
        &args(a),
        &h,
        &mut Streams {
            stdin: &mut input,
            stdout: &mut out,
            stderr: &mut err,
        },
        be,
    );
    assert!(err.is_empty(), "{}", String::from_utf8_lossy(&err));
    (code, String::from_utf8_lossy(&out).replace("\r\n", "\n"))
}

fn backend() -> FakeBackend {
    FakeBackend::new(FakeHost::new(".", SharedSystem::default()))
}

fn proc(pid: u32) -> GameProc {
    GameProc {
        pid,
        created: 5000 + u64::from(pid),
    }
}

#[test]
fn stop_asks_the_mod_then_closes_the_window_and_waits_until_the_game_is_gone() {
    let (here, _bin) = world("stop-ok");
    let mut be = backend();
    be.stop_port.procs = vec![proc(31)];
    be.stop_port.begin.insert(31, Begin::Signalled);
    be.stop_port.ack_after_polls.insert(31, 2);
    be.stop_port.windows = 1;
    be.stop_port.mod_expected = true;
    be.stop_port.gone_after_finds = Some(3);
    let (code, out) = run_stop_cli(&mut be, &here, &["stop"]);
    assert_eq!(code, 0, "{out}");
    assert!(
        out.contains("Asked 1 game process(es) to close; 1 window(s) told to close."),
        "{out}"
    );
    assert!(out.ends_with("The game closed.\n"), "{out}");
    assert_eq!(be.stop_calls.len(), 1);
    assert!(!be.stop_calls[0].end);
    // the channel first, WM_CLOSE after the mod answered, nothing ended
    let calls = be.stop_port.calls.join("\n");
    let begin = calls.find("begin 31").unwrap();
    let close = calls.find("post_close 1").unwrap();
    assert!(begin < close, "{calls}");
    assert!(!calls.contains("terminate"), "{calls}");
    // the steps are in data\launcher.log as the C++ launcher writes them
    let log = std::fs::read_to_string(here.join("data").join("launcher.log")).unwrap();
    assert!(log.contains("] Stop pressed: 1 target(s), pid 31"), "{log}");
    assert!(
        log.lines().all(|l| l.is_empty() || l.starts_with("[stop ")),
        "{log}"
    );
    assert!(log.contains("mod acknowledged after"), "{log}");
    assert!(log.contains("WM_CLOSE posted to 1 window(s)"), "{log}");
}

#[test]
fn stop_with_nothing_running_says_so_and_succeeds() {
    let (here, _) = world("stop-none");
    let mut be = backend();
    let (code, out) = run_stop_cli(&mut be, &here, &["stop"]);
    assert_eq!(code, 0);
    assert_eq!(
        out,
        "No Star Citizen process of this install was found to stop.\n"
    );
    assert!(!be
        .stop_port
        .calls
        .iter()
        .any(|c| c.starts_with("post_close")));
}

#[test]
fn a_game_that_stays_is_ended_only_with_end_or_a_yes() {
    let (here, _) = world("stop-stays");
    // no --end, nobody answers: hint, exit 1, nothing ended
    let mut be = backend();
    be.stop_port.procs = vec![proc(41)];
    be.stop_port.windows = 1;
    let (code, out) = run_stop_cli(&mut be, &here, &["stop"]);
    assert_eq!(code, 1);
    assert!(
        out.contains("Run `sc-offline.exe stop --end` to end it."),
        "{out}"
    );
    assert!(!be
        .stop_port
        .calls
        .iter()
        .any(|c| c.starts_with("terminate")));
    assert!(
        be.stop_asked.is_empty(),
        "the fake's policy is Never without --end"
    );

    // --end: ended without a question
    let mut be = backend();
    be.stop_port.procs = vec![proc(42)];
    be.stop_port.windows = 1;
    let (code, out) = run_stop_cli(&mut be, &here, &["stop", "--end"]);
    assert_eq!(code, 0, "{out}");
    assert!(out.ends_with("Ended the game process.\n"), "{out}");
    assert!(be.stop_port.calls.iter().any(|c| c == "terminate 42"));
    assert!(be.stop_asked.is_empty());
    let log = std::fs::read_to_string(here.join("data").join("launcher.log")).unwrap();
    assert!(
        log.contains("End answered: yes; ending the 1 recorded process(es)"),
        "{log}"
    );
    assert!(log.contains("pid 42: process ended"), "{log}");

    // a refused end is a failure
    let mut be = backend();
    be.stop_port.procs = vec![proc(43)];
    be.stop_port
        .terminate
        .insert(43, Err("cannot open it, error 5".into()));
    let (code, out) = run_stop_cli(&mut be, &here, &["stop", "--end"]);
    assert_eq!(code, 1);
    assert!(out.contains("Couldn't end the game process."), "{out}");
}

#[test]
fn stop_needs_a_known_game_folder() {
    let (here, _) = world("stop-nogame");
    let mut be = backend();
    let nope = here.join("nope").to_string_lossy().into_owned();
    let (code, out) = run_stop_cli(&mut be, &here, &["stop", "--game", &nope]);
    assert_eq!(code, 1);
    assert!(
        out.contains(&format!("[!] no StarCitizen.exe under --game {nope}")),
        "{out}"
    );
    assert!(be.stop_port.calls.is_empty(), "nothing was looked for");
    assert_eq!(
        be.stop_calls,
        [StopArgsLike {
            game: nope,
            end: false
        }
        .into()]
    );
}

/// `StopArgs` without importing its constructor path in every assertion.
struct StopArgsLike {
    game: String,
    end: bool,
}
impl From<StopArgsLike> for scol_cli::StopArgs {
    fn from(a: StopArgsLike) -> Self {
        scol_cli::StopArgs {
            game: a.game,
            end: a.end,
        }
    }
}

// ---- through the built exe ---------------------------------------------------------------------------

fn exe(here: &Path, args: &[&str]) -> (i32, String, String) {
    let o = std::process::Command::new(env!("CARGO_BIN_EXE_sc-offline"))
        .args(args)
        .current_dir(here)
        .env("SC_OFFLINE_GUI", "1")
        .stdin(std::process::Stdio::null())
        .output()
        .unwrap();
    (
        o.status.code().unwrap_or(-1),
        String::from_utf8_lossy(&o.stdout).replace("\r\n", "\n"),
        String::from_utf8_lossy(&o.stderr).replace("\r\n", "\n"),
    )
}

/// A folder that has the exe's neighbours the commands look at: the folder of the exe is `here`, so the
/// exe is copied in (a plugin command roots at the exe's folder).
fn with_exe(tag: &str) -> PathBuf {
    let dir = scratch(tag);
    std::fs::copy(env!("CARGO_BIN_EXE_sc-offline"), dir.join("sc-offline.exe")).unwrap();
    dir
}

fn run_copy(dir: &Path, args: &[&str]) -> (i32, String, String) {
    let o = std::process::Command::new(dir.join("sc-offline.exe"))
        .args(args)
        .current_dir(dir)
        .env("SC_OFFLINE_GUI", "1")
        .stdin(std::process::Stdio::null())
        .output()
        .unwrap();
    (
        o.status.code().unwrap_or(-1),
        String::from_utf8_lossy(&o.stdout).replace("\r\n", "\n"),
        String::from_utf8_lossy(&o.stderr).replace("\r\n", "\n"),
    )
}

#[test]
fn a_plain_catalogue_command_reports_that_nothing_is_published_without_a_request() {
    let dir = with_exe("cat-none");
    for a in [
        &["plugins", "catalogue", "list"][..],
        &["plugins", "catalogue", "install", "demo"][..],
        &["plugins", "catalogue", "update"][..],
        &["plugins", "update"][..],
    ] {
        let (code, out, err) = run_copy(&dir, a);
        assert_eq!(code, 1, "{a:?}: {out}{err}");
        assert_eq!(
            err, "no catalogue is published for this build yet\n",
            "{a:?}"
        );
        assert_eq!(out, "");
    }
    // nothing was written: no data folder was even made
    assert!(!dir.join("data").exists());
    // a local folder is still asked for explicitly, and a missing one is an ordinary error
    let (code, _, err) = run_copy(
        &dir,
        &[
            "plugins",
            "catalogue",
            "list",
            "--source",
            &dir.join("no-such-folder").to_string_lossy(),
        ],
    );
    assert_eq!(code, 1);
    assert!(
        err.contains("refused") || err.contains("no-such-folder"),
        "{err}"
    );
}

#[test]
fn plugin_settings_through_the_exe() {
    let dir = with_exe("settings");
    let p = dir.join("data").join("plugins").join("demo");
    std::fs::create_dir_all(&p).unwrap();
    std::fs::write(
        p.join("plugin.ini"),
        "name = Demo\nversion = 1\n[settings]\nlevel = int default 2 min 1 max 5 label \"Level\"\n",
    )
    .unwrap();
    let (code, out, err) = run_copy(&dir, &["plugins", "settings", "demo"]);
    assert_eq!(code, 0, "{err}");
    assert!(out.contains("level = 2  (int, default)  Level"), "{out}");
    // writing works while no StarCitizen.exe runs on this PC (a stand-in game of another test would stop it)
    let (code, out, err) = run_copy(&dir, &["plugins", "settings", "demo", "level=4"]);
    if code == 1 && err.contains("the game is running") {
        eprintln!(
            "a StarCitizen.exe is running on this PC; the write half of this test is skipped"
        );
        return;
    }
    assert_eq!(code, 0, "{err}");
    assert!(out.contains("level set to 4"), "{out}");
    let (_, out, _) = run_copy(&dir, &["plugins", "settings", "demo"]);
    assert!(out.contains("level = 4  (int, kept)"), "{out}");
    let (code, _, err) = run_copy(&dir, &["plugins", "settings", "demo", "level=9"]);
    assert_eq!(code, 1);
    assert!(err.contains("Level: at most 5"), "{err}");
    // the database is a plain SQLite file with the host's key and value shape
    let db = std::fs::read(dir.join("data").join("storage").join("demo.db")).unwrap();
    assert!(db.starts_with(b"SQLite format 3\0"));
    assert!(db
        .windows(b"sco.settings.level".len())
        .any(|w| w == b"sco.settings.level"));
    assert!(db.windows(b"int:4".len()).any(|w| w == b"int:4"));
}

#[test]
fn the_usage_says_plugins_and_stop() {
    let dir = scratch("usage");
    let (code, out, _) = exe(&dir, &["help"]);
    assert_eq!(code, 0);
    assert!(
        out.contains("\n  stop       ask the running game to close"),
        "{out}"
    );
    assert!(
        out.contains("\n  plugins    list, enable, disable"),
        "{out}"
    );
}

#[test]
fn the_window_restart_reports_when_the_new_exe_cannot_start() {
    // (a running restart is not exercised here: it would open a window)
    let missing = scratch("restart").join("no-such.exe");
    assert!(!scol_cli::window::restart_window(&missing));
}

// ---- report ---------------------------------------------------------------------------------

#[test]
fn report_reads_its_own_flag_and_writes_the_bundle_into_data_reports() {
    use scol_cli::report_args;
    use scol_play::report::ReportArgs;
    let on = |game: &str, game_log: bool| {
        Some(ReportArgs {
            game: game.to_string(),
            game_log,
        })
    };
    assert_eq!(report_args(&args(&[])), on("", true));
    assert_eq!(report_args(&args(&["--no-game-log"])), on("", false));
    assert_eq!(
        report_args(&args(&["--NO-GAME-LOG", "--game", "D:\\g"])),
        on("D:\\g", false)
    );
    assert_eq!(report_args(&args(&["--game"])), None);
    assert_eq!(report_args(&args(&["--dry-run"])), None);

    let (here, _bin) = world("report");
    std::fs::write(here.join("data").join("mod.log"), b"a line of the mod").unwrap();
    let mut be = backend();
    let (code, out) = run_stop_cli(&mut be, &here, &["report", "--no-game-log"]);
    assert_eq!(code, 0, "{out}");
    assert_eq!(be.report_calls, [on("", false).unwrap()]);
    assert!(out.contains("Report:   saved: "), "{out}");
    assert!(
        out.contains("Game.log was left out (--no-game-log)."),
        "{out}"
    );
    let reports: Vec<_> = std::fs::read_dir(here.join("data").join("reports"))
        .unwrap()
        .flatten()
        .collect();
    assert_eq!(reports.len(), 1, "one bundle in data\\reports");
    assert!(reports[0].path().join("status.txt").is_file());
    assert!(reports[0].path().join("mod.log").is_file());

    // The command word after an option, as every other command allows.
    let (code, out) = run_stop_cli(&mut be, &here, &["--game", "D:\\nowhere", "report"]);
    assert_eq!(code, 0, "{out}");
    assert_eq!(be.report_calls.len(), 2);
    assert_eq!(be.report_calls[1], on("D:\\nowhere", true).unwrap());

    // Anything else after `report` is an unknown argument: usage, then the error, and nothing is written.
    let (code, out) = run_stop_cli(&mut be, &here, &["report", "--send"]);
    assert_eq!(code, 1, "{out}");
    assert!(out.starts_with("usage: sc-offline.exe"), "{out}");
    assert!(out.contains("[!] unknown argument '--send'"), "{out}");
    assert_eq!(be.report_calls.len(), 2);
}
