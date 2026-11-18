// SPDX-License-Identifier: GPL-3.0-only
//! Zip check against the index, and the install plan.
//!
//! [`verify_download`] never extracts. [`plan_install`] never writes: it returns what the
//! launcher's installer (staging, per-file journal, rollback) must do, so the same plan can be
//! shown, tested and applied. Every plan lands the plugin disabled.
//!
//! Layout, relative to the launcher folder (the folder that holds `data\`):
//!
//! * `data/launcher/staging/<id>/`  where the installer stages the files and re-verifies them;
//! * `data/plugins/<id>/`           the final folder, with an empty file named `disabled` inside;
//! * `data/launcher/installed.json` one record per plugin the launcher installed.
//!
//! `data/storage/` is never part of a plan.

use std::collections::BTreeSet;
use std::fs::File;
use std::io::Read;
use std::path::{Path, PathBuf};

use scol_plugins::manifest::Kind;
use scol_plugins::sha256::{self, Sha256};
use scol_plugins::verify::{self, Report};
use serde::{Deserialize, Serialize};

use crate::error::Error;
use crate::index::Entry;
use crate::policy::{self, Host};
use crate::semver::Version;

/// Shown with every pulled native plugin.
pub const NATIVE_WARNING: &str =
    "This plugin is native code. It runs inside the game with your Windows rights. \
The launcher checked the file against the signed index and did not run it. It starts disabled; \
turn it on only if you trust its authors.";
/// Shown with a pulled Lua plugin.
pub const LUA_WARNING: &str = "This plugin runs scripts inside the game. It starts disabled; turn it on only if you trust its authors.";

/// Name of the marker file that keeps a plugin off. The host looks for an entry of this name.
pub const DISABLED_MARKER: &str = "disabled";

// ---- installed.json -------------------------------------------------------------------------

pub const INSTALLED_SCHEMA: u32 = 1;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct InstalledRecord {
    pub id: String,
    pub version: String,
    /// `catalogue` or `local`. A local record carries no digests: it is unverified by definition.
    pub source: String,
    pub zip_sha256: Option<String>,
    pub index_digest: Option<String>,
    pub index_sequence: Option<u64>,
    /// UTC time the launcher installed it, `YYYY-MM-DDTHH:MM:SSZ`.
    pub installed: String,
    /// `disabled` or `enabled`. The truth is the marker file; this is what the launcher last set.
    pub state: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct InstalledFile {
    pub schema: u32,
    pub plugins: Vec<InstalledRecord>,
}

impl Default for InstalledFile {
    fn default() -> Self {
        InstalledFile {
            schema: INSTALLED_SCHEMA,
            plugins: Vec::new(),
        }
    }
}

impl InstalledFile {
    pub fn parse(bytes: &[u8]) -> Result<InstalledFile, Error> {
        let bad = |m: String| Error::InstalledFormat(m);
        let text = std::str::from_utf8(bytes).map_err(|_| bad("not UTF-8".into()))?;
        let f: InstalledFile = serde_json::from_str(text).map_err(|e| bad(e.to_string()))?;
        if f.schema != INSTALLED_SCHEMA {
            return Err(bad(format!("schema {} is not supported", f.schema)));
        }
        for pair in f.plugins.windows(2) {
            if pair[0].id >= pair[1].id {
                return Err(bad("records must be sorted by id and unique".into()));
            }
        }
        for r in &f.plugins {
            if r.source != "catalogue" && r.source != "local" {
                return Err(bad(format!("{}: source must be catalogue or local", r.id)));
            }
            if r.state != "disabled" && r.state != "enabled" {
                return Err(bad(format!("{}: state must be disabled or enabled", r.id)));
            }
        }
        Ok(f)
    }

    pub fn find(&self, id: &str) -> Option<&InstalledRecord> {
        self.plugins.iter().find(|r| r.id == id)
    }

    /// A copy with `record` added or replacing the one with the same id, kept sorted.
    pub fn with_record(&self, record: InstalledRecord) -> InstalledFile {
        let mut plugins: Vec<InstalledRecord> = self
            .plugins
            .iter()
            .filter(|r| r.id != record.id)
            .cloned()
            .collect();
        plugins.push(record);
        plugins.sort_by(|a, b| a.id.cmp(&b.id));
        InstalledFile {
            schema: INSTALLED_SCHEMA,
            plugins,
        }
    }

    /// The file text: pretty JSON, one final newline. Same input, same bytes.
    pub fn render(&self) -> String {
        let mut s = serde_json::to_string_pretty(self).expect("plain data serialises");
        s.push('\n');
        s
    }
}

// ---- verify the download --------------------------------------------------------------------

fn sha256_file(path: &Path) -> Result<String, Error> {
    let mut f =
        File::open(path).map_err(|e| Error::Io(format!("cannot open {}: {e}", path.display())))?;
    let mut h = Sha256::new();
    let mut buf = vec![0u8; 64 * 1024];
    loop {
        let n = f
            .read(&mut buf)
            .map_err(|e| Error::Io(format!("cannot read {}: {e}", path.display())))?;
        if n == 0 {
            break;
        }
        h.update(&buf[..n]);
    }
    Ok(sha256::to_hex(&h.finish()))
}

/// Check a downloaded zip against its index entry.
///
/// 1. size equals the index, and the SHA-256 equals the index (before the zip is parsed);
/// 2. the `scol-plugins` zip verifier accepts it (without extracting);
/// 3. the digest the verifier saw equals the index too (the file did not change in between);
/// 4. the plugin inside is the one the index names: same id, version, kind and api.
///
/// The SDK range is not compared: the zip has no SDK range (see [`crate::policy`]).
///
/// The caller still owns `zip`. This check alone does not bind a later extraction to the bytes
/// checked here. The installer MUST use [`stage_download`], which copies the file into a private
/// staging folder while hashing and verifies the copy, and extract only from that copy.
pub fn verify_download(zip: &Path, entry: &Entry) -> Result<Report, Error> {
    let meta = std::fs::metadata(zip)
        .map_err(|e| Error::Io(format!("cannot stat {}: {e}", zip.display())))?;
    if meta.len() != entry.zip_size {
        return Err(Error::ZipDigest(format!(
            "size is {} bytes, the index says {}",
            meta.len(),
            entry.zip_size
        )));
    }
    let digest = sha256_file(zip)?;
    if digest != entry.zip_sha256 {
        return Err(Error::ZipDigest(format!(
            "SHA-256 is {digest}, the index says {}",
            entry.zip_sha256
        )));
    }
    let report = verify::verify_archive(zip).map_err(Error::ZipRefused)?;
    if report.archive.sha256 != entry.zip_sha256 || report.archive.size_bytes != entry.zip_size {
        return Err(Error::ZipDigest(
            "the file changed while it was verified".into(),
        ));
    }
    let m = &report.manifest;
    let mismatch = |what: &str, zip: &str, index: &str| {
        Error::ZipMismatch(format!(
            "{what} is {zip} in the zip and {index} in the index"
        ))
    };
    if m.id != entry.id {
        return Err(mismatch("id", &m.id, &entry.id));
    }
    if m.version != entry.version {
        return Err(mismatch("version", &m.version, &entry.version));
    }
    if m.kind != entry.kind {
        return Err(mismatch("kind", m.kind.as_str(), entry.kind.as_str()));
    }
    if (m.api_major, m.api_minor) != (entry.api_major, entry.api_minor) {
        return Err(mismatch("api", &m.api, &entry.api));
    }
    Ok(report)
}

/// Name of the staged copy inside the staging folder.
pub const STAGED_ZIP: &str = "package.zip";

/// Copy `zip` into `staging_dir` (created if needed) and verify the copy against `entry`.
///
/// The bytes are hashed while they are copied, and capped at the size the index states, so a file
/// that grows or changes after this call cannot reach the installer: later steps use the returned
/// path only. On any failure the staged copy is removed. An existing `package.zip` is replaced.
pub fn stage_download(
    zip: &Path,
    entry: &Entry,
    staging_dir: &Path,
) -> Result<(PathBuf, Report), Error> {
    use std::io::Write;
    std::fs::create_dir_all(staging_dir)
        .map_err(|e| Error::Io(format!("cannot create {}: {e}", staging_dir.display())))?;
    let dest = staging_dir.join(STAGED_ZIP);
    let _ = std::fs::remove_file(&dest);
    let result = (|| -> Result<Report, Error> {
        let src = File::open(zip)
            .map_err(|e| Error::Io(format!("cannot open {}: {e}", zip.display())))?;
        let mut out = std::fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&dest)
            .map_err(|e| Error::Io(format!("cannot create {}: {e}", dest.display())))?;
        let mut limited = src.take(entry.zip_size + 1);
        let mut h = Sha256::new();
        let mut total: u64 = 0;
        let mut buf = vec![0u8; 64 * 1024];
        loop {
            let n = limited
                .read(&mut buf)
                .map_err(|e| Error::Io(format!("cannot read {}: {e}", zip.display())))?;
            if n == 0 {
                break;
            }
            h.update(&buf[..n]);
            out.write_all(&buf[..n])
                .map_err(|e| Error::Io(format!("cannot write {}: {e}", dest.display())))?;
            total += n as u64;
        }
        out.flush()
            .map_err(|e| Error::Io(format!("cannot write {}: {e}", dest.display())))?;
        drop(out);
        if total != entry.zip_size {
            return Err(Error::ZipDigest(format!(
                "size is {total} bytes or more, the index says {}",
                entry.zip_size
            )));
        }
        let digest = sha256::to_hex(&h.finish());
        if digest != entry.zip_sha256 {
            return Err(Error::ZipDigest(format!(
                "SHA-256 is {digest}, the index says {}",
                entry.zip_sha256
            )));
        }
        verify_download(&dest, entry)
    })();
    match result {
        Ok(report) => Ok((dest, report)),
        Err(e) => {
            let _ = std::fs::remove_file(&dest);
            Err(e)
        }
    }
}

// ---- the plan -------------------------------------------------------------------------------

/// What is already on disk, read by the caller.
#[derive(Debug, Clone, Default)]
pub struct LocalState {
    /// `data/launcher/installed.json`, or the default when the file does not exist.
    pub installed: InstalledFile,
    /// Names of the folders under `data/plugins/`.
    pub folders: BTreeSet<String>,
}

#[derive(Debug, Clone, Copy, Default)]
pub struct PlanOptions {
    /// The player confirmed replacing a folder the launcher did not install.
    pub replace_local: bool,
    /// The player clicked a downgrade.
    pub allow_downgrade: bool,
}

/// One file of the plugin, path relative to `data/plugins/<id>/`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PlannedFile {
    pub path: String,
    pub size_bytes: u64,
    pub sha256: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Replaces {
    /// Nothing is installed under this id.
    Nothing,
    /// A catalogue install of this version is replaced (update, reinstall or confirmed downgrade).
    Catalogue(String),
    /// A folder the launcher did not install is replaced (only with `replace_local`).
    Local,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InstallPlan {
    pub id: String,
    pub version: String,
    pub kind: Kind,
    /// `data/launcher/staging/<id>`. The installer stages with [`stage_download`] and extracts only
    /// from the staged copy, never from the downloaded path.
    pub staging_dir: PathBuf,
    /// `data/plugins/<id>`
    pub target_dir: PathBuf,
    /// `data/plugins/<id>/disabled`, created empty. Always present in a plan.
    pub disabled_marker: PathBuf,
    /// `data/launcher/installed.json`
    pub installed_json: PathBuf,
    pub replaces: Replaces,
    /// Sorted by path.
    pub files: Vec<PlannedFile>,
    pub record: InstalledRecord,
    /// The complete new text of `installed.json`.
    pub installed_json_after: String,
    pub warnings: Vec<String>,
}

fn data_dirs(root: &Path, id: &str) -> (PathBuf, PathBuf, PathBuf) {
    let data = root.join("data");
    (
        data.join("launcher").join("staging").join(id),
        data.join("plugins").join(id),
        data.join("launcher").join("installed.json"),
    )
}

/// Build the plan for one verified download.
///
/// * `entry`, `report`: the index entry and the report [`verify_download`] returned for its zip.
/// * `index_digest`, `index_sequence`: from the [`crate::index::AcceptedIndex`] the entry came from.
/// * `host`: what this launcher is built against; the entry is checked again here so a plan can
///   never be built for an entry that [`policy::check_entry`] refuses.
/// * `root`: the launcher folder. `installed_at`: UTC time text, `YYYY-MM-DDTHH:MM:SSZ`.
#[allow(clippy::too_many_arguments)]
pub fn plan_install(
    entry: &Entry,
    report: &Report,
    index_digest: &str,
    index_sequence: u64,
    host: &Host,
    state: &LocalState,
    opts: PlanOptions,
    root: &Path,
    installed_at: &str,
) -> Result<InstallPlan, Error> {
    policy::check_entry(entry, host)?;
    // The report must be the one stage_download (or verify_download) returned for this entry:
    // same archive digest and size, and the same plugin id, version, kind and api.
    let m = &report.manifest;
    if report.archive.sha256 != entry.zip_sha256
        || report.archive.size_bytes != entry.zip_size
        || m.id != entry.id
        || m.version != entry.version
        || m.kind != entry.kind
        || (m.api_major, m.api_minor) != (entry.api_major, entry.api_minor)
    {
        return Err(Error::ZipMismatch(
            "the report is not for this entry".into(),
        ));
    }
    let record_before = state.installed.find(&entry.id);
    let from_catalogue = record_before.is_some_and(|r| r.source == "catalogue");
    let replaces = if from_catalogue {
        let old = record_before.expect("checked above");
        // A version that does not parse cannot be compared: treat it as a possible downgrade and
        // ask for the confirm, never skip the guard.
        let is_downgrade = match (Version::parse(&old.version), Version::parse(&entry.version)) {
            (Some(o), Some(n)) => n < o,
            _ => true,
        };
        if is_downgrade && !opts.allow_downgrade {
            return Err(Error::Downgrade {
                id: entry.id.clone(),
                installed: old.version.clone(),
                offered: entry.version.clone(),
            });
        }
        Replaces::Catalogue(old.version.clone())
    } else if record_before.is_some() || state.folders.contains(&entry.id) {
        if !opts.replace_local {
            return Err(Error::LocalClash(entry.id.clone()));
        }
        Replaces::Local
    } else {
        Replaces::Nothing
    };

    let prefix = format!("{}/", report.plugin.folder);
    let files: Vec<PlannedFile> = report
        .artifacts
        .iter()
        .map(|a| PlannedFile {
            path: a.path.strip_prefix(&prefix).unwrap_or(&a.path).to_string(),
            size_bytes: a.size_bytes,
            sha256: a.sha256.clone(),
        })
        .collect();

    let record = InstalledRecord {
        id: entry.id.clone(),
        version: entry.version.clone(),
        source: "catalogue".into(),
        zip_sha256: Some(entry.zip_sha256.clone()),
        index_digest: Some(index_digest.to_string()),
        index_sequence: Some(index_sequence),
        installed: installed_at.to_string(),
        state: "disabled".into(),
    };
    let after = state.installed.with_record(record.clone());
    let (staging_dir, target_dir, installed_json) = data_dirs(root, &entry.id);
    let warnings = match entry.kind {
        Kind::Native => vec![NATIVE_WARNING.to_string()],
        Kind::Lua => vec![LUA_WARNING.to_string()],
        Kind::Data => Vec::new(),
    };
    Ok(InstallPlan {
        id: entry.id.clone(),
        version: entry.version.clone(),
        kind: entry.kind,
        disabled_marker: target_dir.join(DISABLED_MARKER),
        staging_dir,
        target_dir,
        installed_json,
        replaces,
        files,
        record,
        installed_json_after: after.render(),
        warnings,
    })
}
