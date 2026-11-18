// SPDX-License-Identifier: GPL-3.0-only
//! Writes the files of a plugin zip that `scol-plugins` has already verified.
//!
//! The verifier checks the structure and hashes every member; this reads the central directory again
//! to find each member's bytes, inflates it with a size cap, checks the SHA-256 against the report,
//! and writes it under the destination folder. Only members the report lists are written, so a zip
//! that changed between verify and extract fails the hash check instead of landing.

use std::collections::HashMap;
use std::io::Read;
use std::path::{Path, PathBuf};

use scol_plugins::sha256;
use scol_plugins::verify::Report;

fn le16(b: &[u8], at: usize) -> Option<usize> {
    Some(u16::from_le_bytes(b.get(at..at + 2)?.try_into().ok()?) as usize)
}

fn le32(b: &[u8], at: usize) -> Option<u64> {
    Some(u32::from_le_bytes(b.get(at..at + 4)?.try_into().ok()?) as u64)
}

/// Offset and entry count of the central directory (no zip64: the verifier refuses it).
fn end_record(z: &[u8]) -> Option<(usize, usize)> {
    let lowest = z.len().saturating_sub(22 + 65535);
    let mut at = z.len().checked_sub(22)?;
    loop {
        if z.get(at..at + 4)? == [0x50, 0x4b, 0x05, 0x06] {
            return Some((le32(z, at + 16)? as usize, le16(z, at + 10)?));
        }
        if at == lowest {
            return None;
        }
        at -= 1;
    }
}

/// A destination path from a verified member path: `rel` is the part after the plugin folder.
fn target(root: &Path, rel: &str) -> Result<PathBuf, String> {
    let mut p = root.to_path_buf();
    for seg in rel.split('/') {
        if seg.is_empty() || seg == "." || seg == ".." || seg.contains(['\\', ':']) {
            return Err(format!("unsafe path in the zip: {rel:?}"));
        }
        p.push(seg);
    }
    Ok(p)
}

/// Extract every file the report lists into `dest` (which must not exist yet).
pub fn extract_zip(zip: &Path, report: &Report, dest: &Path) -> Result<(), String> {
    let z = std::fs::read(zip).map_err(|e| format!("cannot read {}: {e}", zip.display()))?;
    let (cd, count) = end_record(&z).ok_or("zip end record not found")?;
    let prefix = format!("{}/", report.plugin.folder);
    let want: HashMap<&str, &scol_plugins::verify::Artifact> = report
        .artifacts
        .iter()
        .map(|a| (a.path.as_str(), a))
        .collect();
    std::fs::create_dir_all(dest).map_err(|e| format!("cannot create {}: {e}", dest.display()))?;
    let mut done = 0usize;
    let mut at = cd;
    for _ in 0..count {
        let bad = || "zip central directory is not readable".to_string();
        if z.get(at..at + 4).ok_or_else(bad)? != [0x50, 0x4b, 0x01, 0x02] {
            return Err(bad());
        }
        let method = le16(&z, at + 10).ok_or_else(bad)?;
        let csize = le32(&z, at + 20).ok_or_else(bad)? as usize;
        let usize_ = le32(&z, at + 24).ok_or_else(bad)? as usize;
        let (n, x, c) = (
            le16(&z, at + 28).ok_or_else(bad)?,
            le16(&z, at + 30).ok_or_else(bad)?,
            le16(&z, at + 32).ok_or_else(bad)?,
        );
        let local = le32(&z, at + 42).ok_or_else(bad)? as usize;
        let name =
            String::from_utf8_lossy(z.get(at + 46..at + 46 + n).ok_or_else(bad)?).into_owned();
        at += 46 + n + x + c;
        let Some(art) = want.get(name.as_str()) else {
            continue;
        };
        if z.get(local..local + 4).ok_or_else(bad)? != [0x50, 0x4b, 0x03, 0x04] {
            return Err(bad());
        }
        let data = local
            + 30
            + le16(&z, local + 26).ok_or_else(bad)?
            + le16(&z, local + 28).ok_or_else(bad)?;
        let raw = z.get(data..data + csize).ok_or_else(bad)?;
        let mut bytes = Vec::with_capacity(usize_.min(1 << 26));
        match method {
            0 => bytes.extend_from_slice(raw),
            8 => {
                flate2::read::DeflateDecoder::new(raw)
                    .take(usize_ as u64 + 1)
                    .read_to_end(&mut bytes)
                    .map_err(|e| format!("{name}: cannot inflate: {e}"))?;
            }
            m => return Err(format!("{name}: compression method {m} is not supported")),
        }
        if bytes.len() != usize_ || bytes.len() as u64 != art.size_bytes {
            return Err(format!("{name}: size differs from the verified size"));
        }
        if sha256::hex_digest(&bytes) != art.sha256 {
            return Err(format!("{name}: SHA-256 differs from the verified hash"));
        }
        let rel = name.strip_prefix(&prefix).unwrap_or(&name);
        let out = target(dest, rel)?;
        if let Some(dir) = out.parent() {
            std::fs::create_dir_all(dir)
                .map_err(|e| format!("cannot create {}: {e}", dir.display()))?;
        }
        std::fs::write(&out, &bytes).map_err(|e| format!("cannot write {}: {e}", out.display()))?;
        done += 1;
    }
    if done != report.artifacts.len() {
        return Err(format!(
            "wrote {done} of {} verified files; the zip changed after verification",
            report.artifacts.len()
        ));
    }
    Ok(())
}
