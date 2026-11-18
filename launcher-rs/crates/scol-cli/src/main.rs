// SPDX-License-Identifier: GPL-3.0-only
//! `sc-offline`: the sc-offline Rust launcher command line (see the library for what it does).

#[cfg(windows)]
fn main() {
    scol_cli::main_entry();
}

#[cfg(not(windows))]
fn main() {
    eprintln!("sc-offline is Windows only, like the launcher it ports");
    std::process::exit(1);
}
