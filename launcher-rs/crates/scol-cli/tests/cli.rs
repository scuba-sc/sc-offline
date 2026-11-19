// SPDX-License-Identifier: GPL-3.0-only
//! Command line behaviour of `help` and `status` on synthetic folders, in process. The comparison with
//! the C++ launcher's output is `launcher-rs/tests/status/compare_status.py`.
#![cfg(windows)]

use scol_cli::testing::FakeBackend;
use scol_cli::{parse_args, run, usage_without_rust_only_lines, Parsed, USAGE};
use scol_cli::{run_with, Streams};
use scol_pcchanges::fake::FakeSystem;
use scol_play::fake::{FakeHost, SharedSystem};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, MutexGuard};

fn quiet() {
    // No "Press Enter" wait in a test run without a console of its own.
    std::env::set_var("SC_OFFLINE_GUI", "1");
}

fn args(a: &[&str]) -> Vec<String> {
    a.iter().map(|s| s.to_string()).collect()
}

fn run_text(a: &[&str], here: &str) -> (i32, String) {
    quiet();
    let mut out = Vec::new();
    let code = run(&args(a), here, &mut out);
    (code, String::from_utf8(out).unwrap())
}

fn scratch(tag: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("scol-cli-{tag}-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

fn tree(root: &Path) -> BTreeMap<String, (bool, u64, Option<Vec<u8>>)> {
    fn walk(dir: &Path, root: &Path, out: &mut BTreeMap<String, (bool, u64, Option<Vec<u8>>)>) {
        for e in std::fs::read_dir(dir).unwrap().flatten() {
            let p = e.path();
            let m = std::fs::symlink_metadata(&p).unwrap();
            let rel = p.strip_prefix(root).unwrap().to_string_lossy().into_owned();
            if m.is_dir() {
                out.insert(rel, (true, 0, None));
                walk(&p, root, out);
            } else {
                out.insert(rel, (false, m.len(), Some(std::fs::read(&p).unwrap())));
            }
        }
    }
    let mut out = BTreeMap::new();
    walk(root, root, &mut out);
    out
}

#[test]
fn help_is_banner_then_usage() {
    for form in [
        &["help"][..],
        &["--help"],
        &["-h"],
        &["/?"],
        &["HELP"],
        &["--dry-run", "help"],
    ] {
        let (code, text) = run_text(form, ".");
        assert_eq!(code, 0, "{form:?}");
        assert_eq!(
            text,
            format!("sc-offline launcher 0.8.0 (based on ChrisWareOffline 0.9.0-rc1)\n\n{USAGE}")
        );
    }
}

#[test]
fn usage_text_is_the_cpp_text() {
    // The same text the C++ launcher prints; the script compares the two programs' output, this
    // pins the pieces a reader looks for.
    assert!(USAGE.starts_with("usage: sc-offline.exe [command] [--game <folder>] [--dry-run] [--skip-eac-check]\n\n  play       (default)"));
    assert!(USAGE.contains(
        "\n  status     check the setup and report; read-only (no writes, no network)\n"
    ));
    assert!(USAGE
        .ends_with("exit codes: 0 ok, 1 error, 2 Easy Anti-Cheat active, 3 the game is running\n"));
    assert_eq!(USAGE.lines().count(), 21);
}

/// The text of `kUsage` in `launcher/launcher.cpp`, read from the source next to this workspace.
fn cpp_usage() -> Option<String> {
    let src = std::fs::read_to_string(
        Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../launcher/launcher.cpp"),
    )
    .ok()?;
    let start = src.find("static const char* kUsage =")?;
    let mut text = String::new();
    for line in src[start..].lines().skip(1) {
        let l = line.trim();
        let Some(body) = l.strip_prefix('"') else {
            break;
        };
        let (body, last) = match body.strip_suffix("\";") {
            Some(b) => (b, true),
            None => (body.strip_suffix('"')?, false),
        };
        text.push_str(&body.replace("\\n", "\n"));
        if last {
            break;
        }
    }
    Some(text)
}

#[test]
fn the_usage_differs_from_the_cpp_usage_by_exactly_the_two_rust_only_lines() {
    let Some(cpp) = cpp_usage() else {
        eprintln!("launcher.cpp is not next to this workspace; skipped");
        return;
    };
    assert!(cpp.starts_with("usage: sc-offline.exe [command]"), "{cpp}");
    // C++ has neither of them, Rust has both, and nothing else differs
    for rust_only in scol_cli::CPP_USAGE_LINES_NOT_HERE {
        assert!(USAGE.contains(&format!("{rust_only}\n")), "{rust_only}");
        assert!(!cpp.contains(rust_only), "{rust_only}");
    }
    assert_eq!(usage_without_rust_only_lines(USAGE), cpp);
    assert_eq!(USAGE.lines().count(), cpp.lines().count() + 2);
    // and `plugins` and `stop` are said exactly once each
    assert_eq!(USAGE.matches("\n  plugins ").count(), 1);
    assert_eq!(USAGE.matches("\n  stop ").count(), 1);
}

/// Tests that set the process environment the checks read run one at a time.
static LOCK: Mutex<()> = Mutex::new(());

fn lock() -> MutexGuard<'static, ()> {
    LOCK.lock().unwrap_or_else(|e| e.into_inner())
}

/// Runs the command line over a backend with the given stdin; returns the code, stdout and stderr.
fn run_streams(
    a: &[&str],
    here: &str,
    be: &mut FakeBackend,
    stdin: &[u8],
) -> (i32, String, String) {
    quiet();
    let (mut out, mut err) = (Vec::new(), Vec::new());
    let mut input = stdin;
    let code = run_with(
        &args(a),
        here,
        &mut Streams {
            stdin: &mut input,
            stdout: &mut out,
            stderr: &mut err,
        },
        be,
    );
    (
        code,
        String::from_utf8_lossy(&out).replace("\r\n", "\n"),
        String::from_utf8_lossy(&err).replace("\r\n", "\n"),
    )
}

#[test]
fn argument_rules() {
    let run_of =
        |command: &str, game: &str, dry: bool, skip_eac: bool, game_log: bool| Parsed::Run {
            command: command.into(),
            game: game.into(),
            dry,
            skip_eac,
            game_log,
            end: false,
        };
    assert_eq!(
        parse_args(&args(&[])),
        run_of("play", "", false, false, false)
    );
    assert_eq!(
        parse_args(&args(&[
            "--dry-run",
            "STATUS",
            "--game",
            "X:\\g",
            "--console",
            "--skip-eac-check"
        ])),
        run_of("status", "X:\\g", true, true, false)
    );
    assert_eq!(
        parse_args(&args(&["uninstall"])),
        run_of("uninstall", "", false, false, false)
    );
    assert_eq!(
        parse_args(&args(&["mcp", "--game-log"])),
        run_of("mcp", "", false, false, true)
    );
    assert_eq!(
        parse_args(&args(&["--game-log", "MCP"])),
        run_of("mcp", "", false, false, true)
    );
    assert_eq!(
        parse_args(&args(&["status", "--game"])),
        Parsed::Stop {
            usage: false,
            to_stderr: false,
            message: "--game needs a folder: sc-offline.exe --game \"D:\\Games\\StarCitizen\""
                .into()
        }
    );
    // a second command word is an unknown argument, as in the C++ loop
    assert_eq!(
        parse_args(&args(&["status", "help"])),
        Parsed::Stop {
            usage: true,
            to_stderr: false,
            message: "unknown argument 'help'".into()
        }
    );
    // stop takes --game and --end; --end belongs to stop alone
    assert_eq!(
        parse_args(&args(&["stop", "--end", "--game", "X:\\g"])),
        Parsed::Run {
            command: "stop".into(),
            game: "X:\\g".into(),
            dry: false,
            skip_eac: false,
            game_log: false,
            end: true
        }
    );
    assert_eq!(
        parse_args(&args(&["STOP"])),
        run_of("stop", "", false, false, false)
    );
    assert_eq!(
        parse_args(&args(&["play", "--end"])),
        Parsed::Stop {
            usage: true,
            to_stderr: false,
            message: "unknown argument '--end' (it belongs to `sc-offline.exe stop --end`)".into()
        }
    );
    // --game-log belongs to mcp
    assert_eq!(
        parse_args(&args(&["play", "--game-log"])),
        Parsed::Stop {
            usage: true,
            to_stderr: false,
            message:
                "unknown argument '--game-log' (it belongs to `sc-offline.exe mcp --game-log`)"
                    .into()
        }
    );
    // with mcp anywhere, the usage goes to stderr: stdout belongs to the protocol
    assert_eq!(
        parse_args(&args(&["mcp", "--bogus"])),
        Parsed::Stop {
            usage: true,
            to_stderr: true,
            message: "unknown argument '--bogus'".into()
        }
    );
    // `--window` alone is handled before the argument loop; inside the loop it is an unknown argument
    assert!(matches!(
        parse_args(&args(&["--window"])),
        Parsed::Stop { usage: true, .. }
    ));
}

#[test]
fn unknown_argument_prints_usage_then_error() {
    let (code, text) = run_text(&["bogus"], ".");
    assert_eq!(code, 1);
    assert_eq!(text, format!("{USAGE}[!] unknown argument 'bogus'\n"));
    let (code, text) = run_text(&["--game-log"], ".");
    assert_eq!(code, 1);
    assert_eq!(
        text,
        format!("{USAGE}[!] unknown argument '--game-log' (it belongs to `sc-offline.exe mcp --game-log`)\n")
    );
}

#[test]
fn mcp_errors_go_to_stderr_and_help_to_stdout() {
    quiet();
    let mut be = FakeBackend::new(FakeHost::new(".", SharedSystem::default()));
    let (code, out, err) = run_streams(&["mcp", "--bogus"], ".", &mut be, b"");
    assert_eq!(code, 1);
    assert_eq!(out, "", "stdout belongs to the protocol");
    assert_eq!(err, format!("{USAGE}[!] unknown argument '--bogus'\n"));
    // `--help` after `mcp` is the same help as anywhere: the last of the two command words wins in C++
    let (code, out, _) = run_streams(&["mcp", "--help"], ".", &mut be, b"");
    assert_eq!(code, 0);
    assert!(
        out.starts_with("sc-offline launcher 0.8.0 (based on"),
        "{out}"
    );
    // end of input ends the server with exit 0 and nothing but protocol lines on stdout
    let here = scratch("mcp");
    std::fs::write(here.join("dinput8.dll"), b"x").unwrap();
    let (code, out, err) = run_streams(
        &["mcp"],
        &here.to_string_lossy(),
        &mut be,
        b"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}\n",
    );
    assert_eq!(code, 0, "{err}");
    assert!(out.lines().all(|l| l.starts_with('{')), "{out}");
    assert!(out.contains("\"id\":1"), "{out}");
    assert!(
        err.starts_with("sc-offline mcp: read-only server on stdio"),
        "{err}"
    );
    let _ = std::fs::remove_dir_all(&here);
}

#[test]
fn update_window_and_plugins_are_dispatched() {
    let _g = lock();
    let mut be = FakeBackend::new(fake_host(".", &scratch("dispatch")));
    // plain update and its extra flags reach the updater
    let (code, _, _) = run_streams(&["update"], ".", &mut be, b"");
    assert_eq!(code, 0);
    let (code, _, _) = run_streams(&["update", "--check"], ".", &mut be, b"");
    assert_eq!(code, 0);
    let (code, _, _) = run_streams(
        &["--elevated-update", "v1.2.3", "staged"],
        ".",
        &mut be,
        b"",
    );
    assert_eq!(code, 0);
    assert_eq!(
        be.update_calls,
        vec![
            vec!["update".to_string()],
            vec!["update".to_string(), "--check".to_string()],
            vec![
                "--elevated-update".to_string(),
                "v1.2.3".to_string(),
                "staged".to_string()
            ],
        ]
    );
    // an unknown flag after update is still an unknown argument
    let (code, text, _) = run_streams(&["update", "--bogus"], ".", &mut be, b"");
    assert_eq!(code, 1);
    assert!(text.contains("unknown argument '--bogus'"), "{text}");
    // --window opens the window from a terminal; no arguments only when double-clicked
    let (code, _, _) = run_streams(&["--window"], ".", &mut be, b"");
    assert_eq!(code, 0);
    assert_eq!(be.window_calls, vec![false]);
    be.wants_window = true;
    let (code, _, _) = run_streams(&[], ".", &mut be, b"");
    assert_eq!(code, 0);
    assert_eq!(be.window_calls, vec![false, true]);
    // --window with other arguments is not the window
    let (code, text, _) = run_streams(&["--window", "status"], ".", &mut be, b"");
    assert_eq!(code, 1);
    assert!(text.contains("unknown argument '--window'"), "{text}");
}

#[test]
fn plugins_list_runs_the_plugin_manager_on_the_launcher_folder() {
    let _g = lock();
    let here = scratch("plugins");
    let mut be = FakeBackend::new(fake_host(&here.to_string_lossy(), &here));
    // empty folder: the manager answers with exit 0 and writes no data folder
    let (code, _, _) = run_streams(&["plugins", "list"], &here.to_string_lossy(), &mut be, b"");
    assert_eq!(code, 0);
    assert!(!here.join("data").exists());
    let (code, _, _) = run_streams(
        &["plugins", "nonsense"],
        &here.to_string_lossy(),
        &mut be,
        b"",
    );
    assert_eq!(code, 1);
}

#[test]
fn self_test_checks_version_sha_and_the_dll() {
    quiet();
    let here = scratch("selftest");
    let h = here.to_string_lossy().into_owned();
    let mut be = FakeBackend::new(FakeHost::new(&h, SharedSystem::default()));
    let (code, out, _) = run_streams(&["--self-test", "v0.8.0"], &h, &mut be, b"");
    assert_eq!(code, 4, "{out}");
    assert_eq!(out, "self-test: no dinput8.dll beside sc-offline.exe\n");
    std::fs::write(here.join("dinput8.dll"), b"x").unwrap();
    let (code, out, _) = run_streams(&["--self-test", "v0.8.0"], &h, &mut be, b"");
    assert_eq!(
        (code, out.as_str()),
        (0, "self-test: sc-offline 0.8.0 ok\n")
    );
    let (code, out, _) = run_streams(&["--self-test", "v9.9.9"], &h, &mut be, b"");
    assert_eq!(
        (code, out.as_str()),
        (2, "self-test: this is 0.8.0, expected v9.9.9\n")
    );
    // a wrong argument count is not the hidden mode: unknown argument, as in C++
    let (code, out, _) = run_streams(&["--self-test"], &h, &mut be, b"");
    assert_eq!(code, 1);
    assert!(
        out.ends_with("[!] unknown argument '--self-test'\n"),
        "{out}"
    );
    let _ = std::fs::remove_dir_all(&here);
}

#[test]
fn helper_and_delete_logs_are_dispatched_by_argument_count() {
    quiet();
    let mut be = FakeBackend::new(FakeHost::new(".", SharedSystem::default()));
    // 6 arguments after the exe name: the helper (here with a bad operation, which exits 4)
    let (code, _, _) = run_streams(
        &["--helper", "bogus", "D:\\x\\Bin64", "1", "Local\\e", "-"],
        ".",
        &mut be,
        b"",
    );
    assert_eq!(code, 4);
    assert_eq!(be.helper_calls.len(), 1);
    // 7 with the allow list
    let (code, _, _) = run_streams(
        &[
            "--helper",
            "bogus",
            "D:\\x\\Bin64",
            "1",
            "Local\\e",
            "fl",
            "10.0.0.0/8",
        ],
        ".",
        &mut be,
        b"",
    );
    assert_eq!(code, 4);
    assert_eq!(be.helper_calls.len(), 2);
    // 5 is not the helper
    let (code, out, _) = run_streams(
        &["--helper", "play", "D:\\x\\Bin64", "1", "Local\\e"],
        ".",
        &mut be,
        b"",
    );
    assert_eq!(code, 1);
    assert!(out.contains("unknown argument '--helper'"), "{out}");
    assert_eq!(be.helper_calls.len(), 2);
    // --delete-logs refuses a folder that is not a game channel folder
    let (code, _, _) = run_streams(&["--delete-logs", "D:\\no\\such", "1"], ".", &mut be, b"");
    assert_eq!(code, 4);
    assert_eq!(
        be.delete_calls,
        vec![("D:\\no\\such".to_string(), "1".to_string())]
    );
}

/// A synthetic launcher folder and game under the system temp folder, with the environment the checks read pointed at it.
fn world(tag: &str) -> (PathBuf, String, String) {
    quiet();
    let root = std::env::temp_dir().join(format!("scol-cli-{tag}-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&root);
    let here = root.join("here");
    let bin = root
        .join("lib")
        .join("StarCitizen")
        .join("LIVE")
        .join("Bin64");
    for d in [
        here.join("data"),
        bin.clone(),
        root.join("pd"),
        root.join("pf86"),
    ] {
        std::fs::create_dir_all(d).unwrap();
    }
    std::fs::write(here.join("dinput8.dll"), b"MZ sc-offline v0.8.0 stand-in").unwrap();
    std::fs::write(bin.join("StarCitizen.exe"), b"stand-in").unwrap();
    std::fs::write(
        here.join("sc-offline.ini"),
        "check_updates = off\nclean_logs = never\n",
    )
    .unwrap();
    std::env::set_var("ProgramData", root.join("pd"));
    std::env::set_var("ProgramFiles(x86)", root.join("pf86"));
    std::env::set_var("APPDATA", root.join("appdata"));
    let game = root
        .join("lib")
        .join("StarCitizen")
        .to_string_lossy()
        .into_owned();
    (root, here.to_string_lossy().into_owned(), game)
}

fn fake_host(here: &str, root: &Path) -> FakeHost {
    let mut sys = FakeSystem::new();
    sys.env.insert(
        "ProgramData".into(),
        root.join("pd").to_string_lossy().into_owned(),
    );
    sys.env.insert(
        "ProgramFiles(x86)".into(),
        root.join("pf86").to_string_lossy().into_owned(),
    );
    let bin = root.join("lib").join("StarCitizen").join("LIVE");
    sys.writable
        .insert(bin.join("Bin64").to_string_lossy().into_owned());
    sys.writable.insert(bin.to_string_lossy().into_owned());
    FakeHost::new(here, SharedSystem::new(sys))
}

#[test]
fn install_and_uninstall_through_the_command_line() {
    let _g = lock();
    let (root, here, game) = world("inst");
    let bin = root
        .join("lib")
        .join("StarCitizen")
        .join("LIVE")
        .join("Bin64");
    let mut be = FakeBackend::new(fake_host(&here, &root));
    let (code, out, _) = run_streams(
        &["install", "--game", &game, "--skip-eac-check"],
        &here,
        &mut be,
        b"",
    );
    assert_eq!(code, 0, "{out}");
    assert!(out.ends_with("Mod:      installed. Start the game your usual way; run `sc-offline.exe uninstall` before going online.\n"), "{out}");
    assert!(bin.join("sc-offline.installed").exists() && bin.join("dinput8.dll").exists());
    let log = std::fs::read_to_string(format!("{here}\\data\\launcher.log")).unwrap();
    assert!(
        log.lines().next().unwrap().ends_with(&format!(
            " sc-offline.exe install --game {game} --skip-eac-check"
        )),
        "{log}"
    );
    let (code, out, _) = run_streams(&["uninstall", "--game", &game], &here, &mut be, b"");
    assert_eq!(code, 0, "{out}");
    assert!(out.ends_with("Mod:      removed.\n"), "{out}");
    assert!(!bin.join("sc-offline.installed").exists() && !bin.join("dinput8.dll").exists());
    let _ = std::fs::remove_dir_all(&root);
}

#[test]
fn play_dry_run_through_the_command_line_changes_nothing() {
    let _g = lock();
    let (root, here, game) = world("dry");
    let before = tree(&root);
    let mut be = FakeBackend::new(fake_host(&here, &root));
    let (code, out, _) = run_streams(&["--dry-run", "--game", &game], &here, &mut be, b"");
    assert_eq!(code, 0, "{out}");
    assert!(
        out.starts_with("sc-offline launcher 0.8.0 - dry run, nothing is changed\n\n"),
        "{out}"
    );
    assert!(out.contains("[dry-run] would start "), "{out}");
    let after = tree(&root);
    let changed: Vec<_> = after
        .iter()
        .filter(|(k, v)| before.get(*k) != Some(v))
        .map(|(k, _)| k.clone())
        .collect();
    assert_eq!(
        changed,
        vec![r"here\data\launcher.log".to_string()],
        "{changed:?}"
    );
    // uninstall dry run: same
    let (code, out, _) = run_streams(
        &["uninstall", "--dry-run", "--game", &game],
        &here,
        &mut be,
        b"",
    );
    assert_eq!(code, 0, "{out}");
    let _ = std::fs::remove_dir_all(&root);
}

#[test]
fn play_with_a_running_game_exits_3_through_the_command_line() {
    // a stand-in process named StarCitizen.exe is not available to this crate's tests; the exit code
    // path is covered in scol-play (a_running_game_stops_everything_with_exit_3). Here: the exit code
    // table of the usage text is the one the flow returns.
    assert!(USAGE
        .contains("exit codes: 0 ok, 1 error, 2 Easy Anti-Cheat active, 3 the game is running"));
    assert_eq!(scol_contract::exit_code::GAME_RUNNING, 3);
    assert_eq!(scol_contract::exit_code::EAC_ACTIVE, 2);
}

#[test]
fn status_reports_a_synthetic_game_and_writes_nothing() {
    let _g = lock();
    let root = scratch("status");
    let here = root.join("here");
    let bin = root
        .join("lib")
        .join("StarCitizen")
        .join("LIVE")
        .join("Bin64");
    std::fs::create_dir_all(&here).unwrap();
    std::fs::create_dir_all(&bin).unwrap();
    std::fs::write(bin.join("StarCitizen.exe"), b"stand-in").unwrap();
    std::fs::write(bin.join("dinput8.dll"), b"MZ sc-offline v0.8.0").unwrap();
    std::fs::write(
        bin.join("sc-offline.installed"),
        b"time=2026-11-17 09:05:03\n",
    )
    .unwrap();
    std::fs::write(here.join("sc-offline.ini"), b"plugins = on\n").unwrap();
    let before = tree(&root);
    let game = root
        .join("lib")
        .join("StarCitizen")
        .to_string_lossy()
        .into_owned();
    let (code, text) = run_text(&["status", "--game", &game], &here.to_string_lossy());
    assert_eq!(code, 0, "{text}");
    assert!(text.starts_with("sc-offline launcher 0.8.0\n\n"));
    assert!(
        text.contains(&format!("Folder:   {}\n", bin.to_string_lossy())),
        "{text}"
    );
    assert!(text.contains("Mod DLL:  missing (dinput8.dll next to sc-offline.exe)\n"));
    assert!(text.contains("Installed: yes, sc-offline 0.8.0, since 2026-11-17 09:05:03\n"));
    assert!(text.ends_with("\n\n"));
    assert_eq!(tree(&root), before, "status changed the folder tree");
    let _ = std::fs::remove_dir_all(&root);
}

#[test]
fn status_errors_exit_one() {
    let _g = lock();
    let root = scratch("missing");
    let here = root.to_string_lossy().into_owned();
    let nothing = root.join("nothing").to_string_lossy().into_owned();
    let (code, text) = run_text(&["status", "--game", &nothing], &here);
    assert_eq!(code, 1);
    assert!(
        text.contains(&format!(
            "[!] no StarCitizen.exe under --game {nothing} (channel LIVE)\n"
        )),
        "{text}"
    );
    std::fs::write(root.join("sc-offline.ini"), format!("game = {nothing}\n")).unwrap();
    let (code, text) = run_text(&["status"], &here);
    assert_eq!(code, 1);
    assert!(text.contains(&format!(
        "[!] no StarCitizen.exe under game = {nothing} in sc-offline.ini (channel LIVE)\n"
    )));
    let _ = std::fs::remove_dir_all(&root);
}

#[test]
fn no_network_crate_in_the_dependency_closure() {
    // The only network code in this binary is WinHTTP through windows-sys (the updater and the catalogue):
    // no HTTP client, TLS library or async runtime crate may be in the Windows build closure. The closure
    // comes from `cargo tree` for the Windows target (the lock file also lists crates only other targets,
    // such as wasm, would build).
    let cargo = std::env::var("CARGO").unwrap_or_else(|_| "cargo".into());
    let out = std::process::Command::new(cargo)
        .args([
            "tree",
            "-p",
            "scol-cli",
            "--target",
            "x86_64-pc-windows-msvc",
            "-e",
            "normal",
            "--prefix",
            "none",
            "--offline",
        ])
        .current_dir(Path::new(env!("CARGO_MANIFEST_DIR")))
        .output()
        .expect("cargo tree runs");
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let text = String::from_utf8_lossy(&out.stdout);
    let seen: std::collections::BTreeSet<String> = text
        .lines()
        .filter_map(|l| l.split_whitespace().next())
        .map(str::to_string)
        .collect();
    assert!(
        seen.contains("scol-play") && seen.contains("scol-mcp"),
        "closure walk is broken: {seen:?}"
    );
    for name in [
        "reqwest",
        "hyper",
        "ureq",
        "curl",
        "tokio",
        "mio",
        "socket2",
        "native-tls",
        "rustls",
        "openssl",
        "schannel",
        "winhttp",
        "attohttpc",
        "isahc",
        "surf",
        "h2",
    ] {
        assert!(
            !seen.contains(name),
            "{name} is in the dependency closure of scol-cli: network access is WinHTTP only"
        );
    }
}
