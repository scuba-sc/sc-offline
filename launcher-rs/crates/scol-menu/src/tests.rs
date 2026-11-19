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
    let mut cfg = Config::new(v, Arc::new(|_| {}));
    cfg.start_visible = true;
    cfg.test_mode = true;
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

fn label_node(id: u32, text: &str) -> NodeSpec {
    node(WIDGET_LABEL, id, text, "")
}

#[test]
fn sidebar_sections_titles_and_hidden_pages() {
    let (_g, v) = setup();
    for (id, owner) in [
        ("crew.crew", "crew"),
        ("loadout.player", "loadout"),
        ("inspector.main", "inspector"),
        ("acme.tool", "acme"),
        ("teleport.travel", "teleport"),
    ] {
        fake::set_panel(id, "Plugin's own title", owner, vec![label_node(1, "x")]);
    }
    fake::set_host_panel("menu.main", "Menu", vec![label_node(1, "main")]);
    fake::set_host_panel("menu.status", "Status", vec![label_node(1, "ready")]);
    assert_eq!(v.enter(), SCO_OK);
    let mut a = app(v);
    a.test_poll();
    let pages = a.test_pages();
    let ids: Vec<&str> = pages.iter().map(|(_, id)| id.as_str()).collect();
    assert_eq!(
        ids,
        [
            "loadout.player",
            "teleport.travel",
            "crew.crew",
            "acme.tool",
            "sc_offline.settings"
        ],
        "host panels and developer pages are not pages"
    );
    assert_eq!(pages[0].0, "Character");
    assert_eq!(pages[3].0, "More plugins");
    assert_eq!(
        a.test_selected(),
        "loadout.player",
        "first page when nothing was saved"
    );
    assert_eq!(a.test_status().map(|p| p.id.as_str()), Some("menu.status"));
    a.test_set_developer(true);
    assert!(a
        .test_pages()
        .iter()
        .any(|(s, id)| s == "Developer" && id == "inspector.main"));
    a.test_set_developer(false);
    a.test_select("sc_offline.settings");
    a.test_poll();
    assert_eq!(
        a.test_page().map(|p| p.id.as_str()),
        Some("menu.main"),
        "Settings shows menu.main"
    );
    v.leave();
}

#[test]
fn asop_panel_is_grouped_under_vehicles_or_left_out() {
    let (_g, v) = setup();
    fake::set_panel(
        "spawn.vehicles",
        "Vehicles",
        "spawn",
        vec![label_node(1, "ships")],
    );
    fake::set_panel("spawn.asop", "Fleet", "spawn", vec![label_node(1, "fleet")]);
    fake::set_panel("evil.asop", "Fleet", "evil", vec![label_node(1, "x")]);
    assert_eq!(v.enter(), SCO_OK);
    let mut a = app(v);
    a.test_poll();
    let ids: Vec<String> = a.test_pages().into_iter().map(|(_, id)| id).collect();
    assert!(!ids.contains(&"spawn.asop".to_string()));
    assert!(
        ids.contains(&"evil.asop".to_string()),
        "only the exact owner and id are grouped"
    );
    a.test_select("spawn.vehicles");
    a.test_poll();
    assert_eq!(a.test_extra().map(|p| p.id.as_str()), Some("spawn.asop"));
    v.leave();
}

#[test]
fn view_and_size_files_are_read_and_written_in_the_cpp_format() {
    let (_g, v) = setup();
    fake::set_panel("crew.crew", "Crew", "crew", vec![label_node(1, "a")]);
    fake::set_panel("npc.npcs", "NPC", "npc", vec![label_node(1, "b")]);
    let dir = std::env::temp_dir().join(format!("scol-menu-ini-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let view_path = dir.join("menu-view.ini");
    std::fs::write(&view_path, "[view]\npage=npc.npcs\ndeveloper=1\n").unwrap();
    assert_eq!(v.enter(), SCO_OK);
    let mut cfg = Config::new(v, Arc::new(|_| {}));
    cfg.test_mode = true;
    cfg.view_path = Some(view_path.clone());
    let mut a = App::new(cfg, Arc::new(Shared::new(true, true, 0)));
    a.test_poll();
    assert_eq!(a.test_selected(), "npc.npcs", "the saved page opens");
    a.test_select("crew.crew");
    a.test_save_view();
    assert_eq!(
        std::fs::read_to_string(&view_path).unwrap(),
        "[view]\npage=crew.crew\ndeveloper=1\n"
    );
    // A saved page that is gone falls back to the first page, and the file keeps its content.
    std::fs::write(&view_path, "[view]\npage=gone.page\ndeveloper=0\n").unwrap();
    let mut cfg = Config::new(v, Arc::new(|_| {}));
    cfg.view_path = Some(view_path.clone());
    let mut b = App::new(cfg, Arc::new(Shared::new(true, true, 0)));
    b.test_poll();
    assert_eq!(b.test_selected(), "crew.crew");
    v.leave();
    let _ = std::fs::remove_dir_all(&dir);
}

#[test]
fn events_from_the_status_panel_and_the_open_page_name_their_own_panel() {
    let (_g, v) = setup();
    fake::set_panel(
        "crew.crew",
        "Crew",
        "crew",
        vec![node(WIDGET_BUTTON, 1, "Go", "b.go")],
    );
    fake::set_host_panel(
        "menu.status",
        "Status",
        vec![node(WIDGET_BUTTON, 1, "Refresh", "st.refresh")],
    );
    assert_eq!(v.enter(), SCO_OK);
    let mut a = app(v);
    a.test_poll();
    // The status panel is a host panel: a click on it goes to menu.status, not to the open page.
    let k = key_of(&v, "menu.status", "st.refresh");
    a.test_send_to("menu.status", ev(WIDGET_BUTTON, k));
    a.test_flush();
    fake::dispatch();
    let d = fake::delivered();
    assert_eq!(d.len(), 1);
    assert_eq!(d[0].panel, "menu.status");
    v.leave();
}

#[test]
fn exports_refuse_bad_input_and_never_panic() {
    use crate::{
        scol_menu_abi, scol_menu_drain, scol_menu_is_broken, scol_menu_is_typing,
        scol_menu_is_visible, scol_menu_request_stop, scol_menu_show, scol_menu_start,
        scol_menu_test_step, scol_menu_version, StartV1,
    };
    // Safe to call before start and with NULL.
    assert_eq!(scol_menu_abi(), 2);
    let v = unsafe { std::ffi::CStr::from_ptr(scol_menu_version()) };
    assert!(!v.to_bytes().is_empty());
    // SAFETY: NULL is the documented refusal; the other calls take no pointers.
    unsafe {
        assert_eq!(scol_menu_start(std::ptr::null()), -1);
        let mut out = [0 as std::ffi::c_char; 8];
        assert_eq!(
            scol_menu_test_step(std::ptr::null(), out.as_mut_ptr(), 8),
            -1
        );
        let line = std::ffi::CString::new("page").unwrap();
        assert_eq!(
            scol_menu_test_step(line.as_ptr(), out.as_mut_ptr(), 8),
            1,
            "not started"
        );
        // A start block of the wrong ABI or size is refused, not trusted.
        let mut bad: StartV1 = std::mem::zeroed();
        bad.size = std::mem::size_of::<StartV1>() as u32;
        bad.abi = 1;
        assert_eq!(scol_menu_start(&bad), -1);
        bad.abi = 2;
        bad.size = 8;
        assert_eq!(scol_menu_start(&bad), -1);
        // Right size and ABI, but no view table: refused with -3.
        bad.size = std::mem::size_of::<StartV1>() as u32;
        assert_eq!(scol_menu_start(&bad), -3);
    }
    scol_menu_request_stop();
    scol_menu_show(1);
    assert_eq!(scol_menu_drain(10), 0);
    assert_eq!(scol_menu_is_typing(), 0);
    assert_eq!(scol_menu_is_visible(), 0);
    // Nothing is running, so "broken" reads as 0 only when a handle exists; with none it says 0 for
    // the hook's purposes (Active() is false then anyway).
    let _ = scol_menu_is_broken();
}
