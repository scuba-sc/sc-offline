// SPDX-License-Identifier: GPL-3.0-only
//! Colours and small widgets. The colours are the `kUi*` constants of `launcher/launcher.cpp`, as in
//! the P4 spike: neutral greys, green only as a status accent.

use eframe::egui::{self, Color32, RichText};

pub const BG: Color32 = Color32::from_rgb(21, 21, 23);
pub const SIDE: Color32 = Color32::from_rgb(17, 17, 19);
pub const CARD: Color32 = Color32::from_rgb(27, 27, 30);
pub const ROW: Color32 = Color32::from_rgb(36, 36, 40);
pub const LINE: Color32 = Color32::from_rgb(40, 40, 44);
pub const TEXT: Color32 = Color32::from_rgb(236, 236, 240);
pub const DIM: Color32 = Color32::from_rgb(118, 118, 126);
pub const GREEN: Color32 = Color32::from_rgb(46, 196, 118);
pub const WARN: Color32 = Color32::from_rgb(232, 168, 56);
pub const BAD: Color32 = Color32::from_rgb(224, 84, 84);

pub fn apply(ctx: &egui::Context) {
    let mut v = egui::Visuals::dark();
    v.panel_fill = BG;
    v.window_fill = CARD;
    v.extreme_bg_color = SIDE;
    v.override_text_color = Some(TEXT);
    v.widgets.noninteractive.bg_stroke.color = LINE;
    v.widgets.inactive.bg_fill = ROW;
    v.widgets.inactive.weak_bg_fill = ROW;
    v.selection.bg_fill = GREEN.gamma_multiply(0.55);
    ctx.set_visuals(v);
}

pub fn dim(text: impl Into<String>) -> RichText {
    RichText::new(text.into()).color(DIM)
}

pub fn warn(text: impl Into<String>) -> RichText {
    RichText::new(text.into()).color(WARN)
}

pub fn good(text: impl Into<String>) -> RichText {
    RichText::new(text.into()).color(GREEN)
}

pub fn bad(text: impl Into<String>) -> RichText {
    RichText::new(text.into()).color(BAD)
}

/// A titled card, like the C++ window's `UiCard`.
pub fn card<R>(ui: &mut egui::Ui, title: &str, body: impl FnOnce(&mut egui::Ui) -> R) -> R {
    egui::Frame::new()
        .fill(CARD)
        .corner_radius(6.0)
        .inner_margin(12.0)
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            if !title.is_empty() {
                ui.label(RichText::new(title).strong());
                ui.add_space(6.0);
            }
            body(ui)
        })
        .inner
}

/// A big button; returns true when clicked.
pub fn button(ui: &mut egui::Ui, text: &str, enabled: bool) -> bool {
    ui.add_enabled(
        enabled,
        egui::Button::new(text).min_size(egui::vec2(140.0, 32.0)),
    )
    .clicked()
}
