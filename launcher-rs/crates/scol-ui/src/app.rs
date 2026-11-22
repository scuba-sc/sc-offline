// SPDX-License-Identifier: GPL-3.0-only
//! The window's state and its worker. Library calls that can take time (status discovery, plugin
//! add and remove, catalogue read and install, the backend commands) run on a worker thread and
//! report back over a channel; the UI thread only reads the channel and draws. Cheap file work (the
//! plugin list, ini edits, the on/off marker of a plugin) runs inline.

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::mpsc::{channel, Receiver, Sender};
use std::sync::Arc;

use eframe::egui;
use scol_config::ini_edit::set_ini_value;
use scol_config::{read_config, Config, Language, MenuChoice};
use scol_contract::capability_env::Switch;
use scol_install::StatusReport;

use crate::backend::{question, Backend, Command, PromptKind, NOT_CONNECTED};
use crate::catalogue::{self, State};
use crate::i18n::{self, tr, trf, Lang};
use crate::plugins::{self, PluginRow};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Page {
    Home,
    Output,
    Plugins,
    Capabilities,
    Settings,
}

pub const PAGES: [Page; 5] = [
    Page::Home,
    Page::Output,
    Page::Plugins,
    Page::Capabilities,
    Page::Settings,
];

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PluginTab {
    Installed,
    Catalogue,
}

/// What the worker is asked to do.
pub enum Job {
    Status,
    Command(Command),
    CatalogueLoad,
    CatalogueInstall(String),
    AddPath(String),
    Remove(String),
    /// Save the edited settings of one plugin: (plugin, [(name, value)]).
    SaveSettings(String, Vec<(String, String)>),
}

/// What the worker answers.
pub enum Msg {
    Status(Box<StatusReport>),
    Line(String),
    CommandDone(Command, Result<i32, String>),
    Catalogue(State),
    /// A plugin was added, installed or removed, or the reason it was not.
    PluginChanged(Result<Changed, String>),
    SettingsSaved(Result<usize, String>),
}

/// A plugin change that went through.
pub struct Changed {
    pub name: String,
    /// `native`, `lua` or `data` (empty after a remove).
    pub kind: String,
    pub removed: bool,
}

/// A line under the page: text and whether it is an error.
#[derive(Debug, Clone)]
pub struct Note {
    pub text: String,
    pub error: bool,
}

/// A question a running command waits on. The Output page shows it with a button for each answer.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Prompt {
    pub kind: PromptKind,
    /// The question, without the list of answers.
    pub text: String,
}

pub struct App {
    pub here: PathBuf,
    pub backend: Arc<dyn Backend>,
    pub lang: Lang,
    pub page: Page,
    pub cfg: Config,
    pub status: Option<StatusReport>,
    pub output: Vec<String>,
    pub plugins: Vec<PluginRow>,
    pub plugin_tab: PluginTab,
    pub open_plugin: Option<String>,
    /// The plugin the player pressed Remove on and has not confirmed yet.
    pub confirm_remove: Option<String>,
    pub add_path: String,
    /// Edited values of the open plugin's settings: name to text. Cleared when the page closes.
    pub drafts: HashMap<String, String>,
    pub catalogue: Option<State>,
    pub caps_more: bool,
    pub caps_auto: bool,
    /// Force the scroll position of the page (the screenshots use it). None: the player scrolls.
    pub scroll_to: Option<f32>,
    pub cap_filter: String,
    pub note: Option<Note>,
    /// Number of worker jobs in flight.
    pub busy: usize,
    /// The commands the worker runs now. Stop runs beside Play.
    pub running: Vec<Command>,
    /// The game process of this install runs (read once per frame).
    pub game_up: bool,
    /// The question the running command asked and nobody answered yet.
    pub prompt: Option<Prompt>,
    tx: Sender<Msg>,
    rx: Receiver<Msg>,
    ctx: Option<egui::Context>,
}

impl App {
    pub fn new(here: PathBuf, backend: Arc<dyn Backend>) -> App {
        let (tx, rx) = channel();
        let mut a = App {
            here,
            backend,
            lang: Lang::En,
            page: Page::Home,
            cfg: Config::default(),
            status: None,
            output: Vec::new(),
            plugins: Vec::new(),
            plugin_tab: PluginTab::Installed,
            open_plugin: None,
            confirm_remove: None,
            add_path: String::new(),
            drafts: HashMap::new(),
            catalogue: None,
            caps_more: false,
            caps_auto: false,
            scroll_to: None,
            cap_filter: String::new(),
            note: None,
            busy: 0,
            running: Vec::new(),
            game_up: false,
            prompt: None,
            tx,
            rx,
            ctx: None,
        };
        a.reload_config();
        a.rescan_plugins();
        a
    }

    /// Repaint requests from the worker go to this context.
    pub fn attach(&mut self, ctx: &egui::Context) {
        self.ctx = Some(ctx.clone());
    }

    pub fn ini_path(&self) -> PathBuf {
        self.here.join(scol_contract::files::INI)
    }

    pub fn t(&self, key: &str) -> String {
        tr(self.lang, key)
    }

    pub fn tf(&self, key: &str, args: &[(&str, &str)]) -> String {
        trf(self.lang, key, args)
    }

    pub fn reload_config(&mut self) {
        self.cfg = read_config(&self.ini_path())
            .map(|p| p.config)
            .unwrap_or_default();
        self.lang = i18n::resolve(self.cfg.language);
    }

    /// The plugin manager's context for this install.
    pub fn pm(&self) -> scol_plugin_manager::Ctx {
        plugins::pm_ctx(&self.here, self.backend.as_ref())
    }

    pub fn rescan_plugins(&mut self) {
        self.plugins = plugins::scan(&self.pm());
    }

    pub fn note(&mut self, text: impl Into<String>) {
        self.note = Some(Note {
            text: text.into(),
            error: false,
        });
    }

    pub fn note_error(&mut self, text: impl Into<String>) {
        self.note = Some(Note {
            text: text.into(),
            error: true,
        });
    }

    /// True while a backend command other than Stop runs (Play and Update are off then, as in the
    /// C++ window).
    pub fn command_running(&self) -> bool {
        self.running.iter().any(|c| *c != Command::Stop)
    }

    /// Settings can be changed: the game is closed and no command runs.
    pub fn editable(&self) -> bool {
        !self.game_up && !self.command_running()
    }

    // ---- jobs -----------------------------------------------------------------------------

    pub fn submit(&mut self, job: Job) {
        self.busy += 1;
        if let Job::Command(c) = &job {
            self.running.push(*c);
            self.output.push(format!("> sc-offline.exe {}", c.word()));
            self.page = Page::Output;
        }
        let tx = self.tx.clone();
        let ctx = self.ctx.clone();
        let here = self.here.clone();
        let backend = self.backend.clone();
        std::thread::spawn(move || {
            let send = |m: Msg| {
                let _ = tx.send(m);
                if let Some(c) = &ctx {
                    c.request_repaint();
                }
            };
            run_job(job, &here, backend.as_ref(), &send);
        });
    }

    /// Answer the question on the Output page: `word` is what the command reads (`y`, `n`, `always`
    /// or `never`). The answer is written after the question, as a console would show it.
    pub fn answer(&mut self, word: &str) {
        if self.prompt.take().is_none() {
            return;
        }
        if let Some(last) = self.output.last_mut() {
            last.push(' ');
            last.push_str(word);
        }
        self.backend.answer(word);
    }

    /// A line of a running command. A question (it ends with the list of answers) is also kept as the
    /// prompt: the Output page comes forward and the taskbar button flashes, because the game has
    /// usually just closed and the window is behind something.
    fn take_line(&mut self, line: String) {
        if let Some((kind, text)) = question(&line) {
            self.prompt = Some(Prompt { kind, text });
            self.page = Page::Output;
            if let Some(c) = &self.ctx {
                c.send_viewport_cmd(egui::ViewportCommand::RequestUserAttention(
                    egui::UserAttentionType::Informational,
                ));
            }
        }
        self.output.push(line);
    }

    fn done_one(&mut self) {
        self.busy = self.busy.saturating_sub(1);
    }

    /// Words for a failed backend call: the "not connected" default gets the player's wording.
    fn backend_error(&self, e: &str, key: &str) -> String {
        if e.contains(NOT_CONNECTED) {
            self.t(key)
        } else {
            self.tf("launcher.ui.error", &[("error", e)])
        }
    }

    fn plugin_added_note(&self, name: &str, kind: &str) -> String {
        let mut text = self.tf("launcher.ui.plugins.added", &[("plugin", name)]);
        let warn = match kind {
            "native" => Some("launcher.ui.plugins.native_warning"),
            "lua" => Some("launcher.ui.plugins.lua_warning"),
            _ => None,
        };
        if let Some(k) = warn {
            text.push(' ');
            text.push_str(&self.t(k));
        }
        text
    }

    /// Take what the worker has sent so far. Returns true when something changed.
    pub fn pump(&mut self) -> bool {
        let mut changed = false;
        while let Ok(m) = self.rx.try_recv() {
            changed = true;
            match m {
                Msg::Status(s) => {
                    self.status = Some(*s);
                    self.done_one();
                }
                Msg::Line(l) => self.take_line(l),
                Msg::CommandDone(c, r) => {
                    self.done_one();
                    if c != Command::Stop {
                        // Whoever asked is gone; an unanswered question took its default.
                        self.prompt = None;
                    }
                    if let Some(i) = self.running.iter().position(|x| *x == c) {
                        self.running.remove(i);
                    }
                    match r {
                        Ok(code) => {
                            let done = if code == 0 {
                                self.t("launcher.output.child_done")
                            } else {
                                self.tf(
                                    "launcher.output.child_finished",
                                    &[("code", &code.to_string())],
                                )
                            };
                            self.output.push(done);
                            self.submit(Job::Status);
                        }
                        Err(e) => {
                            let msg = if e.contains(NOT_CONNECTED) {
                                let action = match c {
                                    Command::Play => self.t("launcher.action.play"),
                                    Command::Stop => self.t("launcher.action.stop"),
                                    Command::Update => self.t("launcher.action.update"),
                                    Command::Repair => self.t("launcher.action.repair"),
                                };
                                self.tf(
                                    "launcher.ui.unavailable",
                                    &[("action", &action), ("command", c.word())],
                                )
                            } else {
                                self.tf("launcher.ui.error", &[("error", &e)])
                            };
                            self.output.push(msg.clone());
                            self.note_error(msg);
                        }
                    }
                }
                Msg::Catalogue(s) => {
                    self.done_one();
                    self.catalogue = Some(s);
                }
                Msg::PluginChanged(r) => {
                    self.done_one();
                    match r {
                        Ok(c) => {
                            let text = if c.removed {
                                self.tf("launcher.ui.plugins.removed", &[("plugin", &c.name)])
                            } else {
                                self.plugin_added_note(&c.name, &c.kind)
                            };
                            self.note(text);
                            self.add_path.clear();
                            self.rescan_plugins();
                            if self.catalogue.is_some() {
                                self.submit(Job::CatalogueLoad);
                            }
                        }
                        Err(e) => {
                            self.rescan_plugins();
                            self.note_error(self.tf("launcher.ui.error", &[("error", &e)]));
                        }
                    }
                }
                Msg::SettingsSaved(r) => {
                    self.done_one();
                    match r {
                        Ok(_) => {
                            self.drafts.clear();
                            self.note(self.t("launcher.plugins.saved"));
                            self.rescan_plugins();
                        }
                        Err(e) => {
                            let t = self.backend_error(&e, "launcher.ui.plugins.save_temp");
                            self.note_error(t);
                        }
                    }
                }
            }
        }
        changed
    }

    // ---- actions the pages call ---------------------------------------------------------

    pub fn toggle_plugin(&mut self, id: &str, enable: bool) {
        match plugins::set_enabled(&self.pm(), id, enable) {
            Ok(()) => {
                let state = self.t(if enable {
                    "launcher.plugins.state_on"
                } else {
                    "launcher.plugins.state_off"
                });
                let mut text = self.tf(
                    "launcher.plugins.state_note",
                    &[("state", &state), ("plugin", id)],
                );
                if enable {
                    let kind = plugins::describe(&self.pm(), id).kind;
                    if kind == "native" {
                        text.push(' ');
                        text.push_str(&self.t("launcher.ui.plugins.native_warning_on"));
                    }
                }
                self.note(text);
            }
            Err(e) => {
                let text = if e.contains("game is running") {
                    self.t("launcher.plugins.close_to_edit")
                } else {
                    self.tf("launcher.ui.error", &[("error", &e)])
                };
                self.note_error(text);
            }
        }
        self.rescan_plugins();
    }

    /// Ask the folder picker; a chosen folder goes into the path box.
    pub fn browse(&mut self) {
        let title = self.t("launcher.ui.plugins.browse_title");
        match self.backend.pick_folder(&title) {
            Ok(Some(p)) => self.add_path = p.display().to_string(),
            Ok(None) => {}
            Err(e) => {
                let t = self.backend_error(&e, "launcher.ui.plugins.browse_temp");
                self.note_error(t);
            }
        }
    }

    /// Write one key of `sc-offline.ini` in place and read the result back.
    pub fn write_ini(&mut self, key: &str, value: &str) -> bool {
        let ok = set_ini_value(&self.ini_path(), key, value);
        self.reload_config();
        ok
    }

    pub fn set_capability(&mut self, switch: &Switch, on: bool) {
        if self.write_ini(switch.ini_key, if on { "on" } else { "off" }) {
            self.note(self.t("launcher.ui.caps.saved"));
        } else {
            self.note_error(self.t("launcher.ui.caps.write_error"));
        }
    }

    pub fn set_language(&mut self, l: Language) {
        if self.write_ini("language", l.word()) {
            self.note(self.t("launcher.language.saved"));
        } else {
            self.note_error(self.t("launcher.language.write_error"));
        }
    }

    pub fn set_clean_logs(&mut self, word: &str) {
        if self.write_ini("clean_logs", word) {
            self.note(self.t("launcher.settings.logs_saved"));
        } else {
            self.note_error(self.t("launcher.settings.logs_write_error"));
        }
    }

    pub fn set_plugins_on(&mut self, on: bool) {
        if self.write_ini("plugins", if on { "on" } else { "off" }) {
            self.note(self.t("launcher.plugins.enabled"));
        } else {
            self.note_error(self.t("launcher.plugins.enable_error"));
        }
    }

    pub fn set_menu(&mut self, m: MenuChoice) {
        if self.write_ini("menu", m.word()) {
            self.note(self.t("launcher.ui.settings.menu_saved"));
        } else {
            self.note_error(self.t("launcher.ui.settings.write_error"));
        }
    }

    pub fn set_check_updates(&mut self, on: bool) {
        if self.write_ini("check_updates", if on { "on" } else { "off" }) {
            self.note(self.t("launcher.ui.settings.saved"));
        } else {
            self.note_error(self.t("launcher.ui.settings.write_error"));
        }
    }
}

fn run_job(job: Job, here: &Path, backend: &dyn Backend, send: &dyn Fn(Msg)) {
    let ctx = plugins::pm_ctx(here, backend);
    match job {
        Job::Status => send(Msg::Status(Box::new(scol_install::collect_status(
            &here.to_string_lossy(),
        )))),
        Job::Command(c) => {
            let r = backend.run(c, here, &mut |line| send(Msg::Line(line.to_string())));
            send(Msg::CommandDone(c, r));
        }
        Job::CatalogueLoad => send(Msg::Catalogue(catalogue::load(&ctx))),
        Job::CatalogueInstall(id) => {
            let r = catalogue::install(&ctx, &id).map(|i| Changed {
                name: i.name,
                kind: i.kind,
                removed: false,
            });
            send(Msg::PluginChanged(r));
        }
        Job::AddPath(p) => {
            let r = plugins::add_path(&ctx, Path::new(&p)).map(|a| Changed {
                name: a.name,
                kind: a.kind,
                removed: false,
            });
            send(Msg::PluginChanged(r));
        }
        Job::Remove(id) => {
            let name = plugins::describe(&ctx, &id).name;
            let r = plugins::remove(&ctx, &id).map(|()| Changed {
                name,
                kind: String::new(),
                removed: true,
            });
            send(Msg::PluginChanged(r));
        }
        Job::SaveSettings(plugin, values) => {
            let mut done = 0usize;
            for (name, value) in &values {
                if let Err(e) = backend.save_plugin_setting(here, &plugin, name, value) {
                    send(Msg::SettingsSaved(Err(e)));
                    return;
                }
                done += 1;
            }
            send(Msg::SettingsSaved(Ok(done)));
        }
    }
}
