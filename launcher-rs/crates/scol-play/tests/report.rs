// SPDX-License-Identifier: GPL-3.0-only
//! `report`: the problem report made on request. A synthetic launcher folder and game folder under the
//! temp dir, a fake host; made-up names only. `tar.exe` is not run by the fake system, so the bundle is
//! the folder the zip would be made from. Nothing is sent, started or run in game.
#![cfg(windows)]

mod common;

use common::{tree, World};
use scol_play::report::{make_report, plugin_lines, ReportArgs, MAX_CRASH_TEXTS, REPORT_DAYS};
use std::path::Path;

const INI: &str =
    "block_network = off\neac_hosts = off\neac_rename = off\ncheck_updates = off\nclean_logs = never\n";

fn run(
    w: &World,
    host: &mut scol_play::fake::FakeHost,
    game: &str,
    game_log: bool,
) -> (i32, String) {
    let mut out = Vec::new();
    let code = make_report(
        host,
        &mut out,
        &w.here,
        &ReportArgs {
            game: game.to_string(),
            game_log,
        },
    );
    (code, String::from_utf8_lossy(&out).replace("\r\n", "\n"))
}

/// The folder named on the "saved" line.
fn saved(text: &str) -> String {
    let line = text
        .lines()
        .find(|l| l.starts_with("Report:   saved: "))
        .unwrap_or_else(|| panic!("{text}"));
    line["Report:   saved: ".len()..]
        .split(" (")
        .next()
        .unwrap()
        .to_string()
}

fn names(dir: &str) -> Vec<String> {
    let mut v: Vec<String> = std::fs::read_dir(dir)
        .unwrap()
        .flatten()
        .map(|e| e.file_name().to_string_lossy().into_owned())
        .collect();
    v.sort();
    v
}

fn age(path: &str, days: u64) {
    let f = std::fs::File::options().write(true).open(path).unwrap();
    f.set_modified(std::time::SystemTime::now() - std::time::Duration::from_secs(days * 86_400))
        .unwrap();
}

/// A world with logs that name a made-up player, a recent crash, an old crash and three plugins.
fn world(tag: &str) -> (World, scol_play::fake::FakeHost) {
    let w = World::new(tag, INI);
    let mut host = w.host(w.system());
    host.env
        .insert("USERPROFILE".into(), "C:\\Users\\tester1".into());
    host.env.insert("USERNAME".into(), "tester1".into());
    let (here, ch) = (w.here.clone(), w.channel.clone());
    std::fs::write(
        format!("{here}\\data\\mod.log"),
        "15:14:20.365 sc-offline started\n[missions] no scripts in C:\\Users\\tester1\\Games\\data\\scripts\n",
    )
    .unwrap();
    std::fs::write(
        format!("{here}\\data\\launcher.log"),
        "--- sc-offline.exe play\nmail someone@example.com\n",
    )
    .unwrap();
    std::fs::write(
        format!("{ch}\\Game.log"),
        "log by tester1 in C:\\Users\\tester1\\AppData\n",
    )
    .unwrap();
    std::fs::create_dir_all(format!("{ch}\\Crashes\\run1")).unwrap();
    std::fs::write(
        format!("{ch}\\Crashes\\run1\\crash.xml"),
        "path C:\\Users\\tester1\\Documents\\x",
    )
    .unwrap();
    std::fs::write(format!("{ch}\\Crashes\\run1\\crash.dmp"), "BINARY").unwrap();
    std::fs::create_dir_all(format!("{ch}\\Crashes\\old")).unwrap();
    std::fs::write(format!("{ch}\\Crashes\\old\\crash.xml"), "an old crash").unwrap();
    age(&format!("{ch}\\Crashes\\old\\crash.xml"), REPORT_DAYS + 3);
    for (name, off) in [("alpha", false), ("beta", true)] {
        let dir = format!("{here}\\data\\plugins\\{name}");
        std::fs::create_dir_all(&dir).unwrap();
        std::fs::write(
            format!("{dir}\\plugin.ini"),
            format!("id = {name}\nversion = 1.2.0\nkind = data\ndescription = by tester1\n"),
        )
        .unwrap();
        if off {
            std::fs::write(format!("{dir}\\disabled"), "").unwrap();
        }
    }
    std::fs::create_dir_all(format!("{here}\\data\\plugins\\not-a-plugin")).unwrap();
    (w, host)
}

#[test]
fn report_bundles_status_logs_and_recent_crash_texts_redacted() {
    let (w, mut host) = world("report");
    host.console_input = true;
    host.answers.push_back("n".into()); // the bug form: no
    let game_before = tree(Path::new(&w.channel));
    let (code, text) = run(&w, &mut host, "", true);
    assert_eq!(code, 0, "{text}");
    assert!(
        text.contains(
            "Report:   collecting the status check and the logs (read-only; nothing is sent)\n"
        ),
        "{text}"
    );
    let dir = saved(&text);
    assert!(
        dir.starts_with(&format!("{}\\data\\reports\\", w.here)),
        "{dir}"
    );
    assert_eq!(
        names(&dir),
        [
            "Crashes_run1_crash.xml",
            "Game.log",
            "README.txt",
            "launcher.log",
            "mod.log",
            "plugins.txt",
            "sc-offline.ini",
            "status.txt",
        ],
        "the dump and the old crash are left out"
    );
    assert!(text.contains("Memory dumps (.dmp) are left out; they can't be redacted."));
    assert!(text.contains("Nothing was sent."), "{text}");
    // every file went through the redactor
    for name in names(&dir) {
        let body = std::fs::read_to_string(format!("{dir}\\{name}")).unwrap();
        assert!(!body.contains("tester1"), "{name}: {body}");
        assert!(!body.contains("someone@example.com"), "{name}: {body}");
    }
    let game_log = std::fs::read_to_string(format!("{dir}\\Game.log")).unwrap();
    assert!(
        game_log.contains("%USERPROFILE%") || game_log.contains("<user>"),
        "{game_log}"
    );
    let status = std::fs::read_to_string(format!("{dir}\\status.txt")).unwrap();
    assert!(status.starts_with("sc-offline launcher "), "{status}");
    assert!(status.contains("Folder:   "), "{status}");
    let plugins = std::fs::read_to_string(format!("{dir}\\plugins.txt")).unwrap();
    assert!(
        plugins.starts_with("2 plugin folders in data\\plugins"),
        "{plugins}"
    );
    assert!(plugins.contains("alpha") && plugins.contains("on   id=alpha version=1.2.0 kind=data"));
    assert!(
        plugins.contains("off  id=beta version=1.2.0 kind=data"),
        "{plugins}"
    );
    assert!(
        !plugins.contains("description"),
        "only id, version and kind are read"
    );
    let readme = std::fs::read_to_string(format!("{dir}\\README.txt")).unwrap();
    assert!(readme.contains("problem report") && readme.contains("Nothing was sent anywhere"));
    // each file is listed with its size
    for name in ["status.txt", "Game.log", "Crashes_run1_crash.xml"] {
        assert!(
            text.lines()
                .any(|l| l.trim_start().starts_with(name)
                    && (l.ends_with(" B") || l.ends_with(" KB"))),
            "{name}: {text}"
        );
    }
    // the question was asked on the console and declined; the game folder was only read
    assert!(host.asked.iter().any(|q| q.contains("answer: n")));
    assert!(host.urls.is_empty() && host.revealed.is_empty());
    assert_eq!(tree(Path::new(&w.channel)), game_before);
}

#[test]
fn report_without_game_log_and_the_bug_form_on_a_yes() {
    let (w, mut host) = world("report-nolog");
    host.console_input = true;
    host.answers.push_back("y".into());
    let (code, text) = run(&w, &mut host, "", false);
    assert_eq!(code, 0, "{text}");
    let dir = saved(&text);
    assert!(!names(&dir).contains(&"Game.log".to_string()));
    assert!(
        text.contains("Game.log was left out (--no-game-log)."),
        "{text}"
    );
    let readme = std::fs::read_to_string(format!("{dir}\\README.txt")).unwrap();
    assert!(readme.contains("Game.log was left out"), "{readme}");
    assert_eq!(host.urls.len(), 1, "{:?}", host.urls);
    assert!(
        host.urls[0].starts_with(
            "https://github.com/scuba-sc/sc-offline/issues/new?template=bug.yml&version="
        ),
        "{}",
        host.urls[0]
    );
    assert!(!host.urls[0].contains("tester1"));
    assert_eq!(host.revealed, [dir]);
}

#[test]
fn the_window_child_is_not_asked_and_a_missing_game_still_gives_a_report() {
    let (w, mut host) = world("report-nogame");
    host.console_input = true;
    host.env.insert("SC_OFFLINE_GUI".into(), "1".into());
    let missing = format!("{}\\nowhere", w.here);
    let (code, text) = run(&w, &mut host, &missing, true);
    assert_eq!(code, 0, "{text}");
    assert!(
        host.asked.is_empty(),
        "the window opens the folder itself: {:?}",
        host.asked
    );
    let dir = saved(&text);
    assert_eq!(
        names(&dir),
        [
            "README.txt",
            "launcher.log",
            "mod.log",
            "plugins.txt",
            "sc-offline.ini",
            "status.txt",
        ]
    );
    assert!(
        text.contains(
            "The game folder was not found, so Game.log and the crash files are not in it."
        ),
        "{text}"
    );
    let status = std::fs::read_to_string(format!("{dir}\\status.txt")).unwrap();
    assert!(
        status.contains("(status ended with exit code 1)"),
        "{status}"
    );
    let readme = std::fs::read_to_string(format!("{dir}\\README.txt")).unwrap();
    assert!(readme.contains("Game: not found"), "{readme}");
}

#[test]
fn only_the_newest_crash_texts_go_in() {
    let (w, mut host) = world("report-many");
    let ch = w.channel.clone();
    for i in 0..(MAX_CRASH_TEXTS + 5) {
        std::fs::write(format!("{ch}\\Crashes\\run1\\extra{i:02}.txt"), "x").unwrap();
    }
    let (code, text) = run(&w, &mut host, "", true);
    assert_eq!(code, 0, "{text}");
    let crash = names(&saved(&text))
        .into_iter()
        .filter(|n| n.starts_with("Crashes_"))
        .count();
    assert_eq!(crash, MAX_CRASH_TEXTS);
}

#[test]
fn plugin_lines_of_an_install_without_plugins() {
    let w = World::new("report-noplug", INI);
    assert_eq!(
        String::from_utf8(plugin_lines(&w.here)).unwrap(),
        "0 plugin folders in data\\plugins\r\n"
    );
}
