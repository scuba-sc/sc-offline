// SPDX-License-Identifier: GPL-3.0-only
//! The egui app of the menu window, laid out like the ImGui shell in `src/menu_shell.cpp`: a title
//! bar, a sidebar of pages under their section names (`nav.rs`), the open page under its header and
//! one-line summary, and a status strip along the bottom that shows the host's `menu.status` panel.
//! It owns the poll loop (stamps, list, copy), the optimistic local values with their
//! acknowledgement by `events_done`, the event outbox with its retry rule, the window rules (size,
//! edges, cursor clip, focus) and the test driver.
use crate::blob::Panel;
use crate::ffi::*;
use crate::nav::{self, Page, Size, Source, ViewState};
use crate::render::{self, ItemRect, Local, NodeRect, Scene};
use crate::script::{self, Op, Target, Where};
use crate::theme;
use crate::view::{Entry, Event, Stamps, View};
use crate::{win, Shared};
use egui::{Color32, Frame, Margin, RichText};
use std::collections::{HashMap, VecDeque};
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::Sender;
use std::sync::Arc;
use std::time::{Duration, Instant};

pub type LogFn = Arc<dyn Fn(&str) + Send + Sync>;

/// Calls into the C++ host. Every one may be absent (tests, the stand-alone example).
#[derive(Clone, Default)]
pub struct Host {
    /// Opens the ImGui window for `sco.ui` tabs ("Classic pages").
    pub open_classic: Option<Arc<dyn Fn() + Send + Sync>>,
    /// The game window handle (0 when not known yet), for the first position, the work area and for
    /// giving the foreground back on hide.
    pub game_window: Option<Arc<dyn Fn() -> isize + Send + Sync>>,
    /// True when the game's own ship list can be read (Core's `game.asop.selection.file`).
    pub asop_available: Option<Arc<dyn Fn() -> bool + Send + Sync>>,
    /// True while the ImGui window is up (or just went down): the open key then leaves this menu closed.
    pub classic_visible: Option<Arc<dyn Fn() -> bool + Send + Sync>>,
}

pub struct Config {
    pub view: View,
    pub log: LogFn,
    pub nudge: Option<Arc<dyn Fn() + Send + Sync>>,
    pub shot_dir: Option<PathBuf>,
    pub start_visible: bool,
    pub test_mode: bool,
    /// Virtual-key code that toggles the window (0 = none).
    pub hotkey: u32,
    /// The window title, also the text in the title bar.
    pub title: String,
    /// `menu-view.ini` and `menu-window.ini` of the C++ menu (the same files), or None.
    pub view_path: Option<PathBuf>,
    pub size_path: Option<PathBuf>,
    pub host: Host,
}

impl Config {
    pub fn new(view: View, log: LogFn) -> Config {
        Config {
            view,
            log,
            nudge: None,
            shot_dir: None,
            start_visible: false,
            test_mode: false,
            hotkey: 0,
            title: "sc-offline".into(),
            view_path: None,
            size_path: None,
            host: Host::default(),
        }
    }
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

const STATUS_PANEL: &str = "menu.status";
const MAIN_PANEL: &str = "menu.main";
const ASOP_PANEL: &str = "spawn.asop";
const VEHICLES_PAGE: &str = "spawn.vehicles";
const TITLE_BAR: f32 = 30.0;

pub struct App {
    cfg: Config,
    shared: Arc<Shared>,
    frame: u64,
    stamps: Option<Stamps>,
    entries: Vec<Entry>,
    pages: Vec<Page>,
    view_state: ViewState,
    view_dirty: bool,
    /// The panel of the open page ("menu.main" for the Settings page).
    panel: Option<Panel>,
    /// "Your own ships" under the vehicles page.
    extra: Option<Panel>,
    status: Option<Panel>,
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
    was_visible: bool,
    last_pointer: Option<egui::Pos2>,
    pending_hide: bool,
    // window
    saved_size: Size,
    applied: Option<Size>,
    last_inner: Option<Size>,
    size_changed_at: Option<Instant>,
    moving: bool,
    hwnd_ready: bool,
    work: (i64, i64),
    classic_requests: u32,
    /// The visible part of the page's scroll area, its content height and offset (test driver).
    page_view: egui::Rect,
    page_content: f32,
    page_offset: f32,
    want_scroll: Option<f32>,
}

impl App {
    pub fn new(cfg: Config, shared: Arc<Shared>) -> Self {
        let visible = cfg.start_visible;
        let view_state = cfg
            .view_path
            .as_deref()
            .map(nav::load_view)
            .unwrap_or_default();
        let saved_size = cfg
            .size_path
            .as_deref()
            .map(nav::load_size)
            .unwrap_or(nav::DEFAULT_SIZE);
        if cfg.view_path.is_some() {
            (cfg.log)(&format!(
                "[menu] view restore: page {}, developer pages {}",
                if view_state.page.is_empty() {
                    "(first)"
                } else {
                    &view_state.page
                },
                if view_state.developer {
                    "shown"
                } else {
                    "hidden"
                }
            ));
        }
        App {
            cfg,
            shared,
            frame: 0,
            stamps: None,
            entries: Vec::new(),
            pages: Vec::new(),
            view_state,
            view_dirty: false,
            panel: None,
            extra: None,
            status: None,
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
            was_visible: false,
            last_pointer: None,
            pending_hide: false,
            saved_size,
            applied: None,
            last_inner: None,
            size_changed_at: None,
            moving: false,
            hwnd_ready: false,
            work: (1920, 1080),
            classic_requests: 0,
            page_view: egui::Rect::NOTHING,
            page_content: 0.0,
            page_offset: 0.0,
            want_scroll: None,
        }
    }

    fn log(&self, msg: &str) {
        (self.cfg.log)(msg);
    }

    fn say(&mut self, msg: impl Into<String>) {
        self.notice = Some((msg.into(), Instant::now()));
    }

    fn selected(&self) -> &str {
        &self.view_state.page
    }

    fn asop_ok(&self) -> bool {
        self.cfg.host.asop_available.as_ref().is_none_or(|f| f())
    }

    /// The sidebar pages: every listed panel that is not host-owned, the ASOP panel only when it is
    /// not grouped under the vehicles page, the menu's own settings and the classic entry.
    fn rebuild_pages(&mut self) {
        let asop = self.asop_ok();
        let grouped = asop
            && self
                .entries
                .iter()
                .any(|e| e.id == VEHICLES_PAGE && e.owner == "spawn")
            && self
                .entries
                .iter()
                .any(|e| e.id == ASOP_PANEL && e.owner == "spawn");
        let mut pages = Vec::new();
        let mut arrival = 0;
        for e in &self.entries {
            if e.flags & PANEL_HOST_OWNED != 0 || e.id == STATUS_PANEL || e.id == MAIN_PANEL {
                continue;
            }
            // The ship list of the game itself: drawn under the vehicles page, or left out when
            // its provider is not there.
            if e.id == ASOP_PANEL && e.owner == "spawn" && (grouped || !asop) {
                continue;
            }
            pages.push(nav::describe(&e.id, &e.title, arrival, Source::Panel));
            arrival += 1;
        }
        pages.push(nav::describe(
            nav::SETTINGS_PAGE,
            "Menu",
            arrival,
            Source::Settings,
        ));
        arrival += 1;
        if self.cfg.host.open_classic.is_some() {
            pages.push(Page {
                id: nav::CLASSIC_ENTRY.into(),
                title: "Classic pages".into(),
                summary:
                    "A few pages still use the older window. They open there; press M to close it."
                        .into(),
                section: nav::Section::Settings,
                rank: 20,
                arrival,
                source: Source::Classic,
            });
        }
        self.pages = nav::arrange(pages, self.view_state.developer);
        let chosen = nav::choose(&self.pages, &self.view_state.page);
        if chosen != self.view_state.page {
            // The saved page is gone (or none was saved): the first page, which is not saved until
            // the player picks one.
            self.view_state.page = chosen;
            self.panel = None;
            self.extra = None;
            self.force_copy = true;
        }
    }

    /// The panel that holds the open page's nodes.
    fn page_panel_id(&self) -> Option<String> {
        let sel = self.selected();
        let id = if sel == nav::SETTINGS_PAGE {
            MAIN_PANEL
        } else {
            sel
        };
        if id.is_empty() {
            return None;
        }
        self.entries
            .iter()
            .find(|e| e.id == id)
            .map(|e| e.id.clone())
    }

    fn select(&mut self, id: &str) {
        if self.view_state.page == id {
            return;
        }
        self.view_state.page = id.to_string();
        self.view_dirty = true;
        self.panel = None;
        self.extra = None;
        self.force_copy = true;
    }

    fn save_view(&mut self) {
        if !self.view_dirty {
            return;
        }
        self.view_dirty = false;
        if let Some(p) = self.cfg.view_path.clone() {
            if !nav::save_view(&p, &self.view_state) {
                self.log("[menu] couldn't save menu-view.ini; the open page will not persist");
            }
        }
    }

    fn open_classic(&mut self) {
        if let Some(f) = self.cfg.host.open_classic.clone() {
            self.classic_requests += 1;
            self.log("[menu] opening the classic pages (ImGui window)");
            f();
            self.set_visible(false);
        }
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
        let wanted = self.page_panel_id();
        if !(first || moved || self.force_copy || (self.panel.is_none() && wanted.is_some())) {
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
        self.rebuild_pages();
        let force = std::mem::take(&mut self.force_copy);
        // Open page.
        match self.page_panel_id() {
            Some(id) => {
                let entry = self.entries.iter().find(|e| e.id == id).cloned();
                if Self::stale(&self.panel, entry.as_ref()) || force {
                    match self.cfg.view.copy(&id) {
                        Ok(p) => {
                            self.panel = Some(p);
                            self.page_error = None;
                        }
                        Err(e) => {
                            self.panel = None;
                            self.page_error = Some(format!("This page could not be read: {e}"));
                            self.log(&format!("[menu] page {id}: {e}"));
                        }
                    }
                }
            }
            None => {
                self.panel = None;
                self.page_error = None;
            }
        }
        // "Your own ships" under the vehicles page.
        let extra_id = (self.selected() == VEHICLES_PAGE
            && self.asop_ok()
            && self
                .entries
                .iter()
                .any(|e| e.id == ASOP_PANEL && e.owner == "spawn"))
        .then(|| ASOP_PANEL.to_string());
        match extra_id {
            Some(id) => {
                let entry = self.entries.iter().find(|e| e.id == id).cloned();
                if Self::stale(&self.extra, entry.as_ref()) || force {
                    self.extra = self.cfg.view.copy(&id).ok();
                }
            }
            None => self.extra = None,
        }
        // The status strip.
        let entry = self.entries.iter().find(|e| e.id == STATUS_PANEL).cloned();
        match entry {
            Some(e) => {
                if Self::stale(&self.status, Some(&e)) || force {
                    self.status = self.cfg.view.copy(STATUS_PANEL).ok();
                }
            }
            None => self.status = None,
        }
    }

    fn stale(cur: &Option<Panel>, entry: Option<&Entry>) -> bool {
        match (cur, entry) {
            (Some(p), Some(e)) => p.id != e.id || p.revision != e.revision || p.epoch != e.epoch,
            (None, Some(_)) => true,
            (Some(_), None) => true,
            _ => false,
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

    // ---- window rules ----

    fn game_window(&self) -> isize {
        self.cfg.host.game_window.as_ref().map_or(0, |f| f())
    }

    /// Work area of the monitor the window is on, in points (96 dpi logical), and the game window's
    /// top-left in points for the first position.
    fn measure(&mut self, ctx: &egui::Context) -> (i64, i64, Option<egui::Pos2>) {
        let ppp = ctx.pixels_per_point().max(0.25);
        let game = self.game_window();
        let anchor = if game != 0 { game } else { win::menu_hwnd() };
        if let Some(w) = win::work_area(anchor) {
            self.work = (
                (w.width() as f32 / ppp) as i64,
                (w.height() as f32 / ppp) as i64,
            );
        } else if let Some(m) = ctx.input(|i| i.viewport().monitor_size) {
            self.work = (m.x as i64, m.y as i64);
        }
        let origin = win::window_rect(game)
            .filter(|_| game != 0)
            .map(|r| egui::pos2(r.left as f32 / ppp, r.top as f32 / ppp));
        (self.work.0, self.work.1, origin)
    }

    fn on_show(&mut self, ctx: &egui::Context) {
        let (ww, wh, origin) = self.measure(ctx);
        let size = nav::restore_size(self.saved_size, ww, wh);
        let min = nav::clamp_size(
            Size {
                width: 1,
                height: 1,
            },
            ww,
            wh,
        );
        let max = nav::clamp_size(
            Size {
                width: 32768,
                height: 32768,
            },
            ww,
            wh,
        );
        let v = |s: Size| egui::vec2(s.width as f32, s.height as f32);
        ctx.send_viewport_cmd(egui::ViewportCommand::MinInnerSize(v(min)));
        ctx.send_viewport_cmd(egui::ViewportCommand::MaxInnerSize(v(max)));
        ctx.send_viewport_cmd(egui::ViewportCommand::InnerSize(v(size)));
        self.applied = Some(size);
        self.last_inner = None;
        self.size_changed_at = None;
        self.log(&format!(
            "[menu] size restore: saved {}x{} -> {}x{} points, work area {}x{}",
            self.saved_size.width, self.saved_size.height, size.width, size.height, ww, wh
        ));
        if !self.cfg.test_mode {
            // Near the game window's corner, kept inside the work area.
            let o = origin.unwrap_or(egui::pos2(60.0, 60.0));
            let x = (o.x + 60.0).clamp(0.0, (ww - size.width as i64).max(0) as f32);
            let y = (o.y + 60.0).clamp(0.0, (wh - size.height as i64).max(0) as f32);
            ctx.send_viewport_cmd(egui::ViewportCommand::OuterPosition(egui::pos2(x, y)));
            win::bring_to_front();
        }
        ctx.send_viewport_cmd(egui::ViewportCommand::Focus);
    }

    fn on_hide(&mut self) {
        self.moving = false;
        win::release_clip();
        if !self.cfg.test_mode {
            let g = self.game_window();
            win::focus(g);
        }
    }

    fn reset_size(&mut self, ctx: &egui::Context) {
        let (ww, wh, _) = self.measure(ctx);
        let size = nav::clamp_size(nav::DEFAULT_SIZE, ww, wh);
        ctx.send_viewport_cmd(egui::ViewportCommand::InnerSize(egui::vec2(
            size.width as f32,
            size.height as f32,
        )));
        let remembered = nav::remember_size(size, ww, wh);
        self.saved_size = remembered;
        self.applied = Some(size);
        if let Some(p) = self.cfg.size_path.clone() {
            if !nav::save_size(&p, remembered) {
                self.log("[menu] couldn't save menu-window.ini; size will not persist");
            }
        }
        self.say("Window size reset");
    }

    /// Size persistence, the cursor clip and the resize edges. Runs every frame while shown.
    fn window_rules(&mut self, ctx: &egui::Context) {
        if !self.hwnd_ready && win::find_menu(&self.cfg.title) {
            self.hwnd_ready = true;
            win::style_frame();
        }
        if self.visible && !self.was_visible {
            self.on_show(ctx);
        } else if !self.visible && self.was_visible {
            self.on_hide();
        }
        self.was_visible = self.visible;
        if !self.visible {
            return;
        }
        // A resize ends when the size stopped changing and the button is up.
        if let Some(inner) = ctx.input(|i| i.viewport().inner_rect) {
            let cur = Size {
                width: inner.width().round() as i32,
                height: inner.height().round() as i32,
            };
            if self.last_inner != Some(cur) {
                self.last_inner = Some(cur);
                self.size_changed_at = Some(Instant::now());
            }
            if self
                .size_changed_at
                .is_some_and(|t| t.elapsed() > Duration::from_millis(250))
                && !win::left_button_down()
            {
                self.size_changed_at = None;
                // A resize that ends at the size the restore applied changed nothing.
                if self.applied != Some(cur) {
                    let (ww, wh) = self.work;
                    let r = nav::remember_size(cur, ww, wh);
                    self.applied = Some(cur);
                    if r != self.saved_size {
                        self.saved_size = r;
                        if let Some(p) = self.cfg.size_path.clone() {
                            if !nav::save_size(&p, r) {
                                self.log(
                                    "[menu] couldn't save menu-window.ini; size will not persist",
                                );
                            }
                        }
                    }
                }
            }
        }
        // Edges: the window has no system frame, so a press on the outer 5 points starts a resize.
        if let Some(p) = ctx.input(|i| i.pointer.hover_pos()) {
            let r = ctx.content_rect();
            if let Some(e) = nav::hit_test(
                p.x - r.left(),
                p.y - r.top(),
                r.width(),
                r.height(),
                nav::EDGE_BASE,
            ) {
                use egui::{CursorIcon as C, ResizeDirection as D};
                let (icon, dir) = match e {
                    nav::Edge::Left => (C::ResizeHorizontal, D::West),
                    nav::Edge::Right => (C::ResizeHorizontal, D::East),
                    nav::Edge::Top => (C::ResizeVertical, D::North),
                    nav::Edge::Bottom => (C::ResizeVertical, D::South),
                    nav::Edge::TopLeft => (C::ResizeNwSe, D::NorthWest),
                    nav::Edge::BottomRight => (C::ResizeNwSe, D::SouthEast),
                    nav::Edge::TopRight => (C::ResizeNeSw, D::NorthEast),
                    nav::Edge::BottomLeft => (C::ResizeNeSw, D::SouthWest),
                };
                ctx.set_cursor_icon(icon);
                if ctx.input(|i| i.pointer.primary_pressed()) {
                    ctx.send_viewport_cmd(egui::ViewportCommand::BeginResize(dir));
                    self.moving = true;
                    win::release_clip();
                }
            }
        }
        // The cursor stays inside the menu while it is in front; it is free while the window is
        // being moved or resized, and when another window has the focus.
        if !self.cfg.test_mode {
            let hwnd = win::menu_hwnd();
            if self.moving && !win::left_button_down() {
                self.moving = false;
            }
            if !win::is_foreground(hwnd) || self.moving {
                win::release_clip();
            } else {
                win::clip_to_menu();
            }
        }
    }

    // ---- test driver ----

    fn find_rect(&self, t: &Target) -> Option<&NodeRect> {
        self.rects.iter().find(|r| match t {
            Target::Tag(tag) => &r.tag == tag,
            Target::Label(l) => &r.label == l,
        })
    }

    /// True when the rect lies in the page column but outside the visible part of its scroll area;
    /// returns the offset that brings it to the middle.
    fn scroll_needed(&self, r: egui::Rect, page: bool) -> Option<f32> {
        if !page {
            return None;
        }
        let v = self.page_view;
        if v.is_negative() || r.min.x < v.min.x {
            return None;
        }
        let inside = r.min.y >= v.min.y + 2.0 && r.max.y <= v.max.y - 2.0;
        if inside {
            return None;
        }
        let max = (self.page_content - v.height()).max(0.0);
        Some((self.page_offset + r.center().y - v.center().y).clamp(0.0, max))
    }

    fn resolve(&mut self, w: &Where) -> Option<egui::Pos2> {
        let (rect, page) = match w {
            Where::Node(t) | Where::Slider(t, _) => self.find_rect(t).map(|r| (r.rect, r.page)),
            Where::Item(t, i) => {
                let n = self.find_rect(t)?;
                let (key, page) = (n.key, n.page);
                self.items
                    .iter()
                    .find(|r| r.key == key && r.index == *i)
                    .map(|r| (r.rect, page))
            }
        }?;
        if let Some(y) = self.scroll_needed(rect, page) {
            self.want_scroll = Some(y);
            return None;
        }
        self.resolve_inner(w)
    }

    fn resolve_inner(&self, w: &Where) -> Option<egui::Pos2> {
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
                    if !self
                        .pages
                        .iter()
                        .any(|p| p.id == id && p.source != Source::Classic)
                    {
                        return if run.waited < 120 {
                            run.waited += 1;
                            Ok(false)
                        } else {
                            Err(format!("no page '{id}'"))
                        };
                    }
                    self.select(&id);
                    run.ops.pop_front();
                    run.ops.push_front(Op::AwaitPanel(id));
                }
                Op::AwaitPanel(id) => {
                    let ready = self.selected() == id
                        && (self.panel.is_some() || self.page_panel_id().is_none());
                    if ready {
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
                Op::Scroll(f) => {
                    let max = (self.page_content - self.page_view.height()).max(0.0);
                    self.want_scroll = Some((max as f64 * f) as f32);
                    run.ops.pop_front();
                    run.ops.push_front(Op::Frames(3));
                }
                Op::Report => {
                    run.report = format!(
                        "page={} nodes={} entries={} content={:.0} view={:.0} classic={} developer={}",
                        self.selected(),
                        self.panel.as_ref().map_or(0, |p| p.nodes.len()),
                        self.pages.len(),
                        self.page_content,
                        self.page_view.height(),
                        self.classic_requests,
                        u8::from(self.view_state.developer)
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

    // ---- drawing ----

    /// Records a control of the shell itself (sidebar entry, settings button) so the test driver
    /// can click it by tag, like a plugin's node.
    fn record_ui(&mut self, tag: &str, label: &str, rect: egui::Rect) {
        self.record_ui_in(tag, label, rect, false);
    }

    fn record_ui_in(&mut self, tag: &str, label: &str, rect: egui::Rect, page: bool) {
        self.rects.push(NodeRect {
            key: 0,
            id: 0,
            tag: tag.to_string(),
            label: label.to_string(),
            kind: 0,
            rect,
            rail: 0.0,
            page,
        });
    }

    fn title_bar(&mut self, ui: &mut egui::Ui, ctx: &egui::Context) {
        let full = ui.max_rect();
        let drag = ui.interact(
            full,
            ui.make_persistent_id("title-drag"),
            egui::Sense::drag(),
        );
        if drag.drag_started() && !self.cfg.test_mode {
            ctx.send_viewport_cmd(egui::ViewportCommand::StartDrag);
            self.moving = true;
            win::release_clip();
        }
        let painter = ui.painter();
        painter.text(
            egui::pos2(full.left() + 14.0, full.center().y),
            egui::Align2::LEFT_CENTER,
            &self.cfg.title,
            egui::TextStyle::Body.resolve(ui.style()),
            theme::TEXT,
        );
        painter.line_segment(
            [
                egui::pos2(full.left(), full.bottom() - 0.5),
                egui::pos2(full.right(), full.bottom() - 0.5),
            ],
            egui::Stroke::new(1.0, theme::BORDER),
        );
        // The close button sits clear of the 5-point resize band at the top and right edges.
        let side = TITLE_BAR - 10.0;
        let rect = egui::Rect::from_min_size(
            egui::pos2(full.right() - side - 8.0, full.top() + 6.0),
            egui::vec2(side, side - 2.0),
        );
        let r = ui.interact(
            rect,
            ui.make_persistent_id("title-close"),
            egui::Sense::click(),
        );
        let fill = if r.is_pointer_button_down_on() {
            theme::LEAF_DEEP
        } else if r.hovered() {
            theme::BUTTON_HOVER
        } else {
            Color32::TRANSPARENT
        };
        let p = ui.painter();
        p.rect_filled(rect, 4.0, fill);
        let c = rect.center();
        let d = 5.0;
        let stroke = egui::Stroke::new(1.5, theme::TEXT);
        p.line_segment([c + egui::vec2(-d, -d), c + egui::vec2(d, d)], stroke);
        p.line_segment([c + egui::vec2(-d, d), c + egui::vec2(d, -d)], stroke);
        let r = r.on_hover_text("Esc or M");
        self.record_ui("nav.close", "Close", rect);
        if r.clicked() {
            self.pending_hide = true;
        }
    }

    fn sidebar(&mut self, ui: &mut egui::Ui) {
        let mut pick: Option<(String, Source)> = None;
        let mut recs: Vec<(String, String, egui::Rect)> = Vec::new();
        let selected = self.selected().to_string();
        egui::ScrollArea::vertical()
            .id_salt("sidebar")
            .auto_shrink([false, false])
            .show(ui, |ui| {
                let mut section = None;
                for (i, p) in self.pages.iter().enumerate() {
                    if section != Some(p.section) {
                        if i > 0 {
                            ui.add_space(6.0);
                        }
                        ui.label(RichText::new(p.section.title()).color(theme::MUTED));
                        section = Some(p.section);
                    }
                    let on = p.source != Source::Classic && selected == p.id;
                    let r = side_item(ui, &p.title, on, &p.summary);
                    recs.push((format!("nav:{}", p.id), p.title.clone(), r.rect));
                    if r.clicked() {
                        pick = Some((p.id.clone(), p.source));
                    }
                }
            });
        for (tag, label, rect) in recs {
            self.record_ui(&tag, &label, rect);
        }
        match pick {
            Some((_, Source::Classic)) => self.open_classic(),
            Some((id, _)) => self.select(&id),
            None => {}
        }
    }

    /// The Settings page: the host's `menu.main` panel (background, About), then the switches the
    /// menu keeps itself.
    fn settings_extras(&mut self, ui: &mut egui::Ui, ctx: &egui::Context) {
        render::heading(ui, "Developer pages");
        let mut dev = self.view_state.developer;
        let r = ui.checkbox(&mut dev, "Show developer pages");
        self.record_ui_in("settings.developer", "Show developer pages", r.rect, true);
        if r.changed() {
            self.view_state.developer = dev;
            self.view_dirty = true;
            self.force_copy = true;
            self.rebuild_pages();
        }
        hint(
            ui,
            "Adds pages for testing and finding bugs, such as the Inspector. They show raw game data and are not needed to play.",
        );
        render::heading(ui, "Window");
        let r = ui.button("Reset window size");
        self.record_ui_in("settings.resetsize", "Reset window size", r.rect, true);
        if r.clicked() {
            self.reset_size(ctx);
        }
        hint(
            ui,
            "Drag any edge of the window to resize it. The size is remembered.",
        );
        if self.cfg.host.open_classic.is_some() {
            render::heading(ui, "Classic pages");
            let r = ui.button("Open classic pages");
            self.record_ui_in("settings.classic", "Open classic pages", r.rect, true);
            if r.clicked() {
                self.open_classic();
            }
            hint(
                ui,
                "A few pages still use the older window. They open there; press M to close it.",
            );
        }
    }

    fn page(&mut self, ui: &mut egui::Ui, ctx: &egui::Context) {
        if let Some(why) = self.dead.clone() {
            ui.colored_label(theme::SEV_ERROR, why);
            return;
        }
        let cur = self.pages.iter().find(|p| p.id == self.selected()).cloned();
        let Some(page) = cur else {
            ui.label(RichText::new("No pages yet.").color(theme::MUTED));
            return;
        };
        ui.label(
            RichText::new(&page.title)
                .size(theme::PAGE_TITLE)
                .color(theme::TEXT),
        );
        if !page.summary.is_empty() {
            hint(ui, &page.summary);
        }
        ui.separator();
        ui.add_space(2.0);
        let is_settings = page.id == nav::SETTINGS_PAGE;
        let mut batch: Vec<(String, Vec<Event>)> = Vec::new();
        let mut rects = Vec::new();
        let mut items = Vec::new();
        let mut area = egui::ScrollArea::vertical()
            .id_salt(("page", page.id.as_str()))
            .auto_shrink([false, false]);
        if let Some(y) = self.want_scroll.take() {
            area = area.vertical_scroll_offset(y);
        }
        let out = area.show(ui, |ui| {
            if let Some(e) = self.page_error.clone() {
                ui.colored_label(theme::SEV_ERROR, e);
            } else if let Some(panel) = self.panel.as_ref() {
                let mut scene = Scene::new(panel, &mut self.local, self.frame);
                scene.draw(ui);
                batch.push((panel.id.clone(), std::mem::take(&mut scene.events)));
                rects.append(&mut scene.rects);
                items.append(&mut scene.items);
            } else if !is_settings {
                ui.label(RichText::new("Nothing to show yet.").color(theme::MUTED));
            } else {
                hint(ui, "Menu settings are unavailable.");
            }
            if let Some(panel) = self.extra.as_ref() {
                render::heading(ui, "Your own ships");
                let mut scene = Scene::new(panel, &mut self.local, self.frame);
                scene.draw(ui);
                batch.push((panel.id.clone(), std::mem::take(&mut scene.events)));
                rects.append(&mut scene.rects);
                items.append(&mut scene.items);
            }
            if is_settings {
                self.settings_extras(ui, ctx);
            }
        });
        self.page_view = out.inner_rect;
        self.page_content = out.content_size.y;
        self.page_offset = out.state.clamped_offset().y;
        self.rects.append(&mut rects);
        self.items.append(&mut items);
        for (pid, events) in batch {
            for e in events {
                self.push_event(&pid, e);
            }
        }
    }

    fn status_strip(&mut self, ui: &mut egui::Ui) {
        let r = ui.max_rect();
        ui.painter().rect_filled(
            egui::Rect::from_min_size(
                egui::pos2(r.left() - 16.0, r.top() - 8.0),
                egui::vec2(4.0, r.height() + 16.0),
            ),
            0.0,
            theme::LEAF,
        );
        if self
            .notice
            .as_ref()
            .is_some_and(|(_, t)| t.elapsed() > Duration::from_secs(3))
        {
            self.notice = None;
        }
        if let Some((m, _)) = &self.notice {
            ui.with_layout(egui::Layout::right_to_left(egui::Align::TOP), |ui| {
                ui.label(RichText::new(m.as_str()).color(theme::SEV_WARN));
            });
        }
        let mut events = Vec::new();
        let mut rects = Vec::new();
        let mut items = Vec::new();
        let mut pid = String::new();
        match self.status.as_ref() {
            Some(panel) => {
                let mut scene = Scene::new(panel, &mut self.local, self.frame);
                // The strip is two lines tall; a longer status scrolls behind the strip's edge.
                ui.scope(|ui| {
                    ui.set_clip_rect(ui.max_rect());
                    scene.draw(ui);
                });
                events = std::mem::take(&mut scene.events);
                rects.append(&mut scene.rects);
                items.append(&mut scene.items);
                pid = panel.id.clone();
            }
            None => {
                ui.label(RichText::new("Status unavailable.").color(theme::MUTED));
            }
        }
        self.rects.append(&mut rects);
        self.items.append(&mut items);
        for e in events {
            self.push_event(&pid, e);
        }
    }

    /// One frame of the shell. Split out so a panic in it can be caught (see `ui`).
    fn frame_body(&mut self, root: &mut egui::Ui) {
        let ctx = &root.ctx().clone();
        self.frame += 1;
        self.shared.frames.fetch_add(1, Ordering::Release);
        if self.shared.stop.load(Ordering::Acquire) {
            win::release_clip();
            ctx.send_viewport_cmd(egui::ViewportCommand::Close);
            return;
        }
        self.visible = self.shared.visible.load(Ordering::Acquire);
        self.pending_hide = false;
        self.poll();
        self.rects.clear();
        self.items.clear();
        self.window_rules(ctx);
        egui::Panel::top("title")
            .exact_size(TITLE_BAR)
            .resizable(false)
            .frame(Frame::new().fill(theme::TITLE))
            .show(root, |ui| self.title_bar(ui, ctx));
        let strip = ctx.global_style().text_styles[&egui::TextStyle::Body].size * 2.0 + 18.0;
        egui::Panel::bottom("status")
            .exact_size(strip)
            .resizable(false)
            .frame(
                Frame::new()
                    .fill(theme::STATUS)
                    .inner_margin(Margin::symmetric(16, 8)),
            )
            .show(root, |ui| self.status_strip(ui));
        let side_w = (ctx.global_style().text_styles[&egui::TextStyle::Body].size * 11.0)
            .clamp(170.0, 260.0);
        egui::Panel::left("nav")
            .exact_size(side_w)
            .resizable(false)
            .frame(
                Frame::new()
                    .fill(theme::SIDEBAR)
                    .inner_margin(Margin::same(10)),
            )
            .show(root, |ui| self.sidebar(ui));
        egui::CentralPanel::default()
            .frame(
                Frame::new()
                    .fill(theme::SOIL)
                    .inner_margin(Margin::symmetric(14, 8)),
            )
            .show(root, |ui| self.page(ui, ctx));
        self.save_view();
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

/// A sidebar row: the page title cut with an ellipsis to the row, the page summary (and the whole
/// title when it was cut) as the tooltip.
fn side_item(ui: &mut egui::Ui, title: &str, selected: bool, summary: &str) -> egui::Response {
    let font = egui::TextStyle::Body.resolve(ui.style());
    let pad = 8.0;
    let width = ui.available_width();
    let mut job = egui::text::LayoutJob::single_section(
        format!("  {title}"),
        egui::TextFormat::simple(font, theme::TEXT),
    );
    job.wrap.max_width = width - 2.0 * pad;
    job.wrap.max_rows = 1;
    job.wrap.break_anywhere = true;
    job.wrap.overflow_character = Some('\u{2026}');
    let galley = ui.painter().layout_job(job);
    let elided = galley.elided;
    let h = galley.size().y + 6.0;
    let (rect, resp) = ui.allocate_exact_size(egui::vec2(width, h), egui::Sense::click());
    if selected {
        ui.painter().rect_filled(rect, 4.0, theme::HEADER);
    } else if resp.hovered() {
        ui.painter().rect_filled(rect, 4.0, theme::HEADER_HOVER);
    }
    ui.painter().galley(
        egui::pos2(rect.left() + pad, rect.center().y - galley.size().y / 2.0),
        galley,
        theme::TEXT,
    );
    let tip = match (elided, summary.is_empty()) {
        (true, false) => format!("{title}\n{summary}"),
        (true, true) => title.to_string(),
        (false, false) => summary.to_string(),
        (false, true) => return resp,
    };
    resp.on_hover_text(tip)
}

/// A muted wrapped line (`Hint` in menu_shell.cpp).
fn hint(ui: &mut egui::Ui, text: &str) {
    ui.add(egui::Label::new(RichText::new(text).color(theme::MUTED)).wrap());
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
        // A panic in one frame must not unwind through the window procedure into the game: it is
        // caught here, the menu says so and reports itself broken (the host then gives M back to
        // the ImGui menu), and the next frames only show the message.
        let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| self.frame_body(root)));
        if r.is_err() {
            self.log(
                "[menu] a panic in a frame; the menu stops drawing pages and reports itself broken",
            );
            self.dead = Some("The menu hit an internal error. Press Esc to close it.".into());
            self.shared.broken.store(true, Ordering::Release);
            win::release_clip();
        }
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
        let panel = self.selected().to_string();
        self.push_event(&panel, e);
    }
    pub(crate) fn test_send_to(&mut self, panel: &str, e: Event) {
        self.push_event(panel, e);
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
    pub(crate) fn test_pages(&self) -> Vec<(String, String)> {
        self.pages
            .iter()
            .map(|p| (p.section.title().to_string(), p.id.clone()))
            .collect()
    }
    pub(crate) fn test_selected(&self) -> String {
        self.selected().to_string()
    }
    pub(crate) fn test_select(&mut self, id: &str) {
        self.select(id);
        self.force_copy = true;
    }
    pub(crate) fn test_status(&self) -> Option<&Panel> {
        self.status.as_ref()
    }
    pub(crate) fn test_extra(&self) -> Option<&Panel> {
        self.extra.as_ref()
    }
    pub(crate) fn test_save_view(&mut self) {
        self.save_view();
    }
    pub(crate) fn test_set_developer(&mut self, on: bool) {
        self.view_state.developer = on;
        self.view_dirty = true;
        self.force_copy = true;
        self.rebuild_pages();
    }
}
