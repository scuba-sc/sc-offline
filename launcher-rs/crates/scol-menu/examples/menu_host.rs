// SPDX-License-Identifier: GPL-3.0-only
//! Test host for the Rust menu against the Rust stand-in view (`sc_offline_menu::fake`). It starts the
//! menu window, plays a click-through with real egui input (toggle, slider, combo, text, button,
//! list, paging), checks that every event reaches the "plugin" side, and writes PNG screenshots.
//! The C++ host in tools/rust-menu-host does the same against sco-core's real view implementation.
//!
//!   cargo run -p scol-menu --example menu_host -- [screenshot folder]
//!
//! Synthetic only. Not run in game.
use sc_offline_menu::fake::{self, Delivered};
use sc_offline_menu::ffi::*;
use sc_offline_menu::{samples, spawn, Config};
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};

fn expect(what: &str, ok: impl Fn(&[Delivered]) -> bool) -> Result<(), String> {
    let end = Instant::now() + Duration::from_secs(3);
    loop {
        let d = fake::delivered();
        if ok(&d) {
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
    let dir = std::env::args()
        .nth(1)
        .map(PathBuf::from)
        .unwrap_or_else(|| "D:/sc241/menu-previews/rust".into());
    match run(dir) {
        Ok(()) => println!("menu_host: all checks passed (synthetic; not run in game)"),
        Err(e) => {
            eprintln!("{e}");
            std::process::exit(1);
        }
    }
}

fn run(dir: PathBuf) -> Result<(), String> {
    fake::reset();
    fake::set_panel("demo.controls", "Flight", "demo", samples::controls());
    fake::set_panel("demo.lists", "Ships and tables", "demo", samples::lists());
    let (table, client) = fake::table();
    // SAFETY: the fake table is a static of this process.
    let view = unsafe { sc_offline_menu::view::View::new(table, client)? };
    let log: sc_offline_menu::LogFn = Arc::new(|m: &str| println!("{m}"));
    let cfg = Config {
        view,
        log,
        nudge: None,
        shot_dir: Some(dir.clone()),
        start_visible: true,
        test_mode: true,
        hotkey: 0,
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
        let step = |line: &str| {
            menu.step(line)
                .map(|r| println!("step {line}: {r}"))
                .map_err(|e| format!("step {line} failed: {e}"))
        };
        step("select demo.controls")?;
        step("check flight.assist")?;
        step("shot controls-before")?;
        step("click flight.assist")?;
        expect("toggle off", |d| {
            d.iter()
                .any(|e| e.tag == "flight.assist" && e.kind == WIDGET_TOGGLE && e.value == 0.0)
        })?;
        step("slider flight.mix 0.5")?;
        expect("percent slider near 57", |d| {
            d.iter().any(|e| {
                e.tag == "flight.mix"
                    && e.kind == WIDGET_SLIDER
                    && (e.value - 57.5).abs() <= 6.0
                    && e.value.fract() == 0.0
            })
        })?;
        step("slider flight.gain 1.0")?;
        expect("number slider near top", |d| {
            d.iter().any(|e| e.tag == "flight.gain" && e.value > 1.8)
        })?;
        step("combo flight.mode 2")?;
        expect("combo index 2", |d| {
            d.iter()
                .any(|e| e.tag == "flight.mode" && e.kind == WIDGET_COMBO && e.index == 2)
        })?;
        step("text flight.name Zed")?;
        expect("text input Zed", |d| {
            d.iter()
                .any(|e| e.tag == "flight.name" && e.kind == WIDGET_TEXT_INPUT && e.text == "Zed")
        })?;
        step("click flight.reset")?;
        expect("button", |d| {
            d.iter()
                .any(|e| e.tag == "flight.reset" && e.kind == WIDGET_BUTTON)
        })?;
        let before = fake::delivered().len();
        step("click flight.calibrate")?;
        std::thread::sleep(Duration::from_millis(150));
        if fake::delivered().len() != before {
            return Err("FAIL a disabled button sent an event".into());
        }
        println!("ok   disabled button sends nothing");
        step("shot controls-after")?;
        step("select demo.lists")?;
        step("list list.ships 2")?;
        expect("list row 2", |d| {
            d.iter()
                .any(|e| e.tag == "list.ships" && e.kind == WIDGET_LIST && e.index == 2)
        })?;
        step("click list.ships.next")?;
        expect("page to row 4", |d| {
            d.iter()
                .any(|e| e.tag == "list.ships" && e.kind == WIDGET_EVENT_PAGE && e.row == 4)
        })?;
        step("click table.stats.next")?;
        expect("table page to row 3", |d| {
            d.iter()
                .any(|e| e.tag == "table.stats" && e.kind == WIDGET_EVENT_PAGE && e.row == 3)
        })?;
        step("click cols.details")?;
        expect("toggle in a column", |d| {
            d.iter().any(|e| e.tag == "cols.details" && e.value == 1.0)
        })?;
        step("shot lists")?;
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
        println!("ok   hide and show");
        step("click flight.assist").ok();
        step("select demo.controls")?;
        step("click flight.assist")?;
        expect("toggle on again after show", |d| {
            d.iter().filter(|e| e.tag == "flight.assist").count() >= 2
        })?;
        menu.show(false);
        std::thread::sleep(Duration::from_millis(200));
        Ok(())
    })();
    quit.store(true, Ordering::Release);
    let _ = game.join();
    let joined = menu.drain(Duration::from_secs(5));
    if !joined {
        return Err("the menu thread did not stop in 5 s".into());
    }
    result
}
