// SPDX-License-Identifier: GPL-3.0-only
//! The update flow: check GitHub, download and stage, swap (elevated when the folder needs it).
//! Network and machine access come in as trait objects.

use std::io::Write;
use std::path::{Path, PathBuf};

use scol_catalogue::fetch::{check_redirect_url, check_url, Fetcher, Step, MAX_REDIRECTS};

use crate::manifest::file_sha256;
use crate::platform::Platform;
use crate::release::{self, UpdateInfo};
use crate::stage::{load_manifest, stage_zip};
use crate::swap::{swap_staged, update_dir, SwapOptions};

/// GET `url` into memory, following at most [`MAX_REDIRECTS`] redirects, each target checked
/// against the release host lists before it is requested.
pub fn get_bytes(net: &mut dyn Fetcher, url: &str, cap: u64) -> Result<Vec<u8>, String> {
    check_url(url, &release::HOSTS).map_err(|e| e.to_string())?;
    let mut cur = url.to_string();
    for hop in 0..=MAX_REDIRECTS {
        match net.fetch_bytes(&cur, cap)? {
            Step::Done(b) if b.len() as u64 <= cap => return Ok(b),
            Step::Done(_) => return Err("the reply is over the size cap".into()),
            Step::Redirect(next) => {
                if hop == MAX_REDIRECTS {
                    return Err("too many redirects".into());
                }
                check_redirect_url(&next, &release::HOSTS, &release::REDIRECT_HOSTS)
                    .map_err(|e| e.to_string())?;
                cur = next;
            }
        }
    }
    Err("too many redirects".into())
}

/// Same, into a file.
pub fn get_file(net: &mut dyn Fetcher, url: &str, dest: &Path, cap: u64) -> Result<u64, String> {
    check_url(url, &release::HOSTS).map_err(|e| e.to_string())?;
    let mut cur = url.to_string();
    for hop in 0..=MAX_REDIRECTS {
        match net.fetch_to_file(&cur, dest, cap)? {
            Step::Done(n) if n <= cap => return Ok(n),
            Step::Done(_) => {
                let _ = std::fs::remove_file(dest);
                return Err("the download is over the size cap".into());
            }
            Step::Redirect(next) => {
                if hop == MAX_REDIRECTS {
                    return Err("too many redirects".into());
                }
                check_redirect_url(&next, &release::HOSTS, &release::REDIRECT_HOSTS)
                    .map_err(|e| e.to_string())?;
                cur = next;
            }
        }
    }
    Err("too many redirects".into())
}

/// S5: what GitHub offers on the player's channel. Err is the reason nothing is offered
/// ("up to date (latest v1.2.3)" starts with "up to date").
pub fn check_latest(
    net: &mut dyn Fetcher,
    current: &str,
    prerelease: bool,
) -> Result<UpdateInfo, String> {
    let url = if prerelease {
        release::RELEASES_LIST
    } else {
        release::RELEASES_LATEST
    };
    let body = get_bytes(net, url, release::MAX_REPLY_BYTES)
        .map_err(|_| "couldn't reach GitHub".to_string())?;
    release::pick(&body, current, prerelease)
}

/// The automatic check before `play`: nothing at all (no request) when `check_updates = off`.
pub fn check_for_play(
    net: &mut dyn Fetcher,
    current: &str,
    prerelease: bool,
    check_updates: bool,
) -> Option<UpdateInfo> {
    if !check_updates {
        return None;
    }
    check_latest(net, current, prerelease).ok()
}

/// Where an update is staged: `data\update` when `data` is writable, else (a Program Files install)
/// `%LOCALAPPDATA%\sc-offline\update`, which the elevated part re-checks file by file.
pub fn stage_root(here: &Path, p: &mut dyn Platform) -> PathBuf {
    let data = here.join("data");
    let _ = std::fs::create_dir_all(&data);
    if p.can_write(&data) {
        return update_dir(here);
    }
    match p.local_app_data() {
        Some(l) => {
            let root = l.join("sc-offline");
            let _ = std::fs::create_dir_all(&root);
            root.join("update")
        }
        None => update_dir(here),
    }
}

/// Zip the stage step, given the zip already on disk. Cleans `new`/`staged` leftovers first and
/// `staged` again on failure.
pub fn stage_local_zip(
    root: &Path,
    zip: &Path,
    tag: &str,
    current: &str,
    out: &mut dyn Write,
) -> Result<(), String> {
    let staged = root.join("staged");
    let _ = std::fs::remove_dir_all(&staged);
    match stage_zip(zip, &staged, tag, current, out) {
        Ok(_) => Ok(()),
        Err(e) => {
            let _ = writeln!(out, "[!] {e}; nothing changed");
            let _ = std::fs::remove_dir_all(&staged);
            Err(e)
        }
    }
}

/// Download the release zip, check it against GitHub's digest and stage it. Network: normal rights
/// only.
pub fn stage_update(
    here: &Path,
    u: &UpdateInfo,
    current: &str,
    net: &mut dyn Fetcher,
    p: &mut dyn Platform,
    out: &mut dyn Write,
) -> Result<(), String> {
    let root = stage_root(here, p);
    let _ = std::fs::remove_dir_all(root.join("staged"));
    let _ = std::fs::remove_dir_all(root.join("new"));
    std::fs::create_dir_all(&root).map_err(|e| format!("couldn't create {root:?} ({e})"))?;
    if u.size > release::MAX_ZIP_BYTES {
        let _ = writeln!(out, "[!] the release is over the size cap; nothing changed");
        return Err("release over the size cap".into());
    }
    // D3: room for the zip, the unpacked copy and the staged copy, plus the files being replaced.
    let need = (if u.size > 0 { u.size } else { 64 << 20 }) * 6 + (32 << 20);
    if let Some(free) = p.free_space(&root) {
        if free < need {
            let _ = writeln!(
                out,
                "[!] not enough free disk space for the update ({} MB free, {} MB needed); nothing changed",
                free >> 20,
                need >> 20
            );
            return Err("not enough free disk space".into());
        }
    }
    let _ = writeln!(out, "[i] downloading {} ...", u.zip_name);
    let zip_path = root.join(&u.zip_name);
    let cap = if u.size > 0 {
        u.size
    } else {
        release::MAX_ZIP_BYTES
    };
    // D3: the timeout is per read (a stalled download), not for the whole file; one retry.
    let mut got = get_file(net, &u.zip_url, &zip_path, cap);
    if got.is_err() {
        let _ = writeln!(out, "[i] the download stalled or failed; trying once more");
        got = get_file(net, &u.zip_url, &zip_path, cap);
    }
    if let Err(e) = got {
        let _ = std::fs::remove_file(&zip_path);
        let _ = writeln!(out, "[!] download failed ({e}); nothing changed");
        return Err("download failed".into());
    }
    let digest = file_sha256(&zip_path).map(|(h, _)| h).unwrap_or_default();
    if digest != u.sha256 {
        let _ = std::fs::remove_file(&zip_path);
        let _ = writeln!(
            out,
            "[!] the download doesn't match GitHub's SHA-256 (got {digest}, expected {}); nothing changed",
            u.sha256
        );
        return Err("the download doesn't match GitHub's SHA-256".into());
    }
    let _ = writeln!(out, "[+] SHA-256 matches GitHub's digest");
    let r = stage_local_zip(&root, &zip_path, &u.tag, current, out);
    let _ = std::fs::remove_file(&zip_path);
    r
}

/// The swap, as administrator when this folder needs it: `--elevated-update <tag> <staged>` only
/// re-checks and swaps; it never touches the network, starts the game or relaunches.
pub fn swap_maybe_elevated(
    here: &Path,
    staged: &Path,
    tag: &str,
    current: &str,
    p: &mut dyn Platform,
    out: &mut dyn Write,
    opts: SwapOptions,
) -> Result<(), String> {
    if p.can_write(here) && p.can_write(&here.join("data")) {
        return swap_staged(here, staged, tag, current, p, out, opts).map(|_| ());
    }
    let _ = writeln!(
        out,
        "[i] this folder needs administrator rights to update; Windows will ask once.\n    (Only the file swap runs as administrator; the game never does.)"
    );
    match p.run_elevated(here, tag, staged) {
        Err(_) => {
            let _ = writeln!(out, "[!] administrator rights refused; nothing changed");
            Err("administrator rights refused".into())
        }
        Ok(0) => Ok(()),
        Ok(code) => {
            let _ = writeln!(
                out,
                "[!] the update failed (exit {code}); see data\\launcher.log. Your current version is unchanged."
            );
            Err(format!("the elevated update failed (exit {code})"))
        }
    }
}

/// Swap an already staged update in; the staging folder goes either way.
pub fn install_staged(
    here: &Path,
    tag: &str,
    current: &str,
    p: &mut dyn Platform,
    out: &mut dyn Write,
    opts: SwapOptions,
) -> Result<(), String> {
    let staged = stage_root(here, p).join("staged");
    let r = swap_maybe_elevated(here, &staged, tag, current, p, out, opts);
    let _ = std::fs::remove_dir_all(&staged);
    r
}

/// Stage then install.
pub fn apply_update(
    here: &Path,
    u: &UpdateInfo,
    current: &str,
    net: &mut dyn Fetcher,
    p: &mut dyn Platform,
    out: &mut dyn Write,
) -> Result<(), String> {
    stage_update(here, u, current, net, p, out)?;
    install_staged(here, &u.tag, current, p, out, SwapOptions::default())
}

/// The elevated half: only accepts a staging folder named `...\update\staged`.
pub fn elevated_update(
    here: &Path,
    tag: &str,
    staged: &Path,
    current: &str,
    p: &mut dyn Platform,
    out: &mut dyn Write,
) -> i32 {
    let s = staged.to_string_lossy().replace('\\', "/");
    let s = s.trim_end_matches('/');
    let tail = "/update/staged";
    let ok_tail = s.len() > tail.len() && s[s.len() - tail.len()..].eq_ignore_ascii_case(tail);
    if !ok_tail || !release::tag_ok(tag) {
        let _ = writeln!(out, "[!] refusing staging folder {}", staged.display());
        return 1;
    }
    // Re-checks everything itself, whoever staged it.
    if load_manifest(staged, tag, current).is_err() {
        let _ = writeln!(
            out,
            "[!] the staged update has no valid manifest; nothing changed"
        );
        return 1;
    }
    match swap_staged(here, staged, tag, current, p, out, SwapOptions::default()) {
        Ok(_) => 0,
        Err(_) => 1,
    }
}

// ---------------------------------------------------------------- the offer

/// What [`offer_update`] ended with.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Offer {
    /// Nothing to install. `why` is what the C++ launcher prints after `Update:   `
    /// (`up to date (latest v1.2.3)`, `couldn't reach GitHub`, ...).
    Nothing { why: String },
    /// One is on offer and this run only says so: `--check`, or nobody could be asked.
    Available(UpdateInfo),
    /// One is on offer and StarCitizen.exe is running: the game must be closed first.
    Blocked(UpdateInfo),
    /// The player said no.
    Declined(UpdateInfo),
    /// Swapped in; the files on disk are the new version.
    Applied(UpdateInfo),
    /// Stage or swap failed; the current version is unchanged.
    Failed(UpdateInfo),
}

/// How an offer behaves.
#[derive(Debug, Clone, Copy, Default)]
pub struct OfferOptions {
    /// `update --check`: report, never ask.
    pub check_only: bool,
    /// `update --yes`: the question was already answered (the window's button).
    pub yes: bool,
    /// The automatic check before `play`: say nothing when there is nothing to install.
    pub quiet: bool,
}

/// The C++ launcher's update section (`wmain`, "0. Updates"): check the player's channel, say what is on
/// offer, refuse while the game runs, ask (`ask` is the console prompt or the window's callback; the
/// platform says whether anybody can answer), download, check, stage and swap, with the text the C++
/// launcher prints. The caller decides what follows an [`Offer::Applied`].
#[allow(clippy::too_many_arguments)]
pub fn offer_update(
    current: &str,
    prerelease: bool,
    opts: OfferOptions,
    here: &Path,
    net: &mut dyn Fetcher,
    p: &mut dyn Platform,
    out: &mut dyn Write,
    ask: &mut dyn FnMut(&str) -> bool,
) -> Offer {
    let u = match check_latest(net, current, prerelease) {
        Ok(u) => u,
        Err(why) => {
            if !opts.quiet {
                let _ = writeln!(out, "Update:   {why}");
            }
            return Offer::Nothing { why };
        }
    };
    let _ = writeln!(
        out,
        "Update:   sc-offline {} is available (you have {current})",
        u.tag
    );
    if opts.check_only {
        return Offer::Available(u);
    }
    if p.game_running() {
        let _ = writeln!(out, "[!] close the game before updating");
        return Offer::Blocked(u);
    }
    if !opts.yes {
        if !p.can_ask() {
            let _ = writeln!(out, "          (not asking: no console to answer in)");
            return Offer::Available(u);
        }
        if !ask("Update now? Your saves and sc-offline.ini are kept.") {
            let _ = writeln!(out, "[i] not updating");
            return Offer::Declined(u);
        }
    }
    match apply_update(here, &u, current, net, p, out) {
        Ok(()) => Offer::Applied(u),
        Err(_) => {
            let _ = writeln!(out, "[!] update failed; your current version is unchanged");
            Offer::Failed(u)
        }
    }
}

/// The automatic check of `play` (`check_updates = on`): [`offer_update`] with the `play` command line
/// to carry on with. `Some(code)` means `play` must end now with that code: an update was swapped in
/// and the new exe took over (or, with no way to start it, the player is told to start again).
#[allow(clippy::too_many_arguments)]
pub fn check_and_offer_for_play(
    current: &str,
    prerelease: bool,
    here: &Path,
    args: &[String],
    net: &mut dyn Fetcher,
    p: &mut dyn Platform,
    out: &mut dyn Write,
    ask: &mut dyn FnMut(&str) -> bool,
) -> Option<i32> {
    let opts = OfferOptions {
        quiet: true,
        ..OfferOptions::default()
    };
    match offer_update(current, prerelease, opts, here, net, p, out, ask) {
        Offer::Applied(_) => Some(finish_applied(here, args, p, out)),
        _ => None,
    }
}

/// What follows an applied update: the platform's restart (a console run starts the new exe with the
/// same arguments; a window child answers with the restart code), else a note to start again.
/// Returns the exit code.
pub fn finish_applied(
    here: &Path,
    args: &[String],
    p: &mut dyn Platform,
    out: &mut dyn Write,
) -> i32 {
    match p.after_update(here, args, out) {
        Some(code) => code,
        None => {
            let _ = writeln!(
                out,
                "[i] updated; start sc-offline.exe again to use the new version"
            );
            0
        }
    }
}
