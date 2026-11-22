// SPDX-License-Identifier: GPL-3.0-only
//! The Plugins page: the installed list (on or off, settings, remove), adding from a folder or zip,
//! and the Catalogue tab. Plugin code is never loaded here. Everything that lands is switched off.

use eframe::egui::{self, RichText};

use crate::app::{App, Job, PluginTab};
use crate::catalogue::State;
use crate::pages::{note_line, Act};
use crate::plugins::PluginRow;
use crate::theme::{self, button, card, dim, plain};

pub fn kind_label(a: &App, kind: &str) -> String {
    match kind {
        "native" => a.t("launcher.ui.plugins.kind.native"),
        "lua" => a.t("launcher.ui.plugins.kind.lua"),
        "data" => a.t("launcher.ui.plugins.kind.data"),
        other => other.to_string(),
    }
}

fn kind_warning(a: &App, kind: &str) -> Option<String> {
    match kind {
        "native" => Some(a.t("launcher.ui.plugins.native_warning")),
        "lua" => Some(a.t("launcher.ui.plugins.lua_warning")),
        _ => None,
    }
}

pub fn draw(a: &mut App, ui: &mut egui::Ui, acts: &mut Vec<Act>) {
    ui.horizontal(|ui| {
        ui.label(
            RichText::new(a.t("launcher.heading.plugins"))
                .heading()
                .strong(),
        );
        ui.add_space(12.0);
        let all = [
            (PluginTab::Installed, "launcher.ui.plugins.tab_installed"),
            (PluginTab::Catalogue, "launcher.ui.plugins.tab_catalogue"),
        ];
        let options: Vec<(String, bool)> = all
            .iter()
            .map(|(tab, key)| (a.t(key), a.plugin_tab == *tab))
            .collect();
        if let Some(i) = theme::segmented(ui, &options, true) {
            acts.push(Act::Tab(all[i].0));
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
    if let Some(open) = a.open_plugin.clone() {
        if let Some(p) = a.plugins.iter().find(|p| p.id == open).cloned() {
            settings(a, ui, &p, acts);
            return;
        }
        acts.push(Act::OpenPlugin(None));
    }
    let editable = a.editable();
    if !a.cfg.plugins {
        ui.horizontal_wrapped(|ui| {
            ui.label(theme::warn(a.t("launcher.plugins.off")));
            if plain(ui, &a.t("launcher.action.turn_on"), editable) {
                acts.push(Act::PluginsOn(true));
            }
        });
        ui.add_space(6.0);
    }
    if !editable {
        ui.label(dim(a.t("launcher.plugins.close_to_edit")));
        ui.add_space(4.0);
    }
    add_card(a, ui, acts, editable);
    ui.add_space(8.0);
    if a.plugins.is_empty() {
        ui.label(dim(a.t("launcher.plugins.empty")));
        return;
    }
    let rows = a.plugins.clone();
    for p in &rows {
        plugin_card(a, ui, p, acts, editable);
        ui.add_space(4.0);
    }
}

fn add_card(a: &mut App, ui: &mut egui::Ui, acts: &mut Vec<Act>, editable: bool) {
    card(ui, &a.t("launcher.ui.plugins.add"), |ui| {
        ui.horizontal(|ui| {
            let hint = a.t("launcher.ui.plugins.add_hint");
            let w = (ui.available_width() - 270.0).max(120.0);
            ui.add(
                egui::TextEdit::singleline(&mut a.add_path)
                    .hint_text(hint)
                    .desired_width(w),
            );
            if plain(ui, &a.t("launcher.ui.plugins.browse"), editable) {
                acts.push(Act::Browse);
            }
            let can = editable && !a.add_path.trim().is_empty() && a.busy == 0;
            if button(ui, &a.t("launcher.ui.plugins.add_button"), can) {
                acts.push(Act::Job(Job::AddPath(
                    a.add_path.trim().trim_matches('"').to_string(),
                )));
            }
        });
        ui.add(egui::Label::new(dim(a.t("launcher.ui.plugins.add_note"))).wrap());
        ui.add_space(2.0);
        if theme::plain(ui, &a.t("launcher.hint.plugins_folder"), true) {
            acts.push(Act::OpenPath(crate::plugins::plugins_dir(&a.here)));
        }
    });
}

fn plugin_card(a: &App, ui: &mut egui::Ui, p: &PluginRow, acts: &mut Vec<Act>, editable: bool) {
    card(ui, "", |ui| {
        ui.horizontal(|ui| {
            let name = if p.ini.name.is_empty() {
                p.id.clone()
            } else {
                p.ini.name.clone()
            };
            if theme::toggle(
                ui,
                p.enabled,
                editable,
                &name,
                &a.t("launcher.plugins.state_on"),
                &a.t("launcher.plugins.state_off"),
            ) {
                acts.push(Act::Toggle(p.id.clone(), !p.enabled));
            }
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                if plain(ui, &a.t("launcher.ui.plugins.remove"), editable) {
                    acts.push(Act::AskRemove(Some(p.id.clone())));
                }
                if plain(ui, &a.t("launcher.hint.settings"), true) {
                    acts.push(Act::OpenPlugin(Some(p.id.clone())));
                }
                // What is left of the row is the name; it is cut with dots when it does not fit.
                ui.with_layout(egui::Layout::left_to_right(egui::Align::Center), |ui| {
                    ui.add(egui::Label::new(RichText::new(name).strong()).truncate());
                });
            });
        });
        if !p.ini.about.is_empty() {
            ui.add(egui::Label::new(theme::body(p.ini.about.clone())).wrap());
        }
        let source = a.t(if p.source == "catalogue" {
            "launcher.ui.plugins.from_catalogue"
        } else {
            "launcher.ui.plugins.from_hand"
        });
        ui.add(
            egui::Label::new(dim(format!(
                "{} {}  |  {}  |  {}",
                p.ini.version,
                p.ini.author,
                kind_label(a, &p.ini.kind),
                source
            )))
            .wrap(),
        );
        match p.ini.kind.as_str() {
            "native" => {
                ui.add(
                    egui::Label::new(theme::warn(a.t("launcher.ui.plugins.native_warning_on")))
                        .wrap(),
                );
            }
            "lua" => {
                ui.add(egui::Label::new(dim(a.t("launcher.ui.plugins.lua_warning_on"))).wrap());
            }
            _ => {}
        }
        if let Some(why) = &p.problem {
            ui.add(
                egui::Label::new(theme::bad(
                    a.tf("launcher.ui.plugins.problem", &[("problem", why)]),
                ))
                .wrap(),
            );
        }
        if a.confirm_remove.as_deref() == Some(p.id.as_str()) {
            ui.add_space(4.0);
            ui.add(
                egui::Label::new(theme::warn(
                    a.tf("launcher.ui.plugins.remove_ask", &[("plugin", &p.id)]),
                ))
                .wrap(),
            );
            ui.horizontal(|ui| {
                if plain(
                    ui,
                    &a.t("launcher.ui.plugins.remove_yes"),
                    editable && a.busy == 0,
                ) {
                    acts.push(Act::AskRemove(None));
                    acts.push(Act::Job(Job::Remove(p.id.clone())));
                }
                if plain(ui, &a.t("launcher.ui.plugins.remove_no"), true) {
                    acts.push(Act::AskRemove(None));
                }
            });
        }
    });
}

/// Settings of one plugin: current value, editable, saved through the backend.
fn settings(a: &mut App, ui: &mut egui::Ui, p: &PluginRow, acts: &mut Vec<Act>) {
    ui.horizontal(|ui| {
        if plain(ui, &a.t("launcher.action.back"), true) {
            acts.push(Act::OpenPlugin(None));
        }
        let name = if p.ini.name.is_empty() {
            &p.id
        } else {
            &p.ini.name
        };
        ui.label(RichText::new(a.tf("launcher.plugins.settings", &[("plugin", name)])).strong());
    });
    ui.add_space(6.0);
    let editable = a.editable();
    if p.ini.kind == "native" {
        ui.add(egui::Label::new(theme::warn(a.t("launcher.ui.plugins.native_warning_on"))).wrap());
    }
    if p.ini.settings_bad {
        ui.add(egui::Label::new(theme::bad(a.t("launcher.plugins.settings_bad"))).wrap());
    }
    if p.settings.is_empty() {
        ui.add_space(4.0);
        ui.label(dim(a.t("launcher.ui.plugins.no_settings")));
        note_line(a, ui);
        return;
    }
    if !editable {
        ui.label(dim(a.t("launcher.plugins.close_to_edit")));
    }
    ui.add_space(4.0);
    let mut changed: Vec<(String, String)> = Vec::new();
    let (on_word, off_word) = (
        a.t("launcher.plugins.state_on"),
        a.t("launcher.plugins.state_off"),
    );
    // The drafts are edited while the page reads other parts of the app: take them out and put them back.
    let mut drafts = std::mem::take(&mut a.drafts);
    for s in &p.settings {
        let draft = drafts
            .entry(s.name.clone())
            .or_insert_with(|| s.value.clone());
        card(ui, "", |ui| {
            ui.horizontal(|ui| {
                ui.label(RichText::new(&s.label).strong());
                ui.with_layout(
                    egui::Layout::right_to_left(egui::Align::Center),
                    |ui| match s.kind.as_str() {
                        "bool" => {
                            let on = draft == "true";
                            if theme::toggle(ui, on, editable, &s.label, &on_word, &off_word) {
                                *draft = if on { "false" } else { "true" }.to_string();
                            }
                        }
                        "enum" => {
                            let choices: Vec<String> = s
                                .range
                                .split('|')
                                .map(|c| c.trim().to_string())
                                .filter(|c| !c.is_empty())
                                .collect();
                            ui.add_enabled_ui(editable, |ui| {
                                egui::ComboBox::from_id_salt(("setting", &p.id, &s.name))
                                    .selected_text(draft.clone())
                                    .show_ui(ui, |ui| {
                                        for c in &choices {
                                            ui.selectable_value(draft, c.clone(), c);
                                        }
                                    });
                            });
                        }
                        _ => {
                            ui.add_enabled(
                                editable,
                                egui::TextEdit::singleline(draft).desired_width(150.0),
                            );
                        }
                    },
                );
            });
            let sub = if s.help.is_empty() {
                a.tf("launcher.ui.plugins.default", &[("value", &s.default)])
            } else {
                s.help.clone()
            };
            ui.add(egui::Label::new(dim(sub)).wrap());
            if !s.range.is_empty() && s.kind != "enum" {
                ui.label(dim(s.range.clone()));
            }
        });
        if *draft != s.value {
            changed.push((s.name.clone(), draft.clone()));
        }
        ui.add_space(4.0);
    }
    a.drafts = drafts;
    ui.horizontal(|ui| {
        let can = editable && !changed.is_empty() && a.busy == 0;
        if button(ui, &a.t("launcher.action.save"), can) {
            acts.push(Act::Job(Job::SaveSettings(p.id.clone(), changed.clone())));
        }
        ui.label(dim(a.t("launcher.plugins.applies_next_start")));
    });
    note_line(a, ui);
}

fn catalogue_tab(a: &mut App, ui: &mut egui::Ui, acts: &mut Vec<Act>) {
    if plain(ui, &a.t("launcher.ui.catalogue.reload"), true) {
        acts.push(Act::Job(Job::CatalogueLoad));
    }
    ui.add_space(6.0);
    let editable = a.editable();
    match a.catalogue.clone() {
        None => {
            ui.label(dim(a.t("launcher.ui.busy")));
        }
        Some(State::NoCatalogue) => {
            card(ui, "", |ui| {
                ui.label(RichText::new(a.t("launcher.ui.catalogue.none_published")).strong());
                ui.add(
                    egui::Label::new(dim(a.t("launcher.ui.catalogue.none_published_why"))).wrap(),
                );
            });
        }
        Some(State::NoIndex) => {
            ui.add(egui::Label::new(dim(a.t("launcher.ui.catalogue.none"))).wrap());
        }
        Some(State::Refused(e)) => {
            ui.add(
                egui::Label::new(theme::bad(
                    a.tf("launcher.ui.catalogue.refused", &[("error", &e)]),
                ))
                .wrap(),
            );
        }
        Some(State::Loaded(l)) => {
            ui.add(
                egui::Label::new(dim(a.tf(
                    "launcher.ui.catalogue.entries",
                    &[
                        ("sequence", &l.sequence.to_string()),
                        ("count", &l.rows.len().to_string()),
                        ("expires", l.expires.get(..10).unwrap_or(&l.expires)),
                    ],
                )))
                .wrap(),
            );
            ui.add_space(6.0);
            for r in &l.rows {
                let e = &r.entry;
                card(ui, "", |ui| {
                    ui.horizontal(|ui| {
                        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                            if let Some(v) = &r.installed {
                                ui.label(theme::good(
                                    a.tf("launcher.ui.catalogue.installed", &[("version", v)]),
                                ));
                            } else if r.refused.is_none() {
                                let can = editable && a.busy == 0;
                                if plain(ui, &a.t("launcher.ui.catalogue.install"), can) {
                                    acts.push(Act::Job(Job::CatalogueInstall(e.id.clone())));
                                }
                            }
                            ui.with_layout(
                                egui::Layout::left_to_right(egui::Align::Center),
                                |ui| {
                                    ui.add(
                                        egui::Label::new(RichText::new(&e.name).strong())
                                            .truncate(),
                                    );
                                },
                            );
                        });
                    });
                    ui.add(
                        egui::Label::new(dim(format!(
                            "{}  |  {}  |  {}  |  {}",
                            e.version,
                            kind_label(a, e.kind.as_str()),
                            e.review.as_str(),
                            e.license
                        )))
                        .wrap(),
                    );
                    if let Some(why) = &r.refused {
                        ui.add(
                            egui::Label::new(theme::warn(
                                a.tf("launcher.ui.catalogue.incompatible", &[("reason", why)]),
                            ))
                            .wrap(),
                        );
                    }
                    if let Some(w) = kind_warning(a, e.kind.as_str()) {
                        ui.add(egui::Label::new(theme::warn(w)).wrap());
                    }
                });
                ui.add_space(4.0);
            }
        }
    }
}
