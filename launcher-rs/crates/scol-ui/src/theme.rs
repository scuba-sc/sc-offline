// SPDX-License-Identifier: GPL-3.0-only
//! Colours and small widgets. The colours started as the `kUi*` constants of `launcher/launcher.cpp`:
//! dark grey panels, light text, a light main button with dark text, and green for good status. The
//! window is dark whatever Windows is set to. Buttons are drawn here by hand so their text is centred,
//! a disabled one stays readable and every kind shares one shape.

use eframe::egui::{self, Color32, CornerRadius, RichText, Stroke, StrokeKind, TextStyle};

pub const BG: Color32 = Color32::from_rgb(19, 19, 22);
pub const SIDE: Color32 = Color32::from_rgb(14, 14, 16);
pub const CARD: Color32 = Color32::from_rgb(26, 26, 30);
pub const ROW: Color32 = Color32::from_rgb(38, 38, 43);
pub const ROW_HOVER: Color32 = Color32::from_rgb(50, 50, 57);
pub const LINE: Color32 = Color32::from_rgb(44, 44, 50);
pub const TEXT: Color32 = Color32::from_rgb(238, 238, 242);
pub const BODY: Color32 = Color32::from_rgb(196, 198, 205);
/// Small print. Light enough to read on a card: about 5:1 against [`CARD`].
pub const DIM: Color32 = Color32::from_rgb(142, 144, 154);
pub const ACTION: Color32 = Color32::from_rgb(230, 230, 234);
pub const ACTION_HOVER: Color32 = Color32::from_rgb(255, 255, 255);
pub const ACTION_TEXT: Color32 = Color32::from_rgb(14, 14, 16);
pub const GREEN: Color32 = Color32::from_rgb(46, 196, 118);
pub const GREEN_HOVER: Color32 = Color32::from_rgb(66, 216, 138);
/// Text on a green fill.
pub const GREEN_TEXT: Color32 = Color32::from_rgb(6, 26, 16);
/// `kUiWarn`: a soft coral.
pub const WARN: Color32 = Color32::from_rgb(255, 132, 104);
pub const WARN_HOVER: Color32 = Color32::from_rgb(255, 156, 132);
pub const BAD: Color32 = Color32::from_rgb(232, 96, 96);

const RADIUS: u8 = 8;

/// The window's visuals: always dark.
pub fn visuals() -> egui::Visuals {
    let mut v = egui::Visuals::dark();
    v.panel_fill = BG;
    v.window_fill = CARD;
    v.extreme_bg_color = SIDE;
    v.faint_bg_color = ROW;
    v.override_text_color = Some(TEXT);
    v.hyperlink_color = GREEN;
    let r = CornerRadius::same(RADIUS);
    v.widgets.noninteractive.bg_stroke = Stroke::new(1.0, LINE);
    v.widgets.noninteractive.corner_radius = r;
    for (w, fill, line) in [
        (&mut v.widgets.inactive, ROW, LINE),
        (&mut v.widgets.hovered, ROW_HOVER, ROW_HOVER),
        (&mut v.widgets.active, ROW_HOVER, ROW_HOVER),
        (&mut v.widgets.open, ROW_HOVER, ROW_HOVER),
    ] {
        w.bg_fill = fill;
        w.weak_bg_fill = fill;
        w.bg_stroke = Stroke::new(1.0, line);
        w.corner_radius = r;
        w.fg_stroke = Stroke::new(1.0, TEXT);
    }
    v.selection.bg_fill = GREEN.gamma_multiply(0.45);
    // The keyboard focus ring.
    v.selection.stroke = Stroke::new(1.5, GREEN);
    v
}

pub fn apply(ctx: &egui::Context) {
    // egui follows the system theme unless told otherwise, and `set_visuals` only fills in the theme
    // that is current. On a PC in light mode the stock light visuals (dark text, light buttons) were
    // drawn over these dark panels. So: one theme, and the same visuals and sizes in both slots.
    ctx.set_theme(egui::ThemePreference::Dark);
    ctx.set_visuals_of(egui::Theme::Light, visuals());
    ctx.set_visuals_of(egui::Theme::Dark, visuals());
    ctx.all_styles_mut(|s| {
        s.spacing.item_spacing = egui::vec2(8.0, 6.0);
        s.spacing.button_padding = egui::vec2(12.0, 6.0);
        s.spacing.interact_size.y = 30.0;
        s.text_styles
            .insert(TextStyle::Body, egui::FontId::proportional(14.0));
        s.text_styles
            .insert(TextStyle::Button, egui::FontId::proportional(14.0));
        s.text_styles
            .insert(TextStyle::Small, egui::FontId::proportional(12.0));
        s.text_styles
            .insert(TextStyle::Heading, egui::FontId::proportional(22.0));
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

/// The small heading of a card or a group: upper case, spaced, dim.
pub fn section(text: &str) -> RichText {
    RichText::new(text.to_uppercase())
        .size(11.5)
        .color(DIM)
        .extra_letter_spacing(0.8)
}

/// The frame of a card: a panel one step lighter than the page, with a thin edge.
pub fn card_frame() -> egui::Frame {
    egui::Frame::new()
        .fill(CARD)
        .stroke(Stroke::new(1.0, LINE))
        .corner_radius(10.0)
        .inner_margin(14.0)
}

/// A titled card, like the C++ window's `UiCard`.
pub fn card<R>(ui: &mut egui::Ui, title: &str, body: impl FnOnce(&mut egui::Ui) -> R) -> R {
    card_frame()
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            if !title.is_empty() {
                ui.label(section(title));
                ui.add_space(6.0);
            }
            body(ui)
        })
        .inner
}

/// How a button is drawn.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    /// The main action of a card: light fill, dark text (`kAction`).
    Primary,
    /// A secondary button (`kPlain`): grey fill.
    Plain,
    /// Play: the green fill.
    Go,
    /// Stop: the coral fill.
    Halt,
    /// The chosen option of a choice.
    Chosen,
    /// An option that is not chosen: no fill until the pointer is on it.
    Option,
}

/// A button drawn by hand. `min` is its smallest size and `font` the text size. True when clicked
/// (pointer, or Space or Enter while it has the keyboard focus).
pub fn draw_button(
    ui: &mut egui::Ui,
    text: &str,
    kind: Kind,
    enabled: bool,
    min: egui::Vec2,
    font: f32,
) -> bool {
    let none = Color32::TRANSPARENT;
    let (fill, hover, fg, edge) = match kind {
        Kind::Primary => (ACTION, ACTION_HOVER, ACTION_TEXT, none),
        Kind::Plain => (ROW, ROW_HOVER, TEXT, LINE),
        Kind::Go => (GREEN, GREEN_HOVER, GREEN_TEXT, none),
        Kind::Halt => (WARN, WARN_HOVER, ACTION_TEXT, none),
        Kind::Chosen => (ROW_HOVER, ROW_HOVER, TEXT, none),
        Kind::Option => (none, ROW, BODY, none),
    };
    let fg = if enabled { fg } else { DIM };
    let galley =
        ui.painter()
            .layout_no_wrap(text.to_string(), egui::FontId::proportional(font), fg);
    let size = egui::vec2(
        (galley.size().x + 28.0).max(min.x),
        (galley.size().y + 12.0).max(min.y),
    );
    let sense = if enabled {
        egui::Sense::click()
    } else {
        egui::Sense::hover()
    };
    let (rect, mut r) = ui.allocate_exact_size(size, sense);
    r.widget_info(|| egui::WidgetInfo::labeled(egui::Role::Button, enabled, text));
    if ui.is_rect_visible(rect) {
        let p = ui.painter();
        let (bg, line) = if !enabled {
            (none, LINE)
        } else if r.hovered() || r.has_focus() {
            (hover, if edge == none { none } else { ROW_HOVER })
        } else {
            (fill, edge)
        };
        let radius = if matches!(kind, Kind::Chosen | Kind::Option) {
            6.0
        } else {
            f32::from(RADIUS)
        };
        p.rect_filled(rect, radius, bg);
        if line != none {
            p.rect_stroke(rect, radius, Stroke::new(1.0, line), StrokeKind::Inside);
        }
        p.galley(rect.center() - galley.size() / 2.0, galley, fg);
        if r.has_focus() {
            p.rect_stroke(
                rect.expand(2.0),
                radius + 2.0,
                Stroke::new(1.5, GREEN),
                StrokeKind::Outside,
            );
        }
    }
    if enabled {
        r = r.on_hover_cursor(egui::CursorIcon::PointingHand);
    }
    enabled && r.clicked()
}

/// The main action: light fill, dark text (`kAction`). True when clicked.
pub fn button(ui: &mut egui::Ui, text: &str, enabled: bool) -> bool {
    draw_button(
        ui,
        text,
        Kind::Primary,
        enabled,
        egui::vec2(120.0, 34.0),
        14.0,
    )
}

/// A secondary button (`kPlain`): grey fill. True when clicked.
pub fn plain(ui: &mut egui::Ui, text: &str, enabled: bool) -> bool {
    draw_button(ui, text, Kind::Plain, enabled, egui::vec2(0.0, 30.0), 14.0)
}

/// One option of a choice among a few: the chosen one is filled.
pub fn choice(ui: &mut egui::Ui, text: &str, chosen: bool, enabled: bool) -> bool {
    let kind = if chosen { Kind::Chosen } else { Kind::Option };
    draw_button(ui, text, kind, enabled, egui::vec2(0.0, 30.0), 14.0)
}

/// A choice among a few in one track (log choice, language, menu, the plugin tabs). `options` are
/// (text, chosen). Returns the index that was clicked.
pub fn segmented(ui: &mut egui::Ui, options: &[(String, bool)], enabled: bool) -> Option<usize> {
    let mut clicked = None;
    egui::Frame::new()
        .fill(SIDE)
        .stroke(Stroke::new(1.0, LINE))
        .corner_radius(9.0)
        .inner_margin(3.0)
        .show(ui, |ui| {
            ui.horizontal(|ui| {
                ui.spacing_mut().item_spacing = egui::vec2(2.0, 2.0);
                for (i, (text, chosen)) in options.iter().enumerate() {
                    if choice(ui, text, *chosen, enabled) {
                        clicked = Some(i);
                    }
                }
            });
        });
    clicked
}

/// A dot with a soft ring: the mark in front of a status line.
pub fn dot(ui: &mut egui::Ui, colour: Color32, size: f32) {
    let (rect, _) = ui.allocate_exact_size(egui::vec2(size, size), egui::Sense::hover());
    let p = ui.painter();
    p.circle_filled(rect.center(), size / 2.0, colour.gamma_multiply(0.22));
    p.circle_filled(rect.center(), size / 4.0, colour);
}

/// The pictures of the side list. Drawn with lines, so no icon font is needed (Wine ships none).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Icon {
    Home,
    Output,
    Plugins,
    Capabilities,
    Settings,
}

/// Draw `icon` in the 16 by 16 square whose top left corner is `at`.
pub fn paint_icon(p: &egui::Painter, at: egui::Pos2, icon: Icon, colour: Color32) {
    let s = Stroke::new(1.5, colour);
    let pt = |x: f32, y: f32| egui::pos2(at.x + x, at.y + y);
    match icon {
        Icon::Home => {
            p.line(vec![pt(1.5, 8.0), pt(8.0, 2.0), pt(14.5, 8.0)], s);
            p.line(
                vec![pt(3.5, 7.0), pt(3.5, 13.5), pt(12.5, 13.5), pt(12.5, 7.0)],
                s,
            );
        }
        Icon::Output => {
            p.rect_stroke(
                egui::Rect::from_min_max(pt(1.5, 2.5), pt(14.5, 13.5)),
                2.5,
                s,
                StrokeKind::Inside,
            );
            p.line(vec![pt(4.5, 6.0), pt(7.0, 8.0), pt(4.5, 10.0)], s);
            p.line_segment([pt(8.5, 10.5), pt(11.5, 10.5)], s);
        }
        Icon::Plugins => {
            for (x, y) in [(2.0, 2.0), (9.0, 2.0), (2.0, 9.0)] {
                p.rect_stroke(
                    egui::Rect::from_min_size(pt(x, y), egui::vec2(5.0, 5.0)),
                    1.5,
                    s,
                    StrokeKind::Inside,
                );
            }
            p.rect_filled(
                egui::Rect::from_min_size(pt(9.0, 9.0), egui::vec2(5.0, 5.0)),
                1.5,
                colour,
            );
        }
        Icon::Capabilities => {
            for (y, x) in [(4.0, 10.0), (8.0, 5.5), (12.0, 10.5)] {
                p.line_segment([pt(2.0, y), pt(14.0, y)], s);
                p.circle_filled(pt(x, y), 2.4, colour);
            }
        }
        Icon::Settings => {
            let c = pt(8.0, 8.0);
            p.circle_stroke(c, 2.3, s);
            p.circle_stroke(c, 5.0, s);
            for i in 0..8u8 {
                let a = std::f32::consts::TAU * f32::from(i) / 8.0;
                let d = egui::vec2(a.cos(), a.sin());
                p.line_segment([c + d * 5.0, c + d * 7.0], s);
            }
        }
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
                StrokeKind::Outside,
            );
        }
        ui.label(if on { good(on_word) } else { dim(off_word) });
        enabled && r.clicked()
    })
    .inner
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Issue 219: on a PC whose Windows theme is light the window drew dark text and light buttons on
    /// its dark panels, because the dark visuals were only stored for the theme that was current.
    #[test]
    fn the_window_is_dark_whatever_the_system_theme_is() {
        for system in [egui::Theme::Light, egui::Theme::Dark] {
            let ctx = egui::Context::default();
            apply(&ctx);
            let input = egui::RawInput {
                system_theme: Some(system),
                ..Default::default()
            };
            let mut seen = None;
            let mut out = ctx.run_ui(input, |ui| seen = Some(ui.visuals().clone()));
            out.textures_delta.clear();
            let v = seen.expect("one frame ran");
            assert!(v.dark_mode, "system theme {system:?}");
            assert_eq!(v.override_text_color, Some(TEXT), "system theme {system:?}");
            assert_eq!(v.panel_fill, BG, "system theme {system:?}");
            assert_eq!(v.widgets.inactive.bg_fill, ROW, "system theme {system:?}");
            assert_eq!(ctx.theme(), egui::Theme::Dark);
            // Sizes are the same in both slots too.
            for theme in [egui::Theme::Light, egui::Theme::Dark] {
                assert_eq!(ctx.style_of(theme).spacing.interact_size.y, 30.0);
            }
        }
    }

    /// Small print stays readable on a card: at least 4.5 to 1.
    #[test]
    fn dim_text_has_enough_contrast_on_a_card() {
        fn lum(c: Color32) -> f32 {
            let f = |v: u8| {
                let v = f32::from(v) / 255.0;
                if v <= 0.04045 {
                    v / 12.92
                } else {
                    ((v + 0.055) / 1.055).powf(2.4)
                }
            };
            0.2126 * f(c.r()) + 0.7152 * f(c.g()) + 0.0722 * f(c.b())
        }
        for (fg, bg) in [
            (DIM, CARD),
            (DIM, BG),
            (DIM, SIDE),
            (BODY, CARD),
            (GREEN_TEXT, GREEN),
        ] {
            let (a, b) = (lum(fg), lum(bg));
            let ratio = (a.max(b) + 0.05) / (a.min(b) + 0.05);
            assert!(ratio >= 4.5, "{fg:?} on {bg:?}: {ratio}");
        }
    }
}
