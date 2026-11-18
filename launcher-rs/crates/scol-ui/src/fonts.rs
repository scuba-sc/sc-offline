// SPDX-License-Identifier: GPL-3.0-only
//! Fonts. egui's bundled fonts are off, so a system font is read at run time. Nothing is embedded
//! or redistributed. The first family is Segoe UI; later entries are fallbacks that egui tries
//! for glyphs the earlier ones lack (Cyrillic is covered by Segoe UI and Arial; the symbol font
//! fills arrows and marks).

use std::sync::Arc;

use eframe::egui::{self, FontData, FontDefinitions, FontFamily};

const PROPORTIONAL: [&str; 5] = [
    "segoeui.ttf",
    "arial.ttf",
    "tahoma.ttf",
    "calibri.ttf",
    "seguisym.ttf",
];
const MONOSPACE: [&str; 2] = ["consola.ttf", "cour.ttf"];

fn fonts_dir() -> String {
    let windir = std::env::var("SystemRoot").unwrap_or_else(|_| "C:/Windows".into());
    format!("{windir}/Fonts")
}

/// Load the fonts. Err when no proportional font could be read (the window cannot draw text).
pub fn install(ctx: &egui::Context) -> Result<usize, String> {
    let dir = fonts_dir();
    let mut defs = FontDefinitions::empty();
    let mut loaded = 0usize;
    for name in PROPORTIONAL {
        if let Ok(bytes) = std::fs::read(format!("{dir}/{name}")) {
            let key = format!("ui-{name}");
            defs.font_data
                .insert(key.clone(), Arc::new(FontData::from_owned(bytes)));
            defs.families
                .entry(FontFamily::Proportional)
                .or_default()
                .push(key.clone());
            // Monospace falls back to the proportional fonts when no mono font exists.
            defs.families
                .entry(FontFamily::Monospace)
                .or_default()
                .push(key);
            loaded += 1;
        }
    }
    if loaded == 0 {
        return Err(format!(
            "no system font found in {dir} (looked for {})",
            PROPORTIONAL.join(", ")
        ));
    }
    for name in MONOSPACE {
        if let Ok(bytes) = std::fs::read(format!("{dir}/{name}")) {
            let key = format!("mono-{name}");
            defs.font_data
                .insert(key.clone(), Arc::new(FontData::from_owned(bytes)));
            defs.families
                .entry(FontFamily::Monospace)
                .or_default()
                .insert(0, key);
            break;
        }
    }
    ctx.set_fonts(defs);
    Ok(loaded)
}
