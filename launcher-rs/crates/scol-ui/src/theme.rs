// SPDX-License-Identifier: GPL-3.0-only
//! Colours and small widgets. The colours are the `kUi*` constants of `launcher/launcher.cpp`: dark
//! grey panels, light text, a light primary button with dark text, and green for good status.

use eframe::egui::{self, Color32, CornerRadius, RichText, Stroke, TextStyle};

pub const BG: Color32 = Color32::from_rgb(21, 21, 23);
pub const SIDE: Color32 = Color32::from_rgb(17, 17, 19);
pub const CARD: Color32 = Color32::from_rgb(27, 27, 30);
pub const ROW: Color32 = Color32::from_rgb(36, 36, 40);
pub const ROW_HOVER: Color32 = Color32::from_rgb(46, 46, 51);
pub const LINE: Color32 = Color32::from_rgb(40, 40, 44);
pub const TEXT: Color32 = Color32::from_rgb(236, 236, 240);
pub const BODY: Color32 = Color32::from_rgb(190, 192, 198);
pub const DIM: Color32 = Color32::from_rgb(118, 118, 126);
pub const ACTION: Color32 = Color32::from_rgb(226, 226, 230);
pub const ACTION_HOVER: Color32 = Color32::from_rgb(255, 255, 255);
pub const ACTION_TEXT: Color32 = Color32::from_rgb(14, 14, 16);
pub const GREEN: Color32 = Color32::from_rgb(46, 196, 118);
/// `kUiWarn`: a soft coral.
pub const WARN: Color32 = Color32::from_rgb(255, 132, 104);
pub const BAD: Color32 = Color32::from_rgb(224, 84, 84);

pub fn apply(ctx: &egui::Context) {
    let mut v = egui::Visuals::dark();
    v.panel_fill = BG;
    v.window_fill = CARD;
    v.extreme_bg_color = SIDE;
    v.faint_bg_color = ROW;
    v.override_text_color = Some(TEXT);
    v.hyperlink_color = GREEN;
    let r = CornerRadius::same(8);
    v.widgets.noninteractive.bg_stroke = Stroke::new(1.0, LINE);
    v.widgets.noninteractive.corner_radius = r;
    for (w, fill) in [
        (&mut v.widgets.inactive, ROW),
        (&mut v.widgets.hovered, ROW_HOVER),
        (&mut v.widgets.active, ROW_HOVER),
        (&mut v.widgets.open, ROW_HOVER),
    ] {
        w.bg_fill = fill;
        w.weak_bg_fill = fill;
        w.bg_stroke = Stroke::NONE;
        w.corner_radius = r;
        w.fg_stroke = Stroke::new(1.0, TEXT);
    }
    v.selection.bg_fill = GREEN.gamma_multiply(0.45);
    // The keyboard focus ring.
    v.selection.stroke = Stroke::new(1.5, GREEN);
    ctx.set_visuals(v);
    ctx.global_style_mut(|s| {
        s.spacing.item_spacing = egui::vec2(8.0, 6.0);
        s.spacing.button_padding = egui::vec2(12.0, 6.0);
        s.spacing.interact_size.y = 28.0;
        s.text_styles
            .insert(TextStyle::Body, egui::FontId::proportional(14.0));
        s.text_styles
            .insert(TextStyle::Button, egui::FontId::proportional(14.0));
        s.text_styles
            .insert(TextStyle::Small, egui::FontId::proportional(12.0));
        s.text_styles
            .insert(TextStyle::Heading, egui::FontId::proportional(20.0));
        s.text_styles
            .insert(TextStyle::Monospace, egui::FontId::monospace(13.0));
    });
}

pub fn dim(text: impl Into<String>) -> RichText {
    RichText::new(text.into()).color(DIM)
}

/// Normal reading text, a little softer than the headings.
pub fn body(text: impl Into<String>) -> RichText {
    RichText::new(text.into()).color(BODY)
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
        .corner_radius(8.0)
        .inner_margin(12.0)
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            if !title.is_empty() {
                ui.label(RichText::new(title).strong());
                ui.add_space(4.0);
            }
            body(ui)
        })
        .inner
}

/// Run `add` with the light "main action" look (`kAction`), hover included.
fn with_action<R>(ui: &mut egui::Ui, add: impl FnOnce(&mut egui::Ui) -> R) -> R {
    ui.scope(|ui| {
        let w = &mut ui.visuals_mut().widgets;
        w.inactive.weak_bg_fill = ACTION;
        w.inactive.bg_fill = ACTION;
        w.hovered.weak_bg_fill = ACTION_HOVER;
        w.hovered.bg_fill = ACTION_HOVER;
        w.active.weak_bg_fill = ACTION_HOVER;
        w.active.bg_fill = ACTION_HOVER;
        add(ui)
    })
    .inner
}

/// The main action: light fill, dark text (`kAction`). True when clicked.
pub fn button(ui: &mut egui::Ui, text: &str, enabled: bool) -> bool {
    let fg = if enabled { ACTION_TEXT } else { DIM };
    let b =
        egui::Button::new(RichText::new(text).color(fg).strong()).min_size(egui::vec2(150.0, 34.0));
    if enabled {
        with_action(ui, |ui| ui.add(b).clicked())
    } else {
        ui.add_enabled(false, b).clicked()
    }
}

/// A secondary button (`kPlain`): grey fill. True when clicked.
pub fn plain(ui: &mut egui::Ui, text: &str, enabled: bool) -> bool {
    ui.add_enabled(enabled, egui::Button::new(text)).clicked()
}

/// A choice among a few (log choice, language, menu): the chosen one is drawn as the main action.
pub fn choice(ui: &mut egui::Ui, text: &str, chosen: bool, enabled: bool) -> bool {
    if chosen {
        let b = egui::Button::new(RichText::new(text).color(ACTION_TEXT).strong());
        with_action(ui, |ui| ui.add_enabled(enabled, b).clicked())
    } else {
        ui.add_enabled(enabled, egui::Button::new(text)).clicked()
    }
}

/// An On/Off switch with its word beside it (`DrawSwitch` of the C++ window). Returns true when it
/// was clicked; the caller flips the value. Reachable with the keyboard: Tab to it, Space or Enter.
pub fn toggle(
    ui: &mut egui::Ui,
    on: bool,
    enabled: bool,
    name: &str,
    on_word: &str,
    off_word: &str,
) -> bool {
    let size = egui::vec2(38.0, 20.0);
    ui.horizontal(|ui| {
        let (rect, mut r) = ui.allocate_exact_size(size, egui::Sense::click());
        r.widget_info(|| egui::WidgetInfo::selected(egui::Role::CheckBox, enabled, on, name));
        if !enabled {
            r = r.on_hover_cursor(egui::CursorIcon::NotAllowed);
        }
        let t = ui.ctx().animate_bool_responsive(r.id, on);
        let track = if on {
            GREEN
        } else {
            Color32::from_rgb(70, 70, 76)
        };
        let track = if enabled {
            track
        } else {
            track.gamma_multiply(0.5)
        };
        let p = ui.painter();
        p.rect_filled(rect, 10.0, track);
        let cx = rect.left() + 10.0 + t * (rect.width() - 20.0);
        p.circle_filled(
            egui::pos2(cx, rect.center().y),
            7.5,
            Color32::from_rgb(244, 244, 247),
        );
        if r.has_focus() {
            p.rect_stroke(
                rect.expand(2.0),
                12.0,
                Stroke::new(1.5, GREEN),
                egui::StrokeKind::Outside,
            );
        }
        ui.label(if on { good(on_word) } else { dim(off_word) });
        enabled && r.clicked()
    })
    .inner
}
