// SPDX-License-Identifier: GPL-3.0-only
//! sc-offline in-game menu in Rust (first working version). It reads `sco.widgets` panels through
//! the widgets view C interface (`sco/widgets_view.h`), draws them with egui on its own thread and
//! sends the player's input back as view events. The in-game host (dinput8.dll, hooks, Core host
//! kit) stays C++ and calls the exports at the bottom of this file. Design: docs/rust-menu.md.
//!
//! Status: built and driven by synthetic tests only. Not run in game.
//!
//! Threads: the menu thread owns the window and every view call except `dispatch_events`, which
//! the host's game thread calls. The thread registers itself with `menu_thread_enter` before it
//! does anything else and leaves on every exit path.
pub mod app;
pub mod blob;
pub mod fake;
pub mod ffi;
pub mod render;
pub mod samples;
pub mod script;
pub mod theme;
pub mod view;

#[cfg(test)]
mod tests;

pub use app::{Config, LogFn};
use std::ffi::{c_char, c_void, CStr};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc;
use std::sync::{Arc, Mutex};
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

pub struct Job {
    pub line: String,
    pub reply: mpsc::Sender<Result<String, String>>,
}

/// State shared between the menu thread, the hotkey thread and the host's calls.
pub struct Shared {
    pub stop: AtomicBool,
    pub typing: AtomicBool,
    pub visible: AtomicBool,
    pub broken: AtomicBool,
    /// Frames the menu thread has run, for the tests.
    pub frames: std::sync::atomic::AtomicU64,
    pub test_mode: bool,
    /// Virtual-key code that opens and closes the window; 0 = none.
    pub hotkey: u32,
    pub jobs: Mutex<std::collections::VecDeque<Job>>,
    pub ctx: Mutex<Option<egui::Context>>,
}

impl Shared {
    fn new(visible: bool, test_mode: bool, hotkey: u32) -> Self {
        Shared {
            stop: AtomicBool::new(false),
            typing: AtomicBool::new(false),
            visible: AtomicBool::new(visible),
            broken: AtomicBool::new(false),
            frames: Default::default(),
            test_mode,
            hotkey,
            jobs: Mutex::new(Default::default()),
            ctx: Mutex::new(None),
        }
    }

    fn context(&self) -> Option<egui::Context> {
        self.ctx.lock().ok().and_then(|c| c.clone())
    }

    pub fn wake(&self) {
        if let Some(c) = self.context() {
            c.request_repaint();
        }
    }

    /// Show or hide the window. Callable from any thread.
    pub fn set_visible(&self, on: bool) {
        self.visible.store(on, Ordering::Release);
        if let Some(c) = self.context() {
            c.send_viewport_cmd(egui::ViewportCommand::Visible(on));
            if on {
                c.send_viewport_cmd(egui::ViewportCommand::Focus);
            }
            c.request_repaint();
        }
    }
}

/// A running menu. Dropping it does not stop the thread; call `request_stop` and `drain`.
pub struct Handle {
    shared: Arc<Shared>,
    thread: Option<JoinHandle<()>>,
    hotkeys: Option<JoinHandle<()>>,
}

impl Handle {
    pub fn request_stop(&self) {
        self.shared.stop.store(true, Ordering::Release);
        if let Some(c) = self.shared.context() {
            c.send_viewport_cmd(egui::ViewportCommand::Close);
            c.request_repaint();
        }
    }

    /// Joins the menu thread. False when it is still running after `timeout`.
    pub fn drain(&mut self, timeout: Duration) -> bool {
        self.request_stop();
        let end = Instant::now() + timeout;
        while self.thread.as_ref().is_some_and(|t| !t.is_finished()) {
            if Instant::now() >= end {
                return false;
            }
            self.shared.wake();
            std::thread::sleep(Duration::from_millis(5));
        }
        if let Some(t) = self.thread.take() {
            let _ = t.join();
        }
        if let Some(h) = self.hotkeys.take() {
            let _ = h.join();
        }
        true
    }

    pub fn is_typing(&self) -> bool {
        self.shared.typing.load(Ordering::Acquire)
    }

    pub fn is_visible(&self) -> bool {
        self.shared.visible.load(Ordering::Acquire)
    }

    pub fn frames(&self) -> u64 {
        self.shared.frames.load(Ordering::Acquire)
    }

    pub fn is_broken(&self) -> bool {
        self.shared.broken.load(Ordering::Acquire)
    }

    pub fn toggle(&self) {
        self.shared
            .set_visible(!self.shared.visible.load(Ordering::Acquire));
    }

    pub fn show(&self, on: bool) {
        self.shared.set_visible(on);
    }

    /// Runs one test-driver step (see `script.rs`) and waits for it. Only in test mode.
    pub fn step(&self, line: &str) -> Result<String, String> {
        if !self.shared.test_mode {
            return Err("the test driver is off (start with the test flag)".into());
        }
        let (tx, rx) = mpsc::channel();
        self.shared
            .jobs
            .lock()
            .map_err(|_| "driver lock poisoned")?
            .push_back(Job {
                line: line.to_string(),
                reply: tx,
            });
        self.shared.wake();
        let end = Instant::now() + Duration::from_secs(30);
        loop {
            match rx.recv_timeout(Duration::from_millis(50)) {
                Ok(r) => return r,
                Err(mpsc::RecvTimeoutError::Timeout) => {
                    if Instant::now() > end {
                        return Err("the menu did not answer in 30 s".into());
                    }
                    self.shared.wake();
                }
                Err(mpsc::RecvTimeoutError::Disconnected) => return Err("the menu stopped".into()),
            }
        }
    }
}

/// Starts the menu thread. The view calls all happen on that thread.
pub fn spawn(config: Config) -> std::io::Result<Handle> {
    let shared = Arc::new(Shared::new(
        config.start_visible,
        config.test_mode,
        config.hotkey,
    ));
    let s = shared.clone();
    let thread = std::thread::Builder::new()
        .name("scol-menu".into())
        .spawn(move || menu_thread(config, s))?;
    let hotkeys = {
        let s = shared.clone();
        std::thread::Builder::new()
            .name("scol-menu-keys".into())
            .spawn(move || hotkey_thread(s))
            .ok()
    };
    Ok(Handle {
        shared,
        thread: Some(thread),
        hotkeys,
    })
}

struct Leave(view::View, LogFn);
impl Drop for Leave {
    fn drop(&mut self) {
        let r = self.0.leave();
        if r != ffi::SCO_OK {
            (self.1)(&format!("[menu] menu_thread_leave answered {r}"));
        }
    }
}

fn menu_thread(config: Config, shared: Arc<Shared>) {
    let log = config.log.clone();
    let result = catch_unwind(AssertUnwindSafe(|| run_window(config, shared.clone())));
    match result {
        Ok(Ok(())) => log("[menu] window closed"),
        Ok(Err(e)) => {
            log(&format!("[menu] stopped: {e}"));
            shared.broken.store(true, Ordering::Release);
        }
        Err(_) => {
            log("[menu] stopped: a panic in the menu thread; the menu stays off for this session");
            shared.broken.store(true, Ordering::Release);
        }
    }
    shared.visible.store(false, Ordering::Release);
}

fn run_window(config: Config, shared: Arc<Shared>) -> Result<(), String> {
    // First statement that touches the view: register this thread (docs/rust-menu.md, 3.2).
    let r = config.view.enter();
    if r != ffi::SCO_OK {
        return Err(format!("menu_thread_enter answered {r}"));
    }
    let _leave = Leave(config.view, config.log.clone());
    if let Ok(caps) = config.view.capabilities() {
        (config.log)(&format!("[menu] view capabilities: {caps:?}"));
    }
    let test = config.test_mode;
    let visible = config.start_visible;
    let mut viewport = egui::ViewportBuilder::default()
        .with_title("Star Citizen Offline Mods")
        .with_inner_size([1000.0, 760.0])
        .with_min_inner_size([640.0, 420.0])
        .with_visible(visible)
        .with_always_on_top();
    if !test {
        viewport = viewport.with_decorations(false).with_taskbar(false);
    }
    let mut options = eframe::NativeOptions {
        viewport,
        run_and_return: true,
        ..Default::default()
    };
    options.event_loop_builder = Some(Box::new(|b| {
        use winit::platform::windows::EventLoopBuilderExtWindows;
        // The menu runs on its own thread, and it leaves the game's DPI awareness alone.
        b.with_any_thread(true);
        b.with_dpi_aware(false);
    }));
    let sh = shared.clone();
    eframe::run_native(
        "scol-menu",
        options,
        Box::new(move |cc| {
            theme::load_fonts(&cc.egui_ctx)?;
            theme::apply(&cc.egui_ctx);
            if let Ok(mut c) = sh.ctx.lock() {
                *c = Some(cc.egui_ctx.clone());
            }
            Ok(Box::new(app::App::new(config, sh.clone())))
        }),
    )
    .map_err(|e| e.to_string())
}

#[cfg(windows)]
fn hotkey_thread(shared: Arc<Shared>) {
    use windows_sys::Win32::System::Threading::GetCurrentProcessId;
    use windows_sys::Win32::UI::Input::KeyboardAndMouse::GetAsyncKeyState;
    use windows_sys::Win32::UI::WindowsAndMessaging::{
        GetForegroundWindow, GetWindowThreadProcessId,
    };
    let mut was_down = false;
    while !shared.stop.load(Ordering::Acquire) {
        std::thread::sleep(Duration::from_millis(25));
        if shared.test_mode || shared.hotkey == 0 {
            continue;
        }
        // SAFETY: plain Win32 queries with no pointers other than a valid out parameter.
        let (down, ours) = unsafe {
            let down = GetAsyncKeyState(shared.hotkey as i32) as u16 & 0x8000 != 0;
            let mut pid = 0u32;
            GetWindowThreadProcessId(GetForegroundWindow(), &mut pid);
            (down, pid == GetCurrentProcessId())
        };
        let edge = down && !was_down;
        was_down = down;
        // The hotkey opens or closes the menu, only while one of this process's windows is in front and no
        // text box has the keyboard.
        if edge && ours && !shared.typing.load(Ordering::Acquire) {
            shared.set_visible(!shared.visible.load(Ordering::Acquire));
        }
    }
}

#[cfg(not(windows))]
fn hotkey_thread(_shared: Arc<Shared>) {}

// ---- the C exports the C++ host calls (src/menu_rust.cpp) ----

pub const ABI: u32 = 1;
pub const FLAG_START_VISIBLE: u32 = 1;
pub const FLAG_TEST_MODE: u32 = 2;

/// Start block. Product-private, not an SDK ABI. `log` and `nudge_game` may be NULL.
#[repr(C)]
pub struct StartV1 {
    pub size: u32,
    pub abi: u32,
    pub game_window: *mut c_void,
    pub view: *const ffi::sco_widgets_view_v1,
    pub client: *mut c_void,
    /// Receives one UTF-8 line, no newline. Any thread.
    pub log: Option<unsafe extern "C" fn(*const c_char)>,
    /// Asks the host to run its game-thread pump (the host posts WM_NULL). Menu thread.
    pub nudge_game: Option<unsafe extern "C" fn()>,
    /// Folder for test screenshots, UTF-8, or NULL.
    pub shot_dir: *const c_char,
    pub flags: u32,
    /// Virtual-key code of the open/close key (for example 0x4D for M), or 0 for none.
    pub hotkey: u32,
}

static RUNNING: Mutex<Option<Handle>> = Mutex::new(None);

fn running() -> std::sync::MutexGuard<'static, Option<Handle>> {
    RUNNING.lock().unwrap_or_else(|p| p.into_inner())
}

/// 0 started; -1 bad block; -2 already started; -3 the view table was refused; -4 no thread.
///
/// # Safety
/// `start` must point to a valid `StartV1` whose pointers are valid for the call, and `view`/`client`
/// must be the static table and handle from the host (see `sco/widgets_view.h`).
#[no_mangle]
pub unsafe extern "C" fn scol_menu_start(start: *const StartV1) -> i32 {
    catch_unwind(|| start_inner(start)).unwrap_or(-1)
}

unsafe fn start_inner(start: *const StartV1) -> i32 {
    if start.is_null()
        || (*start).size < std::mem::size_of::<StartV1>() as u32
        || (*start).abi != ABI
    {
        return -1;
    }
    let s = &*start;
    let mut guard = running();
    if guard.is_some() {
        return -2;
    }
    let view = match view::View::new(s.view, s.client) {
        Ok(v) => v,
        Err(_) => return -3,
    };
    let log_fn = s.log;
    let log: LogFn = Arc::new(move |m: &str| match log_fn {
        Some(f) => {
            if let Ok(c) = std::ffi::CString::new(m.replace('\0', " ")) {
                f(c.as_ptr());
            }
        }
        None => eprintln!("{m}"),
    });
    let nudge = s
        .nudge_game
        .map(|f| Arc::new(move || f()) as Arc<dyn Fn() + Send + Sync>);
    let shot_dir = if s.shot_dir.is_null() {
        None
    } else {
        CStr::from_ptr(s.shot_dir).to_str().ok().map(PathBuf::from)
    };
    let cfg = Config {
        view,
        log,
        nudge,
        shot_dir,
        start_visible: s.flags & FLAG_START_VISIBLE != 0,
        test_mode: s.flags & FLAG_TEST_MODE != 0,
        hotkey: s.hotkey,
    };
    match spawn(cfg) {
        Ok(h) => {
            *guard = Some(h);
            0
        }
        Err(_) => -4,
    }
}

/// Never waits. Any thread.
#[no_mangle]
pub extern "C" fn scol_menu_request_stop() {
    let _ = catch_unwind(|| {
        if let Some(h) = running().as_ref() {
            h.request_stop();
        }
    });
}

/// Control thread. 0 joined (or never started); 1 still running after the timeout.
#[no_mangle]
pub extern "C" fn scol_menu_drain(timeout_ms: u32) -> i32 {
    catch_unwind(|| {
        let mut guard = running();
        let Some(h) = guard.as_mut() else { return 0 };
        if h.drain(Duration::from_millis(u64::from(timeout_ms.min(60_000)))) {
            *guard = None;
            0
        } else {
            1
        }
    })
    .unwrap_or(1)
}

/// Any thread. Reads one atomic the menu thread writes once per frame.
#[no_mangle]
pub extern "C" fn scol_menu_is_typing() -> i32 {
    catch_unwind(|| running().as_ref().is_some_and(|h| h.is_typing()) as i32).unwrap_or(0)
}

/// 1 when the window is shown.
#[no_mangle]
pub extern "C" fn scol_menu_is_visible() -> i32 {
    catch_unwind(|| running().as_ref().is_some_and(|h| h.is_visible()) as i32).unwrap_or(0)
}

/// 1 when the menu thread stopped on an error or a panic.
#[no_mangle]
pub extern "C" fn scol_menu_is_broken() -> i32 {
    catch_unwind(|| running().as_ref().is_some_and(|h| h.is_broken()) as i32).unwrap_or(1)
}

/// Shows (1) or hides (0) the window. Any thread.
#[no_mangle]
pub extern "C" fn scol_menu_show(visible: i32) {
    let _ = catch_unwind(|| {
        if let Some(h) = running().as_ref() {
            h.show(visible != 0);
        }
    });
}

/// Test driver: runs one step line and writes the answer (UTF-8, NUL-terminated, cut to `cap`) to
/// `out`. Returns 0 when the step passed, 1 when it failed, -1 for bad arguments. Test mode only.
///
/// # Safety
/// `line` must be a NUL-terminated string and `out` must have room for `cap` bytes (or be NULL).
#[no_mangle]
pub unsafe extern "C" fn scol_menu_test_step(
    line: *const c_char,
    out: *mut c_char,
    cap: u32,
) -> i32 {
    if line.is_null() {
        return -1;
    }
    let line = CStr::from_ptr(line).to_string_lossy().into_owned();
    let result = catch_unwind(|| {
        // The lock is not held while the step runs.
        let step_shared = running().as_ref().map(|h| h.shared.clone());
        match step_shared {
            Some(shared) => Handle {
                shared,
                thread: None,
                hotkeys: None,
            }
            .step(&line),
            None => Err("the menu is not started".into()),
        }
    })
    .unwrap_or_else(|_| Err("panic in the driver".into()));
    let (code, text) = match &result {
        Ok(t) => (0, t.clone()),
        Err(e) => (1, e.clone()),
    };
    if !out.is_null() && cap > 0 {
        let bytes = text.as_bytes();
        let n = bytes.len().min(cap as usize - 1);
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), out as *mut u8, n);
        *out.add(n) = 0;
    }
    code
}

/// ABI number of this module's export set.
#[no_mangle]
pub extern "C" fn scol_menu_abi() -> u32 {
    ABI
}
