// SPDX-License-Identifier: GPL-3.0-only
//! The five pages and the frame around them. Drawing only reads `App` and records what the player
//! asked for; the actions run after the frame (`Act`), so no widget closure borrows the app twice.

use eframe::egui::{self, RichText};
use scol_contract::capability_env::SWITCHES;
use scol_install::checks::{describe_pc_changes, Eac};

use crate::app::{App, Job, Page, PluginTab, PAGES};
use crate::backend::Command;
use crate::theme::{self, button, card, dim};

/// One thing the player asked for during a frame.
pub enum Act {
    Go(Page),
    Job(Job),
    Toggle(String, bool),
    OpenPlugin(Option<String>),
    SetCap(usize, bool),
    Language(scol_config::Language),
    CleanLogs(&'static str),
    PluginsOn(bool),
    OpenFolder(std::path::PathBuf),
    Tab(PluginTab),
}

fn page_label(a: &App, p: Page) -> String {
    match p {
        Page::Home => a.t("launcher.ui.nav.home"),
        Page::Output => a.t("launcher.heading.output"),
        Page::Plugins => a.t("launcher.heading.plugins"),
        Page::Capabilities => a.t("launcher.ui.nav.capabilities"),
        Page::Settings => a.t("launcher.ui.nav.settings"),
    }
}

/// Draw the whole window for one frame.
pub fn draw(a: &mut App, ui: &mut egui::Ui) {
    a.pump();
    let mut acts: Vec<Act> = Vec::new();
    egui::Panel::left("nav")
        .exact_size(132.0)
        .frame(egui::Frame::new().fill(theme::SIDE).inner_margin(12.0))
        .show(ui, |ui| {
            for p in PAGES {
                if ui.selectable_label(a.page == p, page_label(a, p)).clicked() {
                    acts.push(Act::Go(p));
                }
            }
            ui.add_space(12.0);
            if a.busy > 0 {
                ui.label(dim(a.t("launcher.ui.busy")));
            }
        });
    egui::CentralPanel::default()
        .frame(egui::Frame::new().fill(theme::BG).inner_margin(16.0))
        .show(ui, |ui| {
            egui::ScrollArea::vertical()
                .auto_shrink([false, false])
                .show(ui, |ui| match a.page {
                    Page::Home => home(a, ui, &mut acts),
                    Page::Output => output(a, ui),
                    Page::Plugins => plugins(a, ui, &mut acts),
                    Page::Capabilities => capabilities(a, ui, &mut acts),
                    Page::Settings => settings(a, ui, &mut acts),
                });
        });
    for act in acts {
        apply(a, act);
    }
}

fn apply(a: &mut App, act: Act) {
    match act {
        Act::Go(p) => {
            a.page = p;
            a.note = None;
            if p == Page::Plugins {
                a.rescan_plugins();
            }
        }
        Act::Job(j) => a.submit(j),
        Act::Toggle(id, on) => a.toggle_plugin(&id, on),
        Act::OpenPlugin(id) => a.open_plugin = id,
        Act::SetCap(i, on) => a.set_capability(&SWITCHES[i], on),
        Act::Language(l) => a.set_language(l),
        Act::CleanLogs(w) => a.set_clean_logs(w),
        Act::PluginsOn(on) => a.set_plugins_on(on),
        Act::OpenFolder(p) => a.backend.clone().open(&p),
        Act::Tab(t) => {
            a.plugin_tab = t;
            a.note = None;
            if t == PluginTab::Catalogue && a.catalogue.is_none() {
                a.submit(Job::CatalogueLoad);
            }
        }
    }
}

fn note_line(a: &App, ui: &mut egui::Ui) {
    if let Some(n) = &a.note {
        ui.add_space(6.0);
        ui.label(if n.error {
            theme::bad(n.text.clone())
        } else {
            theme::good(n.text.clone())
        });
    }
}

// ---- Home -----------------------------------------------------------------------------------

fn pc_items(rec: &[u8]) -> String {
    let mut buf = Vec::new();
    describe_pc_changes(rec, &mut buf);
    String::from_utf8_lossy(&buf)
        .lines()
        .map(|l| l.trim().trim_start_matches("- ").to_string())
        .filter(|l| !l.is_empty())
        .collect::<Vec<_>>()
        .join(", ")
}

fn status_rows(a: &App) -> Vec<(String, String, bool)> {
    let Some(s) = &a.status else {
        return vec![(
            a.t("launcher.heading.status"),
            a.t("launcher.status.checking"),
            false,
        )];
    };
    let game = if !s.folder_known {
        a.t("launcher.status.game_not_found")
    } else if s.build.is_empty() {
        a.t("launcher.status.game_found")
    } else {
        a.tf("launcher.status.game_found_build", &[("build", &s.build)])
    };
    let pc = if s.pc_left.is_empty() {
        a.t("launcher.status.pc_none")
    } else {
        let items = pc_items(&s.pc_left);
        let items = if items.is_empty() {
            a.t("launcher.status.pc_record")
        } else {
            items
        };
        a.tf("launcher.status.pc_left", &[("items", &items)])
    };
    let net = if s.wine {
        a.t("launcher.status.net_wine")
    } else if s.network_blocked {
        a.t("launcher.status.net_blocked")
    } else {
        a.t("launcher.status.net_open")
    };
    let eac = match s.eac {
        Eac::Active if s.eac_rename => a.t("launcher.status.eac_on_auto"),
        Eac::Active => a.t("launcher.status.eac_on"),
        Eac::Disabled => a.t("launcher.status.eac_off"),
        Eac::NotInstalled => a.t("launcher.status.eac_none"),
    };
    let plugins = if s.plugins_on {
        a.tf(
            "launcher.status.plugins_count",
            &[
                ("enabled", &s.plugins_enabled.to_string()),
                ("total", &s.plugins_total.to_string()),
            ],
        )
    } else {
        a.t("launcher.status.plugins_off")
    };
    vec![
        (a.t("launcher.status.game"), game, s.folder_known),
        (
            a.t("launcher.status.mod"),
            a.t(if s.mod_installed {
                "launcher.status.mod_installed"
            } else {
                "launcher.status.mod_not_installed"
            }),
            s.mod_installed,
        ),
        (a.t("launcher.status.pc_changes"), pc, s.pc_left.is_empty()),
        (
            a.t("launcher.status.network"),
            net,
            s.network_blocked || s.wine,
        ),
        (a.t("launcher.status.eac"), eac, s.eac != Eac::Active),
        (a.t("launcher.status.plugins"), plugins, s.plugins_on),
        (
            a.t("launcher.ui.home.sdk"),
            crate::catalogue::HOST_SDK.to_string(),
            true,
        ),
    ]
}

fn home(a: &mut App, ui: &mut egui::Ui, acts: &mut Vec<Act>) {
    let running = a.backend.game_running();
    let cmd_busy = a.command_running();
    // Repair banner: leftovers and the game is not running.
    if let Some(s) = &a.status {
        if !s.pc_left.is_empty() && !running && !cmd_busy {
            let items = pc_items(&s.pc_left);
            let items = if items.is_empty() {
                a.t("launcher.status.pc_record")
            } else {
                items
            };
            egui::Frame::new()
                .fill(theme::CARD)
                .stroke(egui::Stroke::new(1.0, theme::WARN))
                .corner_radius(6.0)
                .inner_margin(12.0)
                .show(ui, |ui| {
                    ui.set_width(ui.available_width());
                    ui.label(
                        RichText::new(a.t("launcher.repair.title"))
                            .color(theme::WARN)
                            .strong(),
                    );
                    ui.label(items);
                    ui.label(dim(a.t("launcher.repair.hint")));
                    if button(ui, &a.t("launcher.action.repair"), true) {
                        acts.push(Act::Job(Job::Command(Command::Repair)));
                    }
                });
            ui.add_space(10.0);
        }
    }
    // Light: the one-line verdict.
    let (title, detail, ok) = match &a.status {
        _ if running => (
            a.t("launcher.status.unsafe_running"),
            a.t("launcher.status.running_detail"),
            false,
        ),
        Some(s) if !s.pc_left.is_empty() => {
            let items = pc_items(&s.pc_left);
            let items = if items.is_empty() {
                a.t("launcher.status.pc_record")
            } else {
                items
            };
            (
                a.t("launcher.status.unsafe_leftovers"),
                a.tf("launcher.status.undo_detail", &[("items", &items)]),
                false,
            )
        }
        Some(s) if !s.folder_known => (
            a.t("launcher.status.unknown_folder"),
            a.t("launcher.status.unknown_folder_detail"),
            false,
        ),
        Some(_) => (
            a.t("launcher.status.safe_online"),
            a.t("launcher.status.safe_detail"),
            true,
        ),
        None => (a.t("launcher.status.checking"), String::new(), false),
    };
    ui.label(
        RichText::new(title)
            .size(18.0)
            .color(if ok { theme::GREEN } else { theme::WARN }),
    );
    ui.label(dim(detail));
    ui.add_space(10.0);

    ui.columns(2, |cols| {
        card(&mut cols[0], &a.t("launcher.heading.status"), |ui| {
            egui::Grid::new("status-grid")
                .num_columns(2)
                .spacing([14.0, 6.0])
                .show(ui, |ui| {
                    for (k, v, good) in status_rows(a) {
                        ui.label(dim(k));
                        ui.label(if good {
                            theme::good(v)
                        } else {
                            RichText::new(v)
                        });
                        ui.end_row();
                    }
                });
        });
        card(&mut cols[1], &a.t("launcher.heading.actions"), |ui| {
            let (word, command) = if running {
                ("launcher.action.stop", Command::Stop)
            } else {
                ("launcher.action.play", Command::Play)
            };
            if button(ui, &a.t(word), !cmd_busy || running) {
                acts.push(Act::Job(Job::Command(command)));
            }
            ui.add_space(6.0);
            if button(ui, &a.t("launcher.action.update"), !cmd_busy && !running) {
                acts.push(Act::Job(Job::Command(Command::Update)));
            }
            ui.add_space(6.0);
            if button(ui, &a.t("launcher.action.reload"), true) {
                acts.push(Act::Job(Job::Status));
            }
            if running {
                ui.add_space(6.0);
                ui.label(dim(a.t("launcher.ui.home.running")));
            }
        });
    });
    note_line(a, ui);
}

// ---- Output ---------------------------------------------------------------------------------

fn output(a: &mut App, ui: &mut egui::Ui) {
    ui.heading(a.t("launcher.heading.output"));
    ui.add_space(6.0);
    if a.output.is_empty() {
        ui.label(dim(a.t("launcher.ui.output.empty")));
        return;
    }
    if ui.button(a.t("launcher.action.copy")).clicked() {
        let text = a.output.join("\n");
        ui.ctx().copy_text(text);
        a.note(a.t("launcher.ui.output.copied"));
    }
    ui.add_space(6.0);
    egui::Frame::new()
        .fill(theme::SIDE)
        .corner_radius(6.0)
        .inner_margin(10.0)
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            for l in &a.output {
                ui.label(RichText::new(l).monospace());
            }
        });
    note_line(a, ui);
}

// ---- Plugins --------------------------------------------------------------------------------

fn kind_warning(a: &App, kind: &str) -> Option<String> {
    match kind {
        "native" => Some(a.t("launcher.ui.plugins.native_warning")),
        "lua" => Some(a.t("launcher.ui.plugins.lua_warning")),
        _ => None,
    }
}

fn plugins(a: &mut App, ui: &mut egui::Ui, acts: &mut Vec<Act>) {
    ui.horizontal(|ui| {
        ui.heading(a.t("launcher.heading.plugins"));
        ui.add_space(12.0);
        for (tab, key) in [
            (PluginTab::Installed, "launcher.ui.plugins.tab_installed"),
            (PluginTab::Catalogue, "launcher.ui.plugins.tab_catalogue"),
        ] {
            if ui.selectable_label(a.plugin_tab == tab, a.t(key)).clicked() {
                acts.push(Act::Tab(tab));
            }
        }
    });
    ui.add_space(6.0);
    match a.plugin_tab {
        PluginTab::Installed => installed_tab(a, ui, acts),
        PluginTab::Catalogue => catalogue_tab(a, ui, acts),
    }
    note_line(a, ui);
}

fn installed_tab(a: &mut App, ui: &mut egui::Ui, acts: &mut Vec<Act>) {
    if !a.cfg.plugins {
        ui.horizontal(|ui| {
            ui.label(theme::warn(a.t("launcher.plugins.off")));
            if ui.button(a.t("launcher.action.turn_on")).clicked() {
                acts.push(Act::PluginsOn(true));
            }
        });
        ui.add_space(6.0);
    }
    if let Some(open) = a.open_plugin.clone() {
        if let Some(p) = a.plugins.iter().find(|p| p.id == open) {
            plugin_settings(&*a, ui, p, acts);
            return;
        }
        acts.push(Act::OpenPlugin(None));
    }
    card(ui, &a.t("launcher.ui.plugins.add"), |ui| {
        ui.horizontal(|ui| {
            let hint = a.t("launcher.ui.plugins.add_hint");
            ui.add(
                egui::TextEdit::singleline(&mut a.add_path)
                    .hint_text(hint)
                    .desired_width(ui.available_width() - 190.0),
            );
            let can = !a.add_path.trim().is_empty() && a.busy == 0;
            if button(ui, &a.t("launcher.ui.plugins.add_button"), can) {
                acts.push(Act::Job(Job::AddPath(
                    a.add_path.trim().trim_matches('"').to_string(),
                )));
            }
        });
        if ui
            .small_button(a.t("launcher.hint.plugins_folder"))
            .clicked()
        {
            acts.push(Act::OpenFolder(crate::plugins::plugins_dir(&a.here)));
        }
    });
    ui.add_space(8.0);
    if a.plugins.is_empty() {
        ui.label(dim(a.t("launcher.plugins.empty")));
        return;
    }
    for p in &a.plugins {
        card(ui, "", |ui| {
            ui.horizontal(|ui| {
                let mut on = p.enabled;
                if ui.checkbox(&mut on, "").changed() {
                    acts.push(Act::Toggle(p.id.clone(), on));
                }
                let name = if p.ini.name.is_empty() {
                    p.id.clone()
                } else {
                    p.ini.name.clone()
                };
                ui.label(RichText::new(name).strong());
                ui.label(dim(format!("{} {}", p.ini.version, p.ini.author)));
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    if ui.button(a.t("launcher.hint.settings")).clicked() {
                        acts.push(Act::OpenPlugin(Some(p.id.clone())));
                    }
                    ui.label(dim(a.tf("launcher.plugins.kind", &[("kind", &p.ini.kind)])));
                });
            });
            if !p.ini.about.is_empty() {
                ui.label(dim(p.ini.about.clone()));
            }
        });
        ui.add_space(4.0);
    }
}

/// Plugin settings, read-only for now.
fn plugin_settings(a: &App, ui: &mut egui::Ui, p: &crate::plugins::PluginRow, acts: &mut Vec<Act>) {
    if ui.button(a.t("launcher.action.back")).clicked() {
        acts.push(Act::OpenPlugin(None));
    }
    ui.add_space(6.0);
    card(
        ui,
        &a.tf("launcher.plugins.settings", &[("plugin", &p.id)]),
        |ui| {
            ui.label(dim(a.t("launcher.ui.plugins.settings_readonly")));
            if let Some(w) = kind_warning(a, &p.ini.kind) {
                ui.label(theme::warn(w));
            }
            if p.ini.settings_bad {
                ui.label(theme::bad(a.t("launcher.plugins.settings_bad")));
            }
            ui.add_space(6.0);
            if p.settings.is_empty() {
                ui.label(dim(a.t("launcher.ui.plugins.no_settings")));
            }
            for s in &p.settings {
                ui.horizontal_wrapped(|ui| {
                    ui.label(RichText::new(&s.label).strong());
                    ui.label(dim(format!(
                        "{}  {}",
                        s.kind,
                        a.tf("launcher.ui.plugins.default", &[("value", &s.default)])
                    )));
                    if !s.range.is_empty() {
                        ui.label(dim(s.range.clone()));
                    }
                });
                if !s.help.is_empty() {
                    ui.label(dim(s.help.clone()));
                }
            }
        },
    );
}

fn catalogue_tab(a: &mut App, ui: &mut egui::Ui, acts: &mut Vec<Act>) {
    ui.horizontal(|ui| {
        if ui.button(a.t("launcher.ui.catalogue.reload")).clicked() {
            acts.push(Act::Job(Job::CatalogueLoad));
        }
    });
    ui.add_space(6.0);
    match &a.catalogue {
        None => {
            ui.label(dim(a.t("launcher.ui.busy")));
        }
        Some(Err(e)) if e.is_empty() => {
            ui.label(dim(a.t("launcher.ui.catalogue.none")));
        }
        Some(Err(e)) => {
            ui.label(theme::bad(
                a.tf("launcher.ui.catalogue.refused", &[("error", e)]),
            ));
        }
        Some(Ok(l)) => {
            ui.label(dim(a.tf(
                "launcher.ui.catalogue.entries",
                &[
                    ("sequence", &l.sequence.to_string()),
                    ("count", &l.rows.len().to_string()),
                    ("expires", &l.expires),
                ],
            )));
            ui.add_space(6.0);
            for r in &l.rows {
                let e = &r.entry;
                card(ui, "", |ui| {
                    ui.horizontal(|ui| {
                        ui.label(RichText::new(&e.name).strong());
                        ui.label(dim(format!(
                            "{} {} {}",
                            e.version,
                            e.kind.as_str(),
                            e.review.as_str()
                        )));
                        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                            if let Some(v) = &r.installed {
                                ui.label(theme::good(
                                    a.tf("launcher.ui.catalogue.installed", &[("version", v)]),
                                ));
                            } else if r.refused.is_none() {
                                let can = a.busy == 0;
                                if button(ui, &a.t("launcher.ui.catalogue.install"), can) {
                                    acts.push(Act::Job(Job::CatalogueInstall(e.id.clone())));
                                }
                            }
                        });
                    });
                    ui.label(dim(format!("{}  {}", e.id, e.license)));
                    if let Some(why) = &r.refused {
                        ui.label(theme::warn(
                            a.tf("launcher.ui.catalogue.incompatible", &[("reason", why)]),
                        ));
                    }
                    if let Some(w) = kind_warning(a, e.kind.as_str()) {
                        ui.label(theme::warn(w));
                    }
                });
                ui.add_space(4.0);
            }
        }
    }
}

// ---- Capabilities ---------------------------------------------------------------------------

/// Rows of `capabilities.json` that have no switch: (capability, evidence level, registry default).
pub fn registry_rows(here: &std::path::Path) -> Vec<(String, String, String)> {
    let text = std::fs::read_to_string(here.join("capabilities.json"))
        .unwrap_or_else(|_| scol_contract::CAPABILITIES_JSON.to_string());
    let Ok(v) = serde_json::from_str::<serde_json::Value>(&text) else {
        return Vec::new();
    };
    v.get("capabilities")
        .and_then(|c| c.as_array())
        .map(|rows| {
            rows.iter()
                .filter(|r| r.get("switch").is_none_or(|s| s.is_null()))
                .map(|r| {
                    let g = |k: &str| r.get(k).and_then(|x| x.as_str()).unwrap_or("").to_string();
                    (g("capability"), g("evidence_level"), g("registry_default"))
                })
                .collect()
        })
        .unwrap_or_default()
}

fn capabilities(a: &mut App, ui: &mut egui::Ui, acts: &mut Vec<Act>) {
    ui.heading(a.t("launcher.ui.nav.capabilities"));
    ui.label(dim(a.t("launcher.ui.caps.intro")));
    ui.label(theme::warn(a.t("launcher.ui.not_run_in_game")));
    ui.add_space(8.0);
    let editable = !a.backend.game_running() && !a.command_running();
    for (i, sw) in SWITCHES.iter().enumerate() {
        let mut on = a.cfg.capabilities.is_on(sw);
        card(ui, "", |ui| {
            ui.horizontal(|ui| {
                if ui
                    .add_enabled(
                        editable,
                        egui::Checkbox::new(&mut on, RichText::new(sw.ini_key).strong()),
                    )
                    .changed()
                {
                    acts.push(Act::SetCap(i, on));
                }
            });
            if sw.diagnostic_only {
                ui.label(theme::warn(a.t("launcher.ui.caps.diag")));
            }
            ui.label(dim(scol_config::Capabilities::status_text(sw)));
        });
        ui.add_space(4.0);
    }
    if !editable {
        ui.label(dim(a.t("launcher.plugins.close_to_edit")));
    }
    note_line(a, ui);
    ui.add_space(10.0);
    ui.label(RichText::new(a.t("launcher.ui.caps.registry")).strong());
    ui.add(
        egui::TextEdit::singleline(&mut a.cap_filter)
            .hint_text("filter")
            .desired_width(240.0),
    );
    let rows = registry_rows(&a.here);
    if rows.is_empty() {
        ui.label(dim(a.t("launcher.ui.caps.none")));
        return;
    }
    let f = a.cap_filter.to_ascii_lowercase();
    egui::Grid::new("registry-grid")
        .num_columns(3)
        .spacing([14.0, 4.0])
        .striped(true)
        .show(ui, |ui| {
            for (cap, ev, def) in rows
                .iter()
                .filter(|r| f.is_empty() || r.0.to_ascii_lowercase().contains(&f))
            {
                ui.label(cap);
                ui.label(dim(ev.clone()));
                ui.label(dim(def.clone()));
                ui.end_row();
            }
        });
}

// ---- Settings -------------------------------------------------------------------------------

fn settings(a: &mut App, ui: &mut egui::Ui, acts: &mut Vec<Act>) {
    use scol_config::{Language, LogChoice};
    ui.heading(a.t("launcher.ui.nav.settings"));
    ui.add_space(6.0);
    let editable = !a.backend.game_running() && !a.command_running();

    card(ui, &a.t("launcher.action.repair"), |ui| {
        let left = a.status.as_ref().is_some_and(|s| !s.pc_left.is_empty());
        if button(ui, &a.t("launcher.action.repair"), editable && left) {
            acts.push(Act::Job(Job::Command(Command::Repair)));
        }
        ui.label(dim(if left {
            a.t("launcher.status.pc_record")
        } else {
            a.t("launcher.repair.nothing")
        }));
        ui.label(dim(a.t("launcher.repair.hint")));
    });
    ui.add_space(8.0);

    card(ui, &a.t("launcher.settings.logs_label"), |ui| {
        ui.horizontal(|ui| {
            for (choice, word, key) in [
                (LogChoice::Ask, "ask", "launcher.settings.logs_ask"),
                (LogChoice::Always, "always", "launcher.action.always_delete"),
                (LogChoice::Never, "never", "launcher.action.never_delete"),
            ] {
                if ui
                    .add_enabled(
                        editable,
                        egui::Button::new(a.t(key)).selected(a.cfg.clean_logs == choice),
                    )
                    .clicked()
                {
                    acts.push(Act::CleanLogs(word));
                }
            }
        });
        ui.label(dim(a.t("launcher.settings.logs_hint")));
    });
    ui.add_space(8.0);

    card(ui, &a.t("launcher.ui.settings.language"), |ui| {
        ui.horizontal(|ui| {
            for (l, key) in [
                (Language::Auto, "launcher.language.auto"),
                (Language::English, "launcher.language.english"),
                (Language::Russian, "launcher.language.russian"),
            ] {
                if ui
                    .add_enabled(
                        editable,
                        egui::Button::new(a.t(key)).selected(a.cfg.language == l),
                    )
                    .clicked()
                {
                    acts.push(Act::Language(l));
                }
            }
        });
        ui.label(dim(a.t("launcher.language.hint")));
        if !editable {
            ui.label(dim(a.t("launcher.language.busy")));
        }
    });
    ui.add_space(8.0);

    card(ui, &a.t("launcher.heading.plugins"), |ui| {
        let mut on = a.cfg.plugins;
        if ui
            .add_enabled(editable, egui::Checkbox::new(&mut on, "plugins"))
            .changed()
        {
            acts.push(Act::PluginsOn(on));
        }
        ui.label(dim(a.t("launcher.plugins.applies_next_start")));
    });
    ui.add_space(8.0);

    card(ui, &a.t("launcher.ui.settings.ini_path"), |ui| {
        ui.label(RichText::new(a.ini_path().display().to_string()).monospace());
        if ui.button(a.t("launcher.action.open_ini")).clicked() {
            acts.push(Act::OpenFolder(a.ini_path()));
        }
    });
    note_line(a, ui);
}
