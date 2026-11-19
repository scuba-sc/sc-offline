// SPDX-License-Identifier: GPL-3.0-only
//! Update tests: a local fake serves fixture release metadata and zips; the machine is faked too.
//! Scenarios: good update, tampered digests, oversized files and downloads, path traversal,
//! downgrade, interrupted swap then recovery, failed self-test, running game, `check_updates = off`,
//! elevation, the catalogue fetch path, and (when a C++ launcher built with SCO_UPDATE_TEST is named in
//! `SCOL_CPP_UPDATE_EXE`) the old-launcher contract both ways. Synthetic; nothing here ran in game.

#![allow(clippy::field_reassign_with_default, clippy::type_complexity)]

use std::collections::HashMap;
use std::io::Write;
use std::path::{Path, PathBuf};

use flate2::write::DeflateEncoder;
use flate2::{Compression, Crc};
use scol_catalogue::fetch::{self, Fetcher, Step};
use scol_plugins::sha256::hex_digest;
use scol_update::flow;
use scol_update::manifest;
use scol_update::platform::{Platform, System};
use scol_update::release::{self, UpdateInfo};
use scol_update::stage;
use scol_update::swap::{self, SwapOptions};

// ---------------------------------------------------------------- helpers

fn tmp(name: &str) -> PathBuf {
    let p = std::env::temp_dir().join(format!("scol-update-{}-{name}", std::process::id()));
    let _ = std::fs::remove_dir_all(&p);
    std::fs::create_dir_all(&p).unwrap();
    p
}

fn write(p: &Path, bytes: &[u8]) {
    std::fs::create_dir_all(p.parent().unwrap()).unwrap();
    std::fs::write(p, bytes).unwrap();
}

fn read(p: &Path) -> Vec<u8> {
    std::fs::read(p).unwrap_or_else(|_| b"<missing>".to_vec())
}

/// One zip entry. `lie` writes that size in the headers instead of the real one.
struct Ent {
    name: String,
    data: Vec<u8>,
    deflate: bool,
    lie: Option<u32>,
}

fn ent(name: &str, data: &[u8]) -> Ent {
    Ent {
        name: name.into(),
        data: data.to_vec(),
        deflate: false,
        lie: None,
    }
}

fn zip_bytes(entries: &[Ent]) -> Vec<u8> {
    let mut out = Vec::new();
    let mut cd = Vec::new();
    for e in entries {
        let mut crc = Crc::new();
        crc.update(&e.data);
        let body = if e.deflate {
            let mut enc = DeflateEncoder::new(Vec::new(), Compression::default());
            enc.write_all(&e.data).unwrap();
            enc.finish().unwrap()
        } else {
            e.data.clone()
        };
        let size = e.lie.unwrap_or(e.data.len() as u32);
        let method: u16 = if e.deflate { 8 } else { 0 };
        let off = out.len() as u32;
        let hdr = |out: &mut Vec<u8>, sig: [u8; 4], central: bool| {
            out.extend_from_slice(&sig);
            if central {
                out.extend_from_slice(&20u16.to_le_bytes());
            }
            out.extend_from_slice(&20u16.to_le_bytes());
            out.extend_from_slice(&0u16.to_le_bytes());
            out.extend_from_slice(&method.to_le_bytes());
            out.extend_from_slice(&0u16.to_le_bytes());
            out.extend_from_slice(&0x21u16.to_le_bytes());
            out.extend_from_slice(&crc.sum().to_le_bytes());
            out.extend_from_slice(&(body.len() as u32).to_le_bytes());
            out.extend_from_slice(&size.to_le_bytes());
            out.extend_from_slice(&(e.name.len() as u16).to_le_bytes());
            out.extend_from_slice(&0u16.to_le_bytes());
        };
        hdr(&mut out, [0x50, 0x4b, 3, 4], false);
        out.extend_from_slice(e.name.as_bytes());
        out.extend_from_slice(&body);
        hdr(&mut cd, [0x50, 0x4b, 1, 2], true);
        cd.extend_from_slice(&0u16.to_le_bytes()); // comment
        cd.extend_from_slice(&0u16.to_le_bytes()); // disk
        cd.extend_from_slice(&0u16.to_le_bytes()); // internal
        cd.extend_from_slice(&0u32.to_le_bytes()); // external
        cd.extend_from_slice(&off.to_le_bytes());
        cd.extend_from_slice(e.name.as_bytes());
    }
    let cd_off = out.len() as u32;
    out.extend_from_slice(&cd);
    out.extend_from_slice(&[0x50, 0x4b, 5, 6, 0, 0, 0, 0]);
    out.extend_from_slice(&(entries.len() as u16).to_le_bytes());
    out.extend_from_slice(&(entries.len() as u16).to_le_bytes());
    out.extend_from_slice(&(cd.len() as u32).to_le_bytes());
    out.extend_from_slice(&cd_off.to_le_bytes());
    out.extend_from_slice(&0u16.to_le_bytes());
    out
}

/// The files of a good release `tag` plus its manifest, as zip entries under `sc-offline-<tag>/`.
fn release_entries(tag: &str, files: &[(&str, &[u8])]) -> Vec<Ent> {
    let list: Vec<String> = files
        .iter()
        .map(|(p, d)| {
            format!(
                r#"{{"path":"{p}","sha256":"{}","size":{}}}"#,
                hex_digest(d),
                d.len()
            )
        })
        .collect();
    let manifest = format!(
        r#"{{"format":1,"version":"{}","tag":"{tag}","commit":"0123456789abcdef","files":[{}]}}"#,
        &tag[1..],
        list.join(",")
    );
    let mut v: Vec<Ent> = files
        .iter()
        .map(|(p, d)| ent(&format!("sc-offline-{tag}/{p}"), d))
        .collect();
    v.push(ent(
        &format!("sc-offline-{tag}/manifest.json"),
        manifest.as_bytes(),
    ));
    v
}

const NEW_FILES: [(&str, &[u8]); 6] = [
    ("sc-offline.exe", b"NEW-EXE"),
    ("dinput8.dll", b"NEW-DLL"),
    ("dinput8-rust-menu.dll", b"NEW-RUST-MENU-DLL"),
    ("sc-offline.ini", b"start_ship = default\r\nnew_key = 5\r\n"),
    ("data/new.txt", b"fresh"),
    ("data/wallet.txt", b"RELEASE WALLET MUST NOT LAND"),
];

fn good_zip(tag: &str) -> Vec<u8> {
    zip_bytes(&release_entries(tag, &NEW_FILES))
}

/// An installed 0.7.0 folder with player files.
fn install(name: &str) -> PathBuf {
    let here = tmp(name);
    write(&here.join("sc-offline.exe"), b"OLD-EXE");
    write(&here.join("dinput8.dll"), b"OLD-DLL");
    write(&here.join("dinput8-rust-menu.dll"), b"OLD-RUST-MENU-DLL");
    write(&here.join("sc-offline.ini"), b"start_ship = mine\r\n");
    write(&here.join("data/wallet.txt"), b"my wallet");
    here
}

fn snapshot(root: &Path) -> Vec<(String, Vec<u8>)> {
    fn walk(dir: &Path, root: &Path, out: &mut Vec<(String, Vec<u8>)>) {
        for e in std::fs::read_dir(dir).unwrap().flatten() {
            let p = e.path();
            if p.is_dir() {
                walk(&p, root, out);
            } else {
                let rel = p
                    .strip_prefix(root)
                    .unwrap()
                    .to_string_lossy()
                    .replace('\\', "/");
                if !rel.starts_with("data/update") {
                    out.push((rel, std::fs::read(&p).unwrap()));
                }
            }
        }
    }
    let mut v = Vec::new();
    walk(root, root, &mut v);
    v.sort();
    v
}

const ZIP_URL: &str =
    "https://github.com/scuba-sc/sc-offline/releases/download/v0.7.1/sc-offline-v0.7.1.zip";

fn info_for(zip: &[u8]) -> UpdateInfo {
    UpdateInfo {
        tag: "v0.7.1".into(),
        zip_name: "sc-offline-v0.7.1.zip".into(),
        zip_url: ZIP_URL.into(),
        sha256: hex_digest(zip),
        size: zip.len() as u64,
    }
}

// ---------------------------------------------------------------- fakes

enum Resp {
    Bytes(Vec<u8>),
    Redirect(String),
}

#[derive(Default)]
struct FakeNet {
    routes: HashMap<String, Resp>,
    log: Vec<String>,
}

impl FakeNet {
    fn serve(&mut self, url: &str, bytes: &[u8]) {
        self.routes.insert(url.into(), Resp::Bytes(bytes.to_vec()));
    }
}

impl Fetcher for FakeNet {
    fn fetch_bytes(&mut self, url: &str, max: u64) -> Result<Step<Vec<u8>>, String> {
        self.log.push(url.into());
        match self.routes.get(url) {
            Some(Resp::Bytes(b)) if b.len() as u64 <= max => Ok(Step::Done(b.clone())),
            Some(Resp::Bytes(_)) => Err("over the size cap".into()),
            Some(Resp::Redirect(l)) => Ok(Step::Redirect(l.clone())),
            _ => Err("the server answered 404".into()),
        }
    }
    fn fetch_to_file(&mut self, url: &str, dest: &Path, max: u64) -> Result<Step<u64>, String> {
        self.log.push(url.into());
        match self.routes.get(url) {
            Some(Resp::Bytes(b)) if b.len() as u64 <= max => {
                std::fs::write(dest, b).map_err(|e| e.to_string())?;
                Ok(Step::Done(b.len() as u64))
            }
            Some(Resp::Bytes(_)) => Err("over the size cap".into()),
            Some(Resp::Redirect(l)) => Ok(Step::Redirect(l.clone())),
            _ => Err("the server answered 404".into()),
        }
    }
}

struct FakeMachine {
    game: bool,
    self_test: Box<dyn FnMut(&Path, &str) -> Result<(), String>>,
    writable: bool,
    elevated: Option<Result<i32, String>>,
    elevated_calls: usize,
    free: Option<u64>,
    sleeps: Vec<u64>,
    /// Called with the number of waits so far, each time the swap waits.
    on_sleep: Option<Box<dyn FnMut(usize)>>,
    /// `can_ask` of the platform.
    can_ask: bool,
    /// What `after_update` answers, and the arguments it was given.
    restart: Option<i32>,
    restarts: Vec<Vec<String>>,
}

impl Default for FakeMachine {
    fn default() -> Self {
        FakeMachine {
            game: false,
            self_test: Box::new(|_, _| Ok(())),
            writable: true,
            elevated: None,
            elevated_calls: 0,
            free: None,
            sleeps: vec![],
            on_sleep: None,
            can_ask: true,
            restart: None,
            restarts: vec![],
        }
    }
}

impl Platform for FakeMachine {
    fn game_running(&mut self) -> bool {
        self.game
    }
    fn run_self_test(&mut self, here: &Path, tag: &str) -> Result<(), String> {
        (self.self_test)(here, tag)
    }
    fn can_write(&mut self, _: &Path) -> bool {
        self.writable
    }
    fn run_elevated(&mut self, _: &Path, _: &str, _: &Path) -> Result<i32, String> {
        self.elevated_calls += 1;
        self.elevated.clone().unwrap_or(Err("refused".into()))
    }
    fn free_space(&mut self, _: &Path) -> Option<u64> {
        self.free
    }
    fn local_app_data(&mut self) -> Option<PathBuf> {
        None
    }
    fn sleep_ms(&mut self, ms: u64) {
        self.sleeps.push(ms);
        let n = self.sleeps.len();
        if let Some(f) = self.on_sleep.as_mut() {
            f(n);
        }
    }
    fn can_ask(&mut self) -> bool {
        self.can_ask
    }
    fn after_update(&mut self, _: &Path, args: &[String], _: &mut dyn Write) -> Option<i32> {
        self.restarts.push(args.to_vec());
        self.restart
    }
}

fn apply(
    here: &Path,
    zip: &[u8],
    info: &UpdateInfo,
    m: &mut FakeMachine,
) -> (Result<(), String>, String) {
    let mut net = FakeNet::default();
    net.serve(&info.zip_url, zip);
    apply_with(here, info, &mut net, m)
}

fn apply_with(
    here: &Path,
    info: &UpdateInfo,
    net: &mut FakeNet,
    m: &mut FakeMachine,
) -> (Result<(), String>, String) {
    let mut out = Vec::new();
    let r = flow::apply_update(here, info, "0.7.0", net, m, &mut out);
    (r, String::from_utf8_lossy(&out).into_owned())
}

// ---------------------------------------------------------------- release check

fn release_json(tag: &str, zip: &[u8], url: &str) -> Vec<u8> {
    format!(
        r#"{{"tag_name":"{tag}","prerelease":false,"draft":false,"assets":[{{"name":"sc-offline-{tag}.zip","size":{},"digest":"sha256:{}","browser_download_url":"{url}"}}]}}"#,
        zip.len(),
        hex_digest(zip)
    )
    .into_bytes()
}

#[test]
fn check_latest_offers_newer_and_follows_a_github_redirect() {
    let zip = good_zip("v0.7.1");
    let mut net = FakeNet::default();
    net.serve(
        release::RELEASES_LATEST,
        &release_json("v0.7.1", &zip, ZIP_URL),
    );
    let u = flow::check_latest(&mut net, "0.7.0", false).unwrap();
    assert_eq!(
        (u.tag.as_str(), u.sha256.as_str()),
        ("v0.7.1", hex_digest(&zip).as_str())
    );

    // The asset URL answers 302 to GitHub's signed link; the download follows it.
    let signed = "https://objects.githubusercontent.com/signed?X-Amz=1";
    net.routes
        .insert(ZIP_URL.into(), Resp::Redirect(signed.into()));
    net.serve(signed, &zip);
    let here = install("redirect");
    let (r, _) = apply_with(&here, &u, &mut net, &mut FakeMachine::default());
    assert!(r.is_ok());
    assert_eq!(read(&here.join("sc-offline.exe")), b"NEW-EXE");
    assert_eq!(net.log, vec![release::RELEASES_LATEST, ZIP_URL, signed]);
}

#[test]
fn check_latest_says_why_nothing_is_offered() {
    let zip = good_zip("v0.7.1");
    let mut net = FakeNet::default();
    net.serve(
        release::RELEASES_LATEST,
        &release_json("v0.7.1", &zip, ZIP_URL),
    );
    assert!(flow::check_latest(&mut net, "0.7.1", false)
        .unwrap_err()
        .starts_with("up to date"));
    // A downgrade is never offered.
    assert!(flow::check_latest(&mut net, "0.8.0", false)
        .unwrap_err()
        .starts_with("up to date"));
    // No network.
    let mut dead = FakeNet::default();
    assert_eq!(
        flow::check_latest(&mut dead, "0.7.0", false).unwrap_err(),
        "couldn't reach GitHub"
    );
}

#[test]
fn redirect_to_a_foreign_host_is_refused() {
    let zip = good_zip("v0.7.1");
    let here = install("evil-redirect");
    let mut net = FakeNet::default();
    net.routes.insert(
        ZIP_URL.into(),
        Resp::Redirect("https://evil.example/x.zip".into()),
    );
    net.serve("https://evil.example/x.zip", &zip);
    let (r, _) = apply_with(
        &here,
        &info_for(&zip),
        &mut net,
        &mut FakeMachine::default(),
    );
    assert!(r.is_err());
    assert!(
        !net.log.iter().any(|u| u.contains("evil.example")),
        "must not request the foreign host"
    );
    assert_eq!(read(&here.join("sc-offline.exe")), b"OLD-EXE");
    // Plain http redirect, and a redirect loop, are refused too.
    net.routes.insert(
        ZIP_URL.into(),
        Resp::Redirect("http://objects.githubusercontent.com/x".into()),
    );
    assert!(apply_with(
        &here,
        &info_for(&zip),
        &mut net,
        &mut FakeMachine::default()
    )
    .0
    .is_err());
    net.routes.insert(
        ZIP_URL.into(),
        Resp::Redirect("https://github.com/scuba-sc/sc-offline/loop".into()),
    );
    net.routes.insert(
        "https://github.com/scuba-sc/sc-offline/loop".into(),
        Resp::Redirect(ZIP_URL.into()),
    );
    assert!(apply_with(
        &here,
        &info_for(&zip),
        &mut net,
        &mut FakeMachine::default()
    )
    .0
    .is_err());
}

#[test]
fn check_updates_off_makes_no_request() {
    let mut net = FakeNet::default();
    net.serve(
        release::RELEASES_LATEST,
        &release_json("v0.7.1", &good_zip("v0.7.1"), ZIP_URL),
    );
    assert!(flow::check_for_play(&mut net, "0.7.0", false, false).is_none());
    assert!(net.log.is_empty());
    assert!(flow::check_for_play(&mut net, "0.7.0", false, true).is_some());
    assert_eq!(net.log.len(), 1);
}

// ---------------------------------------------------------------- the swap

#[test]
fn good_update_replaces_program_files_and_keeps_player_files() {
    let here = install("good");
    let zip = good_zip("v0.7.1");
    let mut m = FakeMachine::default();
    m.self_test = Box::new(|here, tag| {
        assert_eq!(tag, "v0.7.1");
        // The new exe is in place when its self-test runs.
        assert_eq!(
            std::fs::read(here.join("sc-offline.exe")).unwrap(),
            b"NEW-EXE"
        );
        Ok(())
    });
    let (r, out) = apply(&here, &zip, &info_for(&zip), &mut m);
    assert!(r.is_ok(), "{out}");
    assert_eq!(read(&here.join("sc-offline.exe")), b"NEW-EXE");
    assert_eq!(read(&here.join("dinput8.dll")), b"NEW-DLL");
    // The second game DLL is carried like the first: staged, checked against the manifest, swapped.
    assert_eq!(
        read(&here.join("dinput8-rust-menu.dll")),
        b"NEW-RUST-MENU-DLL"
    );
    assert_eq!(read(&here.join("data/new.txt")), b"fresh");
    assert_eq!(read(&here.join("data/wallet.txt")), b"my wallet");
    let ini = String::from_utf8(read(&here.join("sc-offline.ini"))).unwrap();
    assert!(ini.starts_with("start_ship = mine\r\n"), "{ini}");
    assert!(ini.contains("# new_key = 5"), "{ini}");
    assert!(
        !ini.contains("\nnew_key = 5"),
        "new key must be commented out: {ini}"
    );
    assert!(
        out.contains("4 files replaced") || out.contains("3 files replaced"),
        "{out}"
    );
    // Journal ends with done; the next start cleans data\update.
    let j = String::from_utf8(read(&swap::journal_path(&here))).unwrap();
    assert!(j.ends_with("done\n"), "{j}");
    assert!(
        j.contains("R sc-offline.exe\n") && j.contains("N data\\new.txt\n"),
        "{j}"
    );
    swap::settle_previous(&here, &mut FakeMachine::default(), &mut Vec::new());
    assert!(!swap::update_dir(&here).exists());
    assert!(!here.join("sc-offline.exe.update-old").exists());
    assert_eq!(read(&here.join("sc-offline.exe")), b"NEW-EXE");
}

#[test]
fn tampered_zip_digest_changes_nothing() {
    let here = install("tampered-zip");
    let zip = good_zip("v0.7.1");
    let mut info = info_for(&zip);
    info.sha256 = hex_digest(b"something else");
    let before = snapshot(&here);
    let (r, out) = apply(&here, &zip, &info, &mut FakeMachine::default());
    assert!(r.is_err());
    assert!(out.contains("doesn't match GitHub's SHA-256"), "{out}");
    assert_eq!(snapshot(&here), before);
}

#[test]
fn tampered_file_digest_in_the_manifest_changes_nothing() {
    let here = install("tampered-file");
    let mut e = release_entries("v0.7.1", &NEW_FILES);
    e[0].data = b"EVIL-EXE".to_vec(); // same length is not needed; size differs too
    let zip = zip_bytes(&e);
    let before = snapshot(&here);
    let (r, out) = apply(&here, &zip, &info_for(&zip), &mut FakeMachine::default());
    assert!(r.is_err(), "{out}");
    assert_eq!(snapshot(&here), before);
    // Same size, different bytes: caught by the digest.
    let mut e = release_entries("v0.7.1", &NEW_FILES);
    e[0].data = b"EVIL-EX".to_vec();
    let zip = zip_bytes(&e);
    let (r, out) = apply(&here, &zip, &info_for(&zip), &mut FakeMachine::default());
    assert!(
        r.is_err() && out.contains("doesn't match the manifest"),
        "{out}"
    );
    assert_eq!(snapshot(&here), before);
}

#[test]
fn oversized_files_and_downloads_are_refused() {
    let here = install("oversized");
    let before = snapshot(&here);
    // A file that inflates past the size its header declares (zip bomb shape).
    let mut e = release_entries("v0.7.1", &NEW_FILES);
    e[3].data = vec![b'A'; 200_000];
    e[3].deflate = true;
    e[3].lie = Some(5);
    let zip = zip_bytes(&e);
    let (r, _) = apply(&here, &zip, &info_for(&zip), &mut FakeMachine::default());
    assert!(r.is_err());
    assert_eq!(snapshot(&here), before);
    // Manifest sizes over the per-file and total caps.
    let sha = hex_digest(b"x");
    let big = format!(
        r#"{{"format":1,"version":"0.7.1","tag":"v0.7.1","files":[{{"path":"a","sha256":"{sha}","size":{}}}]}}"#,
        manifest::MAX_FILE_BYTES + 1
    );
    assert!(manifest::parse(big.as_bytes())
        .unwrap_err()
        .contains("size cap"));
    let three = format!(
        r#"{{"format":1,"version":"0.7.1","tag":"v0.7.1","files":[{}]}}"#,
        (0..3)
            .map(|i| format!(
                r#"{{"path":"f{i}","sha256":"{sha}","size":{}}}"#,
                manifest::MAX_FILE_BYTES
            ))
            .collect::<Vec<_>>()
            .join(",")
    );
    assert!(manifest::parse(three.as_bytes())
        .unwrap_err()
        .contains("total size cap"));
    // The server sends more than GitHub's published size: the capped download fails (twice).
    let zip = good_zip("v0.7.1");
    let mut info = info_for(&zip);
    info.size = zip.len() as u64 - 1;
    let mut net = FakeNet::default();
    net.serve(&info.zip_url, &zip);
    let (r, out) = apply_with(&here, &info, &mut net, &mut FakeMachine::default());
    assert!(r.is_err() && out.contains("trying once more"), "{out}");
    assert_eq!(net.log.len(), 2);
    assert_eq!(snapshot(&here), before);
    // A release whose published size is over the zip cap is not downloaded at all.
    let mut info = info_for(&zip);
    info.size = release::MAX_ZIP_BYTES + 1;
    let mut net = FakeNet::default();
    net.serve(&info.zip_url, &zip);
    assert!(
        apply_with(&here, &info, &mut net, &mut FakeMachine::default())
            .0
            .is_err()
    );
    assert!(net.log.is_empty());
    // Not enough free space: refused before the download.
    let mut m = FakeMachine::default();
    m.free = Some(1 << 20);
    let mut net = FakeNet::default();
    net.serve(ZIP_URL, &zip);
    let (r, out) = apply_with(&here, &info_for(&zip), &mut net, &mut m);
    assert!(
        r.is_err() && out.contains("not enough free disk space"),
        "{out}"
    );
    assert!(net.log.is_empty());
}

#[test]
fn path_traversal_and_unlisted_files_are_refused() {
    let here = install("traversal");
    let before = snapshot(&here);
    let parent_marker = here.parent().unwrap().join("scol-update-evil.txt");
    let _ = std::fs::remove_file(&parent_marker);
    // The manifest lists a ..\ path.
    let mut e = release_entries("v0.7.1", &NEW_FILES);
    let last = e.len() - 1;
    let m = String::from_utf8(e[last].data.clone())
        .unwrap()
        .replace("data/new.txt", "../scol-update-evil.txt");
    e[last].data = m.into_bytes();
    e[3].name = "sc-offline-v0.7.1/../scol-update-evil.txt".into();
    let zip = zip_bytes(&e);
    let (r, _) = apply(&here, &zip, &info_for(&zip), &mut FakeMachine::default());
    assert!(r.is_err());
    assert!(!parent_marker.exists());
    // A zip entry name with .., an absolute path, a drive and a backslash is refused on open.
    for bad in [
        "../x",
        "/x",
        "c:/x",
        "a\\b",
        "sc-offline-v0.7.1/../../x",
        "a/./b",
    ] {
        let zip = zip_bytes(&[ent(bad, b"x")]);
        let p = here.join("bad.zip");
        write(&p, &zip);
        assert!(scol_update::zipx::Archive::open(&p).is_err(), "{bad}");
    }
    std::fs::remove_file(here.join("bad.zip")).unwrap();
    // A file the manifest doesn't list.
    let mut e = release_entries("v0.7.1", &NEW_FILES);
    e.push(ent("sc-offline-v0.7.1/data/extra.dll", b"unlisted"));
    let zip = zip_bytes(&e);
    let (r, out) = apply(&here, &zip, &info_for(&zip), &mut FakeMachine::default());
    assert!(
        r.is_err() && out.contains("which its manifest doesn't list"),
        "{out}"
    );
    // A file outside the release folder.
    let mut e = release_entries("v0.7.1", &NEW_FILES);
    e.push(ent("elsewhere/x.txt", b"x"));
    let zip = zip_bytes(&e);
    assert!(
        apply(&here, &zip, &info_for(&zip), &mut FakeMachine::default())
            .0
            .is_err()
    );
    // A release without the program files.
    let zip = zip_bytes(&release_entries("v0.7.1", &[("data/a.txt", b"a")]));
    assert!(
        apply(&here, &zip, &info_for(&zip), &mut FakeMachine::default())
            .0
            .is_err()
    );
    assert_eq!(snapshot(&here), before);
}

#[test]
fn downgrade_and_wrong_tag_are_refused() {
    let here = install("downgrade");
    let before = snapshot(&here);
    // The release itself says 0.6.0: older than the running 0.7.0, even if offered as 0.7.1.
    let zip = zip_bytes(&release_entries("v0.6.0", &NEW_FILES));
    let mut info = info_for(&zip);
    info.tag = "v0.6.0".into();
    info.zip_name = "sc-offline-v0.6.0.zip".into();
    let (r, out) = apply(&here, &zip, &info, &mut FakeMachine::default());
    assert!(r.is_err() && out.contains("isn't newer"), "{out}");
    // Same version.
    let zip = zip_bytes(&release_entries("v0.7.0", &NEW_FILES));
    let mut info = info_for(&zip);
    info.tag = "v0.7.0".into();
    assert!(apply(&here, &zip, &info, &mut FakeMachine::default())
        .0
        .is_err());
    // The zip holds v0.9.9 but v0.7.1 was asked for: it is not the release that was offered.
    let zip = zip_bytes(&release_entries("v0.9.9", &NEW_FILES));
    let (r, _) = apply(&here, &zip, &info_for(&zip), &mut FakeMachine::default());
    assert!(r.is_err());
    // Manifest version and tag disagree.
    let mut e = release_entries("v0.7.1", &NEW_FILES);
    let last = e.len() - 1;
    e[last].data = String::from_utf8(e[last].data.clone())
        .unwrap()
        .replace(r#""version":"0.7.1""#, r#""version":"0.7.5""#)
        .into_bytes();
    let zip = zip_bytes(&e);
    assert!(
        apply(&here, &zip, &info_for(&zip), &mut FakeMachine::default())
            .0
            .is_err()
    );
    assert_eq!(snapshot(&here), before);
}

#[test]
fn failed_swap_rolls_back_to_the_old_files() {
    let here = install("fail-after");
    let zip = good_zip("v0.7.1");
    let before = snapshot(&here);
    for n in 0..3 {
        let mut net = FakeNet::default();
        net.serve(ZIP_URL, &zip);
        let mut m = FakeMachine::default();
        let mut out = Vec::new();
        let info = info_for(&zip);
        flow::stage_update(&here, &info, "0.7.0", &mut net, &mut m, &mut out).unwrap();
        let r = flow::install_staged(
            &here,
            "v0.7.1",
            "0.7.0",
            &mut m,
            &mut out,
            SwapOptions {
                fail_after: Some(n),
                crash_after: None,
            },
        );
        assert!(r.is_err(), "n={n}");
        assert_eq!(snapshot(&here), before, "n={n}");
        assert!(!here.join("sc-offline.exe.update-new").exists());
    }
}

#[test]
fn interrupted_swap_is_repaired_at_the_next_start() {
    let zip = good_zip("v0.7.1");
    for n in 0..3 {
        let here = install(&format!("crash-{n}"));
        let before = snapshot(&here);
        let mut net = FakeNet::default();
        net.serve(ZIP_URL, &zip);
        let mut m = FakeMachine::default();
        let mut out = Vec::new();
        flow::stage_update(&here, &info_for(&zip), "0.7.0", &mut net, &mut m, &mut out).unwrap();
        let staged = flow::stage_root(&here, &mut m).join("staged");
        // Stop dead mid-swap: the old file is moved aside, the new one is not in place yet.
        let r = swap::swap_staged(
            &here,
            &staged,
            "v0.7.1",
            "0.7.0",
            &mut m,
            &mut out,
            SwapOptions {
                fail_after: None,
                crash_after: Some(n),
            },
        );
        assert_eq!(r.unwrap_err(), "simulated crash");
        assert_ne!(
            snapshot(&here),
            before,
            "the crash must leave a broken tree (n={n})"
        );
        // The next start puts everything back and cleans up.
        let mut out = Vec::new();
        assert!(swap::settle_previous(&here, &mut m, &mut out));
        assert!(String::from_utf8_lossy(&out).contains("a previous update didn't finish"));
        assert_eq!(snapshot(&here), before, "n={n}");
        assert!(!swap::update_dir(&here).exists());
        // A second settle is a no-op.
        assert!(!swap::settle_previous(&here, &mut m, &mut Vec::new()));
    }
}

#[test]
fn a_cpp_journal_with_backslashes_is_settled_too() {
    let here = install("cpp-journal");
    // As the C++ launcher leaves it after a crash: old exe moved aside, journal not done.
    std::fs::rename(
        here.join("sc-offline.exe"),
        here.join("sc-offline.exe.update-old"),
    )
    .unwrap();
    write(&here.join("data/new.txt.update-new"), b"half");
    write(&here.join("data/new.txt"), b"half");
    write(
        &swap::journal_path(&here),
        b"N data\\new.txt\nR sc-offline.exe\n",
    );
    assert!(swap::settle_previous(
        &here,
        &mut FakeMachine::default(),
        &mut Vec::new()
    ));
    assert_eq!(read(&here.join("sc-offline.exe")), b"OLD-EXE");
    assert!(!here.join("data/new.txt").exists() && !here.join("data/new.txt.update-new").exists());
    // A journal naming a path outside the folder is ignored, not followed.
    let outside = here.parent().unwrap().join("scol-update-outside.txt");
    write(&outside, b"keep");
    write(
        &swap::journal_path(&here),
        b"N ..\\scol-update-outside.txt\n",
    );
    swap::settle_previous(&here, &mut FakeMachine::default(), &mut Vec::new());
    assert_eq!(read(&outside), b"keep");
    let _ = std::fs::remove_file(outside);
}

#[test]
fn a_new_exe_that_fails_its_self_test_is_rolled_back() {
    let here = install("selftest-fails");
    let zip = good_zip("v0.7.1");
    let before = snapshot(&here);
    let mut m = FakeMachine::default();
    m.self_test = Box::new(|_, _| Err("the new sc-offline.exe --self-test exited 9".into()));
    let (r, out) = apply(&here, &zip, &info_for(&zip), &mut m);
    assert!(r.is_err() && out.contains("failed its self-test"), "{out}");
    assert_eq!(snapshot(&here), before);
}

#[test]
fn a_running_game_blocks_the_swap() {
    let here = install("game-running");
    let zip = good_zip("v0.7.1");
    let before = snapshot(&here);
    let mut m = FakeMachine::default();
    m.game = true;
    let (r, out) = apply(&here, &zip, &info_for(&zip), &mut m);
    assert!(r.is_err() && out.contains("is running"), "{out}");
    assert_eq!(snapshot(&here), before);
}

#[test]
fn locked_files_are_retried_with_back_off() {
    use std::cell::RefCell;
    use std::os::windows::fs::OpenOptionsExt;
    use std::rc::Rc;

    // The waits are the documented ones, in this order.
    assert_eq!(swap::LOCK_WAITS, [100, 250, 500, 1000, 2000, 4000]);

    // Nothing locked: no waiting.
    let here = install("locked-none");
    let zip = good_zip("v0.7.1");
    let mut m = FakeMachine::default();
    let (r, _) = apply(&here, &zip, &info_for(&zip), &mut m);
    assert!(r.is_ok());
    assert!(m.sleeps.is_empty(), "no lock, no waiting");

    // The old exe is held open without sharing (an antivirus scan, an indexer): moving it aside fails with
    // a sharing violation. Every wait is used, then the swap gives up and puts everything back.
    let here = install("locked-forever");
    let before = snapshot(&here);
    let hold = std::fs::OpenOptions::new()
        .read(true)
        .share_mode(0)
        .open(here.join("sc-offline.exe"))
        .unwrap();
    let mut m = FakeMachine::default();
    let (r, text) = apply(&here, &zip, &info_for(&zip), &mut m);
    assert!(r.is_err());
    assert_eq!(m.sleeps, swap::LOCK_WAITS, "all six waits, growing");
    assert!(
        text.contains("couldn't move sc-offline.exe aside"),
        "{text}"
    );
    drop(hold);
    assert_eq!(
        snapshot(&here),
        before,
        "rolled back; the old files are untouched"
    );

    // The lock goes away during the third wait: the swap then goes through.
    let here = install("locked-briefly");
    let hold = Rc::new(RefCell::new(Some(
        std::fs::OpenOptions::new()
            .read(true)
            .share_mode(0)
            .open(here.join("sc-offline.exe"))
            .unwrap(),
    )));
    let mut m = FakeMachine::default();
    let h2 = Rc::clone(&hold);
    m.on_sleep = Some(Box::new(move |n| {
        if n == 3 {
            h2.borrow_mut().take();
        }
    }));
    let (r, _) = apply(&here, &zip, &info_for(&zip), &mut m);
    assert!(r.is_ok(), "{r:?}");
    assert_eq!(m.sleeps, [100, 250, 500]);
    assert_eq!(read(&here.join("sc-offline.exe")), b"NEW-EXE");
    assert_eq!(read(&here.join("data/wallet.txt")), b"my wallet");
}

#[test]
fn the_retry_helper_waits_only_for_lock_errors() {
    use std::io::{Error, ErrorKind};
    let mut m = FakeMachine::default();
    // sharing violation (32), lock violation (33) and access denied (5) are waited for
    for code in [32, 33, 5] {
        m.sleeps.clear();
        let mut left = 2;
        let r = swap::retrying(&mut m, || {
            if left > 0 {
                left -= 1;
                Err(Error::from_raw_os_error(code))
            } else {
                Ok(7)
            }
        });
        assert_eq!(r.unwrap(), 7);
        assert_eq!(m.sleeps, [100, 250], "code {code}");
    }
    // anything else fails at once
    m.sleeps.clear();
    let r: std::io::Result<()> = swap::retrying(&mut m, || Err(Error::from(ErrorKind::NotFound)));
    assert!(r.is_err());
    assert!(m.sleeps.is_empty());
    // a lock that never goes: six waits, then the last error
    m.sleeps.clear();
    let mut tries = 0;
    let r: std::io::Result<()> = swap::retrying(&mut m, || {
        tries += 1;
        Err(Error::from_raw_os_error(32))
    });
    assert_eq!(r.unwrap_err().raw_os_error(), Some(32));
    assert_eq!(tries, 7);
    assert_eq!(m.sleeps.iter().sum::<u64>(), 7850);
}

#[test]
fn moves_replace_the_target_and_go_through_to_disk() {
    let dir = tmp("move-through");
    write(&dir.join("a"), b"new");
    write(&dir.join("b"), b"old");
    swap::move_through(&dir.join("a"), &dir.join("b")).unwrap();
    assert_eq!(read(&dir.join("b")), b"new");
    assert!(!dir.join("a").exists());
    assert!(swap::move_through(&dir.join("a"), &dir.join("c")).is_err());
}

#[test]
fn elevation_is_used_only_when_the_folder_is_not_writable() {
    let zip = good_zip("v0.7.1");
    // Not writable, elevation refused: nothing changes.
    let here = install("elevated-refused");
    let before = snapshot(&here);
    let mut m = FakeMachine::default();
    m.writable = false;
    let (r, out) = apply(&here, &zip, &info_for(&zip), &mut m);
    assert!(
        r.is_err() && out.contains("administrator rights refused"),
        "{out}"
    );
    assert_eq!(m.elevated_calls, 1);
    assert_eq!(snapshot(&here), before);
    // Elevated half exits non-zero.
    let mut m = FakeMachine::default();
    m.writable = false;
    m.elevated = Some(Ok(1));
    assert!(apply(&here, &zip, &info_for(&zip), &mut m).0.is_err());
    // The elevated half itself: only a folder named ...\update\staged is accepted.
    let mut out = Vec::new();
    let mut m = FakeMachine::default();
    assert_eq!(
        flow::elevated_update(
            &here,
            "v0.7.1",
            &here.join("anywhere"),
            "0.7.0",
            &mut m,
            &mut out
        ),
        1
    );
    assert!(String::from_utf8_lossy(&out).contains("refusing staging folder"));
    assert_eq!(
        flow::elevated_update(
            &here,
            "v0.7.1",
            &here.join("data/update/staged"),
            "0.7.0",
            &mut m,
            &mut out
        ),
        1
    );
    // A valid staged folder is swapped (the part that runs as administrator).
    let mut net = FakeNet::default();
    net.serve(ZIP_URL, &zip);
    let mut mm = FakeMachine::default();
    flow::stage_update(&here, &info_for(&zip), "0.7.0", &mut net, &mut mm, &mut out).unwrap();
    let staged = here.join("data/update/staged");
    assert_eq!(
        flow::elevated_update(&here, "v0.7.1", &staged, "0.7.0", &mut mm, &mut out),
        0
    );
    assert_eq!(read(&here.join("sc-offline.exe")), b"NEW-EXE");
    // A staged folder whose file was changed after staging is refused by the re-check.
    let here = install("elevated-recheck");
    flow::stage_update(&here, &info_for(&zip), "0.7.0", &mut net, &mut mm, &mut out).unwrap();
    write(&here.join("data/update/staged/sc-offline.exe"), b"SWAPPED");
    let staged = here.join("data/update/staged");
    assert_eq!(
        flow::elevated_update(&here, "v0.7.1", &staged, "0.7.0", &mut mm, &mut out),
        1
    );
    assert_eq!(read(&here.join("sc-offline.exe")), b"OLD-EXE");
}

#[test]
fn self_test_exit_codes_match_the_cpp_launcher() {
    let here = tmp("self-test");
    let mut out = Vec::new();
    assert_eq!(swap::self_test("v0.7.1", &here, "0.7.1", &mut out), 4);
    write(&here.join("dinput8.dll"), b"d");
    assert_eq!(swap::self_test("v0.7.1", &here, "0.7.1", &mut out), 0);
    assert_eq!(swap::self_test("v0.7.2", &here, "0.7.1", &mut out), 2);
    assert!(String::from_utf8_lossy(&out).contains("self-test: sc-offline 0.7.1 ok"));
}

// ---------------------------------------------------------------- the command

#[test]
fn update_command_check_ask_and_apply() {
    use scol_update::cli::{run_with, Context};
    let here = install("command");
    let zip = good_zip("v0.7.1");
    let ctx = Context {
        here: here.clone(),
        version: "0.7.0".into(),
        prerelease: false,
        check_updates: true,
    };
    let mut net = FakeNet::default();
    net.serve(
        release::RELEASES_LATEST,
        &release_json("v0.7.1", &zip, ZIP_URL),
    );
    net.serve(ZIP_URL, &zip);
    let args = |a: &[&str]| a.iter().map(|s| s.to_string()).collect::<Vec<_>>();
    let mut out = Vec::new();
    // --check only reports.
    let mut ask = |_: &str| panic!("must not ask");
    assert_eq!(
        run_with(
            &args(&["update", "--check"]),
            &ctx,
            &mut net,
            &mut FakeMachine::default(),
            &mut out,
            &mut ask
        ),
        0
    );
    assert!(
        String::from_utf8_lossy(&out).contains("sc-offline v0.7.1 is available (you have 0.7.0)")
    );
    assert_eq!(read(&here.join("sc-offline.exe")), b"OLD-EXE");
    // Answering no changes nothing.
    let mut no = |_: &str| false;
    assert_eq!(
        run_with(
            &args(&["update"]),
            &ctx,
            &mut net,
            &mut FakeMachine::default(),
            &mut out,
            &mut no
        ),
        0
    );
    assert_eq!(read(&here.join("sc-offline.exe")), b"OLD-EXE");
    // --yes applies.
    assert_eq!(
        run_with(
            &args(&["update", "--yes"]),
            &ctx,
            &mut net,
            &mut FakeMachine::default(),
            &mut out,
            &mut no
        ),
        0
    );
    assert_eq!(read(&here.join("sc-offline.exe")), b"NEW-EXE");
    // Now up to date: exit 0 with the reason.
    let ctx = Context {
        version: "0.7.1".into(),
        ..ctx
    };
    out.clear();
    assert_eq!(
        run_with(
            &args(&["update"]),
            &ctx,
            &mut net,
            &mut FakeMachine::default(),
            &mut out,
            &mut no
        ),
        0
    );
    assert!(String::from_utf8_lossy(&out).contains("Update:   up to date"));
    // Unknown arguments print usage and exit 1.
    assert_eq!(
        run_with(
            &args(&["update", "--bogus"]),
            &ctx,
            &mut net,
            &mut FakeMachine::default(),
            &mut out,
            &mut no
        ),
        1
    );
    // The game running blocks an apply.
    let ctx = Context {
        version: "0.7.0".into(),
        ..ctx
    };
    let mut m = FakeMachine::default();
    m.game = true;
    assert_eq!(
        run_with(
            &args(&["update", "--yes"]),
            &ctx,
            &mut net,
            &mut m,
            &mut out,
            &mut no
        ),
        1
    );
}

// ---------------------------------------------------------------- catalogue fetch path

#[test]
fn catalogue_index_and_zip_go_through_the_same_fetcher_contract() {
    let hosts = ["raw.githubusercontent.com", "github.com"];
    let redirect = ["objects.githubusercontent.com"];
    let idx = "https://raw.githubusercontent.com/scuba-sc/catalogue/main/index.json";
    let sig = "https://raw.githubusercontent.com/scuba-sc/catalogue/main/index.json.sig";
    let mut net = FakeNet::default();
    net.serve(idx, b"{\"index\":true}");
    net.serve(sig, b"sig-line\n");
    let (b, s) = fetch::fetch_index(&mut net, idx, sig, &hosts, &redirect).unwrap();
    assert_eq!(
        (b.as_slice(), s.as_str()),
        (&b"{\"index\":true}"[..], "sig-line\n")
    );
    // Index over the cap is refused by the fetcher's cap.
    net.serve(idx, &vec![b'x'; scol_catalogue::index::MAX_INDEX_BYTES + 1]);
    assert!(fetch::fetch_index(&mut net, idx, sig, &hosts, &redirect).is_err());
    // A redirect to a foreign host is refused; to the allowed CDN host it is followed.
    net.routes.insert(
        idx.into(),
        Resp::Redirect("https://evil.example/index.json".into()),
    );
    assert!(fetch::fetch_index(&mut net, idx, sig, &hosts, &redirect).is_err());
    net.routes.insert(
        idx.into(),
        Resp::Redirect("https://objects.githubusercontent.com/i?sig=1".into()),
    );
    net.serve("https://objects.githubusercontent.com/i?sig=1", b"{}");
    assert!(fetch::fetch_index(&mut net, idx, sig, &hosts, &redirect).is_ok());
    // Zip: capped at the size the signed index declares.
    let entry = scol_catalogue::index::Entry {
        id: "x".into(),
        name: "x".into(),
        version: "1.0.0".into(),
        kind: scol_plugins::manifest::Kind::Data,
        api: "1.0".into(),
        api_major: 1,
        api_minor: 0,
        sdk: ">=2.4.0".into(),
        license: "GPL-3.0-only".into(),
        maintainers: vec![],
        review: scol_catalogue::index::Review::FirstParty,
        evidence: String::new(),
        repository: String::new(),
        commit: String::new(),
        path: String::new(),
        zip_url: "https://github.com/scuba-sc/catalogue/releases/download/x-1/x.zip".into(),
        zip_sha256: String::new(),
        zip_size: 10,
    };
    net.serve(&entry.zip_url, &[7u8; 10]);
    let dest = tmp("cat-zip").join("x.zip");
    fetch::download_zip(&mut net, &entry, &dest, &hosts, &redirect).unwrap();
    assert_eq!(read(&dest), vec![7u8; 10]);
    net.serve(&entry.zip_url, &[7u8; 11]);
    assert!(fetch::download_zip(&mut net, &entry, &dest, &hosts, &redirect).is_err());
}

// ---------------------------------------------------------------- stage only

#[test]
fn stage_zip_deflate_entries_round_trip() {
    let dir = tmp("stage-deflate");
    let big: Vec<u8> = (0..300_000u32)
        .flat_map(|i| (i % 251).to_le_bytes())
        .collect();
    let files: [(&str, &[u8]); 4] = [
        ("sc-offline.exe", b"E"),
        ("dinput8.dll", b"D"),
        ("dinput8-rust-menu.dll", b"R"),
        ("data/big.bin", &big),
    ];
    let mut e = release_entries("v0.7.1", &files);
    e[3].deflate = true;
    let zip = dir.join("r.zip");
    write(&zip, &zip_bytes(&e));
    let staged = dir.join("staged");
    let man = stage::stage_zip(&zip, &staged, "v0.7.1", "0.7.0", &mut Vec::new()).unwrap();
    assert_eq!(man.files.len(), 4);
    assert_eq!(read(&staged.join("data/big.bin")), big);
    assert_eq!(read(&staged.join("manifest.json")), e[4].data);
}

// ---------------------------------------------------------------- old-launcher contract

/// The C++ launcher built with -DSCO_UPDATE_TEST, from the environment.
fn cpp_exe() -> Option<PathBuf> {
    let p = PathBuf::from(std::env::var_os("SCOL_CPP_UPDATE_EXE")?);
    p.is_file().then_some(p)
}

fn standin_exe() -> PathBuf {
    let me = std::env::current_exe().unwrap();
    let p = me
        .parent()
        .unwrap()
        .parent()
        .unwrap()
        .join("examples")
        .join("standin.exe");
    assert!(
        p.is_file(),
        "build the example first: cargo test builds it ({p:?})"
    );
    p
}

/// A machine that is the real one except the game check (a developer may have the game open; this
/// only reads the process list and never touches it).
struct RealButNoGame(System);
impl Platform for RealButNoGame {
    fn game_running(&mut self) -> bool {
        false
    }
    fn run_self_test(&mut self, h: &Path, t: &str) -> Result<(), String> {
        self.0.run_self_test(h, t)
    }
    fn can_write(&mut self, d: &Path) -> bool {
        self.0.can_write(d)
    }
    fn run_elevated(&mut self, h: &Path, t: &str, s: &Path) -> Result<i32, String> {
        self.0.run_elevated(h, t, s)
    }
    fn free_space(&mut self, d: &Path) -> Option<u64> {
        self.0.free_space(d)
    }
    fn local_app_data(&mut self) -> Option<PathBuf> {
        self.0.local_app_data()
    }
    fn sleep_ms(&mut self, ms: u64) {
        self.0.sleep_ms(ms)
    }
}

fn cpp_version(exe: &Path) -> String {
    let o = std::process::Command::new(exe)
        .args(["--self-test", "v0"])
        .output()
        .unwrap();
    let s = String::from_utf8_lossy(&o.stdout).into_owned();
    s.split_whitespace()
        .nth(3)
        .unwrap_or("")
        .to_string() // "self-test: this is 0.8.0, expected v0"
        .trim_end_matches(',')
        .to_string()
}

/// A GitHub release object as the update check reads it.
fn candidate_release(tag: &str, pre: bool) -> String {
    format!(
        r#"{{"tag_name":"{tag}","prerelease":{pre},"draft":false,"assets":[{{"name":"sc-offline-{tag}.zip","size":10,"digest":"sha256:{}","browser_download_url":"https://github.com/scuba-sc/sc-offline/releases/download/{tag}/sc-offline-{tag}.zip"}}]}}"#,
        "a".repeat(64)
    )
}

/// The release-candidate choice, both launchers on the same GitHub replies: 0.7.7 and 0.8.0-rc.1 on the stable
/// channel are never offered a pre-release, and 0.8.0-rc.1 is offered 0.8.0 once it is published. The C++ side
/// is the `--pick-release` hook of a launcher built with -DSCO_UPDATE_TEST (the real `PickRelease`).
#[test]
fn both_launchers_choose_the_same_release_candidate_offers() {
    let Some(cpp) = cpp_exe() else {
        eprintln!("SKIP: set SCOL_CPP_UPDATE_EXE to a launcher built with -DSCO_UPDATE_TEST");
        return;
    };
    let rc1 = candidate_release("v0.8.0-rc.1", true);
    let rc2 = candidate_release("v0.8.0-rc.2", true);
    let stable = candidate_release("v0.8.0", false);
    let old = candidate_release("v0.7.7", false);
    let list = |r: &[&str]| format!("[{}]", r.join(","));
    let cases: Vec<(&str, bool, String, Option<&str>)> = vec![
        ("0.8.0-rc.1", false, stable.clone(), Some("v0.8.0")),
        (
            "0.8.0-rc.1",
            false,
            list(&[&stable, &rc2, &rc1, &old]),
            Some("v0.8.0"),
        ),
        ("0.8.0-rc.1", false, rc2.clone(), None),
        ("0.8.0-rc.1", false, list(&[&rc2, &rc1, &old]), None),
        (
            "0.8.0-rc.1",
            true,
            list(&[&rc2, &rc1, &old]),
            Some("v0.8.0-rc.2"),
        ),
        ("0.8.0-rc.1", true, list(&[&rc1, &old]), None),
        ("0.7.7", false, rc1.clone(), None),
        ("0.7.7", false, list(&[&rc2, &rc1, &old]), None),
        ("0.7.7", false, stable.clone(), Some("v0.8.0")),
        ("0.7.7", true, list(&[&rc1, &old]), Some("v0.8.0-rc.1")),
        ("0.7.8", true, rc1.clone(), Some("v0.8.0-rc.1")),
    ];
    let dir = std::env::temp_dir().join(format!("scol-pick-release-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    for (n, (current, pre, body, want)) in cases.iter().enumerate() {
        let file = dir.join(format!("reply{n}.json"));
        std::fs::write(&file, body).unwrap();
        let o = std::process::Command::new(&cpp)
            .args(["--pick-release", current, if *pre { "1" } else { "0" }])
            .arg(&file)
            .output()
            .unwrap();
        let out = String::from_utf8_lossy(&o.stdout).trim().to_string();
        let rust = release::pick(body.as_bytes(), current, *pre)
            .map(|u| u.tag)
            .ok();
        assert_eq!(
            rust.as_deref(),
            *want,
            "Rust, case {n}: {current} pre={pre}"
        );
        let cpp_says = out.strip_prefix("offer ").map(str::to_string);
        assert_eq!(
            cpp_says.as_deref(),
            *want,
            "C++, case {n}: {current} pre={pre}: {out}"
        );
    }
    let _ = std::fs::remove_dir_all(&dir);
}

#[test]
fn an_old_cpp_launcher_updates_to_a_rust_style_exe() {
    let Some(cpp) = cpp_exe() else {
        eprintln!("SKIP: set SCOL_CPP_UPDATE_EXE to a launcher built with -DSCO_UPDATE_TEST");
        return;
    };
    let standin = std::fs::read(standin_exe()).unwrap();
    let cur = cpp_version(&cpp);
    assert!(!cur.is_empty());
    let tag = "v99.0.0";
    let files: [(&str, &[u8]); 5] = [
        ("sc-offline.exe", &standin),
        ("dinput8.dll", b"NEW-DLL"),
        ("dinput8-rust-menu.dll", b"NEW-RUST-MENU-DLL"),
        ("data/new.txt", b"fresh"),
        ("sc-offline.ini", b"start_ship = default\r\nnew_key = 5\r\n"),
    ];
    let zip = zip_bytes(&release_entries(tag, &files));
    let run = |here: &Path, zipfile: &Path, sha: &str, envs: &[(&str, &str)]| {
        let mut c = std::process::Command::new(here.join("sc-offline.exe"));
        c.args(["--apply-zip", &zipfile.to_string_lossy(), tag, sha])
            .current_dir(here);
        for (k, v) in envs {
            c.env(k, v);
        }
        let o = c.output().unwrap();
        (
            o.status.code(),
            String::from_utf8_lossy(&o.stdout).into_owned(),
        )
    };
    let setup = |name: &str| {
        let here = tmp(name);
        std::fs::copy(&cpp, here.join("sc-offline.exe")).unwrap();
        write(&here.join("dinput8.dll"), b"OLD-DLL");
        write(&here.join("sc-offline.ini"), b"start_ship = mine\r\n");
        write(&here.join("data/wallet.txt"), b"my wallet");
        let z = here.join("release.zip");
        write(&z, &zip);
        (here, z)
    };
    // Good: same manifest, digest and --self-test contract.
    let (here, z) = setup("cpp-good");
    let (code, out) = run(&here, &z, &hex_digest(&zip), &[]);
    assert_eq!(code, Some(0), "{out}");
    assert_eq!(read(&here.join("sc-offline.exe")), standin, "{out}");
    assert_eq!(read(&here.join("data/new.txt")), b"fresh");
    assert_eq!(read(&here.join("data/wallet.txt")), b"my wallet");
    assert!(out.contains("passed its self-test"), "{out}");
    // The new (stand-in) exe fails its self-test: the C++ launcher puts the old exe back.
    let (here, z) = setup("cpp-selftest-fails");
    write(&here.join("standin-fail.txt"), b"x");
    let before = read(&here.join("sc-offline.exe"));
    let (code, out) = run(&here, &z, &hex_digest(&zip), &[]);
    assert_eq!(code, Some(1), "{out}");
    assert_eq!(read(&here.join("sc-offline.exe")), before, "{out}");
    assert!(!here.join("data/new.txt").exists());
    // Our tampered zips are refused by the C++ side as well (same manifest rules).
    let (here, z) = setup("cpp-tampered");
    let mut e = release_entries(tag, &files);
    e[1].data = b"EVIL-DLL".to_vec();
    let bad = zip_bytes(&e);
    write(&z, &bad);
    let (code, out) = run(&here, &z, &hex_digest(&bad), &[]);
    assert_eq!(code, Some(1), "{out}");
    assert_eq!(read(&here.join("dinput8.dll")), b"OLD-DLL");
    // A swap that fails midway (SCO_TEST_FAIL_AFTER) rolls back; the Rust settle then agrees.
    let (here, z) = setup("cpp-fail-after");
    let (code, out) = run(
        &here,
        &z,
        &hex_digest(&zip),
        &[("SCO_TEST_FAIL_AFTER", "1")],
    );
    assert_eq!(code, Some(1), "{out}");
    assert_eq!(read(&here.join("dinput8.dll")), b"OLD-DLL");
    swap::settle_previous(&here, &mut FakeMachine::default(), &mut Vec::new());
    assert_eq!(
        read(&here.join("sc-offline.exe")).len(),
        std::fs::metadata(&cpp).unwrap().len() as usize
    );
}

#[test]
fn the_rust_updater_accepts_a_cpp_exe_as_the_new_launcher() {
    let Some(cpp) = cpp_exe() else {
        eprintln!("SKIP: set SCOL_CPP_UPDATE_EXE to a launcher built with -DSCO_UPDATE_TEST");
        return;
    };
    let cur = cpp_version(&cpp);
    let exe = std::fs::read(&cpp).unwrap();
    let tag = format!("v{cur}");
    let files: [(&str, &[u8]); 3] = [
        ("sc-offline.exe", &exe),
        ("dinput8.dll", b"NEW-DLL"),
        ("dinput8-rust-menu.dll", b"NEW-RUST-MENU-DLL"),
    ];
    let zip = zip_bytes(&release_entries(&tag, &files));
    let here = tmp("rust-applies-cpp");
    write(&here.join("sc-offline.exe"), b"OLD-EXE");
    write(&here.join("dinput8.dll"), b"OLD-DLL");
    let zip_path = here.join("rel.zip");
    write(&zip_path, &zip);
    // The real self-test: Rust launches the new exe's `--self-test <tag>`.
    let staged = here.join("data/update/staged");
    let mut out = Vec::new();
    stage::stage_zip(&zip_path, &staged, &tag, "0.0.1", &mut out).unwrap();
    let mut m = RealButNoGame(System);
    let r = swap::swap_staged(
        &here,
        &staged,
        &tag,
        "0.0.1",
        &mut m,
        &mut out,
        SwapOptions::default(),
    );
    assert!(r.is_ok(), "{}", String::from_utf8_lossy(&out));
    assert_eq!(read(&here.join("sc-offline.exe")), exe);
    // And a new exe that is not the promised version fails the real self-test and is rolled back.
    let here = tmp("rust-applies-wrong-exe");
    write(&here.join("sc-offline.exe"), b"OLD-EXE");
    write(&here.join("dinput8.dll"), b"OLD-DLL");
    let wrong = format!("v{cur}9"); // a tag the exe was not built as
    let zip = zip_bytes(&release_entries(&wrong, &files));
    let zp = here.join("rel.zip");
    write(&zp, &zip);
    let staged = here.join("data/update/staged");
    stage::stage_zip(&zp, &staged, &wrong, "0.0.1", &mut Vec::new()).unwrap();
    let r = swap::swap_staged(
        &here,
        &staged,
        &wrong,
        "0.0.1",
        &mut RealButNoGame(System),
        &mut Vec::new(),
        SwapOptions::default(),
    );
    assert!(r.is_err());
    assert_eq!(read(&here.join("sc-offline.exe")), b"OLD-EXE");
}

// ---------------------------------------------------------------- the offer, the restart, the token

fn offered_net(zip: &[u8]) -> FakeNet {
    let mut net = FakeNet::default();
    net.serve(
        release::RELEASES_LATEST,
        &release_json("v0.7.1", zip, ZIP_URL),
    );
    net.serve(ZIP_URL, zip);
    net
}

fn offer(
    here: &Path,
    net: &mut FakeNet,
    m: &mut FakeMachine,
    opts: flow::OfferOptions,
    answer: bool,
) -> (flow::Offer, String, Vec<String>) {
    let mut out = Vec::new();
    let mut asked = Vec::new();
    let r = flow::offer_update("0.7.0", false, opts, here, net, m, &mut out, &mut |q| {
        asked.push(q.to_string());
        answer
    });
    (r, String::from_utf8_lossy(&out).into_owned(), asked)
}

#[test]
fn the_offer_asks_like_the_cpp_launcher_and_applies_only_on_yes() {
    let here = install("offer");
    let zip = good_zip("v0.7.1");
    let mut net = offered_net(&zip);
    let opts = flow::OfferOptions::default();

    // no: nothing changes
    let mut m = FakeMachine::default();
    let (r, text, asked) = offer(&here, &mut net, &mut m, opts, false);
    assert!(matches!(r, flow::Offer::Declined(_)));
    assert_eq!(
        text,
        "Update:   sc-offline v0.7.1 is available (you have 0.7.0)\n[i] not updating\n"
    );
    assert_eq!(
        asked,
        ["Update now? Your saves and sc-offline.ini are kept."]
    );
    assert_eq!(read(&here.join("sc-offline.exe")), b"OLD-EXE");

    // nobody to ask: the offer is said, nothing is asked, nothing changes
    let mut m = FakeMachine::default();
    m.can_ask = false;
    let (r, text, asked) = offer(&here, &mut net, &mut m, opts, true);
    assert!(matches!(r, flow::Offer::Available(_)));
    assert!(
        text.ends_with("          (not asking: no console to answer in)\n"),
        "{text}"
    );
    assert!(asked.is_empty());

    // the game runs: refused before any question
    let mut m = FakeMachine::default();
    m.game = true;
    let (r, text, asked) = offer(&here, &mut net, &mut m, opts, true);
    assert!(matches!(r, flow::Offer::Blocked(_)));
    assert!(text.contains("[!] close the game before updating"));
    assert!(asked.is_empty());

    // --check never asks
    let mut m = FakeMachine::default();
    let o = flow::OfferOptions {
        check_only: true,
        ..opts
    };
    let (r, _, asked) = offer(&here, &mut net, &mut m, o, true);
    assert!(matches!(r, flow::Offer::Available(_)));
    assert!(asked.is_empty());

    // yes: applied, and the files are the new ones
    let mut m = FakeMachine::default();
    let (r, text, _) = offer(&here, &mut net, &mut m, opts, true);
    assert!(matches!(r, flow::Offer::Applied(_)), "{text}");
    assert_eq!(read(&here.join("sc-offline.exe")), b"NEW-EXE");
    assert_eq!(read(&here.join("data/wallet.txt")), b"my wallet");

    // --yes (the window's button) does not ask
    let here = install("offer-yes");
    let mut m = FakeMachine::default();
    let o = flow::OfferOptions { yes: true, ..opts };
    let (r, _, asked) = offer(&here, &mut net, &mut m, o, false);
    assert!(matches!(r, flow::Offer::Applied(_)));
    assert!(asked.is_empty());

    // a failed swap says so and leaves the old version
    let here = install("offer-fail");
    let mut m = FakeMachine::default();
    m.self_test = Box::new(|_, _| Err("the new sc-offline.exe --self-test exited 2".into()));
    let (r, text, _) = offer(&here, &mut net, &mut m, o, true);
    assert!(matches!(r, flow::Offer::Failed(_)));
    assert!(
        text.ends_with("[!] update failed; your current version is unchanged\n"),
        "{text}"
    );
    assert_eq!(read(&here.join("sc-offline.exe")), b"OLD-EXE");
}

#[test]
fn a_quiet_check_says_nothing_when_there_is_nothing_to_install() {
    let here = install("quiet");
    let zip = good_zip("v0.7.1");
    let mut net = offered_net(&zip);
    let mut out = Vec::new();
    let mut m = FakeMachine::default();
    let opts = flow::OfferOptions {
        quiet: true,
        ..Default::default()
    };
    // current is already the offered version
    let r = flow::offer_update(
        "0.7.1",
        false,
        opts,
        &here,
        &mut net,
        &mut m,
        &mut out,
        &mut |_| true,
    );
    assert!(matches!(r, flow::Offer::Nothing { .. }));
    assert!(out.is_empty(), "{}", String::from_utf8_lossy(&out));
    // a network that fails is as quiet
    let mut dead = FakeNet::default();
    let r = flow::offer_update(
        "0.7.0",
        false,
        opts,
        &here,
        &mut dead,
        &mut m,
        &mut out,
        &mut |_| true,
    );
    assert!(matches!(r, flow::Offer::Nothing { .. }));
    assert!(out.is_empty());
}

#[test]
fn play_ends_after_an_applied_update_and_the_new_exe_takes_over() {
    let here = install("play-restart");
    let zip = good_zip("v0.7.1");
    let mut net = offered_net(&zip);
    let args: Vec<String> = ["play", "--dry-run"]
        .iter()
        .map(|s| s.to_string())
        .collect();
    let run = |here: &Path, net: &mut FakeNet, m: &mut FakeMachine, yes: bool| {
        let mut out = Vec::new();
        let r = flow::check_and_offer_for_play(
            "0.7.0",
            false,
            here,
            &args,
            net,
            m,
            &mut out,
            &mut |_| yes,
        );
        (r, String::from_utf8_lossy(&out).into_owned())
    };

    // declined: play goes on
    let mut m = FakeMachine::default();
    let (r, _) = run(&here, &mut net, &mut m, false);
    assert_eq!(r, None);
    assert!(m.restarts.is_empty());

    // applied: the restart gets the play command line, its code ends play
    let mut m = FakeMachine::default();
    m.restart = Some(10); // a window child answers with the restart code
    let (r, _) = run(&here, &mut net, &mut m, true);
    assert_eq!(r, Some(10));
    assert_eq!(m.restarts, std::slice::from_ref(&args));
    assert_eq!(read(&here.join("sc-offline.exe")), b"NEW-EXE");

    // applied, and the platform has no restart: play still ends, with a note
    let here = install("play-no-restart");
    let mut m = FakeMachine::default();
    let (r, text) = run(&here, &mut net, &mut m, true);
    assert_eq!(r, Some(0));
    assert!(
        text.ends_with("[i] updated; start sc-offline.exe again to use the new version\n"),
        "{text}"
    );
}

#[test]
fn the_update_command_restarts_into_the_new_exe_after_a_swap() {
    use scol_update::cli::{run_with, Context};
    let here = install("command-restart");
    let zip = good_zip("v0.7.1");
    let mut net = offered_net(&zip);
    let ctx = Context {
        here: here.clone(),
        version: "0.7.0".into(),
        prerelease: false,
        check_updates: true,
    };
    let mut m = FakeMachine::default();
    m.restart = Some(42);
    let args = vec!["update".to_string(), "--yes".to_string()];
    let mut out = Vec::new();
    let code = run_with(&args, &ctx, &mut net, &mut m, &mut out, &mut |_| true);
    assert_eq!(code, 42, "the code of the new exe is the command's code");
    assert_eq!(m.restarts, [args]);
    // not applied (declined): no restart
    let here = install("command-no-restart");
    let ctx = Context { here, ..ctx };
    let mut m = FakeMachine::default();
    m.restart = Some(42);
    let code = run_with(
        &["update".to_string()],
        &ctx,
        &mut net,
        &mut m,
        &mut Vec::new(),
        &mut |_| false,
    );
    assert_eq!(code, 0);
    assert!(m.restarts.is_empty());
}

#[test]
fn the_self_test_can_start_with_a_normal_user_token() {
    use scol_update::platform::{is_elevated, run_self_test_as};
    let here = tmp("selftest-rights");
    std::fs::copy(standin_exe(), here.join("sc-offline.exe")).unwrap();
    write(&here.join("dinput8.dll"), b"DLL");
    // with this process's own rights
    let plain = run_self_test_as(&here, "v0", false).unwrap();
    assert!(!plain.restricted_token);
    // with a normal-user token: it either starts that way or falls back to our rights, as the C++ launcher
    // does; either way the stand-in's exit 0 is a pass
    let normal = run_self_test_as(&here, "v0", true).unwrap();
    eprintln!(
        "normal-user token used: {} (this process is elevated: {})",
        normal.restricted_token,
        is_elevated()
    );
    // the platform's own check picks the token by elevation
    assert!(System.run_self_test(&here, "v0").is_ok());
    // an exe that cannot run fails the self-test on both paths
    let bad = tmp("selftest-rights-bad");
    write(&bad.join("sc-offline.exe"), b"not an exe");
    assert!(run_self_test_as(&bad, "v0", false).is_err());
    assert!(run_self_test_as(&bad, "v0", true).is_err());
}
