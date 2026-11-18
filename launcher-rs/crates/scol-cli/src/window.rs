// SPDX-License-Identifier: GPL-3.0-only
//! The window's backend: Play, Update and Repair run this same exe as a child with `SC_OFFLINE_GUI` set
//! (the C++ launcher does the same, so the window and the command line share one code path) and stream
//! its output into the Output page. Stop runs the stop channel of `scol_play` in this process.
//! Catalogue downloads use the WinHTTP fetcher of `scol_update`. Not run in game.

use scol_catalogue::fetch::Fetcher;
use scol_install::status::collect_status;
use scol_play::real::RealStop;
use scol_play::stop::{stop_game, STOP_ACK_WAIT_MS};
use scol_ui::{Backend, Command};
use std::io::{BufRead, BufReader};
use std::os::windows::process::CommandExt;
use std::path::Path;
use std::process::Stdio;
use std::sync::mpsc;
use std::sync::Mutex;
use std::time::{Duration, Instant};

const CREATE_NO_WINDOW: u32 = 0x0800_0000;

/// The window's connection to Play, Stop, Update and Repair.
pub struct WindowBackend {
    exe: std::path::PathBuf,
    /// (bin folder, when the cached running flag was taken, the flag)
    cache: Mutex<(Option<String>, Option<Instant>, bool)>,
}

impl WindowBackend {
    pub fn new() -> Self {
        let exe = std::env::current_exe().unwrap_or_else(|_| "sc-offline.exe".into());
        WindowBackend {
            exe,
            cache: Mutex::new((None, None, false)),
        }
    }

    fn bin(&self, here: &Path) -> String {
        let mut c = self.cache.lock().unwrap_or_else(|e| e.into_inner());
        if c.0.is_none() {
            c.0 = Some(collect_status(&here.to_string_lossy()).bin);
        }
        c.0.clone().unwrap_or_default()
    }
}

impl Default for WindowBackend {
    fn default() -> Self {
        Self::new()
    }
}

fn pump(mut child: std::process::Child, out: &mut dyn FnMut(&str)) -> Result<i32, String> {
    let (tx, rx) = mpsc::channel::<String>();
    let mut readers = Vec::new();
    if let Some(o) = child.stdout.take() {
        let tx = tx.clone();
        readers.push(std::thread::spawn(move || {
            for l in BufReader::new(o).lines().map_while(Result::ok) {
                let _ = tx.send(l);
            }
        }));
    }
    if let Some(e) = child.stderr.take() {
        let tx = tx.clone();
        readers.push(std::thread::spawn(move || {
            for l in BufReader::new(e).lines().map_while(Result::ok) {
                let _ = tx.send(l);
            }
        }));
    }
    drop(tx);
    for l in rx {
        out(&l);
    }
    for r in readers {
        let _ = r.join();
    }
    let st = child.wait().map_err(|e| format!("wait failed: {e}"))?;
    Ok(st.code().unwrap_or(1))
}

impl Backend for WindowBackend {
    fn run(&self, command: Command, here: &Path, out: &mut dyn FnMut(&str)) -> Result<i32, String> {
        if command == Command::Stop {
            let bin = self.bin(here);
            let mut port = RealStop::default();
            let r = stop_game(&mut port, &bin, STOP_ACK_WAIT_MS, &mut |l| out(l));
            if r.no_folder {
                out("The game folder is not known; nothing was stopped.");
            } else if r.targets.is_empty() {
                out("No game process of this install was found.");
            } else {
                out(&format!(
                    "Asked {} game process(es) to close; {} window(s) told to close.",
                    r.targets.len(),
                    r.posted
                ));
            }
            return Ok(0);
        }
        let mut args: Vec<&str> = Vec::new();
        match command {
            Command::Play => args.push("play"),
            Command::Repair => args.push("uninstall"),
            Command::Update => args.extend(["update", "--yes"]),
            Command::Stop => unreachable!(),
        }
        let child = std::process::Command::new(&self.exe)
            .args(&args)
            .env(scol_contract::env::GUI, "1")
            .stdin(Stdio::null())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .creation_flags(CREATE_NO_WINDOW)
            .spawn()
            .map_err(|e| format!("could not start {}: {e}", self.exe.display()))?;
        pump(child, out)
    }

    fn game_running(&self) -> bool {
        // The window asks every frame; look at the process list once a second at most.
        let here = self
            .exe
            .parent()
            .map(|p| p.to_path_buf())
            .unwrap_or_default();
        let bin = self.bin(&here);
        if bin.is_empty() {
            return false;
        }
        let mut c = self.cache.lock().unwrap_or_else(|e| e.into_inner());
        if let Some(t) = c.1 {
            if t.elapsed() < Duration::from_secs(1) {
                return c.2;
            }
        }
        use scol_play::stop::StopPort;
        let running = !RealStop::default().find_game_procs(&bin).is_empty();
        c.1 = Some(Instant::now());
        c.2 = running;
        running
    }

    fn fetcher(&self) -> Option<Box<dyn Fetcher + Send>> {
        Some(Box::new(scol_update::http::catalogue_fetcher(
            scol_install::version::SCO_VERSION,
        )))
    }

    fn open(&self, path: &Path) {
        let _ = std::process::Command::new("explorer.exe")
            .arg(path)
            .creation_flags(CREATE_NO_WINDOW)
            .spawn();
    }
}

/// Opens the window. Frees the console first when this exe owns it (double-clicked).
pub fn open_window(here: &str, free_console: bool) -> i32 {
    #[link(name = "kernel32")]
    extern "system" {
        fn FreeConsole() -> i32;
    }
    if free_console {
        // SAFETY: plain Win32 call without arguments.
        unsafe {
            FreeConsole();
        }
    }
    scol_ui::run_window(here.into(), std::sync::Arc::new(WindowBackend::new()))
}

/// True when this process is the only one on its console (it was double-clicked).
pub fn owns_console() -> bool {
    #[link(name = "kernel32")]
    extern "system" {
        fn GetConsoleProcessList(list: *mut u32, count: u32) -> u32;
    }
    let mut ids = [0u32; 2];
    // SAFETY: the buffer holds `count` entries.
    let n = unsafe { GetConsoleProcessList(ids.as_mut_ptr(), 2) };
    n <= 1
}
