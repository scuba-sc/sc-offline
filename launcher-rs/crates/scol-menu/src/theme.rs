// SPDX-License-Identifier: GPL-3.0-only
//! The menu's colours and metrics, transcribed from `ApplyTheme()` in `src/menu_shell.cpp`. The
//! window is opaque (the ImGui menu draws an optional picture underneath; this first version has
//! none), so alpha tints are pre-blended over `SOIL`.
use egui::{
    Color32, CornerRadius, FontData, FontDefinitions, FontFamily, FontId, Stroke, TextStyle,
};

pub const SOIL: Color32 = Color32::from_rgb(0x0E, 0x18, 0x11);
pub const PANEL: Color32 = Color32::from_rgb(0x17, 0x2A, 0x1B);
pub const PANEL_UP: Color32 = Color32::from_rgb(0x1F, 0x35, 0x23);
pub const POPUP: Color32 = Color32::from_rgb(0x1C, 0x33, 0x21);
pub const LINE: Color32 = Color32::from_rgb(0x2F, 0x4B, 0x32);
pub const BORDER: Color32 = Color32::from_rgb(0x4F, 0x7F, 0x51);
pub const TEXT: Color32 = Color32::from_rgb(0xE6, 0xEF, 0xDD);
pub const MUTED: Color32 = Color32::from_rgb(0x9F, 0xB5, 0x9A);
pub const LEAF: Color32 = Color32::from_rgb(0x86, 0xC6, 0x4B);
pub const LEAF_DEEP: Color32 = Color32::from_rgb(0x3E, 0x6B, 0x27);
pub const HEADER: Color32 = Color32::from_rgb(0x3F, 0x7A, 0x2A);
pub const HEADER_HOVER: Color32 = Color32::from_rgb(0x2D, 0x58, 0x32);
pub const BUTTON: Color32 = Color32::from_rgb(0x1F, 0x35, 0x23);
pub const BUTTON_HOVER: Color32 = Color32::from_rgb(0x2A, 0x47, 0x30);
pub const FRAME: Color32 = Color32::from_rgb(0x17, 0x2A, 0x1B);
pub const FRAME_HOVER: Color32 = Color32::from_rgb(0x1F, 0x35, 0x23);
pub const FRAME_ACTIVE: Color32 = Color32::from_rgb(0x26, 0x41, 0x2A);

pub const SEV_INFO: Color32 = Color32::from_rgb(0x8C, 0xBF, 0xFF);
pub const SEV_WARN: Color32 = Color32::from_rgb(0xFF, 0xCC, 0x4D);
pub const SEV_ERROR: Color32 = Color32::from_rgb(0xFF, 0x73, 0x73);

pub const BODY: f32 = 17.0;
pub const HEADING: f32 = 20.0;
pub const PAGE_TITLE: f32 = 24.0;

/// Blends `c` toward `to` the way ImGui's DisabledAlpha 0.8 fades a widget.
pub fn disabled(c: Color32) -> Color32 {
    c.gamma_multiply(0.8)
}

pub fn apply(ctx: &egui::Context) {
    let mut style = (*ctx.global_style()).clone();
    style.text_styles = [
        (TextStyle::Small, FontId::proportional(14.0)),
        (TextStyle::Body, FontId::proportional(BODY)),
        (TextStyle::Button, FontId::proportional(BODY)),
        (TextStyle::Heading, FontId::proportional(HEADING)),
        (TextStyle::Monospace, FontId::monospace(15.0)),
    ]
    .into();
    style.spacing.item_spacing = egui::vec2(8.0, 7.0);
    style.spacing.button_padding = egui::vec2(9.0, 5.0);
    style.spacing.window_margin = egui::Margin::symmetric(16, 12);
    style.spacing.scroll.bar_width = 11.0;
    style.spacing.slider_width = 200.0;
    style.spacing.interact_size.y = 28.0;
    let r = CornerRadius::same(4);
    let v = &mut style.visuals;
    v.dark_mode = true;
    v.override_text_color = Some(TEXT);
    v.weak_text_color = Some(MUTED);
    v.panel_fill = SOIL;
    v.window_fill = SOIL;
    v.extreme_bg_color = FRAME;
    v.faint_bg_color = Color32::from_rgb(0x12, 0x20, 0x16);
    v.code_bg_color = FRAME;
    v.window_corner_radius = CornerRadius::ZERO;
    v.menu_corner_radius = r;
    v.window_stroke = Stroke::NONE;
    v.popup_shadow = egui::Shadow::NONE;
    v.window_shadow = egui::Shadow::NONE;
    v.selection.bg_fill = LEAF_DEEP;
    v.selection.stroke = Stroke::new(1.0, LEAF);
    v.hyperlink_color = LEAF;
    v.disabled_alpha = 0.8;
    v.slider_trailing_fill = true;
    v.widgets.noninteractive.bg_fill = SOIL;
    v.widgets.noninteractive.weak_bg_fill = SOIL;
    v.widgets.noninteractive.bg_stroke = Stroke::new(1.0, LINE);
    v.widgets.noninteractive.fg_stroke = Stroke::new(1.0, TEXT);
    v.widgets.inactive.bg_fill = FRAME;
    v.widgets.inactive.weak_bg_fill = BUTTON;
    v.widgets.inactive.bg_stroke = Stroke::new(1.0, LINE);
    v.widgets.inactive.fg_stroke = Stroke::new(1.0, TEXT);
    v.widgets.hovered.bg_fill = FRAME_HOVER;
    v.widgets.hovered.weak_bg_fill = BUTTON_HOVER;
    v.widgets.hovered.bg_stroke = Stroke::new(1.0, BORDER);
    v.widgets.hovered.fg_stroke = Stroke::new(1.5, TEXT);
    v.widgets.active.bg_fill = FRAME_ACTIVE;
    v.widgets.active.weak_bg_fill = LEAF_DEEP;
    v.widgets.active.bg_stroke = Stroke::new(1.0, LEAF);
    v.widgets.active.fg_stroke = Stroke::new(2.0, TEXT);
    v.widgets.open.bg_fill = FRAME_ACTIVE;
    v.widgets.open.weak_bg_fill = BUTTON_HOVER;
    v.widgets.open.bg_stroke = Stroke::new(1.0, BORDER);
    for w in [
        &mut v.widgets.noninteractive,
        &mut v.widgets.inactive,
        &mut v.widgets.hovered,
        &mut v.widgets.active,
        &mut v.widgets.open,
    ] {
        w.corner_radius = r;
    }
    ctx.set_global_style(style);
}

/// Loads Bahnschrift, then Segoe UI and other system fonts, from the Windows font folder at run time
/// (nothing is bundled and egui's own fonts are off), as `MenuLoadFonts()` does. The later entries cover
/// glyphs the first one lacks. Err when no font could be read: the menu then does not start rather than
/// let egui fail while the game runs.
pub fn load_fonts(ctx: &egui::Context) -> Result<usize, String> {
    const PROPORTIONAL: [&str; 6] = [
        "bahnschrift.ttf",
        "segoeui.ttf",
        "arial.ttf",
        "tahoma.ttf",
        "calibri.ttf",
        "seguisym.ttf",
    ];
    let mut fonts = FontDefinitions::empty();
    let dir = std::env::var_os("SystemRoot")
        .map(std::path::PathBuf::from)
        .unwrap_or_else(|| "C:/Windows".into())
        .join("Fonts");
    let mut loaded = 0;
    for name in PROPORTIONAL {
        if let Ok(bytes) = std::fs::read(dir.join(name)) {
            let key = format!("system-{name}");
            fonts.font_data.insert(
                key.clone(),
                std::sync::Arc::new(FontData::from_owned(bytes)),
            );
            for family in [FontFamily::Proportional, FontFamily::Monospace] {
                fonts.families.entry(family).or_default().push(key.clone());
            }
            loaded += 1;
        }
    }
    if loaded == 0 {
        return Err(format!("no system font found in {}", dir.display()));
    }
    ctx.set_fonts(fonts);
    Ok(loaded)
}
