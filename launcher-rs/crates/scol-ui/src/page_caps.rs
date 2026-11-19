// SPDX-License-Identifier: GPL-3.0-only
//! The Capabilities page. One card per switch: a short name, one sentence, an On/Off toggle and a small
//! evidence line, all in plain words. The key and the capability name are in the tooltip and in the
//! collapsed Details row. Capabilities without a switch are read-only rows in two groups, "Works by
//! itself" and "Not available yet". The text model is in `caps`.

use eframe::egui::{self, RichText};
use scol_contract::capability_env::SWITCHES;

use crate::app::App;
use crate::caps::{self, Evidence, PlainRow};
use crate::pages::{note_line, title, Act};
use crate::theme::{self, card, dim};

fn evidence_label(a: &App, e: Evidence) -> RichText {
    let t = a.t(e.key());
    if e.is_good() {
        theme::good(t)
    } else {
        dim(t)
    }
}

pub fn draw(a: &mut App, ui: &mut egui::Ui, acts: &mut Vec<Act>) {
    title(
        ui,
        &a.t("launcher.ui.nav.capabilities"),
        &a.t("launcher.ui.caps.intro"),
    );
    let editable = a.editable();
    if !editable {
        ui.label(dim(a.t("launcher.plugins.close_to_edit")));
        ui.add_space(4.0);
    }
    let (on_word, off_word) = (
        a.t("launcher.plugins.state_on"),
        a.t("launcher.plugins.state_off"),
    );
    for (i, sw) in SWITCHES.iter().enumerate() {
        let on = a.cfg.capabilities.is_on(sw);
        let name = caps::switch_name(a.lang, sw);
        let details = caps::switch_details(a.lang, sw);
        card(ui, "", |ui| {
            ui.horizontal(|ui| {
                if theme::toggle(ui, on, editable, &name, &on_word, &off_word) {
                    acts.push(Act::SetCap(i, !on));
                }
                ui.add(egui::Label::new(RichText::new(&name).strong()).truncate())
                    .on_hover_text(details.join("\n"));
            });
            ui.add(egui::Label::new(theme::body(caps::switch_about(a.lang, sw))).wrap());
            ui.label(evidence_label(a, caps::switch_evidence(sw)));
            egui::CollapsingHeader::new(dim(a.t("launcher.ui.caps.details")))
                .id_salt(("details", sw.ini_key))
                .show(ui, |ui| {
                    for l in &details {
                        ui.label(RichText::new(l).monospace().color(theme::DIM));
                    }
                });
        });
        ui.add_space(4.0);
    }
    note_line(a, ui);
    ui.add_space(10.0);

    let hint = a.t("launcher.hint.search");
    ui.add(
        egui::TextEdit::singleline(&mut a.cap_filter)
            .hint_text(hint)
            .desired_width(240.0),
    );
    ui.add_space(4.0);
    let (auto, later) = caps::plain_rows(&a.here);
    let f = a.cap_filter.trim().to_lowercase();
    let keep = |r: &&PlainRow| f.is_empty() || r.name.to_lowercase().contains(&f);
    let auto: Vec<&PlainRow> = auto.iter().filter(keep).collect();
    let later: Vec<&PlainRow> = later.iter().filter(keep).collect();

    group(
        a,
        ui,
        acts,
        Group {
            key: "launcher.ui.caps.group.auto",
            rows: &auto,
            open: a.caps_auto,
            toggle: Act::CapsAuto,
            compact: true,
        },
    );
    ui.add_space(4.0);
    group(
        a,
        ui,
        acts,
        Group {
            key: "launcher.ui.caps.group.later",
            rows: &later,
            open: a.caps_more,
            toggle: Act::CapsMore,
            compact: false,
        },
    );
}

/// One collapsible group of read-only rows.
struct Group<'a> {
    key: &'a str,
    rows: &'a [&'a PlainRow],
    open: bool,
    toggle: fn(bool) -> Act,
    /// One reason for the whole group and a plain two-column list.
    compact: bool,
}

fn group(a: &App, ui: &mut egui::Ui, acts: &mut Vec<Act>, g: Group) {
    let Group {
        key,
        rows,
        open,
        toggle,
        compact,
    } = g;
    let head = format!("{} ({})", a.t(key), rows.len());
    let r = egui::CollapsingHeader::new(RichText::new(head).strong())
        .id_salt(key)
        .open(Some(open))
        .show(ui, |ui| {
            if compact {
                // Every row of this group has the same reason: say it once.
                if let Some(first) = rows.first() {
                    ui.add(egui::Label::new(theme::body(a.t(first.reason_key))).wrap());
                    ui.add_space(4.0);
                }
                egui::Grid::new(("rows", key))
                    .num_columns(2)
                    .spacing([24.0, 5.0])
                    .striped(true)
                    .show(ui, |ui| {
                        for row in rows {
                            ui.label(RichText::new(&row.name))
                                .on_hover_text(caps::row_details(a.lang, row));
                            ui.label(evidence_label(a, row.evidence));
                            ui.end_row();
                        }
                    });
                return;
            }
            for row in rows {
                card(ui, "", |ui| {
                    // The reason says it all: nothing here has run in game, so no evidence label.
                    ui.label(RichText::new(&row.name).strong())
                        .on_hover_text(caps::row_details(a.lang, row));
                    ui.add(egui::Label::new(dim(a.t(row.reason_key))).wrap());
                });
                ui.add_space(3.0);
            }
        });
    if r.header_response.clicked() {
        acts.push(toggle(!open));
    }
}
