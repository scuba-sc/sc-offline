// SPDX-License-Identifier: GPL-3.0-only
//! The whole play flow over the fake host: the helper on a thread, the stand-in game process, a fake
//! system for the PC changes. Every folder is under `D:\t`. Nothing here starts the real game or changes
//! this PC; nothing was run in game.
#![cfg(windows)]

mod common;
use common::*;
use scol_play::fake::GameBehaviour;
use scol_play::Mode;

const NO_PC: &str = "block_network = off\neac_hosts = off\neac_rename = off\ncheck_updates = off\nclean_logs = never\ncrash_reports = off\n";

fn has(text: &str, line: &str) {
    assert!(
        text.lines().any(|l| l == line),
        "missing line {line:?} in:\n{text}"
    );
}

#[test]
fn play_end_to_end_with_the_stand_in_game() {
    let w = World::new("play", NO_PC);
    let mut host = w.host(w.system());
    let (marker, dll) = (w.marker(), w.dll());
    host.on_game = Some(Box::new(move |h| {
        // While the game runs: the mod is in the game folder and the marker says when.
        assert!(
            std::path::Path::new(&marker).exists(),
            "marker while playing"
        );
        assert_eq!(std::fs::read(&dll).unwrap(), OURS_DLL);
        h.calls.push("checked the game folder".into());
    }));
    let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
    assert_eq!(code, 0, "{text}");
    assert!(text.starts_with("sc-offline launcher 0.8.0\n\n"), "{text}");
    has(&text, &format!("Folder:   {}", w.bin));
    has(&text, "Mod:      copied into Bin64");
    has(
        &text,
        "Playing. When the game closes, the mod is removed from Bin64.",
    );
    // the blank line before "Playing" belongs to the C++ text
    assert!(text.contains("\n\nPlaying."), "{text}");
    has(
        &text,
        "Mod:      removed from Bin64; PC changes undone. Safe to go online.",
    );
    assert!(
        !text.contains("PC:       "),
        "no PC change was wanted: {text}"
    );
    // gone again
    assert!(!w.exists(&w.marker()) && !w.exists(&w.dll()));
    // the order of the host calls
    let calls = host.calls.join("\n");
    let pos = |needle: &str| {
        calls
            .find(needle)
            .unwrap_or_else(|| panic!("{needle} not in\n{calls}"))
    };
    assert!(pos("create_session") < pos("start_helper"));
    assert!(pos("start_helper") < pos("wait_ready"));
    assert!(pos("wait_ready") < pos("start_game"));
    assert!(pos("start_game") < pos("wait_game"));
    assert!(pos("checked the game folder") < pos("signal_closed"));
    assert!(pos("signal_closed") < pos("wait_helper Some(120000)"));
    assert!(host.helper_starts.len() == 1 && !host.helper_starts[0].elevated);
    assert!(host.helper_starts[0].args.starts_with(&format!(
        "--helper play \"{}\" 4242 Local\\sc-offline-ready-4242 -",
        w.bin
    )));
    assert_eq!(host.update_checks, 0, "check_updates = off");
    // the log has the run's text and the helper's step lines, header first
    let log = w.log();
    assert!(log.starts_with("--- "), "{log}");
    assert!(log
        .lines()
        .next()
        .unwrap()
        .ends_with(" sc-offline.exe play"));
    assert!(
        log.contains("--- ") && log.contains(" helper play"),
        "{log}"
    );
    assert!(log.contains("[+] copy "), "helper steps in the log: {log}");
    assert!(log.contains("Mod:      copied into Bin64"));
}

#[test]
fn environment_has_the_play_block_and_the_capability_switches() {
    let w = World::new(
        "env",
        &format!("{NO_PC}boot_map = PU\nstart_ship = ANVL_Arrow\nplugins = off\nexp_cvars_native = off\nexp_entities_diag = on\n"),
    );
    let mut host = w.host(w.system());
    host.game = GameBehaviour::Scripted;
    host.env.insert("SCO_AUDIO_NATIVE".into(), "".into());
    host.env.insert("SCO_ENTITIES_DIAG".into(), "1".into());
    let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
    assert_eq!(code, 0, "{text}");
    let get = |n: &str| {
        host.env_log
            .iter()
            .rev()
            .find(|(k, _)| k == n)
            .map(|(_, v)| v.clone())
    };
    assert_eq!(get("SC_OFFLINE_BOOT_MAP").as_deref(), Some("PU"));
    assert_eq!(get("SC_OFFLINE_START_SHIP").as_deref(), Some("ANVL_Arrow"));
    assert_eq!(get("SC_OFFLINE_PLUGINS").as_deref(), Some("off"));
    assert_eq!(
        get("SC_OFFLINE_MOD_LOG").as_deref(),
        Some(format!("{}\\data\\mod.log", w.here).as_str())
    );
    assert_eq!(
        get("SC_USER").as_deref(),
        Some(format!("{}\\user\\client\\0", w.channel).as_str())
    );
    // five switches default on, one turned off in the ini, the diagnostic one turned on
    for on in [
        "SCO_LOADOUT_ASYNC",
        "SCO_INVENTORY_READ",
        "SCO_ACTOR_STATS_WRITE",
        "SCO_AUDIO_NATIVE",
        "SCO_ENTITIES_DIAG",
    ] {
        assert_eq!(get(on).as_deref(), Some("1"), "{on}");
    }
    assert_eq!(
        get("SCO_CVARS_NATIVE").as_deref(),
        Some(""),
        "off removes the variable"
    );
    assert!(!host.env.contains_key("SCO_CVARS_NATIVE"));
    assert!(
        text.contains("Capability switches on: exp_loadout_async (SCO_LOADOUT_ASYNC=1)"),
        "{text}"
    );
    assert!(
        text.contains("exp_entities_diag (SCO_ENTITIES_DIAG=1); not run in game\n"),
        "{text}"
    );
    let _ = (host.env.get("SCO_AUDIO_NATIVE"), &text);
}

#[test]
fn defaults_turn_five_switches_on_and_the_diagnostic_one_off() {
    let w = World::new("defaults", NO_PC);
    let mut host = w.host(w.system());
    host.game = GameBehaviour::Scripted;
    host.env.insert("SCO_ENTITIES_DIAG".into(), "1".into()); // inherited, key off
    let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
    assert_eq!(code, 0, "{text}");
    for on in [
        "SCO_LOADOUT_ASYNC",
        "SCO_INVENTORY_READ",
        "SCO_ACTOR_STATS_WRITE",
        "SCO_CVARS_NATIVE",
        "SCO_AUDIO_NATIVE",
    ] {
        assert_eq!(host.env.get(on).map(String::as_str), Some("1"), "{on}");
    }
    assert!(!host.env.contains_key("SCO_ENTITIES_DIAG"));
    assert!(text.contains("; not run in game\n"), "{text}");
    assert!(
        text.contains("[i] removed SCO_ENTITIES_DIAG from the game's environment: only the exp_ keys in sc-offline.ini set these\n"),
        "{text}"
    );
}

#[test]
fn pc_changes_are_made_through_the_elevated_helper_and_undone() {
    let w = World::new(
        "pc",
        "check_updates = off\nclean_logs = never\ncrash_reports = off\n",
    );
    let mut sys = w.system();
    w.with_eac(&mut sys);
    let mut host = w.host(sys);
    let sys_view = host.sys.clone();
    let (rec, eac, eac_bak) = (w.record_path(), w.eac.clone(), w.eac_bak.clone());
    host.on_game = Some(Box::new(move |h| {
        let sys = h.sys.clone();
        sys.with(|s| {
            let r = s.text(&rec).expect("pc-changes.txt while playing");
            assert!(r.contains("time=2026-11-17 09:05"), "{r}");
            assert!(
                r.contains("firewall=added")
                    && r.contains("hosts=added")
                    && r.contains("eac=renamed"),
                "{r}"
            );
            assert!(s.rules.contains("sc-offline: block StarCitizen.exe"));
            assert!(s.text(HOSTS).unwrap().contains(EAC_HOST));
            assert!(s.files.contains_key(&eac_bak) && !s.files.contains_key(&eac));
        });
    }));
    let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
    assert_eq!(code, 0, "{text}");
    assert!(
        host.helper_starts[0].elevated,
        "PC changes ask for administrator rights once"
    );
    assert!(host.helper_starts[0].notice.is_some());
    assert!(
        text.contains("[i] Windows will ask for administrator rights once:"),
        "{text}"
    );
    has(&text, "PC:       game, RSI Launcher and CrashHandler network blocked, EAC hosts line, EAC renamed (undone when the game closes)");
    has(
        &text,
        "Mod:      removed from Bin64; PC changes undone. Safe to go online.",
    );
    sys_view.with(|s| {
        assert!(s.rules.is_empty(), "rules left: {:?}", s.rules);
        assert!(s.text(&w.record_path()).is_none(), "record left");
        assert_eq!(s.text(HOSTS).unwrap(), "127.0.0.1 localhost\r\n");
        assert!(s.files.contains_key(&w.eac) && !s.files.contains_key(&w.eac_bak));
    });
    assert!(
        host.helper_starts[0].args.contains(" fhel"),
        "{}",
        host.helper_starts[0].args
    );
}

#[test]
fn dry_run_prints_every_step_and_changes_nothing() {
    let w = World::new("dry", "check_updates = off\n");
    let mut sys = w.system();
    w.with_eac(&mut sys);
    let mut host = w.host(sys);
    let before = tree(&w.root);
    let (code, text) = run(&w, &mut host, &args(Mode::Play, true, &["--dry-run"]));
    assert_eq!(code, 0, "{text}");
    assert!(text.starts_with("sc-offline launcher 0.8.0 - dry run, nothing is changed\n\n"));
    for needle in [
        "[dry-run] would add a firewall rule blocking ",
        "[dry-run] would add `127.0.0.1 modules-cdn.eac-prod.on.epicgames.com` to the hosts file",
        "[dry-run] would rename EasyAntiCheat_EOS.exe to .bak",
        "[dry-run] would copy ",
        "[dry-run] would set SC_OFFLINE_BOOT_MAP=PU_All SC_OFFLINE_START_SHIP=DRAK_Cutlass_Black SC_OFFLINE_START= SC_OFFLINE_PLUGINS=on",
        "[dry-run] would start ",
        "[dry-run] after the game closes: take the mod out, put back what was set aside, undo the PC changes",
        "[dry-run] then list this session's Game.log, logbackups and crash files and ask before deleting them",
    ] {
        assert!(text.contains(needle), "missing {needle:?} in:\n{text}");
    }
    let after = tree(&w.root);
    // only data\launcher.log is new
    let mut changed: Vec<_> = after
        .iter()
        .filter(|(k, v)| before.get(*k) != Some(v))
        .map(|(k, _)| k.clone())
        .collect();
    changed.sort();
    assert_eq!(
        changed,
        vec!["here\\data\\launcher.log".to_string()],
        "{changed:?}"
    );
    assert!(
        host.calls.is_empty(),
        "a dry run asks the host for nothing: {:?}",
        host.calls
    );
    host.sys.with(|s| {
        assert!(
            s.rules.is_empty() && s.commands().is_empty(),
            "{:?}",
            s.commands()
        )
    });
    assert_eq!(host.update_checks, 0, "a dry run never checks for updates");
}

#[test]
fn update_check_runs_only_for_a_real_play() {
    let w = World::new("upd", "clean_logs = never\ncrash_reports = off\nblock_network = off\neac_hosts = off\neac_rename = off\n");
    let mut host = w.host(w.system());
    host.game = GameBehaviour::Scripted;
    let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
    assert_eq!(code, 0, "{text}");
    assert_eq!(host.update_checks, 1);
}

#[test]
fn install_then_uninstall_through_the_helper() {
    let w = World::new("inst", NO_PC);
    std::fs::write(w.dll(), OLD_DLL).unwrap(); // another mod's dinput8.dll is set aside and put back
    let mut host = w.host(w.system());
    let (code, text) = run(&w, &mut host, &args(Mode::Install, false, &["install"]));
    assert_eq!(code, 0, "{text}");
    has(&text, "Mod:      installed. Start the game your usual way; run `sc-offline.exe uninstall` before going online.");
    assert!(w.exists(&w.marker()));
    assert_eq!(std::fs::read(w.dll()).unwrap(), OURS_DLL);
    assert!(w.exists(&format!("{}.sc-offline-backup", w.dll())));
    assert!(host.calls.iter().all(|c| !c.starts_with("start_game")));
    assert!(host.helper_starts[0].args.starts_with("--helper install "));

    let (code, text) = run(&w, &mut host, &args(Mode::Uninstall, false, &["uninstall"]));
    assert_eq!(code, 0, "{text}");
    has(&text, "Mod:      removed.");
    assert!(!w.exists(&w.marker()));
    assert_eq!(std::fs::read(w.dll()).unwrap(), OLD_DLL);
    assert!(host.helper_starts[1]
        .args
        .starts_with("--helper uninstall "));
}

#[test]
fn game_folder_that_needs_administrator_rights_elevates_the_helper() {
    let w = World::new("elev", NO_PC);
    let mut sys = w.system();
    sys.writable.clear();
    let mut host = w.host(sys);
    let (code, text) = run(&w, &mut host, &args(Mode::Install, false, &["install"]));
    assert_eq!(code, 0, "{text}");
    assert!(host.helper_starts[0].elevated);
    assert!(
        text.contains(
            "[i] the game folder needs administrator rights for the mod; Windows will ask once."
        ),
        "{text}"
    );
}

#[test]
fn a_running_game_stops_everything_with_exit_3() {
    let w = World::new("running", NO_PC);
    let exe = format!("{}\\StarCitizen.exe", w.bin);
    let mut child = std::process::Command::new(&exe)
        .env("SCOL_STANDIN_MS", "20000")
        .spawn()
        .unwrap();
    std::thread::sleep(std::time::Duration::from_millis(300));
    let mut host = w.host(w.system());
    let before = tree(&w.root);
    let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
    let (ucode, utext) = run(&w, &mut host, &args(Mode::Uninstall, false, &["uninstall"]));
    let _ = child.kill();
    let _ = child.wait();
    assert_eq!(code, 3, "{text}");
    has(
        &text,
        "[!] StarCitizen.exe is already running. Close it first.",
    );
    assert_eq!(ucode, 3, "{utext}");
    has(&utext, "[!] StarCitizen.exe is running. Close it first.");
    assert!(host.calls.is_empty());
    let after = tree(&w.root);
    let changed: Vec<_> = after
        .iter()
        .filter(|(k, v)| before.get(*k) != Some(v))
        .map(|(k, _)| k.clone())
        .collect();
    assert_eq!(changed.len(), 2, "{changed:?}"); // two runs: the second moves the first run's log aside
    assert!(
        changed
            .iter()
            .all(|c| c.starts_with(r"here\data\launcher.log")),
        "{changed:?}"
    );
}

#[test]
fn missing_mod_dll_and_active_eac_stop_before_any_change() {
    let w = World::new(
        "stop",
        "block_network = off\neac_hosts = off\neac_rename = off\ncheck_updates = off\n",
    );
    let mut sys = w.system();
    w.with_eac(&mut sys);
    let mut host = w.host(sys);
    // Easy Anti-Cheat on and nothing set to handle it: exit 2 unless --skip-eac-check
    let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
    assert_eq!(code, 2, "{text}");
    has(
        &text,
        "[!] stopped: Easy Anti-Cheat is active (see above). --skip-eac-check overrides this.",
    );
    assert!(host.calls.is_empty());
    std::fs::remove_file(format!("{}\\dinput8.dll", w.here)).unwrap();
    let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
    assert_eq!(code, 1, "{text}");
    assert!(text.contains("[!] dinput8.dll is missing next to sc-offline.exe.\n    Extract the whole zip into one folder and run this from there.\n"), "{text}");
}

#[test]
fn game_that_cannot_start_is_cleaned_up_and_reported() {
    let w = World::new("nostart", NO_PC);
    let mut host = w.host(w.system());
    host.start_error = Some(740);
    let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
    assert_eq!(code, 1, "{text}");
    assert!(
        text.contains(&format!(
            "[!] couldn't start {}\\StarCitizen.exe (error 740)\n",
            w.bin
        )),
        "{text}"
    );
    assert!(
        text.contains("StarCitizen.exe is set to \"Run this program as an administrator\""),
        "{text}"
    );
    assert!(
        !w.exists(&w.marker()) && !w.exists(&w.dll()),
        "the mod must not stay in Bin64"
    );
    has(
        &text,
        "Mod:      removed from Bin64; PC changes undone. Safe to go online.",
    );
}

#[test]
fn a_helper_that_cannot_start_or_cannot_copy_is_an_error() {
    let w = World::new("helperfail", NO_PC);
    let mut host = w.host(w.system());
    host.helper_error = Some(1223);
    let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
    assert_eq!(code, 1, "{text}");
    has(
        &text,
        "[!] couldn't start the mod helper (error 1223); administrator rights refused?",
    );

    // The helper starts and exits at once (its parent cannot be opened): no copy happened.
    let mut host = w.host(w.system());
    host.parent_openable = false;
    let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
    assert_eq!(code, 1, "{text}");
    assert!(
        text.contains(&format!(
            "[!] couldn't copy the mod into {} (helper exit 4); see data\\launcher.log\n",
            w.bin
        )),
        "{text}"
    );
    assert!(!w.exists(&w.marker()));

    // install: the helper's failure is reported with its exit code
    let mut host = w.host(w.system());
    host.parent_openable = true;
    std::fs::remove_file(format!("{}\\data\\launcher.log", w.here)).ok();
    std::fs::create_dir_all(w.dll()).unwrap(); // a folder where the DLL goes: the copy cannot succeed
    let (code, text) = run(&w, &mut host, &args(Mode::Install, false, &["install"]));
    assert_eq!(code, 1, "{text}");
    assert!(text.contains("[!] the helper failed (exit "), "{text}");
}

#[test]
fn crash_leftovers_can_be_undone_instead_of_playing() {
    let w = World::new("leftover", NO_PC);
    let mut sys = w.system();
    let rec = "time=2026-11-16 20:00\nfirewall=added\nhosts=added\n";
    sys.put(&w.record_path(), rec);
    sys.put(HOSTS, &format!("127.0.0.1 localhost\r\n127.0.0.1 {EAC_HOST} # added by sc-offline, removed when the game closes\r\n"));
    sys.rules.insert("sc-offline: block StarCitizen.exe".into());
    // the self check reads the record from ProgramData on this machine's disk, here the world's
    std::fs::create_dir_all(format!("{}\\sc-offline", w.pd)).unwrap();
    std::fs::write(w.record_path(), rec).unwrap();
    // a crashed run left the mod in too
    std::fs::write(w.dll(), OURS_DLL).unwrap();
    std::fs::write(w.marker(), "time=2026-11-16 20:00\nxml=none\n").unwrap();
    let mut host = w.host(sys);
    host.answers.push_back("y".into());
    let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
    assert_eq!(code, 0, "{text}");
    assert!(
        text.contains(
            "[!] PC changes from a previous run (2026-11-16 20:00) are still in place:\n"
        ),
        "{text}"
    );
    assert!(
        text.contains("[i] undoing them (same as `sc-offline.exe uninstall`)\n"),
        "{text}"
    );
    has(&text, "Mod:      removed.");
    assert!(host.asked.is_empty() || host.asked.iter().all(|a| a.starts_with("answer")));
    assert!(host.helper_starts[0]
        .args
        .starts_with("--helper uninstall "));
    assert!(
        host.helper_starts[0].elevated,
        "undoing PC changes needs the elevated helper"
    );
    host.sys.with(|s| {
        assert!(s.rules.is_empty());
        assert!(s.text(&w.record_path()).is_none());
        assert_eq!(s.text(HOSTS).unwrap(), "127.0.0.1 localhost\r\n");
    });
    assert!(!w.exists(&w.dll()) && !w.exists(&w.marker()));
    assert!(host.calls.iter().all(|c| !c.starts_with("start_game")));

    // answering no keeps playing and undoes them with this session's changes
    drop(host);
    drop(w);
    let w2 = World::new("leftover2", NO_PC);
    let mut sys = w2.system();
    sys.put(&w2.record_path(), rec);
    sys.rules.insert("sc-offline: block StarCitizen.exe".into());
    std::fs::create_dir_all(format!("{}\\sc-offline", w2.pd)).unwrap();
    std::fs::write(w2.record_path(), rec).unwrap();
    let mut host = w2.host(sys);
    host.game = GameBehaviour::Scripted;
    host.answers.push_back("n".into());
    let (code, text) = run(&w2, &mut host, &args(Mode::Play, false, &["play"]));
    assert_eq!(code, 0, "{text}");
    assert!(text.contains("[i] playing; they are undone together with this session's changes when the game closes.
"), "{text}");
    host.sys
        .with(|s| assert!(s.rules.is_empty() && s.text(&w2.record_path()).is_none()));
}

#[test]
fn crash_report_and_session_logs_after_the_game() {
    let w = World::new(
        "crash",
        "block_network = off\neac_hosts = off\neac_rename = off\ncheck_updates = off\n",
    );
    let mut host = w.host(w.system());
    host.env
        .insert("USERPROFILE".into(), "C:\\Users\\tester1".into());
    host.env.insert("USERNAME".into(), "tester1".into());
    // old file the session logic must not touch
    let channel = w.channel.clone();
    std::fs::create_dir_all(format!("{channel}\\logbackups")).unwrap();
    std::fs::write(format!("{channel}\\logbackups\\old.log"), "old").unwrap();
    let old = std::fs::File::options()
        .write(true)
        .open(format!("{channel}\\logbackups\\old.log"))
        .unwrap();
    old.set_modified(std::time::SystemTime::now() - std::time::Duration::from_secs(3600))
        .unwrap();
    drop(old);
    let ch = channel.clone();
    host.on_game = Some(Box::new(move |_| {
        std::fs::create_dir_all(format!("{ch}\\Crashes\\run1")).unwrap();
        std::fs::write(
            format!("{ch}\\Crashes\\run1\\crash.xml"),
            "path C:\\Users\\tester1\\Documents\\x",
        )
        .unwrap();
        std::fs::write(format!("{ch}\\Crashes\\run1\\crash.dmp"), "BINARY").unwrap();
        std::fs::write(
            format!("{ch}\\Game.log"),
            "log by tester1 in C:\\Users\\tester1\\AppData\n",
        )
        .unwrap();
    }));
    // crash report yes, bug form no, delete logs yes, sure yes
    for a in ["y", "n", "y", "y"] {
        host.answers.push_back(a.into());
    }
    let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
    assert_eq!(code, 0, "{text}");
    assert!(
        text.contains("[!] the game crashed this session (2 files in "),
        "{text}"
    );
    assert!(text.contains("Memory dumps (.dmp) are left out; they can't be redacted."));
    // the question has no line end of its own, so the answer's line starts with it
    let report_line = text
        .lines()
        .find(|l| l.contains("Crash:    report saved: "))
        .unwrap_or_else(|| panic!("{text}"));
    let report_line = &report_line[report_line.find("Crash:").unwrap()..];
    assert!(
        report_line.contains("; tar.exe failed, so it is a folder - zip it yourself"),
        "{report_line}"
    );
    let dir = report_line["Crash:    report saved: ".len()..]
        .split(" (")
        .next()
        .unwrap()
        .to_string();
    let game_log = std::fs::read_to_string(format!("{dir}\\Game.log")).unwrap();
    assert!(
        !game_log.contains("tester1"),
        "account name left: {game_log}"
    );
    assert!(
        game_log.contains("%USERPROFILE%") || game_log.contains("<user>"),
        "{game_log}"
    );
    let crash = std::fs::read_to_string(format!("{dir}\\Crashes_run1_crash.xml")).unwrap();
    assert!(!crash.contains("tester1"), "{crash}");
    assert!(
        !w.exists(&format!("{dir}\\Crashes_run1_crash.dmp")),
        "dumps are left out"
    );
    assert!(w.exists(&format!("{dir}\\README.txt")));
    assert!(host.urls.is_empty(), "the bug form was declined");
    // the logs of this session are deleted, the old one stays
    assert!(text.contains("The game wrote these logs during this modded session:\n"));
    assert!(
        text.lines().any(|l| l.contains("Logs:     deleted ")),
        "{text}"
    );
    assert!(
        !w.exists(&format!("{channel}\\Game.log"))
            && !w.exists(&format!("{channel}\\Crashes\\run1\\crash.xml"))
    );
    assert!(
        w.exists(&format!("{channel}\\logbackups\\old.log")),
        "older logs are never touched"
    );
    assert_eq!(
        host.asked
            .iter()
            .filter(|a| a.starts_with("answer"))
            .count(),
        4
    );
}

#[test]
fn clean_logs_never_and_always_and_no_console() {
    for (mode, expect) in [
        ("never", None),
        (
            "always",
            Some("clean_logs = always: deleting them without asking. Older logs are not touched."),
        ),
        (
            "ask",
            Some("Logs:     not asking (no console to answer in); run from a window to be asked"),
        ),
    ] {
        let w = World::new(
            &format!("logs-{mode}"),
            &format!("block_network = off\neac_hosts = off\neac_rename = off\ncheck_updates = off\ncrash_reports = off\nclean_logs = {mode}\n"),
        );
        let mut host = w.host(w.system());
        host.console_input = mode != "ask";
        if mode == "ask" {
            host.console_input = false;
        }
        let ch = w.channel.clone();
        host.on_game = Some(Box::new(move |_| {
            std::fs::write(format!("{ch}\\Game.log"), "x").unwrap();
        }));
        let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
        assert_eq!(code, 0, "{text}");
        match expect {
            Some(line) => has(&text, line),
            None => assert!(!text.contains("Logs:"), "{text}"),
        }
        let kept = w.exists(&format!("{}\\Game.log", w.channel));
        assert_eq!(kept, mode != "always", "{mode}");
        assert!(host.asked.is_empty(), "{mode}: nothing was asked");
    }
}

#[test]
fn log_rotates_and_a_previous_update_is_settled() {
    let w = World::new("rotate", "check_updates = off\n");
    let mut host = w.host(w.system());
    let (c1, _) = run(&w, &mut host, &args(Mode::Play, true, &["--dry-run"]));
    assert_eq!(c1, 0);
    let first = w.log();
    let (c2, _) = run(
        &w,
        &mut host,
        &args(Mode::Uninstall, true, &["uninstall", "--dry-run"]),
    );
    assert_eq!(c2, 0);
    let second = w.log();
    let rotated = std::fs::read_to_string(format!("{}\\data\\launcher.log.1", w.here)).unwrap();
    assert_eq!(rotated, first, "the previous log moves to launcher.log.1");
    assert!(
        second
            .lines()
            .next()
            .unwrap()
            .ends_with(" sc-offline.exe uninstall --dry-run"),
        "{second}"
    );
    assert_ne!(first, second);

    // an update that did not end: the journal says a new file was created and an old one set aside
    let upd = format!("{}\\data\\update", w.here);
    std::fs::create_dir_all(&upd).unwrap();
    std::fs::write(format!("{}\\extra.bin", w.here), "new").unwrap();
    std::fs::write(format!("{}\\tool.txt.update-old", w.here), "old").unwrap();
    std::fs::write(format!("{}\\tool.txt", w.here), "half").unwrap();
    std::fs::write(format!("{upd}\\applied.txt"), "N extra.bin\nR tool.txt\n").unwrap();
    let mut host = w.host(w.system());
    host.game = GameBehaviour::Scripted;
    let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
    assert_eq!(code, 0, "{text}");
    has(
        &text,
        "[!] a previous update didn't finish; putting the old files back",
    );
    assert!(!w.exists(&format!("{}\\extra.bin", w.here)));
    assert_eq!(
        std::fs::read_to_string(format!("{}\\tool.txt", w.here)).unwrap(),
        "old"
    );
    assert!(!w.exists(&upd), "the update folder is gone once settled");
}

#[test]
fn old_discord_switch_turns_the_plugin_off_once() {
    let w = World::new("discord", &format!("{NO_PC}discord_presence = off\n"));
    let mut host = w.host(w.system());
    host.game = GameBehaviour::Scripted;
    std::fs::create_dir_all(format!("{}\\data\\plugins\\discord", w.here)).unwrap();
    let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
    assert_eq!(code, 0, "{text}");
    has(&text, "[i] discord_presence = off: the Discord plugin is turned off (data\\plugins\\discord\\disabled)");
    assert!(w.exists(&format!("{}\\data\\plugins\\discord\\disabled", w.here)));
    let ini = std::fs::read_to_string(format!("{}\\sc-offline.ini", w.here)).unwrap();
    assert!(!ini.contains("discord_presence"), "{ini}");
}

#[test]
fn game_build_is_recorded_after_a_started_game() {
    let w = World::new("build", NO_PC);
    let mut host = w.host(w.system());
    host.game = GameBehaviour::Scripted;
    let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
    assert_eq!(code, 0, "{text}");
    // the stand-in has no build manifest: the build is the exe's size and date, recorded for the next run
    let line = text.lines().find(|l| l.starts_with("Game:     ")).unwrap();
    let build = &line["Game:     ".len()..];
    assert!(build.starts_with("StarCitizen.exe "), "{line}");
    let saved = std::fs::read_to_string(format!("{}\\data\\game-build.txt", w.here)).unwrap();
    assert_eq!(saved, format!("{build}\n"));
}

#[test]
fn the_menu_key_is_exported_as_sc_offline_menu() {
    for (ini, want) in [
        ("", "imgui"),
        ("menu = rust\n", "rust"),
        ("menu = RUST\n", "rust"),
        ("menu = nope\n", "imgui"),
    ] {
        let w = World::new("menu", &format!("{NO_PC}{ini}"));
        let mut host = w.host(w.system());
        host.game = GameBehaviour::Scripted;
        // a value from the player's shell must not choose the menu
        host.env.insert("SC_OFFLINE_MENU".into(), "rust".into());
        let (code, text) = run(&w, &mut host, &args(Mode::Play, false, &["play"]));
        assert_eq!(code, 0, "{text}");
        let last = host
            .env_log
            .iter()
            .rev()
            .find(|(k, _)| k == "SC_OFFLINE_MENU")
            .map(|(_, v)| v.clone());
        assert_eq!(last.as_deref(), Some(want), "ini {ini:?}");
        if ini == "menu = nope\n" {
            assert!(
                text.contains("sc-offline.ini line 8: menu must be imgui or rust"),
                "{text}"
            );
        }
    }
    // install and uninstall do not export it: the game is not started
    let w = World::new("menu-install", &format!("{NO_PC}menu = rust\n"));
    let mut host = w.host(w.system());
    let (code, _) = run(&w, &mut host, &args(Mode::Install, false, &["install"]));
    assert_eq!(code, 0);
    assert!(!host.env_log.iter().any(|(k, _)| k == "SC_OFFLINE_MENU"));
}
