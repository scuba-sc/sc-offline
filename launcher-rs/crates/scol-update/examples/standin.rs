// SPDX-License-Identifier: GPL-3.0-only
//! A stand-in "new sc-offline.exe" for the update tests: it answers the `--self-test <tag>`
//! contract of the C++ launcher and nothing else. Exit 0 when `dinput8.dll` is beside it and
//! `standin-fail.txt` is not; 4 without the dll; 9 with the fail marker (a new launcher that fails
//! its self-test, so the old files must go back). It is not a launcher and never runs the game.

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let here = std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|d| d.to_path_buf()))
        .unwrap_or_default();
    if args.len() == 3 && args[1] == "--self-test" && args[2].starts_with('v') {
        if here.join("standin-fail.txt").exists() {
            println!("self-test: stand-in told to fail");
            std::process::exit(9);
        }
        if !here.join("dinput8.dll").is_file() {
            println!("self-test: no dinput8.dll beside the exe");
            std::process::exit(4);
        }
        println!("self-test: stand-in {} ok", args[2]);
        return;
    }
    println!("stand-in: only --self-test <tag> is supported");
    std::process::exit(1);
}
