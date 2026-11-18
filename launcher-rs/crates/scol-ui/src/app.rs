// SPDX-License-Identifier: GPL-3.0-only
//! The window's state and its worker. Library calls that can take time (status discovery, plugin
//! verify, catalogue read and install, the backend commands) run on a worker thread and report back
//! over a channel; the UI thread only reads the channel and draws. Cheap file work (the plugin
//! list, ini edits) runs inline.

use std::path::{Path, PathBuf};
use std::sync::mpsc::{channel, Receiver, Sender};
use std::sync::Arc;

use eframe::egui;
use scol_config::ini_edit::set_ini_value;
use scol_config::{read_config, Config, Language};
use scol_contract::capability_env::Switch;
use scol_install::StatusReport;

use crate::backend::{Backend, Command};
use crate::catalogue::{self, Installed, Loaded};
use crate::i18n::{self, tr, trf, Lang};
use crate::plugins::{self, Added, PluginRow};

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
}

/// What the worker answers.
pub enum Msg {
    Status(Box<StatusReport>),
    Line(String),
    CommandDone(Command, Result<i32, String>),
    Catalogue(Result<Loaded, String>),
    Installed(Result<Installed, String>),
    Added(Result<Added, String>),
}

/// A line under the page: text and whether it is an error.
#[derive(Debug, Clone)]
pub struct Note {
    pub text: String,
    pub error: bool,
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
    pub add_path: String,
    pub catalogue: Option<Result<Loaded, String>>,
    pub cap_filter: String,
    pub note: Option<Note>,
    /// Number of worker jobs in flight.
    pub busy: usize,
    /// The command whose output the Output page is streaming.
    pub running: Option<Command>,
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
            add_path: String::new(),
            catalogue: None,
            cap_filter: String::new(),
            note: None,
            busy: 0,
            running: None,
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

    pub fn rescan_plugins(&mut self) {
        self.plugins = plugins::scan(&self.here);
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

    /// True while a backend command runs (Play and Update are off then, as in the C++ window).
    pub fn command_running(&self) -> bool {
        self.running.is_some()
    }

    // ---- jobs -----------------------------------------------------------------------------

    pub fn submit(&mut self, job: Job) {
        self.busy += 1;
        if let Job::Command(c) = &job {
            self.running = Some(*c);
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

    /// Take what the worker has sent so far. Returns true when something changed.
    pub fn pump(&mut self) -> bool {
        let mut changed = false;
        while let Ok(m) = self.rx.try_recv() {
            changed = true;
            match m {
                Msg::Status(s) => {
                    self.status = Some(*s);
                    self.busy = self.busy.saturating_sub(1);
                }
                Msg::Line(l) => self.output.push(l),
                Msg::CommandDone(c, r) => {
                    self.busy = self.busy.saturating_sub(1);
                    self.running = None;
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
                            let msg = if e.contains("not connected") {
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
                Msg::Catalogue(r) => {
                    self.busy = self.busy.saturating_sub(1);
                    self.catalogue = Some(r);
                }
                Msg::Installed(r) => {
                    self.busy = self.busy.saturating_sub(1);
                    match r {
                        Ok(i) => {
                            let mut text = self.tf(
                                "launcher.ui.plugins.added",
                                &[("plugin", &format!("{} {}", i.id, i.version))],
                            );
                            for w in &i.warnings {
                                text.push(' ');
                                text.push_str(w);
                            }
                            self.note(text);
                            self.rescan_plugins();
                            self.submit(Job::CatalogueLoad);
                        }
                        Err(e) => self.note_error(self.tf("launcher.ui.error", &[("error", &e)])),
                    }
                }
                Msg::Added(r) => {
                    self.busy = self.busy.saturating_sub(1);
                    match r {
                        Ok(a) => {
                            self.note(self.tf("launcher.ui.plugins.added", &[("plugin", &a.name)]));
                            self.add_path.clear();
                            self.rescan_plugins();
                        }
                        Err(e) => self.note_error(self.tf("launcher.ui.error", &[("error", &e)])),
                    }
                }
            }
        }
        changed
    }

    // ---- actions the pages call ---------------------------------------------------------

    pub fn toggle_plugin(&mut self, id: &str, enable: bool) {
        match plugins::set_enabled(&self.here, id, enable) {
            Ok(()) => {
                let state = self.t(if enable {
                    "launcher.plugins.state_on"
                } else {
                    "launcher.plugins.state_off"
                });
                self.note(self.tf(
                    "launcher.plugins.state_note",
                    &[("state", &state), ("plugin", id)],
                ));
            }
            Err(_) => {
                self.note_error(self.tf("launcher.plugins.disabled_error", &[("plugin", id)]))
            }
        }
        self.rescan_plugins();
    }

    /// Write one key of `sc-offline.ini` in place and read the result back.
    pub fn write_ini(&mut self, key: &str, value: &str) -> bool {
        let ok = set_ini_value(&self.ini_path(), key, value);
        self.reload_config();
        ok
    }

    pub fn set_capability(&mut self, switch: &Switch, on: bool) {
        if self.write_ini(switch.ini_key, if on { "on" } else { "off" }) {
            self.note(self.tf("launcher.ui.caps.saved", &[("key", switch.ini_key)]));
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
}

fn run_job(job: Job, here: &Path, backend: &dyn Backend, send: &dyn Fn(Msg)) {
    match job {
        Job::Status => send(Msg::Status(Box::new(scol_install::collect_status(
            &here.to_string_lossy(),
        )))),
        Job::Command(c) => {
            let r = backend.run(c, here, &mut |line| send(Msg::Line(line.to_string())));
            send(Msg::CommandDone(c, r));
        }
        Job::CatalogueLoad => send(Msg::Catalogue(catalogue::load(here, &backend.trust()))),
        Job::CatalogueInstall(id) => {
            let r = (|| {
                let loaded = catalogue::load(here, &backend.trust()).map_err(|e| {
                    if e.is_empty() {
                        "no catalogue index".to_string()
                    } else {
                        e
                    }
                })?;
                let mut fetcher = backend.fetcher().ok_or_else(|| {
                    "download is not connected yet (catalogue install)".to_string()
                })?;
                let (allowed, redirects) = backend.allowed_hosts();
                let a: Vec<&str> = allowed.iter().map(String::as_str).collect();
                let r: Vec<&str> = redirects.iter().map(String::as_str).collect();
                catalogue::install(here, &loaded, &id, fetcher.as_mut(), (&a, &r))
            })();
            send(Msg::Installed(r));
        }
        Job::AddPath(p) => send(Msg::Added(plugins::add_path(here, Path::new(&p)))),
    }
}
