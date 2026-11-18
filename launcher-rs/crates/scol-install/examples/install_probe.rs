// SPDX-License-Identifier: GPL-3.0-only
//! Test binary for `launcher-rs/tests/install/compare.py`: runs the library's `install` and
//! `uninstall` the way the C++ `sc-offline.exe` is run, with the launcher folder given first.
//!
//!   install_probe <launcher folder> <install|uninstall> [--game <folder>] [--dry-run] [--skip-eac-check]
//!
//! It prints what the launcher prints and exits with the launcher's exit code. It has no PC-change
//! support: those are another part of the port.
#![cfg(windows)]

use std::io::Write;

fn main() {
    let mut args = std::env::args().skip(1);
    let Some(here) = args.next() else {
        eprintln!("usage: install_probe <launcher folder> <install|uninstall> [options]");
        std::process::exit(64);
    };
    let rest: Vec<String> = args.collect();
    let mut stdout = std::io::stdout();
    let code = {
        let mut io = scol_install::InstallIo {
            here,
            out: &mut stdout,
            pc: None,
        };
        scol_install::run_install(&rest, &mut io)
    };
    let _ = stdout.flush();
    std::process::exit(code);
}
