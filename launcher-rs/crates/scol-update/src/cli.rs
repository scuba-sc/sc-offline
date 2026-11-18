// SPDX-License-Identifier: GPL-3.0-only
//! The `update` command and the hidden update entry points the C++ launcher has:
//!
//! - `update [--check] [--yes] [--prerelease]` check GitHub, ask, download, stage, swap;
//! - `--elevated-update <tag> <staged folder>` the swap as administrator, no network;
//! - `--self-test <tag>` what an update runs on the new exe before keeping it;
//! - `--apply-zip <zip> <tag> <sha256>` (feature `test-hooks` only, like `SCO_UPDATE_TEST`).
//!
//! `--here <folder>` overrides the folder of the running exe. Exit codes: 0 ok (or up to date),
//! 1 error.

use std::io::Write;
use std::path::PathBuf;

use scol_catalogue::fetch::Fetcher;

use crate::flow;
use crate::platform::{Platform, System};
use crate::swap;
#[cfg(feature = "test-hooks")]
use crate::swap::SwapOptions;

/// What the command needs to know about the running launcher.
#[derive(Debug, Clone)]
pub struct Context {
    /// The folder of the running exe (holds `sc-offline.exe`, `dinput8.dll`, `data\`).
    pub here: PathBuf,
    /// The running launcher's version, `0.7.1` (no `v`).
    pub version: String,
    /// `update_channel = prerelease` in `sc-offline.ini`.
    pub prerelease: bool,
    /// `check_updates` in `sc-offline.ini` (gates only the automatic check before `play`).
    pub check_updates: bool,
}

const USAGE: &str = "usage: update [--check] [--yes] [--prerelease] [--here <folder>]\n\
                     \x20      --elevated-update <tag> <staged folder>\n\
                     \x20      --self-test <tag>";

/// Real network, machine, console. `args` exclude the program name.
pub fn run(args: &[String]) -> i32 {
    let here = args
        .iter()
        .position(|a| a == "--here")
        .and_then(|i| args.get(i + 1))
        .map(PathBuf::from)
        .or_else(|| {
            std::env::current_exe()
                .ok()
                .and_then(|p| p.parent().map(|d| d.to_path_buf()))
        })
        .unwrap_or_else(|| PathBuf::from("."));
    #[allow(unused_mut)]
    let mut version = env!("CARGO_PKG_VERSION").to_string();
    #[cfg(feature = "test-hooks")]
    if let Some(v) = args
        .iter()
        .position(|a| a == "--current")
        .and_then(|i| args.get(i + 1))
    {
        version = v.clone();
    }
    let ctx = Context {
        here,
        version,
        prerelease: false,
        check_updates: true,
    };
    let mut out = std::io::stdout().lock();
    let mut ask = |q: &str| -> bool {
        use std::io::IsTerminal;
        if !std::io::stdin().is_terminal() {
            return false;
        }
        print!("{q} [y/N] ");
        let _ = std::io::stdout().flush();
        let mut l = String::new();
        std::io::stdin().read_line(&mut l).is_ok() && l.trim().to_ascii_lowercase().starts_with('y')
    };
    #[cfg(windows)]
    {
        let mut net = crate::http::WinHttp::new(&ctx.version, 15_000);
        run_with(args, &ctx, &mut net, &mut System, &mut out, &mut ask)
    }
    #[cfg(not(windows))]
    {
        struct NoNet;
        impl Fetcher for NoNet {
            fn fetch_bytes(
                &mut self,
                _: &str,
                _: u64,
            ) -> Result<scol_catalogue::fetch::Step<Vec<u8>>, String> {
                Err("the network client needs Windows".into())
            }
            fn fetch_to_file(
                &mut self,
                _: &str,
                _: &std::path::Path,
                _: u64,
            ) -> Result<scol_catalogue::fetch::Step<u64>, String> {
                Err("the network client needs Windows".into())
            }
        }
        run_with(args, &ctx, &mut NoNet, &mut System, &mut out, &mut ask)
    }
}

/// The testable form of [`run`]: network, machine, output and the yes/no question are passed in.
pub fn run_with(
    args: &[String],
    ctx: &Context,
    net: &mut dyn Fetcher,
    p: &mut dyn Platform,
    out: &mut dyn Write,
    ask: &mut dyn FnMut(&str) -> bool,
) -> i32 {
    // Drop `--here <x>` and (test builds) `--current <x>` from the argument list.
    let mut rest: Vec<&str> = Vec::new();
    let mut it = args.iter();
    while let Some(a) = it.next() {
        if a == "--here" || (cfg!(feature = "test-hooks") && a == "--current") {
            it.next();
        } else {
            rest.push(a);
        }
    }
    match rest.as_slice() {
        ["--self-test", tag] => swap::self_test(tag, &ctx.here, &ctx.version, out),
        ["--elevated-update", tag, staged] => {
            let _ = std::fs::create_dir_all(ctx.here.join("data"));
            flow::elevated_update(
                &ctx.here,
                tag,
                std::path::Path::new(staged),
                &ctx.version,
                p,
                out,
            )
        }
        #[cfg(feature = "test-hooks")]
        ["--apply-zip", zip, tag, sha] => apply_zip(ctx, p, out, zip, tag, sha),
        ["update", flags @ ..] => update(ctx, flags, net, p, out, ask),
        _ => {
            let _ = writeln!(out, "{USAGE}");
            1
        }
    }
}

fn update(
    ctx: &Context,
    flags: &[&str],
    net: &mut dyn Fetcher,
    p: &mut dyn Platform,
    out: &mut dyn Write,
    ask: &mut dyn FnMut(&str) -> bool,
) -> i32 {
    let (mut check_only, mut yes, mut pre) = (false, false, ctx.prerelease);
    for f in flags {
        match *f {
            "--check" => check_only = true,
            "--yes" => yes = true,
            "--prerelease" => pre = true,
            _ => {
                let _ = writeln!(out, "{USAGE}");
                return 1;
            }
        }
    }
    let _ = std::fs::create_dir_all(ctx.here.join("data"));
    swap::settle_previous(&ctx.here, p, out);
    let u = match flow::check_latest(net, &ctx.version, pre) {
        Ok(u) => u,
        Err(why) => {
            let _ = writeln!(out, "Update:   {why}");
            return if why.starts_with("up to date") { 0 } else { 1 };
        }
    };
    let _ = writeln!(
        out,
        "Update:   sc-offline {} is available (you have {})",
        u.tag, ctx.version
    );
    if check_only {
        return 0;
    }
    if p.game_running() {
        let _ = writeln!(out, "[!] close the game before updating");
        return 1;
    }
    if !yes && !ask("Update now? Your saves and sc-offline.ini are kept.") {
        let _ = writeln!(out, "[i] not updating");
        return 0;
    }
    match flow::apply_update(&ctx.here, &u, &ctx.version, net, p, out) {
        Ok(()) => 0,
        Err(_) => {
            let _ = writeln!(out, "[!] update failed; your current version is unchanged");
            1
        }
    }
}

/// Test hook: the real stage, verify, swap and self-test path against a local zip instead of a
/// download. `SCO_TEST_FAIL_AFTER=N` fails the swap after N files.
#[cfg(feature = "test-hooks")]
fn apply_zip(
    ctx: &Context,
    p: &mut dyn Platform,
    out: &mut dyn Write,
    zip: &str,
    tag: &str,
    sha: &str,
) -> i32 {
    let _ = std::fs::create_dir_all(ctx.here.join("data"));
    swap::settle_previous(&ctx.here, p, out);
    let want = sha.to_ascii_lowercase();
    match crate::manifest::file_sha256(std::path::Path::new(zip)) {
        Some((h, _)) if h == want => {}
        _ => {
            let _ = writeln!(
                out,
                "[!] the zip doesn't match the given SHA-256; nothing changed"
            );
            return 1;
        }
    }
    let root = flow::stage_root(&ctx.here, p);
    let _ = std::fs::remove_dir_all(root.join("staged"));
    let _ = std::fs::create_dir_all(&root);
    let copy = root.join(format!("sc-offline-{tag}.zip"));
    if std::fs::copy(zip, &copy).is_err() {
        let _ = writeln!(out, "[!] couldn't copy the zip");
        return 1;
    }
    let staged = flow::stage_local_zip(&root, &copy, tag, &ctx.version, out);
    let _ = std::fs::remove_file(&copy);
    if staged.is_err() {
        return 1;
    }
    let opts = SwapOptions {
        fail_after: std::env::var("SCO_TEST_FAIL_AFTER")
            .ok()
            .and_then(|v| v.parse().ok()),
        crash_after: None,
    };
    match flow::install_staged(&ctx.here, tag, &ctx.version, p, out, opts) {
        Ok(()) => 0,
        Err(_) => 1,
    }
}
