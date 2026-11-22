// SPDX-License-Identifier: GPL-3.0-only
//! The real [`Backend`]: it runs the commands of the window on this PC by calling the library crates
//! in this process, on the worker thread of the window.
//!
//! - Play and Repair: `scol_play::real::run_command_with` (`run_play` with the real host), with the
//!   same update notice before Play as the command line.
//! - Stop: `scol_play::stop::stop_game` with the real stop port (the mod's stop event first, then
//!   WM_CLOSE to the game's windows).
//! - Update: `scol_update::cli::run_with` with WinHTTP and the real machine; the window's button is the
//!   consent, so the question is answered yes.
//! - Open folder or file: `ShellExecuteW` with the `open` verb.
//!
//! The self test and the tests never construct this type: they would start processes and ask for
//! administrator rights. Nothing here was run in game.

#![cfg(all(windows, feature = "real"))]

use std::io::Write;
use std::os::windows::ffi::OsStrExt;
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::time::{Duration, Instant};

use scol_catalogue::trust::TrustRoot;
use scol_install::status::collect_status;
use scol_install::version::SCO_VERSION;
use scol_play::flow::{Mode, PlayArgs};
use scol_play::real::{run_command_with, RealStop, UpdateHook};
use scol_play::stop::{stop_game, StopPort, STOP_ACK_WAIT_MS};
use windows_sys::Win32::UI::Shell::ShellExecuteW;
use windows_sys::Win32::UI::WindowsAndMessaging::SW_SHOWNORMAL;

use crate::backend::{Backend, Command};

/// Turns bytes written by a library command into lines for the Output page.
struct Lines<'a> {
    out: &'a mut dyn FnMut(&str),
    pending: Vec<u8>,
}

impl<'a> Lines<'a> {
    fn new(out: &'a mut dyn FnMut(&str)) -> Self {
        Lines {
            out,
            pending: Vec::new(),
        }
    }

    fn emit(&mut self) {
        let text = String::from_utf8_lossy(&self.pending).into_owned();
        self.pending.clear();
        (self.out)(text.trim_end_matches('\r'));
    }

    fn finish(&mut self) {
        if !self.pending.is_empty() {
            self.emit();
        }
    }
}

impl Write for Lines<'_> {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        for &b in buf {
            if b == b'\n' {
                self.emit();
            } else {
                self.pending.push(b);
            }
        }
        Ok(buf.len())
    }

    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

fn wide(s: &std::ffi::OsStr) -> Vec<u16> {
    s.encode_wide().chain(std::iter::once(0)).collect()
}

/// The backend of the real PC.
pub struct RealBackend {
    here: PathBuf,
    /// (the game's Bin64 folder once known, when the running flag was taken, the flag)
    cache: Mutex<(Option<String>, Option<Instant>, bool)>,
}

impl RealBackend {
    /// `here` is the install folder (the one with `sc-offline.ini` and `data\`).
    pub fn new(here: PathBuf) -> Self {
        // The commands run in this process, not in a console: they must not wait for Enter.
        std::env::set_var(scol_contract::env::GUI, "1");
        RealBackend {
            here,
            cache: Mutex::new((None, None, false)),
        }
    }

    fn bin(&self, here: &Path) -> String {
        let mut c = self.cache.lock().unwrap_or_else(|e| e.into_inner());
        if c.0.as_deref().is_none_or(str::is_empty) {
            c.0 = Some(collect_status(&here.to_string_lossy()).bin);
        }
        c.0.clone().unwrap_or_default()
    }

    fn stop(&self, here: &Path, out: &mut dyn FnMut(&str)) -> Result<i32, String> {
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
        Ok(0)
    }

    fn play_or_repair(
        &self,
        mode: Mode,
        word: &str,
        here: &Path,
        out: &mut dyn FnMut(&str),
    ) -> Result<i32, String> {
        let args = PlayArgs {
            mode,
            game_arg: String::new(),
            dry: false,
            skip_eac: false,
            command_line: vec!["sc-offline.exe".into(), word.into()],
        };
        // The notice before Play, as on the command line: a short request, only a line of text.
        let hook: UpdateHook = Box::new(|cfg, out| {
            let mut net = scol_update::http::WinHttp::new(SCO_VERSION, 3_000);
            if let Some(u) = scol_update::flow::check_for_play(
                &mut net,
                SCO_VERSION,
                cfg.prereleases,
                cfg.check_updates,
            ) {
                let _ = writeln!(
                    out,
                    "Update:   sc-offline {} is available (you have {SCO_VERSION}); press Update to install it",
                    u.tag
                );
            }
            // Only a notice here: the Update button installs, so Play goes on.
            None
        });
        let mut lines = Lines::new(out);
        let code = run_command_with(
            &args,
            &here.to_string_lossy(),
            &mut lines,
            if mode == Mode::Play { Some(hook) } else { None },
        );
        lines.finish();
        // The folder may have been found or changed by the command.
        self.cache.lock().unwrap_or_else(|e| e.into_inner()).0 = None;
        Ok(code)
    }

    fn update(&self, here: &Path, out: &mut dyn FnMut(&str)) -> Result<i32, String> {
        let cfg = scol_config::read_config(&here.join(scol_contract::files::INI))
            .map(|p| p.config)
            .unwrap_or_default();
        let ctx = scol_update::Context {
            here: here.to_path_buf(),
            version: SCO_VERSION.to_string(),
            prerelease: cfg.prereleases,
            check_updates: cfg.check_updates,
        };
        let mut net = scol_update::http::WinHttp::new(SCO_VERSION, 15_000);
        let mut sys = scol_update::platform::System;
        let mut lines = Lines::new(out);
        let args = vec!["update".to_string(), "--yes".to_string()];
        // Pressing Update is the consent; the command line would ask here.
        let code =
            scol_update::run_with(&args, &ctx, &mut net, &mut sys, &mut lines, &mut |_| true);
        lines.finish();
        Ok(code)
    }
}

impl Backend for RealBackend {
    fn run(&self, command: Command, here: &Path, out: &mut dyn FnMut(&str)) -> Result<i32, String> {
        match command {
            Command::Stop => self.stop(here, out),
            Command::Play => self.play_or_repair(Mode::Play, "play", here, out),
            Command::Repair => self.play_or_repair(Mode::Uninstall, "uninstall", here, out),
            Command::Update => self.update(here, out),
            Command::Report => {
                let mut lines = Lines::new(out);
                let code = scol_play::report::make_report(
                    &mut scol_play::real::RealHost::new(),
                    &mut lines,
                    &here.to_string_lossy(),
                    &scol_play::report::ReportArgs {
                        game: String::new(),
                        game_log: true,
                    },
                );
                lines.finish();
                Ok(code)
            }
        }
    }

    fn game_running(&self) -> bool {
        // The window asks every frame; look at the process list once a second at most.
        let bin = self.bin(&self.here);
        if bin.is_empty() {
            return false;
        }
        let mut c = self.cache.lock().unwrap_or_else(|e| e.into_inner());
        if let Some(t) = c.1 {
            if t.elapsed() < Duration::from_secs(1) {
                return c.2;
            }
        }
        let running = !RealStop::default().find_game_procs(&bin).is_empty();
        c.1 = Some(Instant::now());
        c.2 = running;
        running
    }

    fn game_probe(&self) -> fn() -> bool {
        scol_plugin_manager::store::game_running
    }

    fn trust(&self) -> TrustRoot {
        TrustRoot::embedded()
    }

    fn pick_folder(&self, _title: &str) -> Result<Option<PathBuf>, String> {
        Ok(scol_play::pick::pick_folder(0).map(PathBuf::from))
    }

    fn save_plugin_setting(
        &self,
        here: &Path,
        plugin: &str,
        name: &str,
        value: &str,
    ) -> Result<(), String> {
        crate::backend::save_one_setting(here, plugin, name, value, self.game_running())
    }

    fn open(&self, path: &Path) {
        let (verb, file) = (wide("open".as_ref()), wide(path.as_os_str()));
        // SAFETY: NUL-terminated strings that outlive the call; no window handle, no parameters.
        unsafe {
            ShellExecuteW(
                std::ptr::null_mut(),
                verb.as_ptr(),
                file.as_ptr(),
                std::ptr::null(),
                std::ptr::null(),
                SW_SHOWNORMAL,
            )
        };
    }
}
