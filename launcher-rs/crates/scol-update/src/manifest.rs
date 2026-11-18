// SPDX-License-Identifier: GPL-3.0-only
//! Release manifest, format 1: `manifest.json` in the release folder pins every shipped file by
//! path, size and SHA-256. Same rules as the C++ launcher (S1 to S3), a little stricter on Windows
//! device names.

use std::io::Read;
use std::path::{Path, PathBuf};

use scol_plugins::sha256::{to_hex, Sha256};

use crate::version;

/// The manifest is a small text file.
pub const MAX_MANIFEST_BYTES: u64 = 4 << 20;
/// One file of a release.
pub const MAX_FILE_BYTES: u64 = 512 << 20;
/// All files of a release together.
pub const MAX_TOTAL_BYTES: u64 = 1 << 30;
/// Most files a manifest may list.
pub const MAX_FILES: usize = 5000;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ManifestFile {
    /// Relative, `/`-separated, as written.
    pub path: String,
    pub sha256: String,
    pub size: u64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Manifest {
    pub version: String,
    pub tag: String,
    pub commit: String,
    pub files: Vec<ManifestFile>,
}

impl Manifest {
    pub fn find(&self, rel: &str) -> Option<&ManifestFile> {
        self.files.iter().find(|f| f.path.eq_ignore_ascii_case(rel))
    }
}

const DEVICE_NAMES: [&str; 22] = [
    "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8",
    "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9",
];

/// S3: a relative, `/`-separated path with no `..`, `.`, empty part, `:` (drive letters, alternate
/// data streams), `\`, control or wildcard character, and no part ending in `.` or a space.
/// Returns the path unchanged when it is fine.
pub fn safe_rel_path(p: &str) -> Option<&str> {
    if p.is_empty() || p.len() > 400 || p.starts_with('/') || p.contains(['\\', ':']) {
        return None;
    }
    if p.bytes()
        .any(|c| c < 0x20 || matches!(c, b'"' | b'*' | b'?' | b'<' | b'>' | b'|' | 0x7f))
    {
        return None;
    }
    for part in p.split('/') {
        if part.is_empty() || part == "." || part == ".." || part.ends_with(['.', ' ']) {
            return None;
        }
        let stem = part.split('.').next().unwrap_or("").to_ascii_lowercase();
        if DEVICE_NAMES.contains(&stem.as_str()) {
            return None;
        }
    }
    Some(p)
}

/// The path as a platform path under `root`.
pub fn join(root: &Path, rel: &str) -> PathBuf {
    let mut p = root.to_path_buf();
    for part in rel.split(['/', '\\']) {
        p.push(part);
    }
    p
}

fn is_hex64(s: &str) -> bool {
    s.len() == 64
        && s.bytes()
            .all(|c| c.is_ascii_digit() || (b'a'..=b'f').contains(&c))
}

fn as_u64(v: &serde_json::Value) -> Option<u64> {
    match v {
        serde_json::Value::Number(n) => n.as_u64(),
        serde_json::Value::String(s)
            if !s.is_empty() && s.len() <= 15 && s.bytes().all(|c| c.is_ascii_digit()) =>
        {
            s.parse().ok()
        }
        _ => None,
    }
}

/// Parse `manifest.json`. Unknown keys are ignored. Err is a short reason.
pub fn parse(bytes: &[u8]) -> Result<Manifest, String> {
    let v: serde_json::Value =
        serde_json::from_slice(bytes).map_err(|_| "manifest format isn't 1".to_string())?;
    let top = v.as_object().ok_or("manifest format isn't 1")?;
    let format_ok = match top.get("format") {
        Some(serde_json::Value::Number(n)) => n.as_u64() == Some(1),
        Some(serde_json::Value::String(s)) => s == "1",
        _ => false,
    };
    if !format_ok {
        return Err("manifest format isn't 1".into());
    }
    let get = |k: &str| top.get(k).and_then(|x| x.as_str()).map(str::to_string);
    let (Some(ver), Some(tag)) = (get("version"), get("tag")) else {
        return Err("manifest has no version or tag".into());
    };
    let files = top
        .get("files")
        .and_then(|f| f.as_array())
        .ok_or("manifest has no files list")?;
    if files.len() > MAX_FILES {
        return Err("manifest lists too many files".into());
    }
    let mut m = Manifest {
        version: ver,
        tag,
        commit: get("commit").unwrap_or_default(),
        files: Vec::new(),
    };
    let mut total: u64 = 0;
    for f in files {
        let o = f.as_object().ok_or("manifest files list is malformed")?;
        let (Some(path), Some(sha), Some(size)) = (
            o.get("path").and_then(|x| x.as_str()),
            o.get("sha256").and_then(|x| x.as_str()),
            o.get("size"),
        ) else {
            return Err("a manifest entry lacks path, sha256 or size".into());
        };
        let sha = sha.to_ascii_lowercase();
        if !is_hex64(&sha) {
            return Err(format!("bad sha256 for {path}"));
        }
        let size = as_u64(size).ok_or_else(|| format!("bad size for {path}"))?;
        if safe_rel_path(path).is_none() {
            return Err(format!("refused path in manifest: {path}"));
        }
        if m.find(path).is_some() {
            return Err(format!("path listed twice: {path}"));
        }
        if size > MAX_FILE_BYTES {
            return Err(format!("{path} is over the size cap"));
        }
        total = total.saturating_add(size);
        if total > MAX_TOTAL_BYTES {
            return Err("the release is over the total size cap".into());
        }
        m.files.push(ManifestFile {
            path: path.to_string(),
            sha256: sha,
            size,
        });
    }
    if m.files.is_empty() {
        return Err("manifest lists no files".into());
    }
    Ok(m)
}

/// S2: the manifest belongs to the release that was asked for and is newer than this launcher.
pub fn matches_release(m: &Manifest, tag: &str, current: &str) -> Result<(), String> {
    if m.tag != tag {
        return Err(format!(
            "manifest tag {} isn't the release tag {tag}",
            m.tag
        ));
    }
    if format!("v{}", m.version) != tag {
        return Err(format!(
            "manifest version {} doesn't match tag {tag}",
            m.version
        ));
    }
    if !version::greater(&m.version, current) {
        return Err(format!(
            "manifest version {} isn't newer than {current}",
            m.version
        ));
    }
    Ok(())
}

/// SHA-256 (lower hex) and size of a file, streamed.
pub fn file_sha256(path: &Path) -> Option<(String, u64)> {
    let mut f = std::fs::File::open(path).ok()?;
    let mut h = Sha256::new();
    let mut buf = vec![0u8; 1 << 20];
    let mut total = 0u64;
    loop {
        let n = f.read(&mut buf).ok()?;
        if n == 0 {
            break;
        }
        h.update(&buf[..n]);
        total += n as u64;
    }
    Some((to_hex(&h.finish()), total))
}

/// Every manifest file exists under `root` as a plain file with the listed size and SHA-256.
/// Other files under `root` are ignored here and never copied.
pub fn verify_against(root: &Path, m: &Manifest) -> Result<(), String> {
    for f in &m.files {
        let p = join(root, &f.path);
        match std::fs::symlink_metadata(&p) {
            Ok(md) if md.is_file() => {}
            _ => return Err(format!("{} is missing", f.path)),
        }
        match file_sha256(&p) {
            Some((h, n)) if h == f.sha256 && n == f.size => {}
            _ => return Err(format!("{} doesn't match the manifest", f.path)),
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    const SHA: &str = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

    fn doc(path: &str) -> Vec<u8> {
        format!(
            r#"{{"format":1,"version":"0.7.1","tag":"v0.7.1","commit":"c","files":[{{"path":"{path}","sha256":"{SHA}","size":3}}]}}"#
        )
        .into_bytes()
    }

    #[test]
    fn good_and_bad_paths() {
        assert!(parse(&doc("data/items.txt")).is_ok());
        for bad in [
            "../x", "/x", "a/../b", "a//b", "a\\\\b", "c:/x", "a:b", "a/.", "a./b", "nul.txt",
            "a/CON", "a b ", "x*y",
        ] {
            assert!(parse(&doc(bad)).is_err(), "{bad}");
        }
    }

    #[test]
    fn shape_errors() {
        assert!(parse(b"nope").is_err());
        assert!(parse(br#"{"format":2,"version":"1","tag":"v1","files":[]}"#).is_err());
        assert!(parse(br#"{"format":1,"version":"1","tag":"v1","files":[]}"#).is_err());
        let dup = format!(
            r#"{{"format":"1","version":"1","tag":"v1","files":[{{"path":"A","sha256":"{SHA}","size":1}},{{"path":"a","sha256":"{SHA}","size":1}}]}}"#
        );
        assert!(parse(dup.as_bytes()).unwrap_err().contains("twice"));
    }

    #[test]
    fn release_match() {
        let m = parse(&doc("a")).unwrap();
        assert!(matches_release(&m, "v0.7.1", "0.7.0").is_ok());
        assert!(matches_release(&m, "v0.7.2", "0.7.0").is_err());
        assert!(matches_release(&m, "v0.7.1", "0.7.1").is_err());
        assert!(matches_release(&m, "v0.7.1", "0.8.0").is_err());
    }
}
