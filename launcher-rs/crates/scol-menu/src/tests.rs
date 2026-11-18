// SPDX-License-Identifier: GPL-3.0-only
//! Interface tests of the view wrapper against the Rust stand-in table, and menu-logic tests that
//! need no window. The fake is process-global, so these tests take one lock and run one at a time.
use crate::app::{App, Config};
use crate::blob::build::NodeSpec;
use crate::fake;
use crate::ffi::*;
use crate::render::Local;
use crate::samples;
use crate::view::{Event, View};
use crate::Shared;
use std::sync::{Arc, Mutex, MutexGuard};

static LOCK: Mutex<()> = Mutex::new(());

fn setup() -> (MutexGuard<'static, ()>, View) {
    let g = LOCK.lock().unwrap_or_else(|p| p.into_inner());
    fake::reset();
    let (t, c) = fake::table();
    // SAFETY: the fake table is a static of this process.
    let v = unsafe { View::new(t, c).unwrap() };
    (g, v)
}

fn node(kind: u32, id: u32, label: &str, tag: &str) -> NodeSpec {
    NodeSpec {
        kind,
        id,
        label: label.into(),
        tag: tag.into(),
        ..Default::default()
    }
}

fn slider(id: u32, tag: &str) -> NodeSpec {
    NodeSpec {
        maximum: 100.0,
        ..node(WIDGET_SLIDER, id, tag, tag)
    }
}

fn key_of(v: &View, panel: &str, tag: &str) -> (u64, u64, u32) {
    let p = v.copy(panel).unwrap();
    let n = p.nodes.iter().find(|n| n.tag == tag).unwrap();
    (p.epoch, n.key, n.id)
}

fn ev(kind: u32, (epoch, key, id): (u64, u64, u32)) -> Event {
    Event {
        node_id: id,
        kind,
        index: 0,
        value: 0.0,
        text: None,
        row: -1,
        epoch,
        node_key: key,
    }
}

#[test]
fn table_checks_refuse_a_short_or_foreign_table() {
    let (_g, _v) = setup();
    let (t, c) = fake::table();
    // SAFETY: valid static table; the null cases never dereference.
    unsafe {
        assert!(View::new(std::ptr::null(), c).is_err());
        assert!(View::new(t, std::ptr::null_mut()).is_err());
    }
    let mut short = [0u32; 20];
    short[0] = 40;
    short[1] = WIDGETS_VIEW_VERSION_1_0;
    // SAFETY: the buffer is large enough for the two header words the check reads.
    assert!(unsafe { View::new(short.as_ptr() as *const sco_widgets_view_v1, c) }.is_err());
    short[0] = 72;
    short[1] = 0x0002_0000;
    // SAFETY: as above.
    assert!(unsafe { View::new(short.as_ptr() as *const sco_widgets_view_v1, c) }.is_err());
}

#[test]
fn menu_thread_registration_and_reads() {
    let (_g, v) = setup();
    fake::set_panel("a.one", "One", "a", samples::controls());
    assert_eq!(
        v.list().unwrap_err(),
        SCO_WRONG_THREAD,
        "reads need the registered thread"
    );
    assert_eq!(v.enter(), SCO_OK);
    assert_eq!(v.enter(), SCO_OK, "registering twice is fine");
    let other = std::thread::spawn(move || v.enter()).join().unwrap();
    assert_eq!(other, SCO_FAILED, "one thread at a time");
    let list = v.list().unwrap();
    assert_eq!(list.len(), 1);
    assert_eq!(list[0].id, "a.one");
    assert_eq!(list[0].node_count as usize, samples::controls().len());
    let p = v.copy("a.one").unwrap();
    assert_eq!(p.nodes.len(), samples::controls().len());
    assert_eq!(
        v.copy("missing").unwrap_err(),
        crate::view::CopyError::Code(SCO_NOT_FOUND)
    );
    let caps = v.capabilities().unwrap();
    assert_eq!(caps[0] as usize, caps.len());
    assert_eq!(v.leave(), SCO_OK);
    assert_eq!(v.list().unwrap_err(), SCO_WRONG_THREAD, "after leave");
}

#[test]
fn copy_refuses_a_misaligned_buffer() {
    let (_g, v) = setup();
    fake::set_panel("a.one", "One", "a", samples::controls());
    assert_eq!(v.enter(), SCO_OK);
    let (t, c) = fake::table();
    let id = std::ffi::CString::new("a.one").unwrap();
    let mut mem = vec![0u64; 4096];
    let mut cap = 8 * 4000u32;
    // SAFETY: a deliberate odd address inside a live buffer; the stand-in answers BAD_ARG before it writes.
    let r = unsafe {
        ((*t).copy_panel)(
            c,
            id.as_ptr(),
            (mem.as_mut_ptr() as *mut u8).add(1) as *mut std::ffi::c_void,
            &mut cap,
        )
    };
    assert_eq!(r, SCO_BAD_ARG);
    v.leave();
}

#[test]
fn events_follow_the_key_across_a_republish() {
    let (_g, v) = setup();
    fake::set_panel(
        "a.one",
        "One",
        "a",
        vec![slider(1, "s.one"), node(WIDGET_BUTTON, 2, "Go", "b.go")],
    );
    assert_eq!(v.enter(), SCO_OK);
    let old = key_of(&v, "a.one", "b.go");
    // The plugin replaces the panel; "b.go" now has another id but the same tag.
    fake::set_panel(
        "a.one",
        "One",
        "a",
        vec![node(WIDGET_BUTTON, 9, "Go", "b.go"), slider(1, "s.one")],
    );
    v.submit("a.one", &ev(WIDGET_BUTTON, old)).unwrap();
    fake::dispatch();
    let d = fake::delivered();
    assert_eq!(d.len(), 1);
    assert_eq!(d[0].node_id, 9, "the target gets its own id");
    // A control that is gone is dropped and counted.
    let gone = key_of(&v, "a.one", "s.one");
    fake::set_panel(
        "a.one",
        "One",
        "a",
        vec![node(WIDGET_BUTTON, 9, "Go", "b.go")],
    );
    v.submit("a.one", &ev(WIDGET_SLIDER, gone)).unwrap();
    fake::dispatch();
    assert_eq!(fake::delivered().len(), 1);
    assert_eq!(fake::drops(), 1);
    assert_eq!(v.stamps().unwrap().drops, 1);
    v.leave();
}

#[test]
fn sliders_merge_and_events_done_covers_the_slot() {
    let (_g, v) = setup();
    fake::set_panel("a.one", "One", "a", vec![slider(1, "s.one")]);
    assert_eq!(v.enter(), SCO_OK);
    let k = key_of(&v, "a.one", "s.one");
    let mut last = 0;
    for x in [10.0, 20.0, 30.0] {
        let mut e = ev(WIDGET_SLIDER, k);
        e.value = x;
        last = v.submit("a.one", &e).unwrap();
    }
    assert_eq!(last, 3);
    assert_eq!(v.stamps().unwrap().done, 0);
    fake::dispatch();
    assert_eq!(fake::delivered().len(), 1, "three events, one slot");
    assert_eq!(fake::delivered()[0].value, 30.0);
    assert_eq!(v.stamps().unwrap().done, 3);
    v.leave();
}

#[test]
fn a_full_queue_answers_too_many_and_a_bad_event_is_refused() {
    let (_g, v) = setup();
    fake::set_panel(
        "a.one",
        "One",
        "a",
        vec![node(WIDGET_BUTTON, 1, "Go", "b.go")],
    );
    fake::set_queue_capacity(1);
    assert_eq!(v.enter(), SCO_OK);
    let k = key_of(&v, "a.one", "b.go");
    v.submit("a.one", &ev(WIDGET_BUTTON, k)).unwrap();
    assert_eq!(
        v.submit("a.one", &ev(WIDGET_BUTTON, k)).unwrap_err(),
        SCO_TOO_MANY
    );
    let mut bad = ev(WIDGET_BUTTON, k);
    bad.epoch = 0;
    assert_eq!(v.submit("a.one", &bad).unwrap_err(), SCO_BAD_ARG);
    let mut bad = ev(WIDGET_LABEL, k);
    bad.kind = WIDGET_LABEL;
    assert_eq!(v.submit("a.one", &bad).unwrap_err(), SCO_BAD_ARG);
    v.leave();
}

#[test]
fn stopped_core_answers_unavailable() {
    let (_g, v) = setup();
    fake::stop();
    assert_eq!(v.stamps().unwrap_err(), SCO_UNAVAILABLE);
    assert_eq!(v.capabilities().unwrap_err(), SCO_UNAVAILABLE);
}

fn app(v: View) -> App {
    let cfg = Config {
        view: v,
        log: Arc::new(|_| {}),
        nudge: None,
        shot_dir: None,
        start_visible: true,
        test_mode: true,
        hotkey: 0,
    };
    App::new(cfg, Arc::new(Shared::new(true, true, 0)))
}

#[test]
fn app_polls_the_open_page_and_follows_a_revision() {
    let (_g, v) = setup();
    fake::set_panel(
        "a.one",
        "One",
        "a",
        vec![NodeSpec {
            value: 0.0,
            ..node(WIDGET_TOGGLE, 1, "On", "t.on")
        }],
    );
    assert_eq!(v.enter(), SCO_OK);
    let mut a = app(v);
    a.test_poll();
    assert_eq!(a.test_page().map(|p| p.nodes.len()), Some(1));
    fake::update_node("a.one", "t.on", |n| n.value = 1.0);
    a.test_poll();
    assert_eq!(a.test_page().unwrap().nodes[0].value, 1.0);
    v.leave();
}

#[test]
fn app_keeps_a_local_value_until_the_host_acknowledges_it() {
    let (_g, v) = setup();
    fake::set_panel("a.one", "One", "a", vec![slider(1, "s.one")]);
    assert_eq!(v.enter(), SCO_OK);
    let mut a = app(v);
    a.test_poll();
    let key = a.test_page().unwrap().nodes[0].key;
    // The player dragged to 70: the local value exists, one event is in the outbox.
    a.test_local().insert(
        key,
        Local {
            num: 70.0,
            ..Default::default()
        },
    );
    a.test_send(Event {
        value: 70.0,
        ..ev(WIDGET_SLIDER, key_of(&v, "a.one", "s.one"))
    });
    a.test_flush();
    let seq = a.test_local().get(&key).unwrap().seq;
    assert_eq!(seq, 1);
    for _ in 0..3 {
        a.test_poll();
    }
    assert!(
        a.test_local().contains_key(&key),
        "queued but not handled: the local value stays"
    );
    fake::dispatch();
    for _ in 0..3 {
        a.test_next_frame();
        a.test_poll();
    }
    assert!(
        !a.test_local().contains_key(&key),
        "handled: the host value is the truth again"
    );
    assert_eq!(a.test_page().unwrap().nodes[0].value, 70.0);
    v.leave();
}

#[test]
fn app_retries_taps_when_the_queue_is_full_and_keeps_the_last_slider_value() {
    let (_g, v) = setup();
    fake::set_panel(
        "a.one",
        "One",
        "a",
        vec![node(WIDGET_BUTTON, 1, "Go", "b.go"), slider(2, "s.one")],
    );
    fake::set_queue_capacity(1);
    assert_eq!(v.enter(), SCO_OK);
    let mut a = app(v);
    a.test_poll();
    let b = key_of(&v, "a.one", "b.go");
    let s = key_of(&v, "a.one", "s.one");
    a.test_send(ev(WIDGET_BUTTON, b));
    a.test_send(ev(WIDGET_BUTTON, b));
    a.test_send(Event {
        value: 5.0,
        ..ev(WIDGET_SLIDER, s)
    });
    a.test_send(Event {
        value: 9.0,
        ..ev(WIDGET_SLIDER, s)
    });
    assert_eq!(a.test_outbox_len(), 3, "the two slider values merged");
    for _ in 0..4 {
        a.test_flush();
        fake::dispatch();
    }
    assert_eq!(a.test_outbox_len(), 0);
    let d = fake::delivered();
    assert_eq!(
        d.iter().filter(|e| e.kind == WIDGET_BUTTON).count(),
        2,
        "no tap is lost"
    );
    assert_eq!(
        d.iter()
            .filter(|e| e.kind == WIDGET_SLIDER)
            .map(|e| e.value)
            .collect::<Vec<_>>(),
        vec![9.0]
    );
    v.leave();
}

#[test]
fn app_survives_a_stopped_core() {
    let (_g, v) = setup();
    fake::set_panel(
        "a.one",
        "One",
        "a",
        vec![node(WIDGET_BUTTON, 1, "Go", "b.go")],
    );
    assert_eq!(v.enter(), SCO_OK);
    let mut a = app(v);
    a.test_poll();
    fake::stop();
    a.test_poll();
    assert!(a.test_dead().is_some());
    v.leave();
}
