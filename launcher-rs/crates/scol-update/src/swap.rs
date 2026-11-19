// SPDX-License-Identifier: GPL-3.0-only
//! The journaled swap, its rollback and the start-up settle step. Journal format is the C++
//! launcher's `data\update\applied.txt`: one line per step, written (and flushed) BEFORE the step:
//! `R <rel>` the old file is about to move to `<rel>.update-old`, `N <rel>` a new file is about to be
//! created where none was, `done` last. Lines are read with either slash, so each launcher can
//! settle the other's crash.

use std::ffi::OsString;
use std::fs::{File, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};

use scol_plugins::sha256::{to_hex, Sha256};

use crate::manifest::{self, file_sha256};
use crate::platform::Platform;
use crate::stage::load_manifest;

/// Paths (relative, `/`-separated) an update never replaces. `data/update/` is the update's own.
const KEEP: [&str; 11] = [
    "sc-offline.ini",
    "data/wallet.txt",
    "data/spawn.txt",
    "data/bookmarks.txt",
    "data/locations_found.txt",
    "data/buildables.txt",
    "data/game-path.txt",
    "data/game-build.txt",
    "data/mod.log",
    "data/launcher.log",
    "data/registry-dump.txt",
];

/// A file the player owns: never replaced by an update.
pub fn player_owned(rel: &str) -> bool {
    let r = rel.replace('\\', "/");
    KEEP.iter().any(|k| k.eq_ignore_ascii_case(&r))
        || r.get(..12)
            .is_some_and(|p| p.eq_ignore_ascii_case("data/update/"))
}

pub fn update_dir(here: &Path) -> PathBuf {
    here.join("data").join("update")
}

pub fn journal_path(here: &Path) -> PathBuf {
    update_dir(here).join("applied.txt")
}

fn with_suffix(p: &Path, suffix: &str) -> PathBuf {
    let mut s: OsString = p.as_os_str().to_owned();
    s.push(suffix);
    PathBuf::from(s)
}

fn journal_line(here: &Path, line: &str) -> bool {
    let Ok(mut f) = OpenOptions::new()
        .create(true)
        .append(true)
        .open(journal_path(here))
    else {
        return false;
    };
    f.write_all(format!("{line}\n").as_bytes()).is_ok() && f.sync_all().is_ok()
}

/// D2: antivirus and indexers hold files for a moment. Retry sharing, access-denied and lock errors
/// with growing waits.
pub const LOCK_WAITS: [u64; 6] = [100, 250, 500, 1000, 2000, 4000];

fn transient(e: &std::io::Error) -> bool {
    matches!(e.raw_os_error(), Some(5) | Some(32) | Some(33))
}

/// Moves `from` to `to`, replacing `to`, and asks Windows to have it on disk before returning
/// (`MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH`, as the C++ launcher's `MoveRetrying`): a power cut
/// right after the swap must not leave the journal ahead of the files. Elsewhere a plain rename.
pub fn move_through(from: &Path, to: &Path) -> std::io::Result<()> {
    #[cfg(windows)]
    {
        use std::os::windows::ffi::OsStrExt;
        use windows_sys::Win32::Storage::FileSystem::{
            MoveFileExW, MOVEFILE_REPLACE_EXISTING, MOVEFILE_WRITE_THROUGH,
        };
        let wide = |p: &Path| -> Vec<u16> { p.as_os_str().encode_wide().chain(Some(0)).collect() };
        let (f, t) = (wide(from), wide(to));
        // SAFETY: both paths are NUL-terminated and outlive the call.
        let ok = unsafe {
            MoveFileExW(
                f.as_ptr(),
                t.as_ptr(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH,
            )
        };
        if ok == 0 {
            return Err(std::io::Error::last_os_error());
        }
        Ok(())
    }
    #[cfg(not(windows))]
    {
        std::fs::rename(from, to)
    }
}

/// Runs `op`, and while it fails with a sharing, access-denied or lock error waits (through
/// [`Platform::sleep_ms`]) [`LOCK_WAITS`] in turn and tries again; any other error, or the last wait
/// used up, is the result.
pub fn retrying<T>(
    p: &mut dyn Platform,
    mut op: impl FnMut() -> std::io::Result<T>,
) -> std::io::Result<T> {
    let mut i = 0;
    loop {
        match op() {
            Ok(v) => return Ok(v),
            Err(e) if transient(&e) && i < LOCK_WAITS.len() => {
                p.sleep_ms(LOCK_WAITS[i]);
                i += 1;
            }
            Err(e) => return Err(e),
        }
    }
}

/// D1: copy `src` to `dst` and flush `dst` to disk before returning.
fn copy_flushed(p: &mut dyn Platform, src: &Path, dst: &Path) -> std::io::Result<()> {
    let bytes = std::fs::read(src)?;
    let mut f = retrying(p, || File::create(dst))?;
    f.write_all(&bytes)?;
    f.sync_all()
}

fn read_journal(here: &Path) -> Option<Vec<u8>> {
    std::fs::read(journal_path(here)).ok()
}

fn journal_lines(j: &[u8]) -> Vec<String> {
    String::from_utf8_lossy(j)
        .lines()
        .map(|l| l.trim_end_matches('\r').to_string())
        .collect()
}

/// Undo an unfinished swap from its journal, newest step first.
pub fn roll_back(here: &Path, p: &mut dyn Platform) {
    let Some(j) = read_journal(here) else { return };
    for line in journal_lines(&j).iter().rev() {
        if line.len() < 3 || !line.is_char_boundary(2) {
            continue;
        }
        let kind = line.as_bytes()[0];
        let rel = &line[2..];
        if manifest::safe_rel_path(&rel.replace('\\', "/")).is_none() {
            continue;
        }
        let path = manifest::join(here, rel);
        let _ = std::fs::remove_file(with_suffix(&path, ".update-new"));
        let old = with_suffix(&path, ".update-old");
        if kind == b'N' {
            let _ = std::fs::remove_file(&path);
        } else if kind == b'R' && old.is_file() {
            // A running new exe can't be deleted, but it can be renamed out of the way.
            if path.is_file() && std::fs::remove_file(&path).is_err() {
                let failed = with_suffix(&path, ".update-failed");
                let _ = retrying(p, || move_through(&path, &failed));
            }
            let _ = retrying(p, || move_through(&old, &path));
        }
    }
}

/// Run at every start: finish (clean up) or roll back whatever a previous update left.
/// Returns true when it had to roll back.
pub fn settle_previous(here: &Path, p: &mut dyn Platform, out: &mut dyn Write) -> bool {
    let Some(j) = read_journal(here) else {
        return false;
    };
    let done = j.ends_with(b"done\n");
    if !done {
        let _ = writeln!(
            out,
            "[!] a previous update didn't finish; putting the old files back"
        );
        roll_back(here, p);
    }
    let mut leftover = false;
    for line in journal_lines(&j) {
        if line.len() > 2 && line.as_bytes()[0] == b'R' && line.is_char_boundary(2) {
            let rel = &line[2..];
            if manifest::safe_rel_path(&rel.replace('\\', "/")).is_none() {
                continue;
            }
            let old = with_suffix(&manifest::join(here, rel), ".update-old");
            if old.is_file() && std::fs::remove_file(&old).is_err() {
                leftover = true; // the old exe can still be locked for a moment
            }
        }
    }
    if !leftover {
        let _ = std::fs::remove_dir_all(update_dir(here));
    }
    !done
}

/// Keys the new `sc-offline.ini` has that the player's doesn't: appended commented out, with their
/// defaults (the comment line right above a key travels with it).
pub fn merge_ini(here: &Path, new_ini: &Path, tag: &str, out: &mut dyn Write) {
    let (Ok(mine), Ok(theirs)) = (
        std::fs::read_to_string(here.join("sc-offline.ini")),
        std::fs::read_to_string(new_ini),
    ) else {
        return;
    };
    fn keys(t: &str) -> Vec<(String, String)> {
        let mut out = Vec::new();
        let mut note = String::new();
        for raw in t.lines() {
            let l = raw.trim_end_matches([' ', '\r']);
            let Some(b) = l.find(|c: char| !matches!(c, ' ' | '\t' | '#')) else {
                note.clear();
                continue;
            };
            let eq = l.find('=');
            let k = eq.map(|e| l[b..e.max(b)].trim_end()).unwrap_or("");
            if eq.is_none_or(|e| e < b) || k.is_empty() || k.contains(' ') {
                note = if l.starts_with('#') && eq.is_none() {
                    format!("{l}\r\n")
                } else {
                    String::new()
                };
                continue;
            }
            out.push((k.to_string(), format!("{note}# {}", &l[b..])));
            note.clear();
        }
        out
    }
    let mine_keys = keys(&mine);
    let mut add = String::new();
    for (k, text) in keys(&theirs) {
        if !mine_keys.iter().any(|(m, _)| m.eq_ignore_ascii_case(&k)) {
            add.push_str(&text);
            add.push_str("\r\n");
            let _ = writeln!(
                out,
                "[i] sc-offline.ini: new setting {k} (added commented out; the default applies)"
            );
        }
    }
    if add.is_empty() {
        return;
    }
    let mut text = mine;
    if !text.is_empty() && !text.ends_with('\n') {
        text.push_str("\r\n");
    }
    text.push_str(&format!("\r\n# New in {tag}:\r\n{add}"));
    let _ = std::fs::write(here.join("sc-offline.ini"), text);
}

/// Test and fault-injection knobs; `Default` is the production behaviour.
#[derive(Debug, Clone, Copy, Default)]
pub struct SwapOptions {
    /// Fail the swap after this many files, as a full disk would (rolls back). C++ `SCO_TEST_FAIL_AFTER`.
    pub fail_after: Option<usize>,
    /// Stop dead after this many files, with the next file's old copy moved aside and its new one
    /// not yet in place, as a crash or power cut would. Nothing is rolled back; the next start's
    /// [`settle_previous`] must repair it.
    pub crash_after: Option<usize>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SwapReport {
    pub replaced: usize,
    pub added: usize,
}

/// Swap the files under `staged` into `here`. `tag` is the release tag, `current` the running
/// version. Err is the reason; the old files are back in place (except after `crash_after`).
pub fn swap_staged(
    here: &Path,
    staged: &Path,
    tag: &str,
    current: &str,
    p: &mut dyn Platform,
    out: &mut dyn Write,
    opts: SwapOptions,
) -> Result<SwapReport, String> {
    // D4: never swap files under a running game.
    if p.game_running() {
        let _ = writeln!(
            out,
            "[!] StarCitizen.exe is running; close it before updating. Nothing changed"
        );
        return Err("the game is running".into());
    }
    let man = load_manifest(staged, tag, current)
        .and_then(|m| manifest::verify_against(staged, &m).map(|_| m))
        .map_err(|why| {
            let _ = writeln!(
                out,
                "[!] the staged update doesn't match its manifest ({why}); nothing changed"
            );
            why
        })?;
    let _ = std::fs::create_dir_all(update_dir(here));
    let _ = std::fs::remove_file(journal_path(here));
    let (mut replaced, mut added, mut done) = (0usize, 0usize, 0usize);
    let mut failure: Option<String> = None;
    for mf in &man.files {
        if player_owned(&mf.path) {
            continue;
        }
        if opts.fail_after.is_some_and(|n| done >= n) {
            let _ = writeln!(out, "[!] fail-after {}: failing the swap here", done);
            failure = Some("swap failed (injected)".into());
            break;
        }
        let dst = manifest::join(here, &mf.path);
        let src = manifest::join(staged, &mf.path);
        let fresh = with_suffix(&dst, ".update-new");
        let old = with_suffix(&dst, ".update-old");
        if let Some(parent) = dst.parent() {
            let _ = std::fs::create_dir_all(parent);
        }
        // D1: write the new file beside its target, flushed, and check it before anything moves.
        let wrote = copy_flushed(p, &src, &fresh).is_ok()
            && file_sha256(&fresh).is_some_and(|(h, _)| h == mf.sha256);
        if !wrote {
            let _ = writeln!(out, "[!] couldn't write {}", mf.path);
            let _ = std::fs::remove_file(&fresh);
            failure = Some(format!("couldn't write {}", mf.path));
            break;
        }
        let had = dst.is_file();
        let jrel = mf.path.replace('/', "\\");
        if !journal_line(here, &format!("{} {jrel}", if had { 'R' } else { 'N' })) {
            let _ = std::fs::remove_file(&fresh);
            failure = Some("couldn't write the journal".into());
            break;
        }
        if had && retrying(p, || move_through(&dst, &old)).is_err() {
            let _ = writeln!(out, "[!] couldn't move {} aside", mf.path);
            let _ = std::fs::remove_file(&fresh);
            failure = Some(format!("couldn't move {} aside", mf.path));
            break;
        }
        if opts.crash_after.is_some_and(|n| done >= n) {
            return Err("simulated crash".into());
        }
        let placed = retrying(p, || move_through(&fresh, &dst)).is_ok()
            && file_sha256(&dst).is_some_and(|(h, _)| h == mf.sha256);
        if !placed {
            let _ = writeln!(out, "[!] couldn't put {} in place", mf.path);
            let _ = std::fs::remove_file(&fresh);
            failure = Some(format!("couldn't put {} in place", mf.path));
            break;
        }
        if had {
            replaced += 1;
        } else {
            added += 1;
        }
        done += 1;
    }
    if let Some(why) = failure {
        let _ = writeln!(out, "[!] update failed; putting the old files back");
        roll_back(here, p);
        let _ = std::fs::remove_file(journal_path(here));
        return Err(why);
    }
    // D5: the new launcher must start. If its --self-test fails, the old files go back; the
    // .update-old copies stay until the next good start (settle_previous) either way.
    if let Err(why) = p.run_self_test(here, tag) {
        let _ = writeln!(out, "[!] {why}");
        let _ = writeln!(
            out,
            "[!] the new sc-offline.exe failed its self-test; putting the old files back"
        );
        roll_back(here, p);
        let _ = std::fs::remove_file(journal_path(here));
        return Err(why);
    }
    let _ = writeln!(out, "[+] the new sc-offline.exe passed its self-test");
    if staged.join("sc-offline.ini").is_file() {
        merge_ini(here, &staged.join("sc-offline.ini"), tag, out);
    }
    journal_line(here, "done");
    let _ = writeln!(
        out,
        "[+] updated to {tag}: {replaced} files replaced, {added} added; your saves and sc-offline.ini were kept"
    );
    Ok(SwapReport { replaced, added })
}

/// `--self-test <tag>`: what an update runs on the new launcher before keeping it. The exe is the
/// version the manifest promised, SHA-256 works and `dinput8.dll` is beside it. Writes nothing,
/// reads no network. Exit codes as the C++ launcher: 0 ok, 2 wrong version, 3 SHA-256 broken, 4 no
/// `dinput8.dll`.
pub fn self_test(tag: &str, here: &Path, version: &str, out: &mut dyn Write) -> i32 {
    if tag != format!("v{version}") {
        let _ = writeln!(out, "self-test: this is {version}, expected {tag}");
        return 2;
    }
    let mut h = Sha256::new();
    h.update(b"abc");
    if to_hex(&h.finish()) != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" {
        let _ = writeln!(out, "self-test: SHA-256 broken");
        return 3;
    }
    if !here.join("dinput8.dll").is_file() {
        let _ = writeln!(out, "self-test: no dinput8.dll beside sc-offline.exe");
        return 4;
    }
    let _ = writeln!(out, "self-test: sc-offline {version} ok");
    0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn keep_list() {
        assert!(player_owned("sc-offline.ini"));
        assert!(player_owned("SC-Offline.INI"));
        assert!(player_owned("data/wallet.txt"));
        assert!(player_owned("data\\wallet.txt"));
        assert!(player_owned("data/update/staged/x"));
        assert!(!player_owned("sc-offline.exe"));
        assert!(!player_owned("data/plugins/a/plugin.dll"));
    }
}
