// SPDX-License-Identifier: GPL-3.0-only
//! `scol`: the older name of the `sc-offline` binary, kept as an alias (the parity scripts run it).

#[cfg(windows)]
fn main() {
    scol_cli::main_entry();
}

#[cfg(not(windows))]
fn main() {
    eprintln!("scol is Windows only, like the launcher it ports");
    std::process::exit(1);
}
