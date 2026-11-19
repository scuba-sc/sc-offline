// SPDX-License-Identifier: GPL-3.0-only
//! `--ui-screenshots <folder>`: opens the real window on a fixture install and saves one PNG per
//! page. It needs a display and an OpenGL driver, so it is a manual aid and not part of the self
//! test. PNGs are written by hand (stored zlib through flate2), so no image crate is pulled in.

use std::io::Write;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use eframe::egui::{self, ColorImage, ViewportCommand};

use crate::app::{App, Job, Page, PluginTab, PAGES};
use crate::fixture::{self, FixtureBackend};
use crate::{fonts, pages, theme, WINDOW};

fn crc32(data: &[u8]) -> u32 {
    let mut c = 0xFFFF_FFFFu32;
    for &b in data {
        c ^= u32::from(b);
        for _ in 0..8 {
            c = if c & 1 != 0 {
                0xEDB8_8320 ^ (c >> 1)
            } else {
                c >> 1
            };
        }
    }
    !c
}

fn chunk(out: &mut Vec<u8>, kind: &[u8; 4], data: &[u8]) {
    out.extend_from_slice(&(data.len() as u32).to_be_bytes());
    let mut body = kind.to_vec();
    body.extend_from_slice(data);
    out.extend_from_slice(&body);
    out.extend_from_slice(&crc32(&body).to_be_bytes());
}

/// An 8-bit RGBA PNG of `img`.
pub fn png_bytes(img: &ColorImage) -> Vec<u8> {
    let [w, h] = img.size;
    let mut raw = Vec::with_capacity((w * 4 + 1) * h);
    for y in 0..h {
        raw.push(0);
        for p in &img.pixels[y * w..(y + 1) * w] {
            raw.extend_from_slice(&[p.r(), p.g(), p.b(), p.a()]);
        }
    }
    let mut z = flate2::write::ZlibEncoder::new(Vec::new(), flate2::Compression::fast());
    z.write_all(&raw).expect("memory write");
    let packed = z.finish().expect("memory write");
    let mut out = b"\x89PNG\r\n\x1a\n".to_vec();
    let mut ihdr = Vec::new();
    ihdr.extend_from_slice(&(w as u32).to_be_bytes());
    ihdr.extend_from_slice(&(h as u32).to_be_bytes());
    ihdr.extend_from_slice(&[8, 6, 0, 0, 0]);
    chunk(&mut out, b"IHDR", &ihdr);
    chunk(&mut out, b"IDAT", &packed);
    chunk(&mut out, b"IEND", &[]);
    out
}

struct Shots {
    app: App,
    dir: PathBuf,
    /// Index into the shot list; list = pages, plus the Catalogue tab.
    step: usize,
    frames: u32,
    asked: bool,
    done: Arc<AtomicBool>,
}

/// (file name, page, plugin tab, plugin whose settings are open, capability groups open (1 auto, 2 not yet), scroll)
type Shot = (&'static str, Page, PluginTab, Option<&'static str>, u8, f32);

const SHOTS: [Shot; 14] = [
    ("home", Page::Home, PluginTab::Installed, None, 0, 0.0),
    ("output", Page::Output, PluginTab::Installed, None, 0, 0.0),
    ("plugins", Page::Plugins, PluginTab::Installed, None, 0, 0.0),
    (
        "plugin-settings",
        Page::Plugins,
        PluginTab::Installed,
        Some("beta"),
        0,
        0.0,
    ),
    (
        "catalogue",
        Page::Plugins,
        PluginTab::Catalogue,
        None,
        0,
        0.0,
    ),
    (
        "capabilities",
        Page::Capabilities,
        PluginTab::Installed,
        None,
        0,
        0.0,
    ),
    (
        "capabilities-end",
        Page::Capabilities,
        PluginTab::Installed,
        None,
        0,
        420.0,
    ),
    (
        "capabilities-auto",
        Page::Capabilities,
        PluginTab::Installed,
        None,
        1,
        760.0,
    ),
    (
        "capabilities-not-yet",
        Page::Capabilities,
        PluginTab::Installed,
        None,
        2,
        760.0,
    ),
    (
        "capabilities-not-yet-end",
        Page::Capabilities,
        PluginTab::Installed,
        None,
        2,
        1900.0,
    ),
    (
        "settings",
        Page::Settings,
        PluginTab::Installed,
        None,
        0,
        0.0,
    ),
    (
        "settings-end",
        Page::Settings,
        PluginTab::Installed,
        None,
        0,
        520.0,
    ),
    (
        "capabilities-ru",
        Page::Capabilities,
        PluginTab::Installed,
        None,
        0,
        0.0,
    ),
    (
        "plugins-ru",
        Page::Plugins,
        PluginTab::Installed,
        None,
        0,
        0.0,
    ),
];

impl eframe::App for Shots {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let ctx = ui.ctx().clone();
        if self.step >= SHOTS.len() {
            ctx.send_viewport_cmd(ViewportCommand::Close);
            return;
        }
        let (name, page, tab, open, more, scroll) = SHOTS[self.step];
        self.app.scroll_to = Some(scroll);
        self.app.page = page;
        self.app.plugin_tab = tab;
        self.app.open_plugin = open.map(str::to_string);
        self.app.caps_auto = more & 1 != 0;
        self.app.caps_more = more & 2 != 0;
        if name.ends_with("-ru") && self.app.lang != crate::i18n::Lang::Ru {
            self.app.set_language(scol_config::Language::Russian);
            self.app.note = None;
        }
        pages::draw(&mut self.app, ui);
        self.frames += 1;
        if self.done.swap(false, Ordering::SeqCst) {
            self.step += 1;
            self.frames = 0;
            self.asked = false;
        } else if self.frames > 12 && self.app.busy == 0 && !self.asked {
            self.asked = true;
            let path = self.dir.join(format!("{name}.png"));
            let done = self.done.clone();
            let c2 = ctx.clone();
            ctx.send_viewport_cmd(ViewportCommand::Screenshot(egui::ScreenshotCallback::new(
                move |img| {
                    if let Err(e) = std::fs::write(&path, png_bytes(&img)) {
                        eprintln!("cannot write {}: {e}", path.display());
                    } else {
                        println!("saved {}", path.display());
                    }
                    done.store(true, Ordering::SeqCst);
                    c2.request_repaint();
                },
            )));
        }
        ctx.request_repaint();
    }
}

/// The window size of the shots: `WINDOW`, or `SCOL_UI_SHOT_SIZE=640x420` to check the smallest size.
fn shot_size() -> [f32; 2] {
    std::env::var("SCOL_UI_SHOT_SIZE")
        .ok()
        .and_then(|v| {
            let (w, h) = v.split_once('x')?;
            Some([w.parse().ok()?, h.parse().ok()?])
        })
        .unwrap_or(WINDOW)
}

/// Open the window on a fixture install and save a PNG per page into `dir`.
pub fn run(_here: PathBuf, dir: PathBuf) -> i32 {
    let root = std::env::temp_dir().join(format!("scol-ui-shots-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&root);
    if let Err(e) = fixture::build(&root)
        .and_then(|()| std::fs::create_dir_all(&dir).map_err(|e| e.to_string()))
    {
        eprintln!("screenshots: {e}");
        return 1;
    }
    let app = App::new(root.clone(), Arc::new(FixtureBackend));
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_title("sc-offline screenshots")
            .with_inner_size(shot_size()),
        ..Default::default()
    };
    let mut holder = Some(app);
    let r = eframe::run_native(
        "sc-offline-shots",
        options,
        Box::new(move |cc| {
            fonts::install(&cc.egui_ctx)?;
            theme::apply(&cc.egui_ctx);
            // No fade between shots: every PNG shows the settled state.
            cc.egui_ctx.global_style_mut(|s| s.animation_time = 0.0);
            let mut app = holder.take().expect("once");
            app.attach(&cc.egui_ctx);
            app.submit(Job::Status);
            app.submit(Job::CatalogueLoad);
            app.output.push("> sc-offline.exe status".into());
            app.output
                .push("(synthetic fixture, not run in game)".into());
            let _ = PAGES;
            Ok(Box::new(Shots {
                app,
                dir,
                step: 0,
                frames: 0,
                asked: false,
                done: Arc::new(AtomicBool::new(false)),
            }))
        }),
    );
    let _ = std::fs::remove_dir_all(&root);
    match r {
        Ok(()) => 0,
        Err(e) => {
            eprintln!("screenshots: the window failed: {e}");
            1
        }
    }
}
