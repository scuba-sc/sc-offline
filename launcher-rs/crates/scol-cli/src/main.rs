// SPDX-License-Identifier: GPL-3.0-only
//! `scol`: the sc-offline Rust launcher command line (see the library for what it does).

#[cfg(windows)]
fn main() {
    use std::io::Write;
    let args: Vec<String> = std::env::args_os()
        .skip(1)
        .map(|a| a.to_string_lossy().into_owned())
        .collect();
    // `ExeDir`: the folder of this executable; "." when it cannot be told.
    let here = std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|d| d.to_string_lossy().into_owned()))
        .unwrap_or_else(|| ".".to_string());
    let mut out = std::io::stdout().lock();
    let code = scol_cli::run(&args, &here, &mut out);
    let _ = out.flush();
    drop(out);
    std::process::exit(code);
}

#[cfg(not(windows))]
fn main() {
    eprintln!("scol is Windows only, like the launcher it ports");
    std::process::exit(1);
}
