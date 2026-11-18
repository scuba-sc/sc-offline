// SPDX-License-Identifier: GPL-3.0-only
//! Toolchain proof. This binary does nothing but print its version. The real command line
//! (play, install, uninstall, status, update, mcp) arrives in P3.

fn main() {
    println!("scol-hello {}", env!("CARGO_PKG_VERSION"));
    println!("exit code ok = {}", scol_contract::exit_code::OK);
}
