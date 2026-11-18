// SPDX-License-Identifier: GPL-3.0-only
//! P4 UI spike: two pages (Play, Status) in a dark green theme. Static fake data, no logic.
//! Build one renderer per binary: `--features glow` or `--features wgpu`.
#![windows_subsystem = "windows"]

use eframe::egui::{self, Color32, FontData, FontDefinitions, FontFamily, RichText};

#[cfg(all(feature = "glow", feature = "wgpu"))]
compile_error!("build one renderer at a time");
#[cfg(not(any(feature = "glow", feature = "wgpu")))]
compile_error!("enable feature glow or wgpu");

// Colours from the C++ launcher theme (launcher/launcher.cpp, kUi* constants).
const BG: Color32 = Color32::from_rgb(21, 21, 23);
const SIDE: Color32 = Color32::from_rgb(17, 17, 19);
const CARD: Color32 = Color32::from_rgb(27, 27, 30);
const ROW: Color32 = Color32::from_rgb(36, 36, 40);
const LINE: Color32 = Color32::from_rgb(40, 40, 44);
const TEXT: Color32 = Color32::from_rgb(236, 236, 240);
const TEXT_DIM: Color32 = Color32::from_rgb(118, 118, 126);
const GREEN: Color32 = Color32::from_rgb(46, 196, 118);

#[derive(PartialEq, Clone, Copy)]
enum Page {
    Play,
    Status,
}

struct Launcher {
    page: Page,
}

/// Spike only: load a system UI font at run time (nothing is embedded or redistributed).
/// egui's built-in fonts are disabled, so a real font must be chosen before shipping.
fn install_font(ctx: &egui::Context) {
    let mut fonts = FontDefinitions::empty();
    let windir = std::env::var("SystemRoot").unwrap_or_else(|_| "C:/Windows".into());
    let path = format!("{windir}/Fonts/segoeui.ttf");
    match std::fs::read(&path) {
        Ok(bytes) => {
            fonts.font_data.insert("ui".into(), std::sync::Arc::new(FontData::from_owned(bytes)));
            fonts.families.entry(FontFamily::Proportional).or_default().push("ui".into());
            fonts.families.entry(FontFamily::Monospace).or_default().push("ui".into());
            ctx.set_fonts(fonts);
        }
        Err(e) => {
            eprintln!("no font at {path}: {e}");
            std::process::exit(2);
        }
    }
}

/// Print the graphics adapter to stderr (spike measurement aid).
fn report_adapter(cc: &eframe::CreationContext<'_>) {
    #[cfg(feature = "wgpu")]
    if let Some(rs) = &cc.wgpu_render_state {
        let i = rs.adapter.get_info();
        eprintln!("adapter: {} / {:?} / {:?}", i.name, i.backend, i.device_type);
    }
    #[cfg(feature = "glow")]
    if let Some(gl) = &cc.gl {
        use eframe::glow::HasContext;
        // SAFETY: plain string queries on the current context.
        unsafe {
            eprintln!(
                "gl: {} / {}",
                gl.get_parameter_string(eframe::glow::RENDERER),
                gl.get_parameter_string(eframe::glow::VERSION)
            );
        }
    }
}

fn theme(ctx: &egui::Context) {
    let mut v = egui::Visuals::dark();
    v.panel_fill = BG;
    v.window_fill = CARD;
    v.extreme_bg_color = SIDE;
    v.override_text_color = Some(TEXT);
    v.widgets.noninteractive.bg_stroke.color = LINE;
    v.widgets.inactive.bg_fill = ROW;
    v.widgets.inactive.weak_bg_fill = ROW;
    v.selection.bg_fill = GREEN;
    ctx.set_visuals(v);
}

impl eframe::App for Launcher {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        egui::Panel::left("nav")
            .exact_size(96.0)
            .frame(egui::Frame::new().fill(SIDE).inner_margin(12.0))
            .show(ui, |ui| {
                ui.selectable_value(&mut self.page, Page::Play, "Play");
                ui.selectable_value(&mut self.page, Page::Status, "Status");
            });
        egui::CentralPanel::default()
            .frame(egui::Frame::new().fill(BG).inner_margin(18.0))
            .show(ui, |ui| match self.page {
                Page::Play => {
                    ui.heading("Play");
                    ui.add_space(8.0);
                    ui.label(RichText::new("Spike only. The button does nothing.").color(TEXT_DIM));
                    ui.add_space(12.0);
                    ui.add_enabled(false, egui::Button::new("Play").min_size(egui::vec2(160.0, 36.0)));
                }
                Page::Status => {
                    ui.heading("Status");
                    ui.add_space(8.0);
                    egui::Frame::new().fill(CARD).corner_radius(6.0).inner_margin(12.0).show(ui, |ui| {
                        for (k, v, ok) in [
                            ("Game folder", "found", true),
                            ("SDK", "fake 0.0.0", true),
                            ("Plugins", "3 enabled", true),
                            ("Update", "not checked", false),
                        ] {
                            ui.horizontal(|ui| {
                                ui.label(RichText::new(k).color(TEXT_DIM));
                                ui.label(RichText::new(v).color(if ok { GREEN } else { TEXT }));
                            });
                        }
                        ui.label(RichText::new("Статус (static sample, not live)").color(TEXT_DIM));
                    });
                }
            });
    }
}

fn main() -> eframe::Result {
    #[allow(unused_mut)]
    let mut options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_title("sc-offline launcher spike")
            .with_inner_size([720.0, 376.0])
            .with_resizable(false),
        ..Default::default()
    };
    // Test aid: SCOL_SPIKE_CPU_ADAPTER=1 picks a software adapter (for example WARP on dx12).
    #[cfg(feature = "wgpu")]
    if std::env::var_os("SCOL_SPIKE_CPU_ADAPTER").is_some() {
        use eframe::egui_wgpu::WgpuSetup;
        use std::sync::Arc;
        if let WgpuSetup::CreateNew(c) = &mut options.wgpu_options.wgpu_setup {
            c.native_adapter_selector = Some(Arc::new(|adapters, _surface| {
                adapters
                    .iter()
                    .find(|a| a.get_info().device_type == eframe::wgpu::DeviceType::Cpu)
                    .cloned()
                    .ok_or_else(|| "no software adapter".to_string())
            }));
        }
    }
    eframe::run_native(
        "scol-spike",
        options,
        Box::new(|cc| {
            report_adapter(cc);
            install_font(&cc.egui_ctx);
            theme(&cc.egui_ctx);
            Ok(Box::new(Launcher { page: Page::Play }))
        }),
    )
}
