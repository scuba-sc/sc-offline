// SPDX-License-Identifier: GPL-3.0-only
//! The Settings page: Repair, game logs, language, the in-game menu, plugins on or off, the update
//! check and the settings file. It covers every setting of the C++ Settings page (Repair, logs, the
//! ini) and adds `menu`, which has no C++ page.

use eframe::egui;
use scol_config::{Language, LogChoice, MenuChoice};

use crate::app::{App, Job};
use crate::backend::Command;
use crate::pages::{note_line, title, Act};
use crate::theme::{self, button, card, dim, plain, segmented};

pub fn draw(a: &mut App, ui: &mut egui::Ui, acts: &mut Vec<Act>) {
    title(ui, &a.t("launcher.ui.nav.settings"), "");
    let editable = a.editable();
    if !editable {
        ui.label(dim(a.t("launcher.plugins.close_to_edit")));
        ui.add_space(4.0);
    }

    card(ui, &a.t("launcher.ui.settings.repair"), |ui| {
        let left = a.status.as_ref().is_some_and(|s| !s.pc_left.is_empty());
        if button(ui, &a.t("launcher.action.repair"), editable && left) {
            acts.push(Act::Job(Job::Command(Command::Repair)));
        }
        ui.add(
            egui::Label::new(if left {
                theme::warn(a.t("launcher.status.pc_record"))
            } else {
                dim(a.t("launcher.repair.nothing"))
            })
            .wrap(),
        );
        ui.add(egui::Label::new(dim(a.t("launcher.repair.hint"))).wrap());
    });
    ui.add_space(8.0);

    card(ui, &a.t("launcher.settings.logs_label"), |ui| {
        let all = [
            (LogChoice::Ask, "ask", "launcher.settings.logs_ask"),
            (LogChoice::Always, "always", "launcher.action.always_delete"),
            (LogChoice::Never, "never", "launcher.action.never_delete"),
        ];
        let options: Vec<(String, bool)> = all
            .iter()
            .map(|(c, _, key)| (a.t(key), a.cfg.clean_logs == *c))
            .collect();
        if let Some(i) = segmented(ui, &options, editable) {
            acts.push(Act::CleanLogs(all[i].1));
        }
        ui.add(egui::Label::new(dim(a.t("launcher.settings.logs_hint"))).wrap());
    });
    ui.add_space(8.0);

    card(ui, &a.t("launcher.ui.settings.language"), |ui| {
        let all = [
            (Language::Auto, "launcher.language.auto"),
            (Language::English, "launcher.language.english"),
            (Language::Russian, "launcher.language.russian"),
        ];
        let options: Vec<(String, bool)> = all
            .iter()
            .map(|(l, key)| (a.t(key), a.cfg.language == *l))
            .collect();
        if let Some(i) = segmented(ui, &options, editable) {
            acts.push(Act::Language(all[i].0));
        }
        ui.add(egui::Label::new(dim(a.t("launcher.language.hint"))).wrap());
        if !editable {
            ui.add(egui::Label::new(dim(a.t("launcher.language.busy"))).wrap());
        }
    });
    ui.add_space(8.0);

    card(ui, &a.t("launcher.ui.settings.menu"), |ui| {
        let all = [
            (MenuChoice::Imgui, "launcher.ui.settings.menu_classic"),
            (MenuChoice::Rust, "launcher.ui.settings.menu_new"),
        ];
        let options: Vec<(String, bool)> = all
            .iter()
            .map(|(m, key)| (a.t(key), a.cfg.menu == *m))
            .collect();
        if let Some(i) = segmented(ui, &options, editable) {
            acts.push(Act::Menu(all[i].0));
        }
        ui.add(egui::Label::new(dim(a.t("launcher.ui.settings.menu_hint"))).wrap());
    });
    ui.add_space(8.0);

    card(ui, &a.t("launcher.heading.plugins"), |ui| {
        let on = a.cfg.plugins;
        if theme::toggle(
            ui,
            on,
            editable,
            &a.t("launcher.heading.plugins"),
            &a.t("launcher.plugins.state_on"),
            &a.t("launcher.plugins.state_off"),
        ) {
            acts.push(Act::PluginsOn(!on));
        }
        ui.add(egui::Label::new(dim(a.t("launcher.plugins.applies_next_start"))).wrap());
    });
    ui.add_space(8.0);

    card(ui, &a.t("launcher.ui.settings.updates"), |ui| {
        let on = a.cfg.check_updates;
        if theme::toggle(
            ui,
            on,
            editable,
            &a.t("launcher.ui.settings.updates"),
            &a.t("launcher.plugins.state_on"),
            &a.t("launcher.plugins.state_off"),
        ) {
            acts.push(Act::CheckUpdates(!on));
        }
        ui.add(egui::Label::new(dim(a.t("launcher.ui.settings.updates_hint"))).wrap());
    });
    ui.add_space(8.0);

    card(ui, &a.t("launcher.ui.settings.report"), |ui| {
        ui.horizontal_wrapped(|ui| {
            let idle = !a.running.contains(&Command::Report);
            if button(ui, &a.t("launcher.ui.settings.report_button"), idle) {
                acts.push(Act::Job(Job::Command(Command::Report)));
            }
            if plain(ui, &a.t("launcher.ui.settings.report_open"), true) {
                acts.push(Act::OpenPath(a.reports_dir()));
            }
        });
        ui.add(egui::Label::new(dim(a.t("launcher.ui.settings.report_hint"))).wrap());
    });
    ui.add_space(8.0);

    card(ui, &a.t("launcher.ui.settings.ini_path"), |ui| {
        ui.add(
            egui::Label::new(egui::RichText::new(a.ini_path().display().to_string()).monospace())
                .wrap(),
        );
        ui.horizontal_wrapped(|ui| {
            if plain(ui, &a.t("launcher.action.open_ini"), true) {
                acts.push(Act::OpenPath(a.ini_path()));
            }
            if plain(ui, &a.t("launcher.action.open_folder"), true) {
                acts.push(Act::OpenPath(a.here.clone()));
            }
        });
    });
    note_line(a, ui);
}
