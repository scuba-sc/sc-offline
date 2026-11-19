// SPDX-License-Identifier: GPL-3.0-only
//! Staging: turn a downloaded, digest-checked release zip into a folder holding exactly the files
//! its manifest lists. Same checks as the C++ `StageZip`, but the zip is read in process (no
//! `tar.exe`), the manifest is read and checked before any other entry is written, and every file
//! is written with a size cap taken from the manifest.

use std::fs::File;
use std::io::Write;
use std::path::Path;

use crate::manifest::{self, Manifest};
use crate::zipx::Archive;

/// Read and check a staged or unpacked `manifest.json` in `dir`: parses, belongs to `tag`, is newer
/// than `current` (S2).
pub fn load_manifest(dir: &Path, tag: &str, current: &str) -> Result<Manifest, String> {
    let p = dir.join("manifest.json");
    let md = std::fs::metadata(&p).map_err(|_| "no manifest.json".to_string())?;
    if md.len() > manifest::MAX_MANIFEST_BYTES {
        return Err("manifest.json is over the size cap".into());
    }
    let bytes = std::fs::read(&p).map_err(|_| "no manifest.json".to_string())?;
    let m = manifest::parse(&bytes)?;
    manifest::matches_release(&m, tag, current)?;
    Ok(m)
}

fn write_entry(zip: &mut Archive, idx: usize, dest: &Path, max: u64) -> Result<(), String> {
    if let Some(parent) = dest.parent() {
        std::fs::create_dir_all(parent)
            .map_err(|e| format!("couldn't stage {} ({e})", zip.entries[idx].name))?;
    }
    let mut f = File::create(dest)
        .map_err(|e| format!("couldn't stage {} ({e})", zip.entries[idx].name))?;
    zip.extract(idx, &mut f, max)?;
    f.flush().map_err(|e| e.to_string())?;
    f.sync_all().map_err(|e| e.to_string())
}

/// Stage `zip_path` into `staged` (created fresh by the caller). `tag` is the release tag
/// (`v0.7.1`), `current` the running launcher's version. On error the caller removes `staged`.
/// The release zip holds one folder `sc-offline-<tag>/` with every file and `manifest.json`.
pub fn stage_zip(
    zip_path: &Path,
    staged: &Path,
    tag: &str,
    current: &str,
    out: &mut dyn Write,
) -> Result<Manifest, String> {
    let mut zip = Archive::open(zip_path)?;
    let prefix = format!("sc-offline-{tag}/");
    // 1. The manifest first, checked before anything else is written.
    let mi = zip
        .find(&format!("{prefix}manifest.json"))
        .ok_or("the release's manifest check failed (no manifest.json)")?;
    if zip.entries[mi].size > manifest::MAX_MANIFEST_BYTES {
        return Err(
            "the release's manifest check failed (manifest.json is over the size cap)".into(),
        );
    }
    let mut mbytes = Vec::new();
    zip.extract(mi, &mut mbytes, manifest::MAX_MANIFEST_BYTES)?;
    let man = manifest::parse(&mbytes)
        .and_then(|m| manifest::matches_release(&m, tag, current).map(|_| m))
        .map_err(|why| format!("the release's manifest check failed ({why})"))?;
    // 2. Every file in the zip is the manifest or listed (S3); sizes agree with the manifest.
    for e in &zip.entries {
        if e.is_dir() {
            continue;
        }
        let sub = e
            .name
            .get(..prefix.len())
            .filter(|p| p.eq_ignore_ascii_case(&prefix))
            .map(|_| &e.name[prefix.len()..]);
        let listed = match sub {
            Some(s) if s.eq_ignore_ascii_case("manifest.json") => true,
            Some(s) => match man.find(s) {
                Some(f) if f.size == e.size => true,
                Some(f) => {
                    return Err(format!(
                    "the release's manifest check failed ({} is {} bytes, the manifest says {})",
                    f.path, e.size, f.size
                ))
                }
                None => false,
            },
            None => false,
        };
        if !listed {
            return Err(format!(
                "the zip holds {}, which its manifest doesn't list; nothing changed",
                e.name
            ));
        }
    }
    // 3. Extract exactly the listed files, then re-check them against the manifest.
    std::fs::create_dir_all(staged).map_err(|e| format!("couldn't stage ({e})"))?;
    for f in &man.files {
        let idx = zip.find(&format!("{prefix}{}", f.path)).ok_or_else(|| {
            format!(
                "the release's manifest check failed ({} is missing)",
                f.path
            )
        })?;
        write_entry(&mut zip, idx, &manifest::join(staged, &f.path), f.size)?;
    }
    std::fs::write(staged.join("manifest.json"), &mbytes)
        .map_err(|e| format!("couldn't stage manifest.json ({e})"))?;
    manifest::verify_against(staged, &man).map_err(|why| format!("staging failed ({why})"))?;
    // Both game DLLs travel together: dinput8.dll (the classic menu) and dinput8-rust-menu.dll (the
    // Rust menu hook). A release without either is not a release this launcher can install.
    if man.find("sc-offline.exe").is_none()
        || man.find("dinput8.dll").is_none()
        || man.find("dinput8-rust-menu.dll").is_none()
    {
        return Err(
            "the release doesn't hold sc-offline.exe, dinput8.dll and dinput8-rust-menu.dll; nothing changed"
                .into(),
        );
    }
    let _ = writeln!(
        out,
        "[+] all {} files match the release manifest ({}); staged",
        man.files.len(),
        man.tag
    );
    Ok(man)
}
