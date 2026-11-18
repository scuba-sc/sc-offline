// SPDX-License-Identifier: GPL-3.0-only
//! Reading and writing the small state files, and the "is the game running" guard.

use std::collections::BTreeSet;
use std::path::Path;

use scol_catalogue::index::LastAccepted;
use scol_catalogue::install::InstalledFile;
use serde_json::{json, Value};

use crate::layout::Layout;
use crate::txn::write_atomic;

/// `data/launcher/installed.json`; a missing file is an empty record. A file that does not parse
/// is an error: it is never overwritten behind the player's back.
pub fn read_installed(l: &Layout) -> Result<InstalledFile, String> {
    match std::fs::read(l.installed_json()) {
        Ok(b) => InstalledFile::parse(&b).map_err(|e| format!("installed.json: {e}")),
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(InstalledFile::default()),
        Err(e) => Err(format!("cannot read installed.json: {e}")),
    }
}

/// The last accepted index (sequence and digest), or `None` before the first accepted index.
pub fn read_catalogue_state(l: &Layout) -> Result<Option<LastAccepted>, String> {
    let text = match std::fs::read_to_string(l.catalogue_state()) {
        Ok(t) => t,
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Ok(None),
        Err(e) => return Err(format!("cannot read catalogue-state.json: {e}")),
    };
    let bad =
        |m: &str| format!("catalogue-state.json: {m}; not touching it, fix or remove the file");
    let v: Value = serde_json::from_str(&text).map_err(|e| bad(&e.to_string()))?;
    if v["schema"] != 1 {
        return Err(bad("unsupported schema"));
    }
    let sequence = v["sequence"].as_u64().ok_or_else(|| bad("no sequence"))?;
    let digest = v["digest"].as_str().ok_or_else(|| bad("no digest"))?;
    if digest.len() != 64
        || !digest
            .bytes()
            .all(|c| c.is_ascii_digit() || (b'a'..=b'f').contains(&c))
    {
        return Err(bad("digest is not 64 hex digits"));
    }
    Ok(Some(LastAccepted {
        sequence,
        digest: digest.to_string(),
    }))
}

/// Remember an accepted index. The sequence only ever goes up: a lower one is never written.
pub fn write_catalogue_state(l: &Layout, acc: &LastAccepted) -> Result<(), String> {
    if let Some(old) = read_catalogue_state(l)? {
        if old.sequence > acc.sequence || old == *acc {
            return Ok(());
        }
    }
    std::fs::create_dir_all(l.launcher())
        .map_err(|e| format!("cannot create data/launcher: {e}"))?;
    let mut t = serde_json::to_string_pretty(
        &json!({"schema": 1, "sequence": acc.sequence, "digest": acc.digest}),
    )
    .expect("plain data");
    t.push('\n');
    write_atomic(&l.catalogue_state(), &t)
}

/// Names of the folders under `data/plugins/`.
pub fn plugin_folders(l: &Layout) -> BTreeSet<String> {
    let mut out = BTreeSet::new();
    if let Ok(rd) = std::fs::read_dir(l.plugins()) {
        for e in rd.flatten() {
            if let Some(n) = e.file_name().to_str() {
                out.insert(n.to_string());
            }
        }
    }
    out
}

/// True for a symbolic link or junction.
pub fn is_link(p: &Path) -> bool {
    std::fs::symlink_metadata(p).is_ok_and(|m| m.file_type().is_symlink())
}

/// True when a `StarCitizen.exe` process exists, or the process list cannot be read (the safe
/// answer). Reads the process list only; never opens or touches the process.
#[cfg(windows)]
pub fn game_running() -> bool {
    use windows_sys::Win32::Foundation::{CloseHandle, INVALID_HANDLE_VALUE};
    use windows_sys::Win32::System::Diagnostics::ToolHelp::{
        CreateToolhelp32Snapshot, Process32FirstW, Process32NextW, PROCESSENTRY32W,
        TH32CS_SNAPPROCESS,
    };
    // SAFETY: plain snapshot request.
    let snap = unsafe { CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
    if snap == INVALID_HANDLE_VALUE {
        return true;
    }
    // SAFETY: zeroed plain-data struct whose size field is set before use.
    let mut pe: PROCESSENTRY32W = unsafe { std::mem::zeroed() };
    pe.dwSize = std::mem::size_of::<PROCESSENTRY32W>() as u32;
    let mut hit = false;
    // SAFETY: snap is a valid snapshot handle and pe is initialised.
    let mut ok = unsafe { Process32FirstW(snap, &mut pe) } != 0;
    while ok {
        let len = pe
            .szExeFile
            .iter()
            .position(|&c| c == 0)
            .unwrap_or(pe.szExeFile.len());
        let name = String::from_utf16_lossy(&pe.szExeFile[..len]);
        if name.eq_ignore_ascii_case("StarCitizen.exe") {
            hit = true;
            break;
        }
        // SAFETY: as above.
        ok = unsafe { Process32NextW(snap, &mut pe) } != 0;
    }
    // SAFETY: snap is a handle we own.
    unsafe { CloseHandle(snap) };
    hit
}

#[cfg(not(windows))]
pub fn game_running() -> bool {
    false
}

/// Removes the folders a refused or finished command created and left empty (`staging`,
/// `data/launcher`, `data/plugins`, `data`), so a refusal leaves the tree as it was. Only empty
/// folders go (`remove_dir` fails on anything else), and only those that did not exist before.
pub struct Tidy {
    paths: Vec<std::path::PathBuf>,
}

impl Tidy {
    pub fn new(l: &Layout) -> Tidy {
        let paths = [
            l.launcher().join("staging"),
            l.launcher(),
            l.plugins(),
            l.data(),
        ]
        .into_iter()
        .filter(|p| std::fs::symlink_metadata(p).is_err())
        .collect();
        Tidy { paths }
    }
}

impl Drop for Tidy {
    fn drop(&mut self) {
        for p in &self.paths {
            let _ = std::fs::remove_dir(p);
        }
    }
}
