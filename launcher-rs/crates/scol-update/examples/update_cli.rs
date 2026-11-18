// SPDX-License-Identifier: GPL-3.0-only
//! Runs `scol_update::run` on the command line, for manual checks of the real WinHTTP path
//! (for example `update --check --here <folder>`). The launcher binary calls the same function.

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    std::process::exit(scol_update::run(&args));
}
