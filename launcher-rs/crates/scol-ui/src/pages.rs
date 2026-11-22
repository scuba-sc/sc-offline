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
use crate::backend::{Command, PromptKind};
use crate::theme::{self, button, card, dim, Icon, Kind};

/// The width of the side list.
const SIDE_WIDTH: f32 = 172.0;

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
    /// The answer to the question a running command asked: the word it reads.
    Answer(&'static str),
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

fn page_icon(p: Page) -> Icon {
    match p {
        Page::Home => Icon::Home,
        Page::Output => Icon::Output,
        Page::Plugins => Icon::Plugins,
        Page::Capabilities => Icon::Capabilities,
        Page::Settings => Icon::Settings,
    }
}

/// One line of the side list: a picture and the page name. The page that is open has a filled row and
/// a green bar. `mark` draws a small dot at the right (a question is waiting on that page).
fn nav_button(ui: &mut egui::Ui, text: &str, icon: Icon, selected: bool, mark: bool) -> bool {
    let size = egui::vec2(ui.available_width(), 34.0);
    let (rect, mut r) = ui.allocate_exact_size(size, egui::Sense::click());
    r.widget_info(|| egui::WidgetInfo::selected(egui::Role::Button, true, selected, text));
    if ui.is_rect_visible(rect) {
        let p = ui.painter();
        let fill = if selected {
            theme::ROW
        } else if r.hovered() || r.has_focus() {
            theme::CARD
        } else {
            Color32::TRANSPARENT
        };
        p.rect_filled(rect, 8.0, fill);
        let fg = if selected { theme::TEXT } else { theme::BODY };
        if selected {
            let bar = egui::Rect::from_min_max(
                egui::pos2(rect.left(), rect.top() + 9.0),
                egui::pos2(rect.left() + 3.0, rect.bottom() - 9.0),
            );
            p.rect_filled(bar, 1.5, theme::GREEN);
        }
        theme::paint_icon(
            p,
            egui::pos2(rect.left() + 13.0, rect.center().y - 8.0),
            icon,
            if selected { theme::GREEN } else { theme::DIM },
        );
        let galley = p.layout_no_wrap(text.to_string(), egui::FontId::proportional(14.0), fg);
        p.galley(
            egui::pos2(rect.left() + 39.0, rect.center().y - galley.size().y / 2.0),
            galley,
            fg,
        );
        if mark {
            p.circle_filled(
                egui::pos2(rect.right() - 12.0, rect.center().y),
                3.5,
                theme::WARN,
            );
        }
        if r.has_focus() {
            p.rect_stroke(
                rect,
                8.0,
                egui::Stroke::new(1.5, theme::GREEN),
                egui::StrokeKind::Inside,
            );
        }
    }
    r = r.on_hover_cursor(egui::CursorIcon::PointingHand);
    r.clicked()
}

/// The mark and the name at the top of the side list.
fn brand(ui: &mut egui::Ui) {
    ui.horizontal(|ui| {
        let (rect, _) = ui.allocate_exact_size(egui::vec2(30.0, 30.0), egui::Sense::hover());
        let p = ui.painter();
        p.rect_filled(rect, 8.0, theme::GREEN);
        // A planet and a moon that went its own way.
        p.circle_stroke(
            rect.center(),
            7.0,
            egui::Stroke::new(2.0, theme::GREEN_TEXT),
        );
        p.circle_filled(
            rect.center() + egui::vec2(7.5, -7.5),
            2.6,
            theme::GREEN_TEXT,
        );
        ui.add_space(2.0);
        ui.vertical(|ui| {
            ui.spacing_mut().item_spacing.y = 0.0;
            ui.label(RichText::new("sc-offline").strong().size(15.0));
            ui.label(dim(format!("v{}", scol_install::version::SCO_VERSION)).size(11.5));
        });
    });
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
    // Y and N answer an open question, as in a console (not while a text field has the keyboard).
    if a.prompt.is_some() && !ui.ctx().egui_wants_keyboard_input() {
        for (key, word) in [(Key::Y, "y"), (Key::N, "n")] {
            if ui.input_mut(|inp| inp.consume_key(Modifiers::NONE, key)) {
                acts.push(Act::Answer(word));
                break;
            }
        }
    }
    egui::Panel::left("nav")
        .exact_size(SIDE_WIDTH)
        .frame(egui::Frame::new().fill(theme::SIDE).inner_margin(12.0))
        .show(ui, |ui| {
            brand(ui);
            ui.add_space(16.0);
            ui.spacing_mut().item_spacing.y = 3.0;
            for p in PAGES {
                let mark = p == Page::Output && a.prompt.is_some() && a.page != Page::Output;
                if nav_button(ui, &page_label(a, p), page_icon(p), a.page == p, mark) {
                    acts.push(Act::Go(p));
                }
            }
            // The foot of the list: is the game running, and is anything being worked on.
            ui.with_layout(egui::Layout::bottom_up(egui::Align::Min), |ui| {
                ui.spacing_mut().item_spacing.y = 6.0;
                ui.horizontal(|ui| {
                    let (colour, key) = if a.game_up {
                        (theme::GREEN, "launcher.ui.side.game_running")
                    } else {
                        (theme::DIM, "launcher.ui.side.game_closed")
                    };
                    theme::dot(ui, colour, 12.0);
                    ui.label(dim(a.t(key)).size(12.0));
                });
                if a.busy > 0 {
                    ui.horizontal(|ui| {
                        ui.add(egui::Spinner::new().size(12.0).color(theme::GREEN));
                        ui.label(dim(a.t("launcher.ui.busy")).size(12.0));
                    });
                }
            });
        });
    egui::CentralPanel::default()
        .frame(egui::Frame::new().fill(theme::BG).inner_margin(20.0))
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
                Page::Output => output(a, ui, &mut acts),
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
        Act::Answer(word) => a.answer(word),
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
        ui.add_space(2.0);
        ui.add(egui::Label::new(theme::body(sub)).wrap());
    }
    ui.add_space(12.0);
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

/// How a status line reads: fine, nothing to say, or something the player should look at.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Mark {
    Good,
    Plain,
    Look,
}

impl Mark {
    fn colour(self) -> Color32 {
        match self {
            Mark::Good => theme::GREEN,
            Mark::Plain => theme::DIM,
            Mark::Look => theme::WARN,
        }
    }
}

/// The lines of the Status card: name, value and how it reads.
pub fn status_rows(a: &App) -> Vec<(String, String, Mark)> {
    let Some(s) = &a.status else {
        return vec![(
            a.t("launcher.heading.status"),
            a.t("launcher.status.checking"),
            Mark::Plain,
        )];
    };
    let good = |ok: bool| if ok { Mark::Good } else { Mark::Plain };
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
    let (eac, eac_mark) = match s.eac {
        Eac::Active if s.eac_rename => (a.t("launcher.status.eac_on_auto"), Mark::Good),
        Eac::Active => (a.t("launcher.status.eac_on"), Mark::Look),
        Eac::Disabled => (a.t("launcher.status.eac_off"), Mark::Good),
        Eac::NotInstalled => (a.t("launcher.status.eac_none"), Mark::Good),
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
        (
            a.t("launcher.status.game"),
            game,
            if s.folder_known {
                Mark::Good
            } else {
                Mark::Look
            },
        ),
        (
            a.t("launcher.status.mod"),
            a.t(if s.mod_installed {
                "launcher.status.mod_installed"
            } else {
                "launcher.status.mod_not_installed"
            }),
            // In the game folder while nothing runs is a leftover; out of it is the resting state.
            if s.mod_installed && !a.game_up {
                Mark::Look
            } else {
                Mark::Plain
            },
        ),
        (
            a.t("launcher.status.pc_changes"),
            pc,
            if s.pc_left.is_empty() {
                Mark::Good
            } else {
                Mark::Look
            },
        ),
        (
            a.t("launcher.status.network"),
            net,
            good(s.network_blocked || s.wine),
        ),
        (a.t("launcher.status.eac"), eac, eac_mark),
        (a.t("launcher.status.plugins"), plugins, good(s.plugins_on)),
        (
            a.t("launcher.ui.home.sdk"),
            crate::catalogue::HOST_SDK.to_string(),
            Mark::Plain,
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
            theme::card_frame()
                .stroke(egui::Stroke::new(1.0, theme::WARN))
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
            ui.add_space(12.0);
        }
    }
    // The one-line verdict.
    let (head, detail, mark) = match &a.status {
        _ if running => (
            a.t("launcher.status.unsafe_running"),
            a.t("launcher.status.running_detail"),
            Mark::Look,
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
                Mark::Look,
            )
        }
        Some(s) if !s.folder_known => (
            a.t("launcher.status.unknown_folder"),
            a.t("launcher.status.unknown_folder_detail"),
            Mark::Look,
        ),
        Some(_) => (
            a.t("launcher.status.safe_online"),
            a.t("launcher.status.safe_detail"),
            Mark::Good,
        ),
        None => (a.t("launcher.status.checking"), String::new(), Mark::Plain),
    };
    // The verdict on the left, Play (or Stop) on the right, the smaller actions under them.
    theme::card_frame().inner_margin(18.0).show(ui, |ui| {
        ui.set_width(ui.available_width());
        let (word, command, kind) = if running {
            ("launcher.action.stop", Command::Stop, Kind::Halt)
        } else {
            ("launcher.action.play", Command::Play, Kind::Go)
        };
        let main = egui::vec2(176.0, 50.0);
        ui.horizontal(|ui| {
            let text_width = (ui.available_width() - main.x - 16.0).max(120.0);
            ui.allocate_ui_with_layout(
                egui::vec2(text_width, main.y),
                egui::Layout::top_down(egui::Align::Min),
                |ui| {
                    ui.set_width(text_width);
                    ui.horizontal(|ui| {
                        theme::dot(ui, mark.colour(), 18.0);
                        ui.label(RichText::new(head).size(21.0).strong().color(
                            if mark == Mark::Plain {
                                theme::TEXT
                            } else {
                                mark.colour()
                            },
                        ));
                    });
                    if !detail.is_empty() {
                        ui.horizontal(|ui| {
                            // Under the words of the verdict, not under its dot.
                            ui.add_space(18.0);
                            ui.add(egui::Label::new(theme::body(detail)).wrap());
                        });
                    }
                },
            );
            if theme::draw_button(ui, &a.t(word), kind, !cmd_busy || running, main, 18.0) {
                acts.push(Act::Job(Job::Command(command)));
            }
        });
        ui.add_space(12.0);
        ui.separator();
        ui.add_space(6.0);
        ui.horizontal_wrapped(|ui| {
            if theme::plain(ui, &a.t("launcher.action.update"), !cmd_busy && !running) {
                acts.push(Act::Job(Job::Command(Command::Update)));
            }
            if theme::plain(ui, &a.t("launcher.hint.refresh"), true) {
                acts.push(Act::Job(Job::Status));
            }
            if running {
                ui.label(dim(a.t("launcher.ui.home.running")));
            }
        });
    });
    ui.add_space(12.0);

    card(ui, &a.t("launcher.heading.status"), |ui| {
        egui::Grid::new("status-grid")
            .num_columns(2)
            .min_col_width(150.0)
            .spacing([14.0, 9.0])
            .show(ui, |ui| {
                for (k, v, mark) in status_rows(a) {
                    ui.horizontal(|ui| {
                        theme::dot(ui, mark.colour(), 12.0);
                        ui.label(dim(k));
                    });
                    ui.add(
                        egui::Label::new(match mark {
                            Mark::Look => theme::warn(v),
                            _ => RichText::new(v),
                        })
                        .wrap(),
                    );
                    ui.end_row();
                }
            });
    });
    note_line(a, ui);
}

// ---- Output ---------------------------------------------------------------------------------

/// The question a running command asked, with a button for each answer.
fn prompt_card(a: &App, ui: &mut egui::Ui, acts: &mut Vec<Act>) {
    let Some(q) = &a.prompt else {
        return;
    };
    theme::card_frame()
        .stroke(egui::Stroke::new(1.0, theme::GREEN))
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            ui.label(theme::section(&a.t("launcher.ui.prompt.title")));
            ui.add_space(4.0);
            ui.add(egui::Label::new(RichText::new(&q.text).strong()).wrap());
            ui.add_space(6.0);
            ui.horizontal_wrapped(|ui| {
                let answers: &[(&str, &'static str, bool)] = match q.kind {
                    PromptKind::YesNo => &[
                        ("launcher.action.yes", "y", true),
                        ("launcher.action.no", "n", false),
                    ],
                    PromptKind::Logs => &[
                        ("launcher.action.keep", "n", true),
                        ("launcher.action.delete", "y", false),
                        ("launcher.action.always_delete", "always", false),
                        ("launcher.action.never_delete", "never", false),
                    ],
                };
                for (key, word, main) in answers {
                    let clicked = if *main {
                        button(ui, &a.t(key), true)
                    } else {
                        theme::draw_button(
                            ui,
                            &a.t(key),
                            Kind::Plain,
                            true,
                            egui::vec2(0.0, 34.0),
                            14.0,
                        )
                    };
                    if clicked {
                        acts.push(Act::Answer(word));
                    }
                }
            });
        });
    ui.add_space(10.0);
}

/// The colour of one output line, by how the launcher marks it.
fn line_colour(l: &str) -> Color32 {
    if l.starts_with("[!]") || l.contains("Failed") {
        theme::WARN
    } else if l.starts_with('>') {
        theme::GREEN
    } else if l.starts_with("[i]") || l.starts_with("---") {
        theme::DIM
    } else {
        theme::BODY
    }
}

fn output(a: &mut App, ui: &mut egui::Ui, acts: &mut Vec<Act>) {
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
    ui.add_space(10.0);
    if a.output.is_empty() {
        card(ui, "", |ui| {
            ui.label(dim(a.t("launcher.ui.output.empty")));
        });
        return;
    }
    egui::Frame::new()
        .fill(theme::SIDE)
        .stroke(egui::Stroke::new(1.0, theme::LINE))
        .corner_radius(10.0)
        .inner_margin(12.0)
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            ui.spacing_mut().item_spacing.y = 3.0;
            for l in &a.output {
                ui.add(egui::Label::new(RichText::new(l).monospace().color(line_colour(l))).wrap());
            }
        });
    // Under the lines, where the view sticks: the question is the last thing the command wrote.
    ui.add_space(10.0);
    prompt_card(a, ui, acts);
    note_line(a, ui);
}
