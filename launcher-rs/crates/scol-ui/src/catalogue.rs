// SPDX-License-Identifier: GPL-3.0-only
//! The Catalogue tab's data and the install step. The index is read from
//! `data\launcher\catalogue\index.json` and `index.json.sig` and checked by `scol-catalogue`
//! (signature first). Install downloads through the backend's fetcher, verifies against the signed
//! index, extracts, and lands the plugin disabled. The window never runs plugin code.

use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

use scol_catalogue::fetch::{download_zip, Fetcher};
use scol_catalogue::index::{open_index, Entry, LastAccepted};
use scol_catalogue::install::{
    plan_install, stage_download, InstalledFile, LocalState, PlanOptions, Replaces,
};
use scol_catalogue::policy::{offered, Host};
use scol_catalogue::semver::Version;
use scol_catalogue::trust::TrustRoot;

use crate::extract::extract_zip;
use crate::plugins::{self, HOST_API};

/// The SDK this launcher is pinned to (sco-core 2.4.1, held open).
pub const HOST_SDK: &str = "2.4.1";

#[derive(Debug, Clone)]
pub struct OfferRow {
    pub entry: Entry,
    /// None when installable here; otherwise why not.
    pub refused: Option<String>,
    /// The version installed from the catalogue, when there is one.
    pub installed: Option<String>,
}

#[derive(Debug, Clone)]
pub struct Loaded {
    pub sequence: u64,
    pub expires: String,
    pub digest: String,
    pub rows: Vec<OfferRow>,
}

pub fn dir(here: &Path) -> PathBuf {
    here.join("data").join("launcher").join("catalogue")
}

pub fn installed_json(here: &Path) -> PathBuf {
    here.join("data").join("launcher").join("installed.json")
}

pub fn now_unix() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs() as i64)
        .unwrap_or(0)
}

/// `YYYY-MM-DDTHH:MM:SSZ` for a Unix time (civil-from-days, no time-zone data).
pub fn utc_text(t: i64) -> String {
    let days = t.div_euclid(86400);
    let secs = t.rem_euclid(86400);
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z.rem_euclid(146_097);
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = if m <= 2 { y + 1 } else { y };
    format!(
        "{y:04}-{m:02}-{d:02}T{:02}:{:02}:{:02}Z",
        secs / 3600,
        secs % 3600 / 60,
        secs % 60
    )
}

pub fn host() -> Host {
    Host {
        api_major: HOST_API.major,
        api_minor: HOST_API.minor,
        sdk: Version::parse(HOST_SDK).expect("HOST_SDK is a version"),
    }
}

fn read_installed(here: &Path) -> InstalledFile {
    std::fs::read(installed_json(here))
        .ok()
        .and_then(|b| InstalledFile::parse(&b).ok())
        .unwrap_or_default()
}

/// The newest index the launcher accepted before, from the catalogue records in `installed.json`.
fn last_accepted(f: &InstalledFile) -> Option<LastAccepted> {
    f.plugins
        .iter()
        .filter(|r| r.source == "catalogue")
        .filter_map(|r| {
            Some(LastAccepted {
                sequence: r.index_sequence?,
                digest: r.index_digest.clone()?,
            })
        })
        .max_by_key(|l| l.sequence)
}

/// Read and check the index on disk. `Err("")` means no index files are on this PC; any other Err
/// is the reason the index was refused.
pub fn load(here: &Path, trust: &TrustRoot) -> Result<Loaded, String> {
    let d = dir(here);
    let (Ok(bytes), Ok(sig)) = (
        std::fs::read(d.join("index.json")),
        std::fs::read_to_string(d.join("index.json.sig")),
    ) else {
        return Err(String::new());
    };
    let installed = read_installed(here);
    let last = last_accepted(&installed);
    let acc =
        open_index(&bytes, &sig, trust, now_unix(), last.as_ref()).map_err(|e| e.to_string())?;
    let h = host();
    let rows = offered(&acc.index, &h)
        .into_iter()
        .map(|o| OfferRow {
            entry: o.entry.clone(),
            refused: o.compatible.err().map(|e| e.to_string()),
            installed: installed
                .find(&o.entry.id)
                .filter(|r| r.source == "catalogue")
                .map(|r| r.version.clone()),
        })
        .collect();
    Ok(Loaded {
        sequence: acc.index.sequence,
        expires: acc.index.expires.clone(),
        digest: acc.digest,
        rows,
    })
}

/// What an install did.
#[derive(Debug, Clone)]
pub struct Installed {
    pub id: String,
    pub version: String,
    pub warnings: Vec<String>,
}

/// Download, verify, extract and land one catalogue entry, disabled. `loaded` is the index it came from.
pub fn install(
    here: &Path,
    loaded: &Loaded,
    id: &str,
    fetcher: &mut dyn Fetcher,
    hosts: (&[&str], &[&str]),
) -> Result<Installed, String> {
    let row = loaded
        .rows
        .iter()
        .find(|r| r.entry.id == id)
        .ok_or_else(|| format!("{id} is not in the index"))?;
    if let Some(why) = &row.refused {
        return Err(why.clone());
    }
    let entry = &row.entry;
    let folders = std::fs::read_dir(plugins::plugins_dir(here))
        .map(|rd| {
            rd.flatten()
                .map(|e| e.file_name().to_string_lossy().into_owned())
                .collect()
        })
        .unwrap_or_default();
    let state = LocalState {
        installed: read_installed(here),
        folders,
    };
    let work = plugins::staging_dir(here).join(&entry.id);
    let _ = std::fs::remove_dir_all(&work);
    std::fs::create_dir_all(&work).map_err(|e| e.to_string())?;
    let result = (|| -> Result<Installed, String> {
        let dl = work.join("download.zip");
        download_zip(fetcher, entry, &dl, hosts.0, hosts.1).map_err(|e| e.to_string())?;
        let (staged_zip, report) =
            stage_download(&dl, entry, &work.join("staged")).map_err(|e| e.to_string())?;
        let plan = plan_install(
            entry,
            &report,
            &loaded.digest,
            loaded.sequence,
            &host(),
            &state,
            PlanOptions::default(),
            here,
            &utc_text(now_unix()),
        )
        .map_err(|e| e.to_string())?;
        let files = work.join("files");
        extract_zip(&staged_zip, &report, &files)?;
        let replace = !matches!(plan.replaces, Replaces::Nothing);
        plugins::land(here, &entry.id, &files, replace)?;
        if let Some(parent) = plan.installed_json.parent() {
            std::fs::create_dir_all(parent).map_err(|e| e.to_string())?;
        }
        std::fs::write(&plan.installed_json, plan.installed_json_after.as_bytes())
            .map_err(|e| e.to_string())?;
        Ok(Installed {
            id: entry.id.clone(),
            version: entry.version.clone(),
            warnings: plan.warnings,
        })
    })();
    let _ = std::fs::remove_dir_all(&work);
    result
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn utc_text_known_values() {
        assert_eq!(utc_text(0), "1970-01-01T00:00:00Z");
        assert_eq!(utc_text(951_782_400), "2000-02-29T00:00:00Z");
        assert_eq!(utc_text(1_798_761_599), "2026-12-31T23:59:59Z");
    }
}
