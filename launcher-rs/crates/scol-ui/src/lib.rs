// SPDX-License-Identifier: GPL-3.0-only
//! The sc-offline launcher window (egui, glow renderer). Pages match the C++ window: Home,
//! Output, Plugins (with a Catalogue tab), Capabilities and Settings.
//!
//! The window calls the library crates directly on a worker thread (`scol-install` status,
//! `scol-plugins` verify, `scol-catalogue`, `scol-config`). What it cannot do yet (Play, Stop,
//! Update, Repair, catalogue downloads) goes through the [`Backend`] trait; the integrator connects
//! it, and until then the window says so. Nothing here was run in game.
//!
//! Entry points: [`run`] (CLI-style, returns an exit code) and [`run_window`] (a backend of your own).

pub mod app;
pub mod backend;
pub mod catalogue;
pub mod extract;
pub mod fixture;
pub mod fonts;
pub mod i18n;
pub mod locale_table;
pub mod pages;
pub mod plugins;
pub mod screenshot;
pub mod selftest;
pub mod theme;

use std::path::PathBuf;
use std::sync::Arc;

pub use backend::{Backend, Command, Unconnected};

pub const USAGE: &str = "usage: scol-ui [--here <install folder>] [--ui-self-test] [--ui-screenshots <folder>]\n\
\n\
\x20 (no option)           open the launcher window\n\
\x20 --here <folder>       the install folder (sc-offline.ini and data\\ live there); default: next to this exe\n\
\x20 --ui-self-test        build every page for a few frames on a fixture install, run the page actions, exit 0 or 1\n\
\x20 --ui-screenshots <d>  open the window and save one PNG per page into <d> (needs a display)\n";

struct Window {
    app: app::App,
    first: bool,
}

impl eframe::App for Window {
    fn ui(&mut self, ui: &mut eframe::egui::Ui, _frame: &mut eframe::Frame) {
        if self.first {
            self.first = false;
            self.app.attach(ui.ctx());
            self.app.submit(app::Job::Status);
        }
        pages::draw(&mut self.app, ui);
    }
}

/// Open the window and block until it closes. 0 on a clean close, 1 when the window or font failed.
pub fn run_window(here: PathBuf, backend: Arc<dyn Backend>) -> i32 {
    let a = app::App::new(here, backend);
    let title = a.tf(
        "launcher.window.title",
        &[("version", scol_install::version::SCO_VERSION)],
    );
    let options = eframe::NativeOptions {
        viewport: eframe::egui::ViewportBuilder::default()
            .with_title(title)
            .with_inner_size([820.0, 520.0])
            .with_min_inner_size([640.0, 400.0]),
        ..Default::default()
    };
    let mut holder = Some(a);
    let r = eframe::run_native(
        "sc-offline",
        options,
        Box::new(move |cc| {
            fonts::install(&cc.egui_ctx)?;
            theme::apply(&cc.egui_ctx);
            Ok(Box::new(Window {
                app: holder.take().expect("created once"),
                first: true,
            }))
        }),
    );
    match r {
        Ok(()) => 0,
        Err(e) => {
            eprintln!("sc-offline: the window failed: {e}");
            1
        }
    }
}

fn default_here() -> PathBuf {
    std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|d| d.to_path_buf()))
        .unwrap_or_else(|| PathBuf::from("."))
}

/// The command entry: `args` without the program name.
pub fn run(args: &[String]) -> i32 {
    let mut here: Option<PathBuf> = None;
    let mut self_test = false;
    let mut shots: Option<PathBuf> = None;
    let mut it = args.iter();
    while let Some(a) = it.next() {
        match a.as_str() {
            "--here" => match it.next() {
                Some(v) => here = Some(PathBuf::from(v)),
                None => {
                    eprint!("{USAGE}");
                    return 1;
                }
            },
            "--ui-self-test" => self_test = true,
            "--ui-screenshots" => match it.next() {
                Some(v) => shots = Some(PathBuf::from(v)),
                None => {
                    eprint!("{USAGE}");
                    return 1;
                }
            },
            "-h" | "--help" | "help" => {
                print!("{USAGE}");
                return 0;
            }
            other => {
                eprintln!("scol-ui: unknown option {other:?}");
                eprint!("{USAGE}");
                return 1;
            }
        }
    }
    if self_test {
        return selftest::run(here);
    }
    let here = here.unwrap_or_else(default_here);
    if let Some(dir) = shots {
        return screenshot::run(here, dir);
    }
    run_window(here, Arc::new(Unconnected))
}
