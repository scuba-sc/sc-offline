// SPDX-License-Identifier: GPL-3.0-only
//! Shared fixture: a synthetic world under `D:\t` (the launcher folder, a fake game with the stand-in
//! exe as StarCitizen.exe, ProgramData and Program Files (x86)), and a fake host over it. Made-up names
//! only. No test here starts the real game or changes this PC.

#![allow(dead_code)]
#![cfg(windows)]

use scol_pcchanges::fake::FakeSystem;
use scol_pcchanges::system::LocalTime;
use scol_play::fake::{FakeHost, GameBehaviour, SharedSystem};
use scol_play::{Mode, PlayArgs};
use std::path::{Path, PathBuf};
use std::sync::{Mutex, MutexGuard};

/// Tests set process environment variables that the self checks read, so they run one at a time.
static LOCK: Mutex<()> = Mutex::new(());

pub const OURS_DLL: &[u8] = b"MZ\x90\x00 padding sc-offline v0.7.8.3\x00 more bytes";
pub const OLD_DLL: &[u8] = b"MZ another mod input hook, nothing to do with this launcher";
pub const HOSTS: &str = r"C:\Windows\System32\drivers\etc\hosts";
pub const EAC_HOST: &str = "modules-cdn.eac-prod.on.epicgames.com";

pub struct World {
    _guard: MutexGuard<'static, ()>,
    pub root: PathBuf,
    pub here: String,
    pub bin: String,
    pub channel: String,
    pub pd: String,
    pub pf86: String,
    pub eac: String,
    pub eac_bak: String,
}

fn s(p: &Path) -> String {
    p.to_string_lossy().into_owned()
}

impl World {
    /// A fresh world. `ini` is the content of sc-offline.ini; a game folder is given in it.
    pub fn new(tag: &str, ini: &str) -> World {
        let guard = LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let root = PathBuf::from(format!(r"D:\t\play-{tag}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&root);
        let here = root.join("here");
        let channel = root.join("lib").join("StarCitizen").join("LIVE");
        let bin = channel.join("Bin64");
        let pd = root.join("pd");
        let pf86 = root.join("pf86");
        for d in [&here, &bin, &pd, &pf86, &here.join("data")] {
            std::fs::create_dir_all(d).unwrap();
        }
        std::fs::write(here.join("dinput8.dll"), OURS_DLL).unwrap();
        std::fs::copy(
            env!("CARGO_BIN_EXE_scol-play-standin"),
            bin.join("StarCitizen.exe"),
        )
        .unwrap();
        let game_line = format!("game = {}\n", s(&root.join("lib").join("StarCitizen")));
        std::fs::write(here.join("sc-offline.ini"), format!("{game_line}{ini}")).unwrap();
        // The self checks read these through the process environment; point them into the world.
        std::env::set_var("ProgramData", &pd);
        std::env::set_var("ProgramFiles(x86)", &pf86);
        std::env::set_var("APPDATA", root.join("appdata"));
        std::env::set_var("SC_OFFLINE_GUI", "1"); // no "Press Enter" wait without a console of its own
        let eac = format!("{}\\EasyAntiCheat_EOS\\EasyAntiCheat_EOS.exe", s(&pf86));
        World {
            _guard: guard,
            here: s(&here).replace('/', "\\"),
            bin: s(&bin).replace('/', "\\"),
            channel: s(&channel).replace('/', "\\"),
            pd: s(&pd).replace('/', "\\"),
            pf86: s(&pf86).replace('/', "\\"),
            eac_bak: format!("{eac}.bak"),
            eac,
            root,
        }
    }

    /// Easy Anti-Cheat installed: the real stand-in file (the self check reads it) and the fake's copy.
    pub fn with_eac(&self, sys: &mut FakeSystem) {
        let dir = Path::new(&self.eac).parent().unwrap().to_path_buf();
        std::fs::create_dir_all(dir).unwrap();
        std::fs::write(&self.eac, b"x").unwrap();
        sys.put(&self.eac, "x");
    }

    /// The fake system of this world: the same ProgramData and Program Files (x86), a plain hosts file,
    /// the game and its channel folder writable (so the helper runs unelevated when no PC change is due).
    pub fn system(&self) -> FakeSystem {
        let mut sys = FakeSystem::new();
        sys.env.insert("ProgramData".into(), self.pd.clone());
        sys.env
            .insert("ProgramFiles(x86)".into(), self.pf86.clone());
        sys.put(HOSTS, "127.0.0.1 localhost\r\n");
        sys.writable.insert(self.bin.clone());
        sys.writable.insert(self.channel.clone());
        sys.time = LocalTime {
            year: 2026,
            month: 11,
            day: 17,
            hour: 9,
            minute: 5,
        };
        sys
    }

    pub fn host(&self, sys: FakeSystem) -> FakeHost {
        let mut h = FakeHost::new(&self.here, SharedSystem::new(sys));
        h.game = GameBehaviour::Spawn { ms: 400, exit: 0 };
        h
    }

    pub fn record_path(&self) -> String {
        format!("{}\\sc-offline\\pc-changes.txt", self.pd)
    }

    pub fn marker(&self) -> String {
        format!("{}\\sc-offline.installed", self.bin)
    }

    pub fn dll(&self) -> String {
        format!("{}\\dinput8.dll", self.bin)
    }

    pub fn log(&self) -> String {
        std::fs::read_to_string(format!("{}\\data\\launcher.log", self.here)).unwrap_or_default()
    }

    pub fn exists(&self, rel: &str) -> bool {
        Path::new(rel).exists()
    }
}

impl Drop for World {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.root);
    }
}

pub fn args(mode: Mode, dry: bool, line: &[&str]) -> PlayArgs {
    PlayArgs {
        mode,
        game_arg: String::new(),
        dry,
        skip_eac: false,
        command_line: line.iter().map(|x| x.to_string()).collect(),
    }
}

pub fn run(w: &World, host: &mut FakeHost, a: &PlayArgs) -> (i32, String) {
    let mut out = Vec::new();
    let code = scol_play::run_play(a, host, &w.here, &mut out);
    (code, String::from_utf8_lossy(&out).replace("\r\n", "\n"))
}

/// Every file under `root` with its size, to show that a dry run changed nothing.
pub fn tree(root: &Path) -> std::collections::BTreeMap<String, u64> {
    fn walk(dir: &Path, root: &Path, out: &mut std::collections::BTreeMap<String, u64>) {
        for e in std::fs::read_dir(dir).unwrap().flatten() {
            let p = e.path();
            let rel = s(p.strip_prefix(root).unwrap());
            if p.is_dir() {
                out.insert(rel, 0);
                walk(&p, root, out);
            } else {
                out.insert(rel, std::fs::metadata(&p).unwrap().len());
            }
        }
    }
    let mut out = std::collections::BTreeMap::new();
    walk(root, root, &mut out);
    out
}
