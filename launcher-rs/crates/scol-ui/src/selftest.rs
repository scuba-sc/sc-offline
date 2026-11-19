// SPDX-License-Identifier: GPL-3.0-only
//! `--ui-self-test`: no window and no GPU. Builds every page for a few frames with egui's own
//! context on a fixture install, runs the page actions (plugin toggle, capability switch, settings,
//! add from folder, remove, the calls that have no connection yet) and checks the files they write.
//! Exit 0 or 1. The fixture backend runs nothing: no process, no game, no network.

use std::path::PathBuf;
use std::sync::Arc;
use std::time::{Duration, Instant};

use eframe::egui;

use crate::app::{App, Job, Page, PluginTab, PAGES};
use crate::backend::Command;
use crate::catalogue::State;
use crate::fixture::{self, FixtureBackend};
use crate::{fonts, pages, theme};

const SIZE: egui::Vec2 = egui::vec2(crate::WINDOW[0], crate::WINDOW[1]);

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
    std::env::temp_dir().join(format!("scol-ui-selftest-{}", std::process::id()))
}

fn find_switch(key: &str) -> Result<&'static scol_contract::capability_env::Switch, String> {
    scol_config::Capabilities::switch_for_key(key).ok_or_else(|| format!("switch {key} missing"))
}

fn read_ini(root: &std::path::Path) -> Result<String, String> {
    std::fs::read_to_string(root.join("sc-offline.ini")).map_err(|e| e.to_string())
}

fn every_page(ctx: &egui::Context, app: &mut App) -> Result<(), String> {
    for lang in [
        scol_config::Language::English,
        scol_config::Language::Russian,
    ] {
        app.set_language(lang);
        for p in PAGES {
            app.page = p;
            if p == Page::Plugins {
                for tab in [PluginTab::Installed, PluginTab::Catalogue] {
                    app.plugin_tab = tab;
                    check(
                        frames(ctx, app, 3) > 0,
                        &format!("{p:?}/{tab:?} draws ({})", lang.word()),
                    )?;
                }
                app.open_plugin = Some("beta".into());
                app.plugin_tab = PluginTab::Installed;
                check(frames(ctx, app, 3) > 0, "plugin settings draws")?;
                app.open_plugin = None;
            } else if p == Page::Capabilities {
                app.caps_more = true;
                app.caps_auto = true;
                check(
                    frames(ctx, app, 3) > 0,
                    &format!("Capabilities draws with every group open ({})", lang.word()),
                )?;
                app.caps_more = false;
                app.caps_auto = false;
            } else {
                check(
                    frames(ctx, app, 3) > 0,
                    &format!("{p:?} draws ({})", lang.word()),
                )?;
            }
        }
    }
    app.set_language(scol_config::Language::English);
    Ok(())
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
    check(
        app.cfg
            .capabilities
            .is_on(find_switch("exp_loadout_async")?)
            && !app
                .cfg
                .capabilities
                .is_on(find_switch("exp_entities_diag")?),
        "capability defaults: switches on, diagnostic off",
    )?;

    app.submit(Job::Status);
    app.submit(Job::CatalogueLoad);
    wait_idle(&ctx, &mut app)?;
    check(
        app.status.is_some(),
        "status report arrived from scol-install",
    )?;

    every_page(&ctx, &mut app)?;

    // Catalogue: the trust root decides. With the fixtures feature the TEST key is trusted and the
    // golden index lists entries; without it the shipped (empty) root says no catalogue is published.
    match &app.catalogue {
        Some(State::Loaded(l)) => {
            check(
                !l.rows.is_empty(),
                "catalogue lists entries from the verified index",
            )?;
            println!(
                "      index number {}, {} entries",
                l.sequence,
                l.rows.len()
            );
        }
        Some(State::NoCatalogue) => println!(
            "note  no trust key in this build: the Catalogue tab says no catalogue is published"
        ),
        Some(State::Refused(e)) => println!("note  the test index was refused here: {e}"),
        Some(State::NoIndex) => println!("note  no catalogue index in the fixture"),
        None => return Err("catalogue job returned nothing".into()),
    }

    // Plugin toggle writes the marker (through scol-plugin-manager).
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
    check(
        app.note.as_ref().is_some_and(|n| !n.error),
        "toggle says what it did",
    )?;

    // Capability switch edits the ini in place.
    let sw = find_switch("exp_loadout_async")?;
    app.set_capability(sw, false);
    let ini = read_ini(&root)?;
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
    app.set_capability(sw, true);
    check(
        app.cfg.capabilities.is_on(sw),
        "config reads the switch back as on",
    )?;

    // Settings keys, the menu among them.
    app.set_clean_logs("always");
    app.set_menu(scol_config::MenuChoice::Rust);
    app.set_check_updates(false);
    app.set_language(scol_config::Language::Auto);
    let ini = read_ini(&root)?;
    check(
        ini.contains("clean_logs = always")
            && ini.contains("language = auto")
            && ini.contains("menu = rust")
            && ini.contains("check_updates = off"),
        "settings write clean_logs, language, menu and check_updates",
    )?;
    check(
        app.cfg.menu == scol_config::MenuChoice::Rust,
        "config reads menu = rust back",
    )?;
    app.set_menu(scol_config::MenuChoice::Imgui);
    check(
        app.cfg.menu == scol_config::MenuChoice::Imgui,
        "the menu goes back to Classic",
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
    check(
        app.note
            .as_ref()
            .is_some_and(|n| !n.error && n.text.contains("Delta")),
        "the note names the added plugin",
    )?;

    // Remove it again.
    app.submit(Job::Remove("delta".into()));
    wait_idle(&ctx, &mut app)?;
    check(
        !root.join("data/plugins/delta").exists() && !app.plugins.iter().any(|p| p.id == "delta"),
        "remove takes the folder out of the list",
    )?;

    // The calls with no connection yet answer in plain words and keep the window alive.
    app.browse();
    check(
        app.note
            .as_ref()
            .is_some_and(|n| n.error && n.text.contains("not in this build yet")),
        "Browse says the folder picker is not in this build yet",
    )?;
    app.submit(Job::SaveSettings(
        "beta".into(),
        vec![("count".into(), "7".into())],
    ));
    wait_idle(&ctx, &mut app)?;
    check(
        app.note
            .as_ref()
            .is_some_and(|n| n.error && n.text.contains("not in this build yet")),
        "saving a plugin setting says it is not in this build yet",
    )?;
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
