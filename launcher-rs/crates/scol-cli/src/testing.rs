// SPDX-License-Identifier: GPL-3.0-only
//! A [`Backend`] over `scol_play::fake::FakeHost`, for tests: the whole play flow runs with a stand-in game
//! process, a shared fake system for the PC changes and the helper on a thread. It never starts the real
//! game, never asks for administrator rights and never touches a firewall, the hosts file or the EAC exe.

use crate::Backend;
use scol_play::fake::{FakeHost, FakeSignals, SharedSystem};
use scol_play::{helper_main, PlayArgs};
use std::io::Write;

pub struct FakeBackend {
    pub host: FakeHost,
    /// Arguments of every `--helper` call this backend was given.
    pub helper_calls: Vec<Vec<String>>,
    pub delete_calls: Vec<(String, String)>,
    /// Arguments of every `update` call.
    pub update_calls: Vec<Vec<String>>,
    /// `free_console` of every window call.
    pub window_calls: Vec<bool>,
    pub wants_window: bool,
}

impl FakeBackend {
    pub fn new(host: FakeHost) -> Self {
        FakeBackend {
            host,
            helper_calls: Vec::new(),
            delete_calls: Vec::new(),
            update_calls: Vec::new(),
            window_calls: Vec::new(),
            wants_window: false,
        }
    }
}

impl Backend for FakeBackend {
    fn play(&mut self, args: &PlayArgs, here: &str, out: &mut dyn Write) -> i32 {
        scol_play::run_play(args, &mut self.host, here, out)
    }
    fn helper(&mut self, args: &[String], here: &str, out: &mut dyn Write) -> i32 {
        self.helper_calls.push(args.to_vec());
        let mut sys: SharedSystem = self.host.sys.clone();
        let mut signals = FakeSignals {
            bus: Default::default(),
            parent_openable: self.host.parent_openable,
        };
        helper_main(args, here, &mut sys, &mut signals, out)
    }
    fn delete_logs(&mut self, channel_dir: &str, since: &str) -> i32 {
        self.delete_calls
            .push((channel_dir.to_string(), since.to_string()));
        scol_play::delete_session_logs(channel_dir, since.parse().unwrap_or(0))
    }
    fn update(&mut self, args: &[String], _here: &str, _out: &mut dyn Write) -> i32 {
        self.update_calls.push(args.to_vec());
        0
    }
    fn window(&mut self, _here: &str, free_console: bool) -> i32 {
        self.window_calls.push(free_console);
        0
    }
    fn wants_window(&self) -> bool {
        self.wants_window
    }
}
