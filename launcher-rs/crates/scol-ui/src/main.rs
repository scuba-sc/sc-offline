// SPDX-License-Identifier: GPL-3.0-only
//! `scol-ui.exe`: the launcher window. See `scol_ui::run`.
#![cfg_attr(not(test), windows_subsystem = "windows")]

fn main() {
    // A GUI-subsystem exe has no console; attach the parent's so `--help` and the self test can print.
    #[cfg(windows)]
    attach_parent_console();
    let args: Vec<String> = std::env::args().skip(1).collect();
    std::process::exit(scol_ui::run(&args));
}

#[cfg(windows)]
fn attach_parent_console() {
    #[link(name = "kernel32")]
    extern "system" {
        fn AttachConsole(process_id: u32) -> i32;
    }
    // ATTACH_PARENT_PROCESS = u32::MAX. Failure (started by double click) is fine: nothing to print to.
    // SAFETY: plain Win32 call with a constant argument.
    unsafe {
        AttachConsole(u32::MAX);
    }
}
