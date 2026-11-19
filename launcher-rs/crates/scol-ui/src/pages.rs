// SPDX-License-Identifier: GPL-3.0-only
//! The frame around the pages, Home and Output. Plugins, Capabilities and Settings are in
//! `page_plugins`, `page_caps` and `page_settings`. Drawing only reads `App` and records what the
//! player asked for; the actions run after the frame (`Act`), so no widget closure borrows the app
//! twice.
//!
//! Keyboard: Tab walks the side list first and then the page, in reading order. Ctrl+1 to Ctrl+5 jump
//! to a page, F5 reads the status again.

use eframe::egui::{self, Color32, Key, Modifiers, RichText};
use scol_config::MenuChoice;
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
    AskRemove(Option<String>),
    SetCap(usize, bool),
    Language(scol_config::Language),
    CleanLogs(&'static str),
    PluginsOn(bool),
    Menu(MenuChoice),
    CheckUpdates(bool),
    OpenPath(std::path::PathBuf),
    Tab(PluginTab),
    Browse,
    CapsMore(bool),
    CapsAuto(bool),
}

pub fn page_label(a: &App, p: Page) -> String {
    match p {
        Page::Home => a.t("launcher.ui.nav.home"),
        Page::Output => a.t("launcher.heading.output"),
        Page::Plugins => a.t("launcher.heading.plugins"),
        Page::Capabilities => a.t("launcher.ui.nav.capabilities"),
        Page::Settings => a.t("launcher.ui.nav.settings"),
    }
}

fn nav_button(ui: &mut egui::Ui, text: &str, selected: bool) -> bool {
    let rt = if selected {
        RichText::new(text).color(theme::GREEN).strong()
    } else {
        RichText::new(text).color(theme::BODY)
    };
    let fill = if selected {
        theme::ROW_HOVER
    } else {
        Color32::TRANSPARENT
    };
    let b = egui::Button::new(rt)
        .fill(fill)
        .min_size(egui::vec2(ui.available_width(), 32.0));
    ui.add(b).clicked()
}

/// Draw the whole window for one frame.
pub fn draw(a: &mut App, ui: &mut egui::Ui) {
    a.pump();
    a.game_up = a.backend.game_running();
    let mut acts: Vec<Act> = Vec::new();
    // Ctrl+1..5 and F5.
    for (i, key) in [Key::Num1, Key::Num2, Key::Num3, Key::Num4, Key::Num5]
        .into_iter()
        .enumerate()
    {
        if ui.input_mut(|inp| inp.consume_key(Modifiers::COMMAND, key)) {
            acts.push(Act::Go(PAGES[i]));
        }
    }
    if ui.input_mut(|inp| inp.consume_key(Modifiers::NONE, Key::F5)) {
        acts.push(Act::Job(Job::Status));
    }
    egui::Panel::left("nav")
        .exact_size(150.0)
        .frame(egui::Frame::new().fill(theme::SIDE).inner_margin(12.0))
        .show(ui, |ui| {
            ui.label(RichText::new("sc-offline").strong().size(16.0));
            ui.label(dim(format!("v{}", scol_install::version::SCO_VERSION)));
            ui.add_space(14.0);
            for p in PAGES {
                if nav_button(ui, &page_label(a, p), a.page == p) {
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
            let mut area = egui::ScrollArea::vertical()
                .id_salt(("page", a.page as usize))
                .auto_shrink([false, false])
                .stick_to_bottom(a.page == Page::Output && a.scroll_to.is_none());
            if let Some(y) = a.scroll_to {
                area = area.vertical_scroll_offset(y);
            }
            area.show(ui, |ui| match a.page {
                Page::Home => home(a, ui, &mut acts),
                Page::Output => output(a, ui),
                Page::Plugins => crate::page_plugins::draw(a, ui, &mut acts),
                Page::Capabilities => crate::page_caps::draw(a, ui, &mut acts),
                Page::Settings => crate::page_settings::draw(a, ui, &mut acts),
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
            a.confirm_remove = None;
            if p == Page::Plugins {
                a.rescan_plugins();
            }
        }
        Act::Job(j) => a.submit(j),
        Act::Toggle(id, on) => a.toggle_plugin(&id, on),
        Act::OpenPlugin(id) => {
            a.open_plugin = id;
            a.drafts.clear();
            a.note = None;
        }
        Act::AskRemove(id) => a.confirm_remove = id,
        Act::SetCap(i, on) => a.set_capability(&scol_contract::capability_env::SWITCHES[i], on),
        Act::Language(l) => a.set_language(l),
        Act::CleanLogs(w) => a.set_clean_logs(w),
        Act::PluginsOn(on) => a.set_plugins_on(on),
        Act::Menu(m) => a.set_menu(m),
        Act::CheckUpdates(on) => a.set_check_updates(on),
        Act::OpenPath(p) => a.backend.clone().open(&p),
        Act::Tab(t) => {
            a.plugin_tab = t;
            a.note = None;
            if t == PluginTab::Catalogue && a.catalogue.is_none() {
                a.submit(Job::CatalogueLoad);
            }
        }
        Act::Browse => a.browse(),
        Act::CapsMore(v) => a.caps_more = v,
        Act::CapsAuto(v) => a.caps_auto = v,
    }
}

pub fn note_line(a: &App, ui: &mut egui::Ui) {
    if let Some(n) = &a.note {
        ui.add_space(6.0);
        ui.add(
            egui::Label::new(if n.error {
                theme::bad(n.text.clone())
            } else {
                theme::good(n.text.clone())
            })
            .wrap(),
        );
    }
}

/// A page title with the small line under it.
pub fn title(ui: &mut egui::Ui, text: &str, sub: &str) {
    ui.label(RichText::new(text).heading().strong());
    if !sub.is_empty() {
        ui.add(egui::Label::new(theme::body(sub)).wrap());
    }
    ui.add_space(8.0);
}

// ---- Home -----------------------------------------------------------------------------------

pub fn pc_items(rec: &[u8]) -> String {
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
    let running = a.game_up;
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
                .corner_radius(8.0)
                .inner_margin(12.0)
                .show(ui, |ui| {
                    ui.set_width(ui.available_width());
                    ui.label(
                        RichText::new(a.t("launcher.repair.title"))
                            .color(theme::WARN)
                            .strong(),
                    );
                    ui.add(egui::Label::new(items).wrap());
                    ui.add(egui::Label::new(dim(a.t("launcher.repair.hint"))).wrap());
                    ui.add_space(4.0);
                    if button(ui, &a.t("launcher.action.repair"), true) {
                        acts.push(Act::Job(Job::Command(Command::Repair)));
                    }
                });
            ui.add_space(10.0);
        }
    }
    // Light: the one-line verdict.
    let (head, detail, ok) = match &a.status {
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
    ui.label(RichText::new(head).size(19.0).strong().color(if ok {
        theme::GREEN
    } else {
        theme::WARN
    }));
    ui.add(egui::Label::new(dim(detail)).wrap());
    ui.add_space(10.0);

    ui.columns(2, |cols| {
        card(&mut cols[0], &a.t("launcher.heading.status"), |ui| {
            egui::Grid::new("status-grid")
                .num_columns(2)
                .spacing([14.0, 6.0])
                .show(ui, |ui| {
                    for (k, v, good) in status_rows(a) {
                        ui.label(dim(k));
                        ui.add(
                            egui::Label::new(if good {
                                theme::good(v)
                            } else {
                                RichText::new(v)
                            })
                            .wrap(),
                        );
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
            ui.add_space(4.0);
            if theme::plain(ui, &a.t("launcher.action.update"), !cmd_busy && !running) {
                acts.push(Act::Job(Job::Command(Command::Update)));
            }
            if theme::plain(ui, &a.t("launcher.action.reload"), true) {
                acts.push(Act::Job(Job::Status));
            }
            if running {
                ui.add_space(4.0);
                ui.add(egui::Label::new(dim(a.t("launcher.ui.home.running"))).wrap());
            }
        });
    });
    note_line(a, ui);
}

// ---- Output ---------------------------------------------------------------------------------

fn output(a: &mut App, ui: &mut egui::Ui) {
    ui.horizontal(|ui| {
        ui.label(
            RichText::new(a.t("launcher.heading.output"))
                .heading()
                .strong(),
        );
        if a.command_running() {
            ui.add(egui::Spinner::new().size(14.0).color(theme::GREEN));
        }
        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
            if theme::plain(ui, &a.t("launcher.action.copy"), !a.output.is_empty()) {
                let text = a.output.join("\n");
                ui.ctx().copy_text(text);
                a.note(a.t("launcher.ui.output.copied"));
            }
        });
    });
    ui.add_space(6.0);
    if a.output.is_empty() {
        ui.label(dim(a.t("launcher.ui.output.empty")));
        return;
    }
    egui::Frame::new()
        .fill(theme::SIDE)
        .corner_radius(8.0)
        .inner_margin(10.0)
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            for l in &a.output {
                let colour = if l.starts_with("[!]") || l.contains("Failed") {
                    theme::WARN
                } else if l.starts_with('>') {
                    theme::GREEN
                } else {
                    theme::BODY
                };
                ui.add(egui::Label::new(RichText::new(l).monospace().color(colour)).wrap());
            }
        });
    note_line(a, ui);
}
