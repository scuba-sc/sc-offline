// SPDX-License-Identifier: GPL-3.0-only
//! Test host for the Rust menu against the Rust stand-in view (`sc_offline_menu::fake`). It starts the
//! menu window with real egui input, then:
//!   1. opens every page (the sample panels, the host panels, and every panel of a panel dump from the
//!      plugin workflow tests when `--dump` is given), saves a PNG of it and clicks through every
//!      control on it (toggle, slider, combo, list, table paging, text, button), checking that each
//!      one reaches the "plugin" side with the right kind, value and node;
//!   2. checks the shell: sections and titles in the sidebar, developer pages, the Settings page, the
//!      classic entry, the saved page and window size files, hide and show.
//!
//! The C++ host in tools/rust-menu-host does the click-through against sco-core's real view.
//!
//!   cargo run -p scol-menu --example menu_host -- [--dump panels.txt] [--out screenshot folder]
//!
//! Synthetic only. Not run in game.
use sc_offline_menu::blob::build::NodeSpec;
use sc_offline_menu::dump;
use sc_offline_menu::fake::{self, Delivered};
use sc_offline_menu::ffi::*;
use sc_offline_menu::{samples, spawn, Config, Handle, Host};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};

static CHECKS: AtomicU32 = AtomicU32::new(0);

fn expect(what: &str, ok: impl Fn(&[Delivered]) -> bool) -> Result<(), String> {
    let end = Instant::now() + Duration::from_secs(4);
    loop {
        let d = fake::delivered();
        if ok(&d) {
            CHECKS.fetch_add(1, Ordering::Relaxed);
            println!("ok   {what}");
            return Ok(());
        }
        if Instant::now() > end {
            return Err(format!("FAIL {what}; delivered so far: {d:?}"));
        }
        std::thread::sleep(Duration::from_millis(20));
    }
}

fn main() {
    let mut dump_path: Option<PathBuf> = None;
    let mut out: PathBuf = "D:/sc241/menu-previews/rust2".into();
    let mut only: Option<String> = None;
    let mut args = std::env::args().skip(1);
    while let Some(a) = args.next() {
        match a.as_str() {
            "--dump" => dump_path = args.next().map(PathBuf::from),
            "--only" => only = args.next(),
            "--out" => {
                if let Some(o) = args.next() {
                    out = PathBuf::from(o)
                }
            }
            other => out = PathBuf::from(other),
        }
    }
    match run(out, dump_path, only) {
        Ok(()) => println!(
            "menu_host: all checks passed ({} checks; synthetic; not run in game)",
            CHECKS.load(Ordering::Relaxed)
        ),
        Err(e) => {
            eprintln!("{e}");
            std::process::exit(1);
        }
    }
}

fn file_name(page: &str) -> String {
    page.chars()
        .map(|c| {
            if c.is_ascii_alphanumeric() || c == '.' || c == '-' || c == '_' {
                c
            } else {
                '_'
            }
        })
        .collect()
}

fn disabled_chain(nodes: &[NodeSpec], i: usize) -> bool {
    let mut cur = &nodes[i];
    loop {
        if cur.flags & WIDGET_DISABLED != 0 {
            return true;
        }
        match nodes.iter().find(|n| n.id == cur.parent && cur.parent != 0) {
            Some(p) => cur = p,
            None => return false,
        }
    }
}

fn step(menu: &Handle, line: &str) -> Result<String, String> {
    menu.step(line)
        .map_err(|e| format!("step '{line}' failed: {e}"))
}

/// Clicks every control of one panel and checks the event of each. Returns the number of controls.
fn exercise(menu: &Handle, panel: &str, nodes: &[NodeSpec]) -> Result<usize, String> {
    let mut n_done = 0;
    for (i, n) in nodes.iter().enumerate() {
        if n.tag.is_empty() || disabled_chain(nodes, i) {
            continue;
        }
        let live = fake::node(panel, &n.tag).unwrap_or_else(|| n.clone());
        let tag = n.tag.clone();
        let p = panel.to_string();
        let kind = n.kind;
        fake::clear_delivered();
        match kind {
            WIDGET_TOGGLE => {
                step(menu, &format!("click {tag}"))?;
                let want = if live.value != 0.0 { 0.0 } else { 1.0 };
                expect(&format!("{panel} {tag}: toggle"), |d| {
                    d.iter().any(|e| {
                        e.panel == p && e.tag == tag && e.kind == WIDGET_TOGGLE && e.value == want
                    })
                })?;
            }
            WIDGET_SLIDER => {
                let (lo, mut hi) = (live.minimum, live.maximum);
                if hi <= lo {
                    hi = lo + 1.0;
                }
                let frac = if (live.value - lo) / (hi - lo) > 0.5 {
                    0.15
                } else {
                    0.85
                };
                step(menu, &format!("slider {tag} {frac}"))?;
                let integer = live.presentation != NUMERIC_NUMBER;
                expect(&format!("{panel} {tag}: slider"), |d| {
                    d.iter().any(|e| {
                        e.panel == p
                            && e.tag == tag
                            && e.kind == WIDGET_SLIDER
                            && e.value >= lo - 1e-9
                            && e.value <= hi + 1e-9
                            && (!integer || e.value.fract() == 0.0)
                            && (integer
                                || ((e.value * 1000.0).round() / 1000.0 - e.value).abs() < 1e-9)
                    })
                })?;
            }
            WIDGET_COMBO if live.items.len() > 1 => {
                let k = ((live.index.max(0) as usize) + 1) % live.items.len();
                step(menu, &format!("combo {tag} {k}"))?;
                expect(&format!("{panel} {tag}: combo item {k}"), |d| {
                    d.iter().any(|e| {
                        e.panel == p
                            && e.tag == tag
                            && e.kind == WIDGET_COMBO
                            && e.index == k as i32
                    })
                })?;
            }
            WIDGET_LIST | WIDGET_TABLE => {
                let vis = live.visible_rows as usize;
                let rows = if kind == WIDGET_TABLE {
                    if live.columns == 0 {
                        0
                    } else {
                        live.items.len() / live.columns as usize
                    }
                } else {
                    live.items.len()
                };
                let first = (live.first_row as usize).min(rows);
                let last = if vis == 0 {
                    rows
                } else {
                    rows.min(first + vis)
                };
                if kind == WIDGET_LIST && last > first {
                    let cur = live.index.max(-1);
                    if let Some(k) = (first..last).find(|&k| k as i32 != cur) {
                        step(menu, &format!("list {tag} {k}"))?;
                        expect(&format!("{panel} {tag}: list row {k}"), |d| {
                            d.iter().any(|e| {
                                e.panel == p
                                    && e.tag == tag
                                    && e.kind == WIDGET_LIST
                                    && e.index == k as i32
                            })
                        })?;
                    }
                }
                if vis > 0 && rows > vis && last < rows {
                    step(menu, &format!("click {tag}.next"))?;
                    expect(&format!("{panel} {tag}: page to row {last}"), |d| {
                        d.iter().any(|e| {
                            e.panel == p
                                && e.tag == tag
                                && e.kind == WIDGET_EVENT_PAGE
                                && e.row == last as i32
                        })
                    })?;
                    // And back, so the next pass starts where this one did.
                    step(menu, &format!("click {tag}.prev"))?;
                    expect(&format!("{panel} {tag}: page back"), |d| {
                        d.iter().any(|e| {
                            e.panel == p
                                && e.tag == tag
                                && e.kind == WIDGET_EVENT_PAGE
                                && e.row == first as i32
                        })
                    })?;
                }
            }
            WIDGET_TEXT_INPUT => {
                step(menu, &format!("text {tag} Zed"))?;
                expect(&format!("{panel} {tag}: text"), |d| {
                    d.iter().any(|e| {
                        e.panel == p
                            && e.tag == tag
                            && e.kind == WIDGET_TEXT_INPUT
                            && e.text == "Zed"
                    })
                })?;
            }
            WIDGET_BUTTON => {
                if std::env::var_os("MENU_HOST_DEBUG").is_some() {
                    step(menu, &format!("check {tag}"))?;
                    step(menu, &format!("shot pre-{tag}"))?;
                }
                step(menu, &format!("click {tag}"))?;
                if std::env::var_os("MENU_HOST_DEBUG").is_some() {
                    step(menu, &format!("shot post-{tag}"))?;
                }
                expect(&format!("{panel} {tag}: button"), |d| {
                    d.iter()
                        .any(|e| e.panel == p && e.tag == tag && e.kind == WIDGET_BUTTON)
                })?;
            }
            _ => continue,
        }
        n_done += 1;
    }
    Ok(n_done)
}

/// Opens a page, saves its picture (top, and bottom when it scrolls), clicks through its controls.
fn visit(
    menu: &Handle,
    page: &str,
    panel: &str,
    nodes: &[NodeSpec],
    out: &Path,
) -> Result<usize, String> {
    step(menu, &format!("select {page}"))?;
    step(menu, "wait 150")?;
    let report = step(menu, "page")?;
    let base = file_name(panel);
    step(menu, &format!("shot {base}"))?;
    let num = |key: &str| -> f32 {
        report
            .split_whitespace()
            .find_map(|w| w.strip_prefix(&format!("{key}=")))
            .and_then(|v| v.parse().ok())
            .unwrap_or(0.0)
    };
    if num("content") > num("view") + 8.0 {
        step(menu, "scroll 1")?;
        step(menu, &format!("shot {base}-bottom"))?;
        step(menu, "scroll 0")?;
    }
    let n = exercise(menu, panel, nodes)?;
    step(menu, "scroll 0")?;
    step(menu, "wait 100")?;
    // The controls changed the panel; a second picture shows the state after the clicks.
    if n > 0 {
        step(menu, &format!("shot {base}-after"))?;
    }
    let _ = out;
    Ok(n)
}

fn run(out: PathBuf, dump_path: Option<PathBuf>, only: Option<String>) -> Result<(), String> {
    fake::reset();
    let mut pages: Vec<(String, Vec<NodeSpec>)> = vec![
        ("demo.controls".into(), samples::controls()),
        ("demo.lists".into(), samples::lists()),
        ("demo.shapes".into(), samples::shapes()),
    ];
    if let Some(p) = &dump_path {
        let text = std::fs::read_to_string(p).map_err(|e| format!("{}: {e}", p.display()))?;
        for d in dump::last_states(&dump::parse(&text)?) {
            if d.nodes.is_empty() || d.id == "menu.status" || d.id == "menu.main" {
                continue;
            }
            pages.retain(|(id, _)| id != &d.id);
            pages.push((d.id, d.nodes));
        }
    }
    for (id, nodes) in &pages {
        let owner = id.split('.').next().unwrap_or("x");
        fake::set_panel(id, id, owner, nodes.clone());
    }
    fake::set_host_panel("menu.main", "Menu", samples::host_main());
    fake::set_host_panel("menu.status", "Status", samples::host_status());
    let (table, client) = fake::table();
    // SAFETY: the fake table is a static of this process.
    let view = unsafe { sc_offline_menu::view::View::new(table, client)? };
    let log: sc_offline_menu::LogFn = Arc::new(|m: &str| println!("{m}"));
    let scratch = std::env::temp_dir().join(format!("scol-menu-host-{}", std::process::id()));
    std::fs::create_dir_all(&scratch).map_err(|e| e.to_string())?;
    let (view_path, size_path) = (
        scratch.join("menu-view.ini"),
        scratch.join("menu-window.ini"),
    );
    let classic = Arc::new(AtomicU32::new(0));
    let mut cfg = Config::new(view, log);
    cfg.shot_dir = Some(out.clone());
    cfg.start_visible = true;
    cfg.test_mode = true;
    cfg.title = "sc-offline v0.8.0".into();
    cfg.view_path = Some(view_path.clone());
    cfg.size_path = Some(size_path.clone());
    cfg.host = Host {
        open_classic: Some({
            let c = classic.clone();
            Arc::new(move || {
                c.fetch_add(1, Ordering::Relaxed);
            })
        }),
        game_window: None,
        asop_available: Some(Arc::new(|| true)),
        classic_visible: None,
    };
    let mut menu = spawn(cfg).map_err(|e| e.to_string())?;
    // The game thread: drains the event queue the way Menu_GameThreadFrame does.
    let quit = Arc::new(AtomicBool::new(false));
    let game = {
        let q = quit.clone();
        std::thread::spawn(move || {
            while !q.load(Ordering::Acquire) {
                fake::dispatch();
                std::thread::sleep(Duration::from_millis(4));
            }
        })
    };
    let result = (|| -> Result<(), String> {
        let mut controls = 0;
        // Shell first: the pages the sidebar lists, in the order the C++ menu lists them.
        for (id, nodes) in &pages {
            if only.as_ref().is_some_and(|o| o != id) {
                continue;
            }
            // The game's own ship list is drawn under the vehicles page, not as a page.
            let page = if id == "spawn.asop" {
                "spawn.vehicles"
            } else {
                id.as_str()
            };
            controls += visit(&menu, page, id, nodes, &out)?;
        }
        println!(
            "info {} controls clicked on {} pages",
            controls,
            pages.len()
        );
        // The Settings page shows the host's menu.main panel and the menu's own switches.
        controls += visit(&menu, "sc_offline.settings", "menu.main", &[], &out)?;
        // The page the player opened is saved in the C++ file format.
        step(&menu, "select demo.lists")?;
        step(&menu, "wait 200")?;
        let saved =
            std::fs::read_to_string(&view_path).map_err(|e| format!("menu-view.ini: {e}"))?;
        if !saved.starts_with("[view]\npage=demo.lists\n") {
            return Err(format!(
                "FAIL menu-view.ini holds the open page; got {saved:?}"
            ));
        }
        CHECKS.fetch_add(1, Ordering::Relaxed);
        println!("ok   open page saved: {}", saved.replace('\n', " | "));
        // Developer pages are hidden until the switch is on; the switch is saved.
        fake::set_panel("inspector.main", "Inspector", "inspector", samples::lists());
        step(&menu, "select sc_offline.settings")?;
        step(&menu, "wait 150")?;
        if step(&menu, "check nav:inspector.main").is_ok() {
            return Err("FAIL a developer page shows before the switch is on".into());
        }
        CHECKS.fetch_add(1, Ordering::Relaxed);
        println!("ok   developer page hidden");
        step(&menu, "click settings.developer")?;
        step(&menu, "wait 200")?;
        step(&menu, "check nav:inspector.main")?;
        step(&menu, "shot settings-developer-on")?;
        CHECKS.fetch_add(1, Ordering::Relaxed);
        println!("ok   developer page listed after the switch");
        let saved = std::fs::read_to_string(&view_path).map_err(|e| e.to_string())?;
        if !saved.contains("developer=1\n") {
            return Err(format!("FAIL developer=1 not saved: {saved:?}"));
        }
        CHECKS.fetch_add(1, Ordering::Relaxed);
        step(&menu, "click settings.developer")?;
        // Reset window size writes the default size file.
        step(&menu, "click settings.resetsize")?;
        step(&menu, "wait 200")?;
        let size =
            std::fs::read_to_string(&size_path).map_err(|e| format!("menu-window.ini: {e}"))?;
        if size != "[window]\nwidth=1000\nheight=800\n" {
            return Err(format!("FAIL window size file after reset: {size:?}"));
        }
        CHECKS.fetch_add(1, Ordering::Relaxed);
        println!("ok   window size reset saved");
        // The classic entry asks the host to open the ImGui window and hides this one.
        step(&menu, "click nav:sc_offline.classic")?;
        std::thread::sleep(Duration::from_millis(300));
        if classic.load(Ordering::Relaxed) != 1 {
            return Err(format!(
                "FAIL classic entry did not call the host (calls: {})",
                classic.load(Ordering::Relaxed)
            ));
        }
        if menu.is_visible() {
            return Err("FAIL the menu stays open after the classic entry".into());
        }
        CHECKS.fetch_add(1, Ordering::Relaxed);
        println!("ok   classic entry opened the ImGui window and closed this one");
        menu.show(true);
        std::thread::sleep(Duration::from_millis(400));
        // Hide and show from outside the menu thread, the way the M key does.
        menu.show(false);
        std::thread::sleep(Duration::from_millis(300));
        let hidden_at = menu.frames();
        std::thread::sleep(Duration::from_millis(400));
        println!("info frames while hidden: {}", menu.frames() - hidden_at);
        menu.show(true);
        std::thread::sleep(Duration::from_millis(400));
        if menu.frames() <= hidden_at {
            return Err("FAIL the window did not draw again after show".into());
        }
        CHECKS.fetch_add(1, Ordering::Relaxed);
        println!("ok   hide and show");
        step(&menu, "select demo.controls")?;
        fake::clear_delivered();
        step(&menu, "click flight.assist")?;
        expect("click after show", |d| {
            d.iter().any(|e| e.tag == "flight.assist")
        })?;
        menu.show(false);
        std::thread::sleep(Duration::from_millis(200));
        let _ = controls;
        Ok(())
    })();
    quit.store(true, Ordering::Release);
    let _ = game.join();
    let joined = menu.drain(Duration::from_secs(5));
    let _ = std::fs::remove_dir_all(&scratch);
    if !joined {
        return Err("the menu thread did not stop in 5 s".into());
    }
    result
}
