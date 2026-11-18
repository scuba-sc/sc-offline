// SPDX-License-Identifier: GPL-3.0-only
//! Draws one parsed panel with egui. This is the port of Core's `imgui_adapter.cpp` and the pure
//! rules of `imgui_layout.h`: label column (widest sibling label, at most 40 percent of the width,
//! at least 100 px), control width (the rest of the row, at least 140 px, capped at 560 px in a
//! container wider than 900 px), severity spelled out as text, section headers for labelled ROW and
//! COLUMN nodes, a ROW of COLUMNs shares its width, paging with "Rows a-b of n" and Previous/Next.
//!
//! The renderer never sends anything itself. It collects `view::Event` values for the app, and it
//! records the screen rectangle of every interactive node so the test driver can click them.
use crate::blob::{Node, Panel};
use crate::ffi::*;
use crate::theme;
use crate::view::Event;
use egui::{Align, Button, Color32, Layout, RichText, Ui};
use std::collections::HashMap;

/// What the menu holds for a node between the player's change and the host catching up.
#[derive(Debug, Clone, Default)]
pub struct Local {
    pub num: f64,
    pub idx: i32,
    pub text: String,
    /// Sequence number of the last event sent for this node; 0 while it is still in the outbox.
    pub seq: u64,
    /// Frame number in which the player last had the pointer or the keyboard on the node.
    pub touched: u64,
}

pub struct NodeRect {
    pub key: u64,
    pub id: u32,
    pub tag: String,
    pub label: String,
    pub kind: u32,
    pub rect: egui::Rect,
    /// Width of the slider rail, for the test driver.
    pub rail: f32,
}

pub struct ItemRect {
    pub key: u64,
    pub index: usize,
    pub rect: egui::Rect,
}

pub struct Scene<'a> {
    panel: &'a Panel,
    local: &'a mut HashMap<u64, Local>,
    frame: u64,
    pub events: Vec<Event>,
    pub rects: Vec<NodeRect>,
    pub items: Vec<ItemRect>,
    children: HashMap<u32, Vec<usize>>,
    widest: HashMap<u32, f32>,
}

const LABEL_MIN: f32 = 100.0;
const LABEL_SHARE: f32 = 0.40;
const CONTROL_MIN: f32 = 140.0;
const WIDE_CONTAINER: f32 = 900.0;
const WIDE_CONTROL_MAX: f32 = 560.0;
const LIST_MAX: f32 = 640.0;

pub fn is_form(kind: u32) -> bool {
    matches!(
        kind,
        WIDGET_COMBO | WIDGET_SLIDER | WIDGET_TEXT_INPUT | WIDGET_TOGGLE
    )
}

fn severity(sev: u32) -> Option<(&'static str, Color32)> {
    match sev {
        SEVERITY_INFO => Some(("Info:", theme::SEV_INFO)),
        SEVERITY_WARNING => Some(("Warning:", theme::SEV_WARN)),
        SEVERITY_ERROR => Some(("Error:", theme::SEV_ERROR)),
        _ => None,
    }
}

/// A NUMBER slider shows at most three decimals and drops trailing zeros.
pub fn round3(v: f64) -> f64 {
    let r = (v * 1000.0).round() / 1000.0;
    if r == 0.0 {
        0.0
    } else {
        r
    }
}

pub fn label_column(widest: f32, available: f32) -> f32 {
    if widest <= 0.0 {
        return 0.0;
    }
    (LABEL_SHARE * available).min(widest).max(LABEL_MIN)
}

pub fn control_width(available: f32, column: f32, spacing: f32, max: f32) -> f32 {
    let mut w = available;
    if column > 0.0 {
        w -= column + spacing;
    }
    let cap = if max > 0.0 {
        max
    } else if available > WIDE_CONTAINER {
        WIDE_CONTROL_MAX
    } else {
        f32::MAX
    };
    w.clamp(CONTROL_MIN, cap.max(CONTROL_MIN))
}

impl<'a> Scene<'a> {
    pub fn new(panel: &'a Panel, local: &'a mut HashMap<u64, Local>, frame: u64) -> Self {
        let mut children: HashMap<u32, Vec<usize>> = HashMap::new();
        for (i, n) in panel.nodes.iter().enumerate() {
            children.entry(n.parent).or_default().push(i);
        }
        Scene {
            panel,
            local,
            frame,
            events: Vec::new(),
            rects: Vec::new(),
            items: Vec::new(),
            children,
            widest: HashMap::new(),
        }
    }

    pub fn draw(&mut self, ui: &mut Ui) {
        // Widest form label per parent, measured once.
        let font = egui::TextStyle::Body.resolve(ui.style());
        for n in &self.panel.nodes {
            if is_form(n.kind) && !n.label.is_empty() {
                let mut w = ui
                    .painter()
                    .layout_no_wrap(n.label.clone(), font.clone(), Color32::WHITE)
                    .size()
                    .x;
                if let Some((name, _)) = severity(n.severity) {
                    w += ui
                        .painter()
                        .layout_no_wrap(name.into(), font.clone(), Color32::WHITE)
                        .size()
                        .x
                        + ui.spacing().item_spacing.x;
                }
                let e = self.widest.entry(n.parent).or_insert(0.0);
                *e = e.max(w);
            }
        }
        let roots = self.children.get(&0).cloned().unwrap_or_default();
        for r in roots {
            self.node(ui, r, false);
        }
    }

    fn event(&self, n: &Node, kind: u32) -> Event {
        Event {
            node_id: n.id,
            kind,
            index: 0,
            value: 0.0,
            text: None,
            row: -1,
            epoch: self.panel.epoch,
            node_key: n.key,
        }
    }

    fn touch(&mut self, key: u64) {
        let f = self.frame;
        if let Some(l) = self.local.get_mut(&key) {
            l.touched = f;
        }
    }

    fn set_local(&mut self, n: &Node, f: impl FnOnce(&mut Local)) {
        let frame = self.frame;
        let l = self.local.entry(n.key).or_insert_with(|| Local {
            num: n.value,
            idx: n.index,
            text: n.text.clone(),
            seq: 0,
            touched: frame,
        });
        l.seq = 0;
        l.touched = frame;
        f(l);
    }

    fn num(&self, n: &Node) -> f64 {
        self.local.get(&n.key).map_or(n.value, |l| l.num)
    }
    fn idx(&self, n: &Node) -> i32 {
        self.local.get(&n.key).map_or(n.index, |l| l.idx)
    }

    fn record(&mut self, n: &Node, rect: egui::Rect, rail: f32) {
        self.rects.push(NodeRect {
            key: n.key,
            id: n.id,
            tag: n.tag.clone(),
            label: n.label.clone(),
            kind: n.kind,
            rect,
            rail,
        });
    }

    fn label_cell(&self, ui: &mut Ui, n: &Node, column: f32) {
        let h = ui.spacing().interact_size.y;
        ui.allocate_ui_with_layout(
            egui::vec2(column, h),
            Layout::left_to_right(Align::Center),
            |ui| {
                ui.set_width(column);
                if let Some((name, color)) = severity(n.severity) {
                    ui.label(RichText::new(name).color(color));
                }
                ui.add(egui::Label::new(n.label.as_str()).truncate());
            },
        );
    }

    fn node(&mut self, ui: &mut Ui, i: usize, inherited_disabled: bool) {
        let panel = self.panel;
        let n = &panel.nodes[i];
        let disabled = inherited_disabled || n.disabled();
        let tooltip = n.tooltip.clone();
        let response = ui
            .add_enabled_ui(!disabled, |ui| {
                ui.push_id(n.id, |ui| self.body(ui, i, disabled)).inner
            })
            .inner;
        if let Some(r) = response {
            if !tooltip.is_empty() {
                let r = r.on_hover_text(tooltip.clone());
                let _ = r.on_disabled_hover_text(tooltip);
            }
        }
    }

    fn body(&mut self, ui: &mut Ui, i: usize, disabled: bool) -> Option<egui::Response> {
        let panel = self.panel;
        let n = &panel.nodes[i];
        let available = ui.available_width();
        let spacing = ui.spacing().item_spacing.x;
        let mut column = 0.0;
        match n.kind {
            WIDGET_LABEL => {
                let text = match severity(n.severity) {
                    Some((name, color)) => {
                        let mut job = egui::text::LayoutJob::default();
                        let f = egui::TextStyle::Body.resolve(ui.style());
                        job.append(
                            &format!("{name} "),
                            0.0,
                            egui::TextFormat::simple(f.clone(), color),
                        );
                        job.append(&n.label, 0.0, egui::TextFormat::simple(f, theme::TEXT));
                        return Some(ui.add(egui::Label::new(job).wrap()));
                    }
                    None => RichText::new(n.label.as_str()),
                };
                return Some(ui.add(egui::Label::new(text).wrap()));
            }
            WIDGET_BUTTON => {
                let caption = if n.label.is_empty() {
                    "Activate"
                } else {
                    n.label.as_str()
                };
                let mut text = RichText::new(caption);
                if let Some((_, color)) = severity(n.severity) {
                    text = text.color(color);
                }
                let r = ui.add(Button::new(text));
                self.record(n, r.rect, 0.0);
                if r.clicked() && !disabled {
                    let e = self.event(n, WIDGET_BUTTON);
                    self.events.push(e);
                }
                return Some(r);
            }
            WIDGET_ROW | WIDGET_COLUMN => {
                let kids = self.children.get(&n.id).cloned().unwrap_or_default();
                if !n.label.is_empty() && n.severity == 0 {
                    ui.add_space(4.0);
                    ui.label(
                        RichText::new(n.label.as_str())
                            .color(theme::LEAF)
                            .size(theme::HEADING - 2.0),
                    );
                    ui.separator();
                } else if let Some((name, color)) = severity(n.severity) {
                    ui.label(RichText::new(format!("{name} {}", n.label)).color(color));
                }
                let all_columns =
                    !kids.is_empty() && kids.iter().all(|&k| panel.nodes[k].kind == WIDGET_COLUMN);
                if n.kind == WIDGET_ROW
                    && kids.len() >= 2
                    && all_columns
                    && kids.len() <= MAX_COLUMNS as usize
                {
                    ui.columns(kids.len(), |cols| {
                        for (c, &k) in cols.iter_mut().zip(kids.iter()) {
                            self.node(c, k, disabled);
                        }
                    });
                } else if n.kind == WIDGET_ROW {
                    ui.horizontal_wrapped(|ui| {
                        for &k in &kids {
                            self.node(ui, k, disabled);
                        }
                    });
                } else {
                    ui.vertical(|ui| {
                        for &k in &kids {
                            self.node(ui, k, disabled);
                        }
                    });
                }
                return None;
            }
            _ => {}
        }
        // Everything below is a form control or a list/table; labelled forms get the label column.
        if is_form(n.kind) && !n.label.is_empty() {
            column = label_column(
                self.widest.get(&n.parent).copied().unwrap_or(0.0),
                available,
            );
        } else if !n.label.is_empty() || n.severity != 0 {
            // A label on a line of its own above a list, table, or an unlabelled-form control.
            let mut job = egui::text::LayoutJob::default();
            let f = egui::TextStyle::Body.resolve(ui.style());
            if let Some((name, color)) = severity(n.severity) {
                job.append(
                    &format!("{name} "),
                    0.0,
                    egui::TextFormat::simple(f.clone(), color),
                );
            }
            job.append(&n.label, 0.0, egui::TextFormat::simple(f, theme::TEXT));
            ui.add(egui::Label::new(job).wrap());
        }
        let ctrl = control_width(available, column, spacing, 0.0);
        match n.kind {
            WIDGET_TOGGLE | WIDGET_SLIDER | WIDGET_COMBO | WIDGET_TEXT_INPUT => {
                let mut out = None;
                ui.horizontal(|ui| {
                    if column > 0.0 {
                        self.label_cell(ui, n, column);
                    }
                    out = Some(match n.kind {
                        WIDGET_TOGGLE => self.toggle(ui, n),
                        WIDGET_SLIDER => self.slider(ui, n, ctrl),
                        WIDGET_COMBO => self.combo(ui, n, ctrl),
                        _ => self.text_input(ui, n, ctrl),
                    });
                });
                out
            }
            WIDGET_LIST => Some(self.list(ui, n, available)),
            WIDGET_TABLE => Some(self.table(ui, n)),
            _ => None,
        }
    }

    fn toggle(&mut self, ui: &mut Ui, n: &Node) -> egui::Response {
        let mut on = self.num(n) != 0.0;
        let r = ui.checkbox(&mut on, "");
        self.record(n, r.rect, 0.0);
        if r.changed() {
            let v = if on { 1.0 } else { 0.0 };
            self.set_local(n, |l| l.num = v);
            let mut e = self.event(n, WIDGET_TOGGLE);
            e.value = v;
            self.events.push(e);
        }
        r
    }

    fn slider(&mut self, ui: &mut Ui, n: &Node, ctrl: f32) -> egui::Response {
        let (mut lo, mut hi) = (n.minimum, n.maximum);
        if hi <= lo || hi.is_nan() || lo.is_nan() {
            hi = lo + 1.0;
        }
        let integer = n.presentation != NUMERIC_NUMBER;
        let percent = n.presentation == NUMERIC_INTEGER_PERCENT;
        let mut v = self.num(n).clamp(lo, hi);
        let before = v;
        let rail = (ctrl - 72.0).max(60.0);
        let mut slider = egui::Slider::new(&mut v, lo..=hi).clamping(egui::SliderClamping::Always);
        if integer {
            slider = slider.integer();
        } else {
            slider = slider.max_decimals(3);
        }
        if percent {
            slider = slider
                .custom_formatter(|x, _| format!("{:.0}%", x))
                .custom_parser(|s| s.trim().trim_end_matches('%').trim().parse::<f64>().ok());
        }
        let r = ui
            .scope(|ui| {
                ui.spacing_mut().slider_width = rail;
                ui.add(slider)
            })
            .inner;
        lo = lo.min(hi);
        self.record(n, r.rect, rail);
        if r.dragged() || r.has_focus() {
            self.touch(n.key);
        }
        if r.changed() {
            let v = if integer {
                v.round().clamp(lo, hi)
            } else {
                round3(v).clamp(lo, hi)
            };
            if v != before {
                self.set_local(n, |l| l.num = v);
                let mut e = self.event(n, WIDGET_SLIDER);
                e.value = v;
                self.events.push(e);
            }
        }
        r
    }

    fn combo(&mut self, ui: &mut Ui, n: &Node, ctrl: f32) -> egui::Response {
        let cur = self.idx(n);
        let preview = usize::try_from(cur)
            .ok()
            .and_then(|i| n.items.get(i))
            .cloned()
            .unwrap_or_default();
        let mut picked = None;
        let mut item_rects = Vec::new();
        let inner = egui::ComboBox::from_id_salt(("combo", n.id))
            .width(ctrl)
            .selected_text(preview)
            .show_ui(ui, |ui| {
                ui.set_max_width(theme_popup_width(ctrl));
                for (k, text) in n.items.iter().enumerate() {
                    let r = ui.selectable_label(cur == k as i32, text.as_str());
                    item_rects.push(ItemRect {
                        key: n.key,
                        index: k,
                        rect: r.rect,
                    });
                    if r.clicked() {
                        picked = Some(k as i32);
                    }
                }
            });
        self.items.extend(item_rects);
        self.record(n, inner.response.rect, 0.0);
        if let Some(k) = picked {
            if k != cur {
                self.set_local(n, |l| l.idx = k);
                let mut e = self.event(n, WIDGET_COMBO);
                e.index = k;
                self.events.push(e);
            }
        }
        inner.response
    }

    fn text_input(&mut self, ui: &mut Ui, n: &Node, ctrl: f32) -> egui::Response {
        let mut s = self
            .local
            .get(&n.key)
            .map_or_else(|| n.text.clone(), |l| l.text.clone());
        let r = ui.add(
            egui::TextEdit::singleline(&mut s)
                .password(n.password())
                .desired_width(ctrl)
                .char_limit(MAX_TEXT),
        );
        self.record(n, r.rect, 0.0);
        if r.has_focus() {
            self.touch(n.key);
        }
        if r.changed() {
            let text = s.clone();
            self.set_local(n, |l| l.text = text);
            let mut e = self.event(n, WIDGET_TEXT_INPUT);
            e.text = Some(s);
            self.events.push(e);
        }
        r
    }

    fn list(&mut self, ui: &mut Ui, n: &Node, available: f32) -> egui::Response {
        let cur = self.idx(n);
        let (first, last) = n.window();
        let width = control_width(available, 0.0, ui.spacing().item_spacing.x, LIST_MAX);
        let mut picked = None;
        let mut item_rects = Vec::new();
        let frame = egui::Frame::new()
            .stroke(egui::Stroke::new(1.0, theme::LINE))
            .corner_radius(4)
            .inner_margin(2);
        let framed = frame.show(ui, |ui| {
            ui.set_width(width - 6.0);
            egui::ScrollArea::vertical()
                .id_salt(("list", n.id))
                .max_height(220.0)
                .auto_shrink([false, true])
                .show(ui, |ui| {
                    ui.with_layout(Layout::top_down_justified(Align::LEFT), |ui| {
                        for k in first..last {
                            let text = &n.items[k];
                            let r = ui
                                .add(Button::selectable(cur == k as i32, text.as_str()).truncate());
                            item_rects.push(ItemRect {
                                key: n.key,
                                index: k,
                                rect: r.rect,
                            });
                            if r.clicked() {
                                picked = Some(k as i32);
                            }
                        }
                    });
                });
        });
        self.items.extend(item_rects);
        self.record(n, framed.response.rect, 0.0);
        if let Some(k) = picked {
            if k != cur {
                self.set_local(n, |l| l.idx = k);
                let mut e = self.event(n, WIDGET_LIST);
                e.index = k;
                self.events.push(e);
            }
        }
        self.pager(ui, n);
        framed.response
    }

    fn table(&mut self, ui: &mut Ui, n: &Node) -> egui::Response {
        let (first, last) = n.window();
        let cols = n.columns as usize;
        let r = if cols == 0 || cols > MAX_COLUMNS as usize {
            ui.label("")
        } else {
            egui::Frame::new()
                .stroke(egui::Stroke::new(1.0, theme::LINE))
                .corner_radius(4)
                .inner_margin(6)
                .show(ui, |ui| {
                    egui::Grid::new(("table", n.id))
                        .num_columns(cols)
                        .striped(true)
                        .show(ui, |ui| {
                            for row in first..last {
                                for c in 0..cols {
                                    ui.label(n.items[row * cols + c].as_str());
                                }
                                ui.end_row();
                            }
                        });
                })
                .response
        };
        self.pager(ui, n);
        r
    }

    fn pager(&mut self, ui: &mut Ui, n: &Node) {
        let rows = n.rows();
        if n.visible_rows == 0 || rows <= n.visible_rows as usize {
            return;
        }
        let (first, last) = n.window();
        let mut want = None;
        let mut buttons = Vec::new();
        ui.horizontal(|ui| {
            ui.label(
                RichText::new(format!("Rows {}-{} of {}", first + 1, last, rows))
                    .color(theme::MUTED),
            );
            let prev = ui.add_enabled(first > 0, Button::new("Previous"));
            if prev.clicked() {
                want = Some(first.saturating_sub(n.visible_rows as usize));
            }
            let next = ui.add_enabled(last < rows, Button::new("Next"));
            if next.clicked() && want.is_none() {
                want = Some(last);
            }
            buttons.push(("prev", "Previous", prev.rect));
            buttons.push(("next", "Next", next.rect));
        });
        // Test driver handles: "<tag>.prev" and "<tag>.next".
        for (suffix, label, rect) in buttons {
            self.rects.push(NodeRect {
                key: n.key,
                id: n.id,
                tag: format!("{}.{suffix}", n.tag),
                label: label.into(),
                kind: WIDGET_EVENT_PAGE,
                rect,
                rail: 0.0,
            });
        }
        if let Some(row) = want {
            let mut e = self.event(n, WIDGET_EVENT_PAGE);
            e.row = row as i32;
            self.events.push(e);
        }
    }
}

fn theme_popup_width(ctrl: f32) -> f32 {
    ctrl.clamp(280.0, 640.0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn label_column_rules() {
        assert_eq!(label_column(0.0, 800.0), 0.0);
        assert_eq!(label_column(50.0, 800.0), 100.0);
        assert_eq!(label_column(200.0, 800.0), 200.0);
        assert_eq!(label_column(600.0, 800.0), 320.0);
    }

    #[test]
    fn control_width_rules() {
        assert_eq!(control_width(300.0, 100.0, 8.0, 0.0), 192.0);
        assert_eq!(control_width(200.0, 150.0, 8.0, 0.0), 140.0);
        assert_eq!(control_width(1200.0, 0.0, 8.0, 0.0), 560.0);
        assert_eq!(
            control_width(1200.0, 0.0, 8.0, LIST_MAX),
            640.0_f32.min(1200.0)
        );
    }

    #[test]
    fn number_rounding() {
        assert_eq!(round3(1.23456), 1.235);
        assert_eq!(round3(-0.0001), 0.0);
    }
}
