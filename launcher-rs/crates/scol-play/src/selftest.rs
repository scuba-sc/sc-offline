// SPDX-License-Identifier: GPL-3.0-only
//! `--self-test <tag>`: what an update runs on a new launcher before keeping it (`SelfTest` of the C++
//! launcher). Checks this exe is the version the manifest promised, that SHA-256 works and that
//! `dinput8.dll` is beside it, and says which game DLL a Play would install for `menu = rust`.
//! Writes nothing and reads no network.

use scol_install::moddll::{menu_from_ini, select_source};
use scol_install::version::SCO_VERSION;
use scol_plugins::sha256::hex_digest;
use std::io::Write;

/// Exit code: the version is not the one expected.
pub const EXIT_WRONG_VERSION: i32 = 2;
/// Exit code: SHA-256 gave a wrong digest.
pub const EXIT_SHA_BROKEN: i32 = 3;
/// Exit code: no `dinput8.dll` beside the exe.
pub const EXIT_NO_DLL: i32 = 4;

/// `SelfTest`. `here` is the folder of the exe.
pub fn self_test(tag: &str, here: &str, out: &mut dyn Write) -> i32 {
    if tag != format!("v{SCO_VERSION}") {
        let _ = writeln!(out, "self-test: this is {SCO_VERSION}, expected {tag}");
        return EXIT_WRONG_VERSION;
    }
    if hex_digest(b"abc") != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" {
        let _ = writeln!(out, "self-test: SHA-256 broken");
        return EXIT_SHA_BROKEN;
    }
    if !std::path::Path::new(&format!("{here}\\dinput8.dll")).is_file() {
        let _ = writeln!(out, "self-test: no dinput8.dll beside sc-offline.exe");
        return EXIT_NO_DLL;
    }
    // The DLL a Play would install under the ini's menu key: dinput8-rust-menu.dll only for menu = rust
    // with that file beside the exe, else dinput8.dll (checked above). Says nothing for the default.
    let menu = menu_from_ini(here);
    if let Some(note) = select_source(here, menu).note(menu) {
        let _ = writeln!(out, "self-test: {}", note.trim_start_matches("[i] "));
    }
    let _ = writeln!(out, "self-test: sc-offline {SCO_VERSION} ok");
    0
}
