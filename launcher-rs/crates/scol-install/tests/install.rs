// SPDX-License-Identifier: GPL-3.0-only
//! `put_mod`, `take_mod` and `run` on synthetic folders (a plain file named StarCitizen.exe, a
//! dinput8.dll made of text). Synthetic tests only; nothing was run in game. The text and the file
//! results are the ones `PutMod` and `TakeMod` of the C++ launcher give; `tests/install/compare.py`
//! checks that against the built `sc-offline.exe`.
#![cfg(windows)]

use scol_install::install::{put_mod, take_mod, take_mod_with, Io};
use scol_install::paths::GamePaths;
use std::path::{Path, PathBuf};
use std::time::Duration;

const OURS: &[u8] = b"MZ padding sc-offline v0.8.0-rc.1\0 tail";
const OTHER: &[u8] = b"MZ another mod";

struct Fixture {
    root: PathBuf,
}

impl Fixture {
    fn new(name: &str) -> Self {
        let root = std::env::temp_dir().join(format!("scol-install-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&root);
        let f = Self { root };
        std::fs::create_dir_all(f.bin()).unwrap();
        std::fs::create_dir_all(f.here().join("data")).unwrap();
        std::fs::write(f.bin().join("StarCitizen.exe"), b"x").unwrap();
        std::fs::write(f.here().join("dinput8.dll"), OURS).unwrap();
        f
    }
    fn here(&self) -> PathBuf {
        self.root.join("here")
    }
    fn bin(&self) -> PathBuf {
        self.root.join("lib").join("LIVE").join("Bin64")
    }
    fn user(&self) -> PathBuf {
        self.root
            .join("lib")
            .join("LIVE")
            .join("user")
            .join("client")
            .join("0")
    }
    fn paths(&self) -> GamePaths {
        GamePaths::new(self.bin().to_str().unwrap())
    }
    fn here_str(&self) -> String {
        self.here().to_string_lossy().into_owned()
    }
    fn put(&self, mode: &str, dry: bool) -> (i32, String) {
        let mut out = Vec::new();
        let rc = put_mod(&self.here_str(), &self.paths(), mode, dry, &mut out);
        (rc, String::from_utf8(out).unwrap())
    }
    fn take(&self, dry: bool) -> (i32, String) {
        let mut out = Vec::new();
        let rc = take_mod(&self.paths(), dry, &mut out);
        (rc, String::from_utf8(out).unwrap())
    }
}

impl Drop for Fixture {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.root);
    }
}

fn read(p: &Path) -> Vec<u8> {
    std::fs::read(p).unwrap()
}

#[test]
fn game_paths_names() {
    let f = Fixture::new("names");
    let g = f.paths();
    assert!(g.dll.ends_with("Bin64\\dinput8.dll"));
    assert!(g
        .dll_backup
        .ends_with("Bin64\\dinput8.dll.sc-offline-backup"));
    assert!(g.marker.ends_with("Bin64\\sc-offline.installed"));
    assert!(g.xml.ends_with("LIVE\\user\\client\\0\\default_1.xml"));
    assert!(g.xml_backup.ends_with("default_1.xml.sc-offline-backup"));
}

#[test]
fn fresh_install_without_xml_writes_marker_and_uninstall_removes_it() {
    let f = Fixture::new("fresh");
    let (rc, out) = f.put("install", false);
    assert_eq!(rc, 0);
    assert!(out.contains("[i] data\\OfflineDB\\default_1.xml is missing"));
    assert!(out.contains("[+] copy dinput8.dll to "));
    assert_eq!(read(&f.bin().join("dinput8.dll")), OURS);
    let marker = String::from_utf8(read(&f.bin().join("sc-offline.installed"))).unwrap();
    let lines: Vec<&str> = marker.lines().collect();
    assert_eq!(lines[0], "version=0.8.0-rc.1");
    assert!(lines[1].starts_with("sha256=") && lines[1].len() == 7 + 64);
    assert_eq!(lines[2], "mode=install");
    assert!(lines[3].starts_with("time=20") && lines[3].len() == 5 + 19);
    assert_eq!(lines[4], "xml=skip");
    assert!(marker.ends_with('\n'));

    let (rc, out) = f.take(false);
    assert_eq!(rc, 0);
    assert!(out.contains("[+] delete "));
    assert!(!f.bin().join("dinput8.dll").exists());
    assert!(!f.bin().join("sc-offline.installed").exists());
}

#[test]
fn xml_is_backed_up_replaced_and_restored() {
    let f = Fixture::new("xml");
    std::fs::create_dir_all(f.here().join("data").join("OfflineDB")).unwrap();
    std::fs::write(
        f.here()
            .join("data")
            .join("OfflineDB")
            .join("default_1.xml"),
        b"mod",
    )
    .unwrap();
    std::fs::create_dir_all(f.user()).unwrap();
    std::fs::write(f.user().join("default_1.xml"), b"player").unwrap();
    let (rc, out) = f.put("play", false);
    assert_eq!(rc, 0);
    assert!(out.contains("[+] back up your default_1.xml to "));
    assert_eq!(read(&f.user().join("default_1.xml")), b"mod");
    assert_eq!(
        read(&f.user().join("default_1.xml.sc-offline-backup")),
        b"player"
    );
    let marker = String::from_utf8(read(&f.bin().join("sc-offline.installed"))).unwrap();
    assert!(marker.contains("mode=play\n") && marker.ends_with("xml=backup\n"));
    let (rc, out) = f.take(false);
    assert_eq!(rc, 0);
    assert!(out.contains("[+] restore your default_1.xml from "));
    assert_eq!(read(&f.user().join("default_1.xml")), b"player");
    assert!(!f.user().join("default_1.xml.sc-offline-backup").exists());
}

#[test]
fn no_player_xml_before_means_it_is_deleted_again() {
    let f = Fixture::new("xmlnone");
    std::fs::create_dir_all(f.here().join("data").join("OfflineDB")).unwrap();
    std::fs::write(
        f.here()
            .join("data")
            .join("OfflineDB")
            .join("default_1.xml"),
        b"mod",
    )
    .unwrap();
    assert_eq!(f.put("install", false).0, 0);
    assert_eq!(read(&f.user().join("default_1.xml")), b"mod");
    let (rc, out) = f.take(false);
    assert_eq!(rc, 0);
    assert!(out.contains("(there was none before sc-offline)"));
    assert!(!f.user().join("default_1.xml").exists());
}

#[test]
fn foreign_dll_is_set_aside_and_put_back() {
    let f = Fixture::new("foreign");
    std::fs::write(f.bin().join("dinput8.dll"), OTHER).unwrap();
    let (rc, out) = f.put("install", false);
    assert_eq!(rc, 0);
    assert!(out.contains("[+] set aside the other mod's dinput8.dll as "));
    assert_eq!(read(&f.bin().join("dinput8.dll")), OURS);
    assert_eq!(read(&f.bin().join("dinput8.dll.sc-offline-backup")), OTHER);
    let (rc, out) = f.take(false);
    assert_eq!(rc, 0);
    assert!(out.contains("[+] put the other mod's dinput8.dll back"));
    assert_eq!(read(&f.bin().join("dinput8.dll")), OTHER);
    assert!(!f.bin().join("dinput8.dll.sc-offline-backup").exists());
}

#[test]
fn second_foreign_dll_with_a_backup_already_there_refuses() {
    let f = Fixture::new("refuse");
    std::fs::write(f.bin().join("dinput8.dll"), OTHER).unwrap();
    std::fs::write(f.bin().join("dinput8.dll.sc-offline-backup"), b"older").unwrap();
    let (rc, out) = f.put("install", false);
    assert_eq!(rc, 2);
    assert!(out.contains("belongs to another mod and"));
    assert!(out.contains("already exists; move one of them away first"));
    assert_eq!(read(&f.bin().join("dinput8.dll")), OTHER);
    assert!(!f.bin().join("sc-offline.installed").exists());
}

#[test]
fn crash_leftover_keeps_the_backups_as_they_are() {
    let f = Fixture::new("crash");
    std::fs::create_dir_all(f.here().join("data").join("OfflineDB")).unwrap();
    std::fs::write(
        f.here()
            .join("data")
            .join("OfflineDB")
            .join("default_1.xml"),
        b"mod",
    )
    .unwrap();
    std::fs::create_dir_all(f.user()).unwrap();
    std::fs::write(f.user().join("default_1.xml"), b"mod").unwrap();
    std::fs::write(f.user().join("default_1.xml.sc-offline-backup"), b"player").unwrap();
    std::fs::write(f.bin().join("dinput8.dll"), OTHER).unwrap();
    std::fs::write(f.bin().join("dinput8.dll.sc-offline-backup"), b"first").unwrap();
    std::fs::write(
        f.bin().join("sc-offline.installed"),
        b"version=x\nmode=play\ntime=2026-11-17 09:05:03\nxml=backup\n",
    )
    .unwrap();
    let (rc, out) = f.put("install", false);
    assert_eq!(rc, 0, "{out}");
    // The marker means a crash: the DLL there is not "another mod's", the backups stay.
    assert!(!out.contains("set aside"));
    assert!(!out.contains("back up"));
    assert_eq!(
        read(&f.user().join("default_1.xml.sc-offline-backup")),
        b"player"
    );
    assert_eq!(
        read(&f.bin().join("dinput8.dll.sc-offline-backup")),
        b"first"
    );
    let marker = String::from_utf8(read(&f.bin().join("sc-offline.installed"))).unwrap();
    assert!(marker.ends_with("xml=backup\n"));
}

#[test]
fn dry_run_prints_the_steps_and_changes_nothing() {
    let f = Fixture::new("dry");
    std::fs::write(f.bin().join("dinput8.dll"), OTHER).unwrap();
    let (rc, out) = f.put("install", true);
    assert_eq!(rc, 0);
    assert!(out.contains("[dry-run] would set aside the other mod's dinput8.dll as "));
    assert!(out.contains("[dry-run] would copy dinput8.dll to "));
    assert!(out.contains("[dry-run] would write "));
    assert_eq!(read(&f.bin().join("dinput8.dll")), OTHER);
    assert!(!f.bin().join("dinput8.dll.sc-offline-backup").exists());
    assert!(!f.bin().join("sc-offline.installed").exists());

    // And an uninstall dry run on a real install.
    assert_eq!(f.put("install", false).0, 0);
    let (rc, out) = f.take(true);
    assert_eq!(rc, 0);
    assert!(out.contains("[dry-run] would delete "));
    assert!(out.contains("[dry-run] would put the other mod's dinput8.dll back"));
    assert_eq!(read(&f.bin().join("dinput8.dll")), OURS);
    assert!(f.bin().join("sc-offline.installed").exists());
}

#[test]
fn uninstall_leaves_a_foreign_dll_and_an_unrecorded_xml_alone() {
    let f = Fixture::new("alone");
    std::fs::write(f.bin().join("dinput8.dll"), OTHER).unwrap();
    std::fs::create_dir_all(f.user()).unwrap();
    std::fs::write(f.user().join("default_1.xml"), b"maybe ours").unwrap();
    let (rc, out) = f.take(false);
    assert_eq!(rc, 0);
    assert!(out.contains("isn't sc-offline's; left alone"));
    assert!(out.contains("an older sc-offline may have put it there, but there's no record of it"));
    assert_eq!(read(&f.bin().join("dinput8.dll")), OTHER);
    assert_eq!(read(&f.user().join("default_1.xml")), b"maybe ours");
}

#[test]
#[allow(clippy::permissions_set_readonly_false)] // Windows only: this clears the read-only attribute
fn a_dll_that_cannot_be_deleted_gives_exit_3_and_keeps_the_marker() {
    let f = Fixture::new("locked");
    assert_eq!(f.put("install", false).0, 0);
    let dll = f.bin().join("dinput8.dll");
    let mut perm = std::fs::metadata(&dll).unwrap().permissions();
    perm.set_readonly(true);
    std::fs::set_permissions(&dll, perm).unwrap();
    let mut out = Vec::new();
    let rc = take_mod_with(&f.paths(), false, &mut out, 3, Duration::from_millis(5));
    let out = String::from_utf8(out).unwrap();
    assert_eq!(rc, 3);
    assert!(out.contains("couldn't delete "));
    assert!(out.contains("(error 5)"));
    assert!(f.bin().join("sc-offline.installed").exists());
    let mut perm = std::fs::metadata(&dll).unwrap().permissions();
    perm.set_readonly(false);
    std::fs::set_permissions(&dll, perm).unwrap();
}

fn run_text(f: &Fixture, args: &[&str]) -> (i32, String) {
    let args: Vec<String> = args.iter().map(|a| a.to_string()).collect();
    let mut out = Vec::new();
    let rc = {
        let mut io = Io {
            here: f.here_str(),
            out: &mut out,
            pc: None,
        };
        scol_install::run_install(&args, &mut io)
    };
    (rc, String::from_utf8(out).unwrap())
}

#[test]
fn run_rejects_bad_arguments_before_touching_anything() {
    let f = Fixture::new("args");
    let (rc, out) = run_text(&f, &["install", "--game"]);
    assert_eq!(rc, 1);
    assert!(out.contains("[!] --game needs a folder"));
    let (rc, out) = run_text(&f, &["install", "--bogus"]);
    assert_eq!(rc, 1);
    assert!(out.contains("unknown argument '--bogus'"));
    let (rc, _) = run_text(&f, &["--dry-run"]);
    assert_eq!(rc, 1);
    assert!(!f.here().join("data").join("launcher.log").exists());
}

#[test]
fn run_dry_run_install_writes_only_the_log() {
    let f = Fixture::new("rundry");
    let lib = f.root.join("lib");
    let (rc, out) = run_text(
        &f,
        &[
            "install",
            "--dry-run",
            "--skip-eac-check",
            "--game",
            lib.to_str().unwrap(),
        ],
    );
    assert_eq!(rc, 0, "{out}");
    assert!(out.starts_with("sc-offline launcher 0.8.0-rc.1 - dry run, nothing is changed\n\n"));
    assert!(out.contains("[dry-run] would copy dinput8.dll to "));
    assert!(!f.bin().join("dinput8.dll").exists());
    assert!(!f.bin().join("sc-offline.installed").exists());
    let log = String::from_utf8(read(&f.here().join("data").join("launcher.log"))).unwrap();
    assert!(log.starts_with("--- 20") && log.contains("sc-offline.exe install --dry-run"));
}

#[test]
fn run_refuses_while_a_stand_in_game_process_runs() {
    // A copy of ping.exe named StarCitizen.exe in the scratch Bin64: a stand-in process, not the game.
    let ping = Path::new(&std::env::var("SystemRoot").unwrap_or("C:\\Windows".into()))
        .join("System32")
        .join("PING.EXE");
    if !ping.is_file() {
        return;
    }
    let f = Fixture::new("running");
    let exe = f.bin().join("StarCitizen.exe");
    std::fs::copy(&ping, &exe).unwrap();
    let mut child = std::process::Command::new(&exe)
        .args(["-n", "20", "127.0.0.1"])
        .stdout(std::process::Stdio::null())
        .spawn()
        .unwrap();
    let lib = f.root.join("lib");
    let game = lib.to_str().unwrap();
    let (rc_i, out_i) = run_text(&f, &["install", "--skip-eac-check", "--game", game]);
    let (rc_u, out_u) = run_text(&f, &["uninstall", "--game", game]);
    let _ = child.kill();
    let _ = child.wait();
    assert_eq!(rc_i, 3, "{out_i}");
    assert!(out_i.contains("[!] StarCitizen.exe is already running. Close it first."));
    assert_eq!(rc_u, 3, "{out_u}");
    assert!(out_u.contains("[!] StarCitizen.exe is running. Close it first."));
    assert!(!f.bin().join("dinput8.dll").exists());
}

// ---- the two game DLLs: dinput8.dll (classic menu) and dinput8-rust-menu.dll (Rust menu hook) ----

const CLASSIC: &[u8] = b"MZ classic host, no marker text";
const RUST_MENU: &[u8] = b"MZ rust-menu host, no marker text";

impl Fixture {
    /// Ships both DLLs beside the launcher, as a 0.8.0 package does, and sets the ini's `menu` key.
    fn ship_both(&self, menu: Option<&str>) {
        std::fs::write(self.here().join("dinput8.dll"), CLASSIC).unwrap();
        std::fs::write(self.here().join("dinput8-rust-menu.dll"), RUST_MENU).unwrap();
        self.set_menu(menu);
    }
    fn set_menu(&self, menu: Option<&str>) {
        let ini = self.here().join("sc-offline.ini");
        match menu {
            Some(m) => std::fs::write(ini, format!("menu = {m}\n")).unwrap(),
            None => {
                let _ = std::fs::remove_file(ini);
            }
        }
    }
    fn installed(&self) -> Vec<u8> {
        read(&self.bin().join("dinput8.dll"))
    }
}

#[test]
fn default_menu_installs_dinput8_dll_even_when_the_rust_menu_dll_is_there() {
    let f = Fixture::new("two-default");
    f.ship_both(None);
    let (rc, out) = f.put("install", false);
    assert_eq!(rc, 0);
    assert_eq!(f.installed(), CLASSIC);
    assert!(out.contains("[+] copy dinput8.dll to "));
    assert!(!out.contains("rust"), "{out}");
    // An explicit imgui is the same.
    let (rc, _) = f.take(false);
    assert_eq!(rc, 0);
    f.set_menu(Some("imgui"));
    assert_eq!(f.put("install", false).0, 0);
    assert_eq!(f.installed(), CLASSIC);
}

#[test]
fn menu_rust_with_the_file_installs_the_rust_menu_dll_under_the_game_side_name() {
    let f = Fixture::new("two-rust");
    f.ship_both(Some("rust"));
    let (rc, out) = f.put("install", false);
    assert_eq!(rc, 0);
    assert_eq!(f.installed(), RUST_MENU);
    assert!(
        out.contains("[+] copy dinput8-rust-menu.dll to ") && out.contains("Bin64\\dinput8.dll"),
        "{out}"
    );
    assert!(
        out.contains("menu = rust:") && out.contains("not been run in game"),
        "{out}"
    );
    assert!(!f.bin().join("dinput8-rust-menu.dll").exists());
    // The marker records the DLL that went in, in the usual format.
    let marker = String::from_utf8(read(&f.bin().join("sc-offline.installed"))).unwrap();
    assert!(marker.contains(&format!(
        "sha256={}\n",
        scol_plugins::sha256::hex_digest(RUST_MENU)
    )));
    // Uninstall takes it out like any other.
    let (rc, _) = f.take(false);
    assert_eq!(rc, 0);
    assert!(!f.bin().join("dinput8.dll").exists());
}

#[test]
fn menu_rust_without_the_file_installs_dinput8_dll_and_says_why() {
    let f = Fixture::new("two-rust-missing");
    f.ship_both(Some("rust"));
    std::fs::remove_file(f.here().join("dinput8-rust-menu.dll")).unwrap();
    let (rc, out) = f.put("play", false);
    assert_eq!(rc, 0);
    assert_eq!(f.installed(), CLASSIC);
    assert!(
        out.contains("menu = rust, but dinput8-rust-menu.dll is not beside sc-offline.exe")
            && out.contains("shows the classic menu"),
        "{out}"
    );
    assert!(out.contains("[+] copy dinput8.dll to "), "{out}");
}

#[test]
fn switching_the_menu_key_replaces_the_installed_dll_on_the_next_put() {
    let f = Fixture::new("two-switch");
    f.ship_both(Some("rust"));
    assert_eq!(f.put("install", false).0, 0);
    assert_eq!(f.installed(), RUST_MENU);

    // Back to imgui: the installed bytes equal a shipped DLL, so it is ours (no .sc-offline-backup),
    // and it is replaced because it is not the selected one.
    f.set_menu(Some("imgui"));
    let (rc, out) = f.put("play", true);
    assert_eq!(rc, 0);
    assert!(
        out.contains("the installed dinput8.dll carries the new Rust menu; this Play replaces it with the classic menu (dinput8.dll)"),
        "{out}"
    );
    assert_eq!(f.installed(), RUST_MENU, "a dry run changes nothing");
    let (rc, _) = f.put("play", false);
    assert_eq!(rc, 0);
    assert_eq!(f.installed(), CLASSIC);
    assert!(!f.bin().join("dinput8.dll.sc-offline-backup").exists());

    // And forward again.
    f.set_menu(Some("rust"));
    let (rc, out) = f.put("play", false);
    assert_eq!(rc, 0);
    assert!(
        out.contains("carries the classic menu; this Play replaces it with the new Rust menu"),
        "{out}"
    );
    assert_eq!(f.installed(), RUST_MENU);
}

#[test]
fn an_installed_copy_of_either_shipped_dll_is_ours_even_without_the_version_text() {
    let f = Fixture::new("two-ours");
    f.ship_both(Some("imgui"));
    // Another mod's DLL is still set aside ...
    std::fs::write(f.bin().join("dinput8.dll"), OTHER).unwrap();
    assert_eq!(f.put("install", false).0, 0);
    assert_eq!(read(&f.bin().join("dinput8.dll.sc-offline-backup")), OTHER);
    // ... but a leftover copy of the Rust menu DLL (no marker) is replaced without a backup.
    let _ = std::fs::remove_file(f.bin().join("sc-offline.installed"));
    let _ = std::fs::remove_file(f.bin().join("dinput8.dll.sc-offline-backup"));
    std::fs::write(f.bin().join("dinput8.dll"), RUST_MENU).unwrap();
    assert_eq!(f.put("install", false).0, 0);
    assert_eq!(f.installed(), CLASSIC);
    assert!(!f.bin().join("dinput8.dll.sc-offline-backup").exists());
}

#[test]
fn dry_run_names_the_selected_dll_and_changes_nothing() {
    let f = Fixture::new("two-dry");
    f.ship_both(Some("rust"));
    let (rc, out) = f.put("install", true);
    assert_eq!(rc, 0);
    assert!(
        out.contains("[dry-run] would copy dinput8-rust-menu.dll to "),
        "{out}"
    );
    assert!(!f.bin().join("dinput8.dll").exists());
}
