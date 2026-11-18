// SPDX-License-Identifier: GPL-3.0-only
//! The fake [`Host`] every test uses. It keeps the environment in a map, answers the questions from a
//! queue, runs the helper on a thread of this process (the same `scol_pcchanges::helper::run` over the
//! same copy code, with a shared fake system for the PC changes), connects the two through an in-memory
//! bus instead of named events, and starts the game as a stand-in process (a tiny exe that sleeps and
//! exits) or not at all. It never starts the real game, never asks for administrator rights and never
//! touches a firewall, the hosts file or the EAC exe.

use crate::helper::{helper_main, split_args};
use crate::host::{Host, Ready, Signals};
use scol_pcchanges::fake::FakeSystem;
use scol_pcchanges::system::{ErrorCode, LocalTime};
use scol_pcchanges::{HelperStart, System};
use std::collections::{BTreeMap, VecDeque};
use std::io::Write;
use std::process::{Child, Command};
use std::sync::{Arc, Condvar, Mutex};
use std::thread::JoinHandle;
use std::time::Duration;

/// A [`FakeSystem`] that the launcher side and the helper thread share.
#[derive(Clone, Default)]
pub struct SharedSystem(pub Arc<Mutex<FakeSystem>>);

impl SharedSystem {
    pub fn new(sys: FakeSystem) -> Self {
        SharedSystem(Arc::new(Mutex::new(sys)))
    }
    /// Runs `f` on the fake system.
    pub fn with<R>(&self, f: impl FnOnce(&mut FakeSystem) -> R) -> R {
        f(&mut self.0.lock().unwrap())
    }
}

impl System for SharedSystem {
    fn env(&self, name: &str) -> Option<String> {
        self.with(|s| s.env(name))
    }
    fn run(&mut self, exe: &str, args: &str) -> i32 {
        self.with(|s| s.run(exe, args))
    }
    fn read_file(&mut self, path: &str) -> Result<Vec<u8>, ErrorCode> {
        self.with(|s| s.read_file(path))
    }
    fn write_file(&mut self, path: &str, data: &[u8]) -> Result<(), ErrorCode> {
        self.with(|s| s.write_file(path, data))
    }
    fn rename(&mut self, from: &str, to: &str, replace: bool) -> Result<(), ErrorCode> {
        self.with(|s| s.rename(from, to, replace))
    }
    fn delete_file(&mut self, path: &str) {
        self.with(|s| s.delete_file(path))
    }
    fn create_dir(&mut self, path: &str) {
        self.with(|s| s.create_dir(path))
    }
    fn is_file(&mut self, path: &str) -> bool {
        self.with(|s| s.is_file(path))
    }
    fn can_write_to(&mut self, dir: &str) -> bool {
        self.with(|s| s.can_write_to(dir))
    }
    fn is_elevated(&mut self) -> bool {
        self.with(|s| s.is_elevated())
    }
    fn on_wine(&mut self) -> bool {
        self.with(|s| s.on_wine())
    }
    fn local_time(&mut self) -> LocalTime {
        self.with(|s| s.local_time())
    }
    fn registry_rsi_launcher(&mut self) -> Option<String> {
        self.with(|s| s.registry_rsi_launcher())
    }
    fn resolve_dirs(&mut self, bin: &str) -> Option<(String, String)> {
        self.with(|s| s.resolve_dirs(bin))
    }
}

#[derive(Default)]
struct BusState {
    ready: bool,
    closed: bool,
    parent_alive: bool,
    helper_exit: Option<i32>,
}

/// The ready and closed events and the parent process of one session, in memory.
#[derive(Default)]
pub struct Bus {
    st: Mutex<BusState>,
    cv: Condvar,
}

impl Bus {
    fn set(&self, f: impl FnOnce(&mut BusState)) {
        f(&mut self.st.lock().unwrap());
        self.cv.notify_all();
    }
    fn wait(&self, ms: Option<u32>, done: impl Fn(&BusState) -> bool) -> bool {
        let mut g = self.st.lock().unwrap();
        let deadline = ms.map(|m| std::time::Instant::now() + Duration::from_millis(u64::from(m)));
        while !done(&g) {
            match deadline {
                None => g = self.cv.wait(g).unwrap(),
                Some(d) => {
                    let now = std::time::Instant::now();
                    if now >= d {
                        return false;
                    }
                    g = self.cv.wait_timeout(g, d - now).unwrap().0;
                }
            }
        }
        true
    }
}

/// The helper's side of the bus.
pub struct FakeSignals {
    pub bus: Arc<Bus>,
    /// False makes `open_parent` fail, as a launcher that is already gone does.
    pub parent_openable: bool,
}

impl Signals for FakeSignals {
    fn open_parent(&mut self, _pid: u32) -> bool {
        self.bus.set(|s| s.parent_alive = self.parent_openable);
        self.parent_openable
    }
    fn close_parent(&mut self) {}
    fn signal_ready(&mut self, _event: &str) {
        self.bus.set(|s| s.ready = true);
    }
    fn wait_session_end(&mut self, _event: &str) {
        self.bus.wait(None, |s| s.closed || !s.parent_alive);
    }
    fn poll_ms(&self) -> u64 {
        10
    }
}

/// How the fake starts the game.
#[derive(Clone, Debug)]
pub enum GameBehaviour {
    /// Start the file at the game path as a process (the stand-in exe copied there) and let it run for
    /// this many milliseconds.
    Spawn { ms: u64, exit: i32 },
    /// Start nothing; the game "runs" only while the `on_game` callback does.
    Scripted,
}

/// Called while the game is "running": the mod is in, the PC changes are made.
pub type OnGame = Box<dyn FnMut(&mut FakeHost)>;

pub struct FakeHost {
    /// The launcher's folder; the helper thread copies the mod from here.
    pub here: String,
    pub sys: SharedSystem,
    pub env: BTreeMap<String, String>,
    /// Every `set_env` call in order.
    pub env_log: Vec<(String, String)>,
    pub pid: u32,
    pub elevated: bool,
    pub console_input: bool,
    /// Lines the player "types". When empty, input ends.
    pub answers: VecDeque<String>,
    /// The questions asked.
    pub asked: Vec<String>,
    /// Everything the host was asked to do, one line each.
    pub calls: Vec<String>,
    pub game: GameBehaviour,
    /// Fail `start_game` with this Windows error.
    pub start_error: Option<u32>,
    /// Fail `start_helper` with this Windows error.
    pub helper_error: Option<u32>,
    pub parent_openable: bool,
    pub on_game: Option<OnGame>,
    pub helper_starts: Vec<HelperStart>,
    pub urls: Vec<String>,
    pub revealed: Vec<String>,
    pub update_checks: usize,
    pub elevated_log_deletes: Vec<(String, u64)>,
    /// What `delete_logs_elevated` returns.
    pub elevated_delete_result: i32,
    pub game_started_at: Option<std::time::Instant>,
    bus: Arc<Bus>,
    helper: Option<JoinHandle<i32>>,
    child: Option<Child>,
}

impl FakeHost {
    pub fn new(here: &str, sys: SharedSystem) -> Self {
        FakeHost {
            here: here.to_string(),
            sys,
            env: BTreeMap::new(),
            env_log: Vec::new(),
            pid: 4242,
            elevated: false,
            console_input: true,
            answers: VecDeque::new(),
            asked: Vec::new(),
            calls: Vec::new(),
            game: GameBehaviour::Scripted,
            start_error: None,
            helper_error: None,
            parent_openable: true,
            on_game: None,
            helper_starts: Vec::new(),
            urls: Vec::new(),
            revealed: Vec::new(),
            update_checks: 0,
            elevated_log_deletes: Vec::new(),
            elevated_delete_result: 1,
            game_started_at: None,
            bus: Arc::new(Bus::default()),
            helper: None,
            child: None,
        }
    }
}

impl Host for FakeHost {
    fn pid(&self) -> u32 {
        self.pid
    }
    fn env(&self, name: &str) -> Option<String> {
        self.env.get(name).cloned()
    }
    fn set_env(&mut self, name: &str, value: &str) {
        self.env_log.push((name.to_string(), value.to_string()));
        if value.is_empty() {
            self.env.remove(name);
        } else {
            self.env.insert(name.to_string(), value.to_string());
        }
    }
    fn system(&mut self) -> &mut dyn System {
        &mut self.sys
    }
    fn is_elevated(&mut self) -> bool {
        self.elevated
    }
    fn owns_console_input(&mut self) -> bool {
        self.console_input
    }
    fn read_line(&mut self) -> Option<String> {
        let a = self.answers.pop_front();
        if let Some(l) = &a {
            self.asked.push(format!("answer: {l}"));
        }
        a
    }
    fn create_session(&mut self, event: &str, with_closed: bool) -> Result<(), u32> {
        self.calls
            .push(format!("create_session {event} closed={with_closed}"));
        self.bus = Arc::new(Bus::default());
        Ok(())
    }
    fn start_helper(&mut self, start: &HelperStart) -> Result<(), u32> {
        self.calls.push(format!(
            "start_helper elevated={} {}",
            start.elevated, start.args
        ));
        self.helper_starts.push(start.clone());
        if let Some(e) = self.helper_error {
            return Err(e);
        }
        let args = split_args(&start.args);
        let here = self.here.clone();
        let mut sys = self.sys.clone();
        let mut signals = FakeSignals {
            bus: self.bus.clone(),
            parent_openable: self.parent_openable,
        };
        let bus = self.bus.clone();
        self.helper = Some(std::thread::spawn(move || {
            let code = helper_main(&args, &here, &mut sys, &mut signals, &mut std::io::sink());
            bus.set(|s| s.helper_exit = Some(code));
            code
        }));
        Ok(())
    }
    fn wait_ready(&mut self) -> Ready {
        self.calls.push("wait_ready".into());
        self.bus.wait(None, |s| s.ready || s.helper_exit.is_some());
        let st = self.bus.st.lock().unwrap();
        if st.ready {
            Ready::Ready
        } else {
            Ready::HelperGone(st.helper_exit.unwrap_or(1))
        }
    }
    fn signal_closed(&mut self) {
        self.calls.push("signal_closed".into());
        self.bus.set(|s| s.closed = true);
    }
    fn wait_helper(&mut self, ms: Option<u32>) -> Option<i32> {
        self.calls.push(format!("wait_helper {ms:?}"));
        if self.bus.wait(ms, |s| s.helper_exit.is_some()) {
            let code = self.bus.st.lock().unwrap().helper_exit;
            if let Some(h) = self.helper.take() {
                let _ = h.join();
            }
            code
        } else {
            None
        }
    }
    fn helper_finished(&mut self) -> bool {
        self.bus.st.lock().unwrap().helper_exit.is_some()
    }
    fn end_session(&mut self) {
        self.calls.push("end_session".into());
        // The launcher exits: a helper still waiting stops waiting.
        self.bus.set(|s| s.parent_alive = false);
        if let Some(h) = self.helper.take() {
            let _ = h.join();
        }
    }
    fn start_game(&mut self, exe: &str, dir: &str, _out: &mut dyn Write) -> Result<(), u32> {
        self.calls.push(format!("start_game {exe} in {dir}"));
        if let Some(e) = self.start_error {
            return Err(e);
        }
        self.game_started_at = Some(std::time::Instant::now());
        if let GameBehaviour::Spawn { ms, exit } = self.game {
            let child = Command::new(exe)
                .current_dir(dir)
                .env("SCOL_STANDIN_MS", ms.to_string())
                .env("SCOL_STANDIN_EXIT", exit.to_string())
                .spawn()
                .map_err(|e| e.raw_os_error().unwrap_or(2) as u32)?;
            self.child = Some(child);
        }
        Ok(())
    }
    fn wait_game(&mut self) {
        self.calls.push("wait_game".into());
        if let Some(mut cb) = self.on_game.take() {
            cb(self);
            self.on_game = Some(cb);
        }
        if let Some(mut c) = self.child.take() {
            let _ = c.wait();
        }
    }
    fn game_poll_ms(&self) -> u64 {
        20
    }
    fn sleep(&mut self, ms: u64) {
        std::thread::sleep(Duration::from_millis(ms));
    }
    fn now_ticks(&mut self) -> u64 {
        crate::now_ticks()
    }
    fn open_url(&mut self, url: &str) {
        self.urls.push(url.to_string());
    }
    fn reveal_file(&mut self, path: &str) {
        self.revealed.push(path.to_string());
    }
    fn delete_logs_elevated(&mut self, channel_dir: &str, since: u64) -> i32 {
        self.elevated_log_deletes
            .push((channel_dir.to_string(), since));
        self.elevated_delete_result
    }
    fn check_update(&mut self, _cfg: &scol_config::Config, _out: &mut dyn Write) {
        self.update_checks += 1;
    }
}
