// SPDX-License-Identifier: GPL-3.0-only
//! `scol-pm`: the plugin manager on its own. The launcher CLI calls the same
//! `scol_plugin_manager::run` for `scol plugins ...`.

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    std::process::exit(scol_plugin_manager::run(&args));
}
