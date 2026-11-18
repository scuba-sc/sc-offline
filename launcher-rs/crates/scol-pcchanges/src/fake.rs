// SPDX-License-Identifier: GPL-3.0-only
//! The fake [`System`] every test uses. It keeps files in memory, remembers firewall rule names
//! the way `netsh` would answer `show`, `add` and `delete`, and logs every call as one line so a
//! test can assert the exact command lines.

use crate::helper::HelperHooks;
use crate::system::{ErrorCode, LocalTime, System};
use std::collections::{BTreeMap, BTreeSet};

/// Error code the fake gives to a failure it was told to make.
pub const FAKE_ERROR: ErrorCode = 5;

#[derive(Default)]
pub struct FakeSystem {
    pub env: BTreeMap<String, String>,
    /// Files by exact path.
    pub files: BTreeMap<String, Vec<u8>>,
    /// Calls in order: `run "<exe>" <args>`, `write <path>`, `rename <a> -> <b> replace=<bool>`,
    /// `delete <path>`, `mkdir <dir>`.
    pub log: Vec<String>,
    /// Firewall rule names currently present.
    pub rules: BTreeSet<String>,
    /// A `run` whose command line contains one of these returns 1 and changes nothing.
    pub fail_run: Vec<String>,
    /// Writes to these paths fail.
    pub fail_write: Vec<String>,
    /// Renames from these paths fail.
    pub fail_rename: Vec<String>,
    /// Reads of these paths fail.
    pub fail_read: Vec<String>,
    /// Folders `can_write_to` accepts.
    pub writable: BTreeSet<String>,
    pub elevated: bool,
    pub wine: bool,
    pub time: LocalTime,
    /// What the registry lookup of RSI Launcher.exe finds.
    pub registry_rsi: Option<String>,
}

impl FakeSystem {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn put(&mut self, path: &str, data: &str) {
        self.files
            .insert(path.to_string(), data.as_bytes().to_vec());
    }

    pub fn text(&self, path: &str) -> Option<String> {
        self.files
            .get(path)
            .map(|b| String::from_utf8_lossy(b).into_owned())
    }

    /// The `run` lines of the log.
    pub fn commands(&self) -> Vec<String> {
        self.log
            .iter()
            .filter(|l| l.starts_with("run "))
            .cloned()
            .collect()
    }
}

fn rule_name(args: &str) -> Option<String> {
    let start = args.find("name=\"")? + 6;
    let len = args[start..].find('"')?;
    Some(args[start..start + len].to_string())
}

impl System for FakeSystem {
    fn env(&self, name: &str) -> Option<String> {
        self.env.get(name).cloned()
    }

    fn run(&mut self, exe: &str, args: &str) -> i32 {
        let line = format!("\"{exe}\" {args}");
        self.log.push(format!("run {line}"));
        if self.fail_run.iter().any(|f| line.contains(f.as_str())) {
            return 1;
        }
        if let Some(name) = rule_name(args) {
            if args.starts_with("advfirewall firewall add rule") {
                self.rules.insert(name);
            } else if args.starts_with("advfirewall firewall delete rule") {
                return if self.rules.remove(&name) { 0 } else { 1 };
            } else if args.starts_with("advfirewall firewall show rule") {
                return if self.rules.contains(&name) { 0 } else { 1 };
            }
        }
        0
    }

    fn read_file(&mut self, path: &str) -> Result<Vec<u8>, ErrorCode> {
        if self.fail_read.iter().any(|p| p == path) {
            return Err(FAKE_ERROR);
        }
        self.files.get(path).cloned().ok_or(2)
    }

    fn write_file(&mut self, path: &str, data: &[u8]) -> Result<(), ErrorCode> {
        self.log.push(format!("write {path}"));
        if self.fail_write.iter().any(|p| p == path) {
            return Err(FAKE_ERROR);
        }
        self.files.insert(path.to_string(), data.to_vec());
        Ok(())
    }

    fn rename(&mut self, from: &str, to: &str, replace: bool) -> Result<(), ErrorCode> {
        self.log
            .push(format!("rename {from} -> {to} replace={replace}"));
        if self.fail_rename.iter().any(|p| p == from) {
            return Err(FAKE_ERROR);
        }
        if !replace && self.files.contains_key(to) {
            return Err(183);
        }
        match self.files.remove(from) {
            Some(data) => {
                self.files.insert(to.to_string(), data);
                Ok(())
            }
            None => Err(2),
        }
    }

    fn delete_file(&mut self, path: &str) {
        self.log.push(format!("delete {path}"));
        self.files.remove(path);
    }

    fn create_dir(&mut self, path: &str) {
        self.log.push(format!("mkdir {path}"));
    }

    fn is_file(&mut self, path: &str) -> bool {
        self.files.contains_key(path)
    }

    fn can_write_to(&mut self, dir: &str) -> bool {
        self.writable.contains(dir)
    }

    fn is_elevated(&mut self) -> bool {
        self.elevated
    }

    fn on_wine(&mut self) -> bool {
        self.wine
    }

    fn local_time(&mut self) -> LocalTime {
        self.time
    }

    fn registry_rsi_launcher(&mut self) -> Option<String> {
        self.registry_rsi.clone()
    }

    fn resolve_dirs(&mut self, bin: &str) -> Option<(String, String)> {
        if bin.is_empty() || bin.contains('\0') {
            return None;
        }
        let text = bin.replace('/', "\\");
        let mut parts: Vec<&str> = Vec::new();
        for c in text.split('\\') {
            match c {
                "" | "." => {}
                ".." => {
                    // Never above the drive.
                    if parts.len() > 1 {
                        parts.pop();
                    }
                }
                other => parts.push(other),
            }
        }
        if parts.is_empty() {
            return None;
        }
        let root = parts[0].ends_with(':');
        let join = |p: &[&str]| -> String {
            if p.len() == 1 && root {
                format!("{}\\", p[0])
            } else {
                p.join("\\")
            }
        };
        let bin_out = join(&parts);
        let channel = join(&parts[..parts.len().saturating_sub(1).max(1)]);
        Some((bin_out, channel))
    }
}

/// A fake for the launcher side of the helper. It logs each call.
#[derive(Default)]
pub struct FakeHooks {
    pub log: Vec<String>,
    pub parent_opens: bool,
    pub put_result: i32,
    pub take_result: i32,
}

impl FakeHooks {
    pub fn new() -> Self {
        FakeHooks {
            parent_opens: true,
            ..Default::default()
        }
    }
}

impl HelperHooks for FakeHooks {
    fn open_parent(&mut self, pid: u32) -> bool {
        self.log.push(format!("open_parent {pid}"));
        self.parent_opens
    }
    fn close_parent(&mut self) {
        self.log.push("close_parent".to_string());
    }
    fn take_mod(&mut self, bin: &str) -> i32 {
        self.log.push(format!("take_mod {bin}"));
        self.take_result
    }
    fn put_mod(&mut self, bin: &str, mode: &str) -> i32 {
        self.log.push(format!("put_mod {bin} {mode}"));
        self.put_result
    }
    fn signal_ready(&mut self, event: &str) {
        self.log.push(format!("signal_ready {event}"));
    }
    fn wait_for_session_end(&mut self, event: &str) {
        self.log.push(format!("wait_for_session_end {event}"));
    }
    fn wait_game_closed(&mut self, bin: &str) {
        self.log.push(format!("wait_game_closed {bin}"));
    }
}
