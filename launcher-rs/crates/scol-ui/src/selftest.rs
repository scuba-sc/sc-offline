// SPDX-License-Identifier: GPL-3.0-only
//! `--ui-self-test`: no window and no GPU. Builds every page for a few frames with egui's own
//! context on a fixture install, runs the page actions (plugin toggle, capability switch, language,
//! add from folder, an unavailable command) and checks the files they write. Exit 0 or 1.

use std::path::PathBuf;
use std::sync::Arc;
use std::time::{Duration, Instant};

use eframe::egui;

use crate::app::{App, Job, Page, PAGES};
use crate::backend::Command;
use crate::fixture::{self, FixtureBackend};
use crate::{fonts, pages, theme};

const SIZE: egui::Vec2 = egui::vec2(820.0, 520.0);

/// One frame of the whole window.
pub fn frame(ctx: &egui::Context, app: &mut App, n: u32) -> usize {
    let input = egui::RawInput {
        screen_rect: Some(egui::Rect::from_min_size(egui::Pos2::ZERO, SIZE)),
        time: Some(f64::from(n) / 60.0),
        ..Default::default()
    };
    let mut out = ctx.run_ui(input, |ui| pages::draw(app, ui));
    // Nothing paints here, so the texture uploads are dropped on purpose.
    out.textures_delta.clear();
    out.shapes.len()
}

fn frames(ctx: &egui::Context, app: &mut App, count: u32) -> usize {
    let mut shapes = 0;
    for n in 0..count {
        shapes += frame(ctx, app, n);
    }
    shapes
}

fn wait_idle(ctx: &egui::Context, app: &mut App) -> Result<(), String> {
    let start = Instant::now();
    while app.busy > 0 {
        if start.elapsed() > Duration::from_secs(90) {
            return Err("a worker job did not finish in 90 seconds".into());
        }
        std::thread::sleep(Duration::from_millis(20));
        frame(ctx, app, 0);
    }
    Ok(())
}

fn check(ok: bool, what: &str) -> Result<(), String> {
    if ok {
        println!("ok    {what}");
        Ok(())
    } else {
        Err(format!("FAILED {what}"))
    }
}

fn scratch(here: Option<PathBuf>) -> PathBuf {
    if let Some(h) = here {
        return h;
    }
    let base = if std::path::Path::new("D:/t").is_dir() {
        PathBuf::from("D:/t")
    } else {
        std::env::temp_dir()
    };
    base.join(format!("scol-ui-selftest-{}", std::process::id()))
}

fn go(here: Option<PathBuf>) -> Result<(), String> {
    let root = scratch(here);
    let _ = std::fs::remove_dir_all(&root);
    fixture::build(&root)?;
    println!("fixture install: {}", root.display());

    let ctx = egui::Context::default();
    let loaded = fonts::install(&ctx)?;
    theme::apply(&ctx);
    println!("ok    {loaded} system font file(s) loaded");

    let mut app = App::new(root.clone(), Arc::new(FixtureBackend));
    app.attach(&ctx);
    check(app.plugins.len() == 3, "plugin list reads three folders")?;
    check(
        app.plugins.iter().filter(|p| p.enabled).count() == 2,
        "beta starts disabled",
    )?;

    app.submit(Job::Status);
    app.submit(Job::CatalogueLoad);
    wait_idle(&ctx, &mut app)?;
    check(
        app.status.is_some(),
        "status report arrived from scol-install",
    )?;

    // Every page, a few frames each, in both languages.
    for lang in [
        scol_config::Language::English,
        scol_config::Language::Russian,
    ] {
        app.set_language(lang);
        for p in PAGES {
            app.page = p;
            if p == Page::Plugins {
                for tab in [
                    crate::app::PluginTab::Installed,
                    crate::app::PluginTab::Catalogue,
                ] {
                    app.plugin_tab = tab;
                    check(
                        frames(&ctx, &mut app, 3) > 0,
                        &format!("{p:?}/{tab:?} draws ({})", lang.word()),
                    )?;
                }
                app.open_plugin = Some("beta".into());
                app.plugin_tab = crate::app::PluginTab::Installed;
                check(frames(&ctx, &mut app, 3) > 0, "plugin settings draws")?;
                app.open_plugin = None;
            } else {
                check(
                    frames(&ctx, &mut app, 3) > 0,
                    &format!("{p:?} draws ({})", lang.word()),
                )?;
            }
        }
    }
    app.set_language(scol_config::Language::English);

    // Catalogue: with the golden index and the TEST key the list has entries; without them it is refused.
    match &app.catalogue {
        Some(Ok(l)) => {
            check(
                !l.rows.is_empty(),
                "catalogue lists entries from the verified index",
            )?;
            println!(
                "      index sequence {}, {} entries",
                l.sequence,
                l.rows.len()
            );
        }
        Some(Err(e)) => println!(
            "note  catalogue not loaded here ({}): build with --features fixtures",
            if e.is_empty() { "no index" } else { e }
        ),
        None => return Err("catalogue job returned nothing".into()),
    }

    // Plugin toggle writes the marker.
    app.toggle_plugin("beta", true);
    check(
        !root.join("data/plugins/beta/disabled").exists(),
        "enable removes the disabled marker",
    )?;
    app.toggle_plugin("alpha", false);
    check(
        root.join("data/plugins/alpha/disabled").exists(),
        "disable writes the disabled marker",
    )?;

    // Capability switch edits the ini in place.
    let sw =
        scol_config::Capabilities::switch_for_key("exp_loadout_async").ok_or("switch missing")?;
    app.set_capability(sw, false);
    let ini = std::fs::read_to_string(root.join("sc-offline.ini")).map_err(|e| e.to_string())?;
    check(
        ini.contains("exp_loadout_async = off") || ini.contains("exp_loadout_async=off"),
        "capability toggle writes exp_loadout_async",
    )?;
    check(
        ini.contains("exp_audio_native = off"),
        "the other lines of the ini are kept",
    )?;
    check(
        !app.cfg.capabilities.is_on(sw),
        "config reads the switch back as off",
    )?;
    let diag = scol_config::Capabilities::switch_for_key("exp_entities_diag")
        .ok_or("diag switch missing")?;
    check(
        !app.cfg.capabilities.is_on(diag),
        "exp_entities_diag is off by default",
    )?;

    // Settings keys.
    app.set_clean_logs("always");
    app.set_language(scol_config::Language::Auto);
    let ini = std::fs::read_to_string(root.join("sc-offline.ini")).map_err(|e| e.to_string())?;
    check(
        ini.contains("clean_logs = always") && ini.contains("language = auto"),
        "settings write clean_logs and language",
    )?;

    // Add from folder: a folder with a plugin.ini; it lands disabled.
    let src = root.join("incoming").join("delta");
    std::fs::create_dir_all(&src).map_err(|e| e.to_string())?;
    std::fs::write(
        src.join("plugin.ini"),
        "id = delta\nname = Delta\nversion = 1.0\napi = 1.0\nkind = lua\nentry = main.lua\n",
    )
    .map_err(|e| e.to_string())?;
    std::fs::write(src.join("main.lua"), "-- synthetic\n").map_err(|e| e.to_string())?;
    app.submit(Job::AddPath(src.to_string_lossy().into_owned()));
    wait_idle(&ctx, &mut app)?;
    check(
        root.join("data/plugins/delta/disabled").exists(),
        "add from folder lands the plugin disabled",
    )?;
    check(
        app.plugins.iter().any(|p| p.id == "delta" && !p.enabled),
        "list shows the new plugin off",
    )?;

    // A command that is not connected gives a clear message, not a crash.
    app.submit(Job::Command(Command::Play));
    wait_idle(&ctx, &mut app)?;
    check(
        app.output
            .iter()
            .any(|l| l.contains("not available in this window yet")),
        "Play says it is not available yet",
    )?;
    app.page = Page::Output;
    check(
        frames(&ctx, &mut app, 3) > 0,
        "Output page draws the streamed lines",
    )?;

    let _ = std::fs::remove_dir_all(&root);
    Ok(())
}

/// Returns the exit code.
pub fn run(here: Option<PathBuf>) -> i32 {
    match go(here) {
        Ok(()) => {
            println!("ui self test: ok (not run in game)");
            0
        }
        Err(e) => {
            eprintln!("ui self test: {e}");
            1
        }
    }
}
