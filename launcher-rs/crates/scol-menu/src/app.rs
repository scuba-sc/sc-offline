// SPDX-License-Identifier: GPL-3.0-only
//! The egui app of the menu window: sidebar of pages, page header, the open page's nodes, a status
//! strip. It owns the poll loop (stamps, list, copy), the optimistic local values with their
//! acknowledgement by `events_done`, the event outbox with its retry rule, and the test driver.
use crate::blob::Panel;
use crate::ffi::*;
use crate::render::{ItemRect, Local, NodeRect, Scene};
use crate::script::{self, Op, Target, Where};
use crate::theme;
use crate::view::{Entry, Event, Stamps, View};
use crate::Shared;
use egui::{Color32, Frame, Margin, RichText};
use std::collections::{HashMap, VecDeque};
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::Sender;
use std::sync::Arc;
use std::time::{Duration, Instant};

pub type LogFn = Arc<dyn Fn(&str) + Send + Sync>;

pub struct Config {
    pub view: View,
    pub log: LogFn,
    pub nudge: Option<Arc<dyn Fn() + Send + Sync>>,
    pub shot_dir: Option<PathBuf>,
    pub start_visible: bool,
    pub test_mode: bool,
    /// Virtual-key code that toggles the window (0 = none).
    pub hotkey: u32,
}

struct Out {
    panel: String,
    event: Event,
}

struct Running {
    ops: VecDeque<Op>,
    reply: Sender<Result<String, String>>,
    waited: u32,
    since: Instant,
    shot: Option<Arc<AtomicBool>>,
    pause_until: Option<Instant>,
    report: String,
}

pub struct App {
    cfg: Config,
    shared: Arc<Shared>,
    frame: u64,
    stamps: Option<Stamps>,
    entries: Vec<Entry>,
    selected: Option<String>,
    panel: Option<Panel>,
    page_error: Option<String>,
    force_copy: bool,
    local: HashMap<u64, Local>,
    outbox: Vec<Out>,
    notice: Option<(String, Instant)>,
    last_drops: u64,
    dead: Option<String>,
    rects: Vec<NodeRect>,
    items: Vec<ItemRect>,
    pending_input: Vec<egui::Event>,
    run: Option<Running>,
    visible: bool,
    last_pointer: Option<egui::Pos2>,
    pending_hide: bool,
}

impl App {
    pub fn new(cfg: Config, shared: Arc<Shared>) -> Self {
        let visible = cfg.start_visible;
        App {
            cfg,
            shared,
            frame: 0,
            stamps: None,
            entries: Vec::new(),
            selected: None,
            panel: None,
            page_error: None,
            force_copy: false,
            local: HashMap::new(),
            outbox: Vec::new(),
            notice: None,
            last_drops: 0,
            dead: None,
            rects: Vec::new(),
            items: Vec::new(),
            pending_input: Vec::new(),
            run: None,
            visible,
            last_pointer: None,
            pending_hide: false,
        }
    }

    fn log(&self, msg: &str) {
        (self.cfg.log)(msg);
    }

    fn say(&mut self, msg: impl Into<String>) {
        self.notice = Some((msg.into(), Instant::now()));
    }

    /// One poll: stamps first (atomics only on the host side), the heavier calls only when a counter moved.
    fn poll(&mut self) {
        if self.dead.is_some() {
            return;
        }
        let s = match self.cfg.view.stamps() {
            Ok(s) => s,
            Err(code) => {
                let why = if code == SCO_UNAVAILABLE {
                    "The game side of the menu has stopped.".to_string()
                } else {
                    format!("The menu lost its connection (code {code}).")
                };
                self.log(&format!(
                    "[menu] stamps answered {code}; the menu stops polling"
                ));
                self.dead = Some(why);
                return;
            }
        };
        if s.drops > self.last_drops {
            self.last_drops = s.drops;
            self.say("A panel changed; try again");
        }
        // Acknowledged values: the host has handled that event, so its revision is the truth again.
        let (done, frame) = (s.done, self.frame);
        let before = self.local.len();
        self.local
            .retain(|_, l| !(l.seq != 0 && l.seq <= done && l.touched + 1 < frame));
        if self.local.len() != before {
            self.force_copy = true;
        }
        let first = self.stamps.is_none();
        let moved = self
            .stamps
            .is_none_or(|o| o.global != s.global || o.list != s.list);
        self.stamps = Some(s);
        if !(first
            || moved
            || self.force_copy
            || (self.panel.is_none() && !self.entries.is_empty()))
        {
            return;
        }
        match self.cfg.view.list() {
            Ok(entries) => self.entries = entries,
            Err(code) => {
                self.log(&format!("[menu] list_panels answered {code}"));
                self.dead = Some(format!("The panel list could not be read (code {code})."));
                return;
            }
        }
        let valid = self
            .selected
            .as_ref()
            .is_some_and(|id| self.entries.iter().any(|e| &e.id == id));
        if !valid {
            self.selected = self.entries.first().map(|e| e.id.clone());
            self.panel = None;
        }
        let Some(sel) = self.selected.clone() else {
            return;
        };
        let entry = self.entries.iter().find(|e| e.id == sel).cloned();
        let stale = match (&self.panel, &entry) {
            (Some(p), Some(e)) => p.id != e.id || p.revision != e.revision || p.epoch != e.epoch,
            (None, Some(_)) => true,
            _ => false,
        };
        if stale || self.force_copy {
            self.force_copy = false;
            match self.cfg.view.copy(&sel) {
                Ok(p) => {
                    self.panel = Some(p);
                    self.page_error = None;
                }
                Err(e) => {
                    self.panel = None;
                    self.page_error = Some(format!("This page could not be read: {e}"));
                    self.log(&format!("[menu] page {sel}: {e}"));
                }
            }
        }
    }

    fn push_event(&mut self, panel: &str, e: Event) {
        let mergeable = e.kind == WIDGET_SLIDER || e.kind == WIDGET_TEXT_INPUT;
        if mergeable {
            if let Some(o) = self.outbox.iter_mut().find(|o| {
                o.panel == panel
                    && o.event.kind == e.kind
                    && o.event.node_id == e.node_id
                    && o.event.node_key == e.node_key
            }) {
                o.event = e;
                return;
            }
        }
        self.outbox.push(Out {
            panel: panel.to_string(),
            event: e,
        });
    }

    /// Sends what it can. A full queue keeps BUTTON, TOGGLE, COMBO, LIST and PAGE events for the
    /// next frame (never discarded); only the latest SLIDER or TEXT_INPUT value per node is kept.
    fn flush(&mut self) {
        let mut sent = false;
        while let Some(o) = self.outbox.first() {
            match self.cfg.view.submit(&o.panel, &o.event) {
                Ok(seq) => {
                    if let Some(l) = self.local.get_mut(&o.event.node_key) {
                        l.seq = seq;
                    }
                    sent = true;
                    self.outbox.remove(0);
                }
                Err(SCO_TOO_MANY) => break,
                Err(code) => {
                    let key = o.event.node_key;
                    self.local.remove(&key);
                    self.log(&format!(
                        "[menu] submit_event answered {code}; the event is dropped"
                    ));
                    self.say("A panel changed; try again");
                    self.force_copy = true;
                    self.outbox.remove(0);
                }
            }
        }
        if sent {
            if let Some(n) = &self.cfg.nudge {
                n();
            }
        }
    }

    fn set_visible(&mut self, on: bool) {
        self.visible = on;
        self.shared.set_visible(on);
    }

    // ---- test driver ----

    fn find_rect(&self, t: &Target) -> Option<&NodeRect> {
        self.rects.iter().find(|r| match t {
            Target::Tag(tag) => &r.tag == tag,
            Target::Label(l) => &r.label == l,
        })
    }

    fn resolve(&self, w: &Where) -> Option<egui::Pos2> {
        match w {
            Where::Node(t) => self.find_rect(t).map(|r| r.rect.center()),
            Where::Slider(t, f) => self.find_rect(t).map(|r| {
                let radius = 9.0;
                let x = r.rect.left() + radius + (*f as f32) * (r.rail - 2.0 * radius).max(1.0);
                egui::pos2(x, r.rect.center().y)
            }),
            Where::Item(t, i) => {
                let key = self.find_rect(t)?.key;
                self.items
                    .iter()
                    .find(|r| r.key == key && r.index == *i)
                    .map(|r| r.rect.center())
            }
        }
    }

    fn start_job(&mut self) {
        if self.run.is_some() {
            return;
        }
        let job = self.shared.jobs.lock().ok().and_then(|mut q| q.pop_front());
        let Some(job) = job else { return };
        match script::parse(&job.line) {
            Ok(ops) => {
                self.run = Some(Running {
                    ops: ops.into(),
                    reply: job.reply,
                    waited: 0,
                    since: Instant::now(),
                    shot: None,
                    pause_until: None,
                    report: "done".into(),
                });
            }
            Err(e) => {
                let _ = job.reply.send(Err(e));
            }
        }
    }

    /// Plays micro operations until one needs a frame boundary.
    fn drive(&mut self, ctx: &egui::Context) {
        self.start_job();
        let Some(mut run) = self.run.take() else {
            return;
        };
        let outcome = self.drive_inner(ctx, &mut run);
        match outcome {
            Ok(true) => {
                let _ = run.reply.send(Ok(run.report));
            }
            Ok(false) => self.run = Some(run),
            Err(e) => {
                let _ = run.reply.send(Err(e));
            }
        }
        if self.run.is_some() || self.shared.jobs.lock().is_ok_and(|q| !q.is_empty()) {
            ctx.request_repaint();
        }
    }

    fn drive_inner(&mut self, ctx: &egui::Context, run: &mut Running) -> Result<bool, String> {
        if run.since.elapsed() > Duration::from_secs(20) {
            return Err(format!("timed out at {:?}", run.ops.front()));
        }
        loop {
            let Some(op) = run.ops.front().cloned() else {
                return Ok(true);
            };
            match op {
                Op::Select(id) => {
                    if !self.entries.iter().any(|e| e.id == id) {
                        return if run.waited < 120 {
                            run.waited += 1;
                            Ok(false)
                        } else {
                            Err(format!("no page '{id}'"))
                        };
                    }
                    self.selected = Some(id);
                    self.panel = None;
                    self.force_copy = true;
                    run.ops.pop_front();
                    run.ops
                        .push_front(Op::AwaitPanel(self.selected.clone().unwrap_or_default()));
                }
                Op::AwaitPanel(id) => {
                    if self.panel.as_ref().is_some_and(|p| p.id == id) {
                        run.ops.pop_front();
                        run.waited = 0;
                    } else if run.waited < 120 {
                        run.waited += 1;
                        return Ok(false);
                    } else {
                        return Err(format!("page '{id}' did not load"));
                    }
                }
                Op::MoveTo(w) => match self.resolve(&w) {
                    Some(p) => {
                        self.pending_input.push(script::pointer_moved(p));
                        self.last_pointer = Some(p);
                        run.ops.pop_front();
                        run.waited = 0;
                        return Ok(false);
                    }
                    None if run.waited < 180 => {
                        run.waited += 1;
                        return Ok(false);
                    }
                    None => return Err(format!("not drawn: {w:?}")),
                },
                Op::Down | Op::Up => {
                    let p = self.last_pointer.unwrap_or(egui::Pos2::ZERO);
                    self.pending_input
                        .push(script::pointer_button(p, matches!(op, Op::Down)));
                    run.ops.pop_front();
                    return Ok(false);
                }
                Op::Frames(n) => {
                    run.ops.pop_front();
                    if n > 1 {
                        run.ops.push_front(Op::Frames(n - 1));
                    }
                    return Ok(false);
                }
                Op::Key(k) => {
                    self.pending_input.extend(script::ctrl_key(k));
                    run.ops.pop_front();
                    return Ok(false);
                }
                Op::Text(t) => {
                    self.pending_input.push(egui::Event::Text(t));
                    run.ops.pop_front();
                    return Ok(false);
                }
                Op::Pause(ms) => {
                    let until = *run
                        .pause_until
                        .get_or_insert_with(|| Instant::now() + Duration::from_millis(ms));
                    if Instant::now() >= until {
                        run.pause_until = None;
                        run.ops.pop_front();
                    } else {
                        return Ok(false);
                    }
                }
                Op::Check(t) => {
                    if self.find_rect(&t).is_some() {
                        run.ops.pop_front();
                    } else if run.waited < 60 {
                        run.waited += 1;
                        return Ok(false);
                    } else {
                        return Err(format!("not drawn: {t:?}"));
                    }
                }
                Op::Report => {
                    run.report = format!(
                        "page={} nodes={} entries={}",
                        self.selected.clone().unwrap_or_default(),
                        self.panel.as_ref().map_or(0, |p| p.nodes.len()),
                        self.entries.len()
                    );
                    run.ops.pop_front();
                }
                Op::Shot(name) => match &run.shot {
                    None => {
                        let Some(dir) = self.cfg.shot_dir.clone() else {
                            return Err("no screenshot folder".into());
                        };
                        let flag = Arc::new(AtomicBool::new(false));
                        let done = flag.clone();
                        let path = dir.join(format!("{name}.png"));
                        let log = self.cfg.log.clone();
                        ctx.send_viewport_cmd(egui::ViewportCommand::Screenshot(
                            egui::ScreenshotCallback::new(move |img| {
                                if let Err(e) = save_png(&path, &img) {
                                    log(&format!("[menu] screenshot failed: {e}"));
                                }
                                done.store(true, Ordering::Release);
                            }),
                        ));
                        run.shot = Some(flag);
                        return Ok(false);
                    }
                    Some(flag) => {
                        if flag.load(Ordering::Acquire) {
                            run.shot = None;
                            run.report = format!("saved {name}.png");
                            run.ops.pop_front();
                        } else {
                            return Ok(false);
                        }
                    }
                },
            }
        }
    }

    fn sidebar(&mut self, ui: &mut egui::Ui) {
        ui.add_space(4.0);
        ui.label(RichText::new("Pages").color(theme::MUTED).size(14.0));
        ui.add_space(2.0);
        let mut pick = None;
        egui::ScrollArea::vertical()
            .auto_shrink([false, false])
            .show(ui, |ui| {
                ui.with_layout(egui::Layout::top_down_justified(egui::Align::LEFT), |ui| {
                    for e in &self.entries {
                        let on = self.selected.as_deref() == Some(e.id.as_str());
                        let title = if e.title.is_empty() {
                            e.id.as_str()
                        } else {
                            e.title.as_str()
                        };
                        let r = ui.add(egui::Button::selectable(on, title).truncate());
                        if r.clicked() && !on {
                            pick = Some(e.id.clone());
                        }
                    }
                });
            });
        if let Some(id) = pick {
            self.selected = Some(id);
            self.panel = None;
            self.force_copy = true;
        }
    }

    fn page(&mut self, ui: &mut egui::Ui, ctx: &egui::Context) {
        if let Some(why) = self.dead.clone() {
            ui.colored_label(theme::SEV_ERROR, why);
            return;
        }
        let entry = self
            .selected
            .as_ref()
            .and_then(|id| self.entries.iter().find(|e| &e.id == id));
        let (title, owner) = match entry {
            Some(e) => (
                if e.title.is_empty() {
                    e.id.clone()
                } else {
                    e.title.clone()
                },
                e.owner.clone(),
            ),
            None => ("No pages yet".into(), String::new()),
        };
        let head = ui.horizontal(|ui| {
            ui.label(
                RichText::new(&title)
                    .size(theme::PAGE_TITLE)
                    .color(theme::TEXT),
            );
            if !owner.is_empty() {
                ui.label(RichText::new(owner).color(theme::MUTED));
            }
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                if ui.button("Close").on_hover_text("Esc or M").clicked() {
                    self.pending_hide = true;
                }
            });
        });
        let drag = ui.interact(
            head.response.rect,
            ui.make_persistent_id("drag"),
            egui::Sense::drag(),
        );
        if drag.drag_started() {
            ctx.send_viewport_cmd(egui::ViewportCommand::StartDrag);
        }
        ui.separator();
        if let Some(e) = self.page_error.clone() {
            ui.colored_label(theme::SEV_ERROR, e);
            return;
        }
        let Some(panel) = self.panel.as_ref() else {
            ui.label(RichText::new("Nothing to show yet.").color(theme::MUTED));
            return;
        };
        let mut scene = Scene::new(panel, &mut self.local, self.frame);
        egui::ScrollArea::vertical()
            .id_salt(("page", panel.id.as_str()))
            .auto_shrink([false, false])
            .show(ui, |ui| {
                scene.draw(ui);
            });
        let (events, rects, items) = (
            std::mem::take(&mut scene.events),
            std::mem::take(&mut scene.rects),
            std::mem::take(&mut scene.items),
        );
        let pid = panel.id.clone();
        self.rects = rects;
        self.items = items;
        for e in events {
            self.push_event(&pid, e);
        }
    }
}

fn save_png(path: &std::path::Path, img: &egui::ColorImage) -> Result<(), String> {
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    }
    let file = std::fs::File::create(path).map_err(|e| e.to_string())?;
    let mut enc = png::Encoder::new(
        std::io::BufWriter::new(file),
        img.size[0] as u32,
        img.size[1] as u32,
    );
    enc.set_color(png::ColorType::Rgba);
    enc.set_depth(png::BitDepth::Eight);
    let mut w = enc.write_header().map_err(|e| e.to_string())?;
    w.write_image_data(img.as_raw()).map_err(|e| e.to_string())
}

impl eframe::App for App {
    fn raw_input_hook(&mut self, _ctx: &egui::Context, raw: &mut egui::RawInput) {
        raw.events.append(&mut self.pending_input);
    }

    fn clear_color(&self, _visuals: &egui::Visuals) -> [f32; 4] {
        theme::SOIL.to_normalized_gamma_f32()
    }

    fn ui(&mut self, root: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let ctx = &root.ctx().clone();
        self.frame += 1;
        self.shared.frames.fetch_add(1, Ordering::Release);
        if self.shared.stop.load(Ordering::Acquire) {
            ctx.send_viewport_cmd(egui::ViewportCommand::Close);
            return;
        }
        self.visible = self.shared.visible.load(Ordering::Acquire);
        self.pending_hide = false;
        self.poll();
        self.rects.clear();
        self.items.clear();
        egui::Panel::left("nav")
            .exact_size(200.0)
            .resizable(false)
            .frame(
                Frame::new()
                    .fill(theme::PANEL)
                    .inner_margin(Margin::same(8)),
            )
            .show(root, |ui| self.sidebar(ui));
        egui::Panel::bottom("status")
            .frame(
                Frame::new()
                    .fill(Color32::from_rgb(0x0B, 0x14, 0x0D))
                    .inner_margin(Margin::symmetric(12, 6)),
            )
            .show(root, |ui| {
                let r = ui.max_rect();
                ui.painter().rect_filled(
                    egui::Rect::from_min_size(
                        egui::pos2(r.left() - 12.0, r.top() - 6.0),
                        egui::vec2(r.width() + 24.0, 4.0),
                    ),
                    0.0,
                    theme::LEAF_DEEP,
                );
                if self
                    .notice
                    .as_ref()
                    .is_some_and(|(_, t)| t.elapsed() > Duration::from_secs(3))
                {
                    self.notice = None;
                }
                let text = match &self.notice {
                    Some((m, _)) => m.clone(),
                    None => format!("{} pages", self.entries.len()),
                };
                ui.label(RichText::new(text).color(theme::MUTED).size(14.0));
            });
        egui::CentralPanel::default()
            .frame(
                Frame::new()
                    .fill(theme::SOIL)
                    .inner_margin(Margin::symmetric(16, 12)),
            )
            .show(root, |ui| self.page(ui, ctx));
        self.flush();
        self.drive(ctx);
        let typing = ctx.egui_wants_keyboard_input();
        self.shared
            .typing
            .store(typing && self.visible, Ordering::Release);
        let esc = ctx.input(|i| i.key_pressed(egui::Key::Escape));
        if (esc && !typing && !self.cfg.test_mode) || self.pending_hide {
            self.set_visible(false);
        }
        // Poll while visible even without input, so a plugin-driven revision shows within one interval.
        ctx.request_repaint_after(Duration::from_millis(if self.visible { 33 } else { 50 }));
    }
}

#[cfg(test)]
impl App {
    pub(crate) fn test_poll(&mut self) {
        self.poll();
    }
    pub(crate) fn test_page(&self) -> Option<&Panel> {
        self.panel.as_ref()
    }
    pub(crate) fn test_local(&mut self) -> &mut HashMap<u64, Local> {
        &mut self.local
    }
    pub(crate) fn test_send(&mut self, e: Event) {
        let panel = self.selected.clone().unwrap_or_default();
        self.push_event(&panel, e);
    }
    pub(crate) fn test_flush(&mut self) {
        self.flush();
    }
    pub(crate) fn test_next_frame(&mut self) {
        self.frame += 2;
    }
    pub(crate) fn test_outbox_len(&self) -> usize {
        self.outbox.len()
    }
    pub(crate) fn test_dead(&self) -> Option<&String> {
        self.dead.as_ref()
    }
}
