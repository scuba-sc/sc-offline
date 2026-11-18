// SPDX-License-Identifier: GPL-3.0-only
//! Command line behaviour of `help` and `status` on synthetic folders, in process. The comparison with
//! the C++ launcher's output is `launcher-rs/tests/status/compare_status.py`.
#![cfg(windows)]

use scol_cli::{parse_args, run, Parsed, USAGE};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

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
            format!("sc-offline launcher 0.7.8.3 (based on ChrisWareOffline 0.9.0-rc1)\n\n{USAGE}")
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
    assert_eq!(USAGE.lines().count(), 16);
}

#[test]
fn argument_rules() {
    assert_eq!(
        parse_args(&args(&[])),
        Parsed::Run {
            command: "play".into(),
            game: "".into()
        }
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
        Parsed::Run {
            command: "status".into(),
            game: "X:\\g".into()
        }
    );
    assert_eq!(
        parse_args(&args(&["status", "--game"])),
        Parsed::Stop {
            usage: false,
            message: "--game needs a folder: sc-offline.exe --game \"D:\\Games\\StarCitizen\""
                .into()
        }
    );
    // a second command word is an unknown argument, as in the C++ loop
    assert_eq!(
        parse_args(&args(&["status", "help"])),
        Parsed::Stop {
            usage: true,
            message: "unknown argument 'help'".into()
        }
    );
    assert!(matches!(
        parse_args(&args(&["--window"])),
        Parsed::Stop { usage: false, .. }
    ));
}

#[test]
fn unknown_argument_prints_usage_then_error() {
    let (code, text) = run_text(&["bogus"], ".");
    assert_eq!(code, 1);
    assert_eq!(text, format!("{USAGE}[!] unknown argument 'bogus'\n"));
}

#[test]
fn commands_that_change_things_are_not_built_yet() {
    for c in ["play", "install", "uninstall", "update"] {
        let (code, text) = run_text(&[c], ".");
        assert_eq!(code, 1);
        assert!(
            text.starts_with(&format!(
                "[!] '{c}' is not available in the Rust launcher yet"
            )),
            "{text}"
        );
    }
    // no arguments means play
    let (code, text) = run_text(&[], ".");
    assert_eq!(code, 1);
    assert!(text.contains("'play' is not available"));
}

#[test]
fn status_reports_a_synthetic_game_and_writes_nothing() {
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
    std::fs::write(bin.join("dinput8.dll"), b"MZ sc-offline v0.7.8.3").unwrap();
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
    assert!(text.starts_with("sc-offline launcher 0.7.8.3\n\n"));
    assert!(
        text.contains(&format!("Folder:   {}\n", bin.to_string_lossy())),
        "{text}"
    );
    assert!(text.contains("Mod DLL:  missing (dinput8.dll next to sc-offline.exe)\n"));
    assert!(text.contains("Installed: yes, sc-offline 0.7.8.3, since 2026-11-17 09:05:03\n"));
    assert!(text.ends_with("\n\n"));
    assert_eq!(tree(&root), before, "status changed the folder tree");
    let _ = std::fs::remove_dir_all(&root);
}

#[test]
fn status_errors_exit_one() {
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
fn no_network_crate_in_the_dependency_lock() {
    // `status` makes no network request: nothing in the lock file can open a connection.
    let lock =
        std::fs::read_to_string(Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Cargo.lock"))
            .unwrap();
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
            !lock.contains(&format!("name = \"{name}\"")),
            "{name} is in Cargo.lock: the launcher's read-only status must stay offline"
        );
    }
}
