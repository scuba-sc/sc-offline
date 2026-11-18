// SPDX-License-Identifier: GPL-3.0-only
//! The `--helper` side: the mod copy and the waits that the PC-changes helper (`scol_pcchanges::helper`)
//! reaches through [`HelperHooks`].

use crate::host::Signals;
use crate::log::{open_log, LogOnly, Tee};
use scol_install::install::{put_mod, take_mod};
use scol_install::paths::GamePaths;
use scol_pcchanges::{HelperHooks, System};
use std::io::Write;

/// The copy and the waits of the helper: `PutMod` and `TakeMod` on the real folders, the events and the
/// parent process through [`Signals`].
pub struct FileHooks<'a> {
    /// The launcher's own folder (`dinput8.dll`, `data\`).
    pub here: String,
    pub signals: &'a mut dyn Signals,
    /// Where the step lines of the copy go (the helper's console is hidden, so the log).
    pub log: &'a mut dyn Write,
}

impl HelperHooks for FileHooks<'_> {
    fn open_parent(&mut self, pid: u32) -> bool {
        self.signals.open_parent(pid)
    }
    fn close_parent(&mut self) {
        self.signals.close_parent();
    }
    fn take_mod(&mut self, bin: &str) -> i32 {
        take_mod(&GamePaths::new(bin), false, self.log)
    }
    fn put_mod(&mut self, bin: &str, mode: &str) -> i32 {
        put_mod(&self.here, &GamePaths::new(bin), mode, false, self.log)
    }
    fn signal_ready(&mut self, event: &str) {
        self.signals.signal_ready(event);
    }
    fn wait_for_session_end(&mut self, event: &str) {
        self.signals.wait_session_end(event);
    }
    fn wait_game_closed(&mut self, bin: &str) {
        let ms = self.signals.poll_ms();
        while self.signals.game_may_be_running(bin) {
            self.signals.sleep(ms);
        }
    }
}

/// `Helper` of the C++ launcher: `args` are the arguments after the exe name (`--helper <op> <Bin64>
/// <pid> <event> <flags> [<allow>]`). Opens `data\launcher.log` for append with the header
/// `helper <op>`, and runs `scol_pcchanges::helper::run` over the real folders. `out` is the console
/// (hidden in a real run). Returns the helper's exit code: 0 ok, 2 copy failed, 3 removal failed,
/// 4 bad arguments, 5 a PC change failed.
pub fn helper_main(
    args: &[String],
    here: &str,
    sys: &mut dyn System,
    signals: &mut dyn Signals,
    out: &mut dyn Write,
) -> i32 {
    let op = args.get(1).map(String::as_str).unwrap_or("");
    let log = open_log(&format!("{here}\\data"), true, &format!("helper {op}"));
    let hook_log = log.as_ref().and_then(|f| f.try_clone().ok());
    let mut tee = Tee { out, log };
    let mut hook_out = LogOnly(hook_log);
    let mut hooks = FileHooks {
        here: here.to_string(),
        signals,
        log: &mut hook_out,
    };
    scol_pcchanges::helper::run(
        args,
        &mut scol_pcchanges::Io {
            sys,
            hooks: &mut hooks,
            out: &mut tee,
        },
    )
}

/// Splits a helper command line (`--helper play "D:\x\Bin64" 42 Local\ready-42 fhel 10.0.0.0/8`) into
/// arguments: spaces separate, double quotes group, no escapes (a Windows path has no quote).
pub fn split_args(line: &str) -> Vec<String> {
    let mut out = Vec::new();
    let mut cur = String::new();
    let mut quoted = false;
    let mut any = false;
    for c in line.chars() {
        match c {
            '"' => {
                quoted = !quoted;
                any = true;
            }
            ' ' if !quoted => {
                if any {
                    out.push(std::mem::take(&mut cur));
                    any = false;
                }
            }
            _ => {
                cur.push(c);
                any = true;
            }
        }
    }
    if any {
        out.push(cur);
    }
    out
}

#[cfg(test)]
mod tests {
    use super::split_args;

    #[test]
    fn splits_quoted_path() {
        assert_eq!(
            split_args("--helper play \"D:\\a b\\Bin64\" 7 Local\\e fhel 1.2.3.4/8"),
            vec![
                "--helper",
                "play",
                "D:\\a b\\Bin64",
                "7",
                "Local\\e",
                "fhel",
                "1.2.3.4/8"
            ]
        );
        assert_eq!(split_args("  a   b "), vec!["a", "b"]);
        assert!(split_args("").is_empty());
    }
}
