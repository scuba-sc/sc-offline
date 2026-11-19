// SPDX-License-Identifier: GPL-3.0-only
//! A Rust stand-in for Core's `sco_widgets_view_v1` table, used by the unit tests and by
//! `examples/menu_host.rs`. It follows the header: one registered menu thread, an event queue that
//! the "game thread" drains with `dispatch_events`, `events_done` in whole slots, and the event
//! matching rules (epoch equal: id, kind and key; epoch different: key). It plays the plugin as
//! well: a delivered event updates the node (toggle, slider, combo, list, text, page) and moves the
//! panel revision. The real implementation lives in sco-core (`src/widgets/view.cpp`); the C++ host
//! in `tools/rust-menu-host` runs the menu against that one.
use crate::blob::build::{self, NodeSpec};
use crate::ffi::*;
use std::collections::VecDeque;
use std::ffi::{c_char, c_void, CStr};
use std::sync::{Mutex, MutexGuard};
use std::thread::ThreadId;

pub struct FakePanel {
    pub id: String,
    pub title: String,
    pub owner: String,
    pub flags: u32,
    pub revision: u64,
    pub epoch: u64,
    pub nodes: Vec<NodeSpec>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Delivered {
    pub panel: String,
    pub tag: String,
    pub label: String,
    pub node_id: u32,
    pub kind: u32,
    pub index: i32,
    pub value: f64,
    pub text: String,
    pub row: i32,
}

struct Queued {
    last: u64,
    panel: String,
    kind: u32,
    node_id: u32,
    index: i32,
    value: f64,
    text: Option<String>,
    row: i32,
    epoch: u64,
    key: u64,
}

#[derive(Default)]
struct State {
    panels: Vec<FakePanel>,
    counter: u64,
    global: u64,
    list: u64,
    seq: u64,
    done: u64,
    drops: u64,
    queue: VecDeque<Queued>,
    thread: Option<ThreadId>,
    delivered: Vec<Delivered>,
    stopped: bool,
    capacity: usize,
}

static STATE: Mutex<Option<State>> = Mutex::new(None);

fn lock() -> MutexGuard<'static, Option<State>> {
    STATE.lock().unwrap_or_else(|p| p.into_inner())
}

fn with<R>(f: impl FnOnce(&mut State) -> R) -> R {
    let mut g = lock();
    f(g.get_or_insert_with(|| State {
        capacity: 256,
        ..Default::default()
    }))
}

/// The table and client to hand to `View::new` or to `scol_menu_start`.
pub fn table() -> (*const sco_widgets_view_v1, *mut c_void) {
    (
        &TABLE as *const sco_widgets_view_v1,
        std::ptr::dangling_mut::<c_void>(),
    )
}

pub fn reset() {
    *lock() = Some(State {
        capacity: 256,
        ..Default::default()
    });
}

pub fn stop() {
    with(|s| s.stopped = true);
}

pub fn set_queue_capacity(n: usize) {
    with(|s| s.capacity = n);
}

fn bump(s: &mut State) -> u64 {
    s.counter += 1;
    s.global = s.counter;
    s.counter
}

/// Adds or replaces a panel (a replace gets a new epoch).
pub fn set_panel(id: &str, title: &str, owner: &str, nodes: Vec<NodeSpec>) {
    set_panel_flags(id, title, owner, PANEL_TAGGED_EVENTS, nodes);
}

/// A panel of the host's reserved identity (`menu.main`, `menu.status`): flagged host-owned.
pub fn set_host_panel(id: &str, title: &str, nodes: Vec<NodeSpec>) {
    set_panel_flags(
        id,
        title,
        "menu",
        PANEL_TAGGED_EVENTS | PANEL_HOST_OWNED,
        nodes,
    );
}

fn set_panel_flags(id: &str, title: &str, owner: &str, flags: u32, nodes: Vec<NodeSpec>) {
    with(|s| {
        let rev = bump(s);
        s.list = rev;
        let panel = FakePanel {
            id: id.into(),
            title: title.into(),
            owner: owner.into(),
            flags,
            revision: rev,
            epoch: rev,
            nodes,
        };
        match s.panels.iter().position(|p| p.id == id) {
            Some(i) => s.panels[i] = panel,
            None => s.panels.push(panel),
        }
    });
}

/// Changes a node in place (revision moves, epoch does not), like `update_nodes`.
pub fn update_node(panel: &str, tag: &str, f: impl FnOnce(&mut NodeSpec)) -> bool {
    with(|s| {
        let rev = bump(s);
        if let Some(p) = s.panels.iter_mut().find(|p| p.id == panel) {
            if let Some(n) = p.nodes.iter_mut().find(|n| n.tag == tag) {
                f(n);
                p.revision = rev;
                return true;
            }
        }
        false
    })
}

pub fn node(panel: &str, tag: &str) -> Option<NodeSpec> {
    with(|s| {
        s.panels
            .iter()
            .find(|p| p.id == panel)?
            .nodes
            .iter()
            .find(|n| n.tag == tag)
            .cloned()
    })
}

pub fn delivered() -> Vec<Delivered> {
    with(|s| s.delivered.clone())
}

pub fn clear_delivered() {
    with(|s| s.delivered.clear());
}

pub fn drops() -> u64 {
    with(|s| s.drops)
}

/// What the game thread does once per pass.
pub fn dispatch() -> (u32, u32) {
    let (mut a, mut b) = (0u32, 0u32);
    unsafe { dispatch_events(std::ptr::dangling_mut::<c_void>(), &mut a, &mut b) };
    (a, b)
}

fn blob_of(p: &FakePanel) -> Vec<u8> {
    build::panel(
        &p.id, &p.title, &p.owner, p.revision, p.epoch, p.flags, &p.nodes,
    )
}

fn on_menu_thread(s: &State) -> bool {
    s.thread == Some(std::thread::current().id())
}

unsafe fn cstr(p: *const c_char) -> Option<String> {
    if p.is_null() {
        None
    } else {
        CStr::from_ptr(p).to_str().ok().map(str::to_owned)
    }
}

unsafe extern "C" fn capabilities(client: *mut c_void, caps: *mut u32) -> i32 {
    if client.is_null() || caps.is_null() || *caps < 2 {
        return SCO_BAD_ARG;
    }
    if with(|s| s.stopped) {
        return SCO_UNAVAILABLE;
    }
    let all = [
        VIEW_CAPS_1_0 as u32,
        7,
        64,
        256,
        NODE_SIZE as u32,
        ENTRY_SIZE as u32,
    ];
    let n = (*caps as usize).min(all.len());
    for (i, v) in all.iter().take(n).enumerate() {
        *caps.add(i) = if i == 0 { n as u32 } else { *v };
    }
    SCO_OK
}

unsafe extern "C" fn stamps(
    client: *mut c_void,
    g: *mut u64,
    l: *mut u64,
    d: *mut u64,
    e: *mut u64,
) -> i32 {
    if client.is_null() {
        return SCO_BAD_ARG;
    }
    with(|s| {
        if s.stopped {
            return SCO_UNAVAILABLE;
        }
        for (p, v) in [(g, s.global), (l, s.list), (d, s.drops), (e, s.done)] {
            if !p.is_null() {
                *p = v;
            }
        }
        SCO_OK
    })
}

unsafe extern "C" fn menu_thread_enter(client: *mut c_void) -> i32 {
    if client.is_null() {
        return SCO_BAD_ARG;
    }
    with(|s| match s.thread {
        Some(t) if t != std::thread::current().id() => SCO_FAILED,
        _ => {
            s.thread = Some(std::thread::current().id());
            SCO_OK
        }
    })
}

unsafe extern "C" fn menu_thread_leave(client: *mut c_void) -> i32 {
    if client.is_null() {
        return SCO_BAD_ARG;
    }
    with(|s| match s.thread {
        Some(t) if t != std::thread::current().id() => SCO_WRONG_THREAD,
        _ => {
            s.thread = None;
            SCO_OK
        }
    })
}

unsafe extern "C" fn list_panels(
    client: *mut c_void,
    out: *mut sco_view_panel_entry,
    stride: u32,
    count: *mut u32,
) -> i32 {
    if client.is_null()
        || count.is_null()
        || out.is_null()
        || (stride as usize) < ENTRY_SIZE
        || !stride.is_multiple_of(8)
    {
        return SCO_BAD_ARG;
    }
    with(|s| {
        if s.stopped {
            return SCO_UNAVAILABLE;
        }
        if !on_menu_thread(s) {
            return SCO_WRONG_THREAD;
        }
        if (*count as usize) < s.panels.len() {
            *count = s.panels.len() as u32;
            return SCO_TOO_MANY;
        }
        for (i, p) in s.panels.iter().enumerate() {
            let mut e = sco_view_panel_entry::zeroed();
            e.size = ENTRY_SIZE as u32;
            e.flags = p.flags;
            e.revision = p.revision;
            e.epoch = p.epoch;
            e.node_count = p.nodes.len() as u32;
            for (dst, src) in [
                (&mut e.id, &p.id),
                (&mut e.title, &p.title),
                (&mut e.owner, &p.owner),
            ] {
                let b = src.as_bytes();
                let n = b.len().min(63);
                dst[..n].copy_from_slice(&b[..n]);
            }
            std::ptr::write(
                (out as *mut u8).add(i * stride as usize) as *mut sco_view_panel_entry,
                e,
            );
        }
        *count = s.panels.len() as u32;
        SCO_OK
    })
}

unsafe extern "C" fn copy_panel(
    client: *mut c_void,
    id: *const c_char,
    out: *mut c_void,
    bytes: *mut u32,
) -> i32 {
    if client.is_null() || bytes.is_null() {
        return SCO_BAD_ARG;
    }
    let Some(id) = cstr(id) else {
        return SCO_BAD_ARG;
    };
    if !(out as usize).is_multiple_of(8) {
        return SCO_BAD_ARG;
    }
    with(|s| {
        if s.stopped {
            return SCO_UNAVAILABLE;
        }
        if !on_menu_thread(s) {
            return SCO_WRONG_THREAD;
        }
        let Some(p) = s.panels.iter().find(|p| p.id == id) else {
            return SCO_NOT_FOUND;
        };
        let blob = blob_of(p);
        if (*bytes as usize) < blob.len() || out.is_null() {
            *bytes = blob.len() as u32;
            return SCO_TOO_MANY;
        }
        std::ptr::copy_nonoverlapping(blob.as_ptr(), out as *mut u8, blob.len());
        *bytes = blob.len() as u32;
        SCO_OK
    })
}

unsafe extern "C" fn submit_event(
    client: *mut c_void,
    id: *const c_char,
    e: *const sco_view_event,
    seq: *mut u64,
) -> i32 {
    if client.is_null() || e.is_null() {
        return SCO_BAD_ARG;
    }
    let Some(id) = cstr(id) else {
        return SCO_BAD_ARG;
    };
    let e = *e;
    if e.size < EVENT_SIZE as u32
        || e._reserved != 0
        || e.epoch == 0
        || e.node_key == 0
        || !e.value.is_finite()
    {
        return SCO_BAD_ARG;
    }
    let kinds = [
        WIDGET_BUTTON,
        WIDGET_TOGGLE,
        WIDGET_SLIDER,
        WIDGET_COMBO,
        WIDGET_LIST,
        WIDGET_TEXT_INPUT,
        WIDGET_EVENT_PAGE,
    ];
    if !kinds.contains(&e.kind) {
        return SCO_BAD_ARG;
    }
    let text = if e.kind == WIDGET_TEXT_INPUT {
        Some(cstr(e.text).unwrap_or_default())
    } else {
        None
    };
    if text.as_ref().is_some_and(|t| t.len() > MAX_TEXT) {
        return SCO_BAD_ARG;
    }
    with(|s| {
        if s.stopped {
            return SCO_UNAVAILABLE;
        }
        if !on_menu_thread(s) {
            return SCO_WRONG_THREAD;
        }
        if !s.panels.iter().any(|p| p.id == id) {
            return SCO_NOT_FOUND;
        }
        let mergeable = e.kind == WIDGET_SLIDER || e.kind == WIDGET_TEXT_INPUT;
        if mergeable {
            if let Some(last) = s.queue.back_mut() {
                if last.panel == id
                    && last.node_id == e.node_id
                    && last.kind == e.kind
                    && last.epoch == e.epoch
                    && last.key == e.node_key
                {
                    s.seq += 1;
                    last.last = s.seq;
                    last.value = e.value;
                    last.text = text;
                    if !seq.is_null() {
                        *seq = s.seq;
                    }
                    return SCO_OK;
                }
            }
        }
        if s.queue.len() >= s.capacity {
            return SCO_TOO_MANY;
        }
        s.seq += 1;
        s.queue.push_back(Queued {
            last: s.seq,
            panel: id,
            kind: e.kind,
            node_id: e.node_id,
            index: e.index,
            value: e.value,
            text,
            row: e.row,
            epoch: e.epoch,
            key: e.node_key,
        });
        if !seq.is_null() {
            *seq = s.seq;
        }
        SCO_OK
    })
}

unsafe extern "C" fn dispatch_events(
    client: *mut c_void,
    dispatched: *mut u32,
    dropped: *mut u32,
) -> i32 {
    if client.is_null() {
        return SCO_BAD_ARG;
    }
    let (mut ok, mut gone) = (0u32, 0u32);
    with(|s| {
        let taken: Vec<Queued> = s.queue.drain(..).collect();
        for q in taken {
            let rev = s.counter + 1;
            let mut delivered = None;
            if let Some(p) = s.panels.iter_mut().find(|p| p.id == q.panel) {
                let is_page = q.kind == WIDGET_EVENT_PAGE;
                let kind_ok = |n: &NodeSpec| {
                    if is_page {
                        n.kind == WIDGET_LIST || n.kind == WIDGET_TABLE
                    } else {
                        n.kind == q.kind
                    }
                };
                // A page event carries the key of the LIST or TABLE node itself.
                let target = if p.epoch == q.epoch {
                    p.nodes.iter().position(|n| {
                        n.id == q.node_id && kind_ok(n) && build::node_key(n) == q.key
                    })
                } else {
                    p.nodes
                        .iter()
                        .position(|n| kind_ok(n) && build::node_key(n) == q.key)
                };
                if let Some(i) = target {
                    let n = &mut p.nodes[i];
                    if n.flags & WIDGET_DISABLED == 0 {
                        match q.kind {
                            WIDGET_TOGGLE | WIDGET_SLIDER => n.value = q.value,
                            WIDGET_COMBO | WIDGET_LIST => n.index = q.index,
                            WIDGET_TEXT_INPUT => n.text = q.text.clone().unwrap_or_default(),
                            WIDGET_EVENT_PAGE => n.first_row = q.row.max(0) as u32,
                            _ => {}
                        }
                        p.revision = rev;
                        delivered = Some(Delivered {
                            panel: q.panel.clone(),
                            tag: n.tag.clone(),
                            label: n.label.clone(),
                            node_id: n.id,
                            kind: q.kind,
                            index: q.index,
                            value: q.value,
                            text: q.text.clone().unwrap_or_default(),
                            row: q.row,
                        });
                    }
                }
            }
            match delivered {
                Some(d) => {
                    s.counter = rev;
                    s.global = rev;
                    s.delivered.push(d);
                    ok += 1;
                }
                None => {
                    s.drops += 1;
                    gone += 1;
                }
            }
            s.done = q.last;
        }
    });
    if !dispatched.is_null() {
        *dispatched = ok;
    }
    if !dropped.is_null() {
        *dropped = gone;
    }
    SCO_OK
}

pub static TABLE: sco_widgets_view_v1 = sco_widgets_view_v1 {
    size: TABLE_SIZE as u32,
    version: WIDGETS_VIEW_VERSION_1_0,
    capabilities,
    stamps,
    menu_thread_enter,
    menu_thread_leave,
    list_panels,
    copy_panel,
    submit_event,
    dispatch_events,
};
