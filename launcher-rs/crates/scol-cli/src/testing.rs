// SPDX-License-Identifier: GPL-3.0-only
//! A [`Backend`] over `scol_play::fake::FakeHost`, for tests: the whole play flow runs with a stand-in game
//! process, a shared fake system for the PC changes and the helper on a thread. It never starts the real
//! game, never asks for administrator rights and never touches a firewall, the hosts file or the EAC exe.

use crate::{run_stop, Backend, StopArgs, StopTiming};
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
    /// Every `stop` call, and the fake stop port it runs over.
    pub stop_calls: Vec<StopArgs>,
    pub stop_port: scol_play::stop::FakeStopPort,
    /// How `stop` waits and what it does with a game that stays (the tests shorten the wait).
    pub stop_timing: StopTiming,
    /// What the console answers to a question of `stop`, and the questions asked.
    pub stop_answer: bool,
    pub stop_asked: Vec<String>,
    /// Every `report` call.
    pub report_calls: Vec<scol_play::report::ReportArgs>,
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
            stop_calls: Vec::new(),
            stop_port: Default::default(),
            stop_timing: StopTiming {
                ack_wait_ms: 100,
                wait_seconds: 2,
                end: scol_play::stop::EndPolicy::Never,
            },
            stop_answer: false,
            stop_asked: Vec::new(),
            report_calls: Vec::new(),
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
    fn stop(&mut self, args: &StopArgs, here: &str, out: &mut dyn Write) -> i32 {
        self.stop_calls.push(args.clone());
        let mut timing = self.stop_timing;
        if args.end {
            timing.end = scol_play::stop::EndPolicy::Yes;
        }
        let (answer, asked) = (self.stop_answer, &mut self.stop_asked);
        run_stop(
            args,
            here,
            &mut self.stop_port,
            timing,
            &mut |q| {
                asked.push(q.to_string());
                answer
            },
            out,
        )
    }
    fn report(
        &mut self,
        args: &scol_play::report::ReportArgs,
        here: &str,
        out: &mut dyn Write,
    ) -> i32 {
        self.report_calls.push(args.clone());
        scol_play::report::make_report(&mut self.host, out, here, args)
    }
    fn window(&mut self, _here: &str, free_console: bool) -> i32 {
        self.window_calls.push(free_console);
        0
    }
    fn wants_window(&self) -> bool {
        self.wants_window
    }
}
