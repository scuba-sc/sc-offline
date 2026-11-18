// SPDX-License-Identifier: GPL-3.0-only
//! The Plugins page's data: the folders under `data\plugins`, the `disabled` marker, and adding a
//! plugin from a folder or a zip. Reads `plugin.ini` as text only; it never loads plugin code.
//! Everything that lands is disabled until the player turns it on.

use std::path::{Path, PathBuf};

use scol_config::plugin_ini::{parse_plugin_ini, PluginIni};
use scol_contract::files::PLUGIN_DISABLED_MARKER;
use scol_plugins::manifest::{self, HostApi, Kind};
use scol_plugins::verify::verify_archive;

use crate::extract::extract_zip;

/// The plugin API this launcher is built against (`docs/rust-launcher.md`, catalogue policy).
pub const HOST_API: HostApi = HostApi { major: 1, minor: 1 };

const MAX_FILES: usize = 4096;
const MAX_TOTAL: u64 = 512 * 1024 * 1024;
const MAX_DEPTH: usize = 16;

#[derive(Debug, Clone)]
pub struct SettingRow {
    pub label: String,
    pub kind: String,
    pub default: String,
    pub help: String,
    pub range: String,
}

#[derive(Debug, Clone)]
pub struct PluginRow {
    pub id: String,
    pub ini: PluginIni,
    pub enabled: bool,
    pub settings: Vec<SettingRow>,
}

pub fn plugins_dir(here: &Path) -> PathBuf {
    here.join("data").join("plugins")
}

pub fn staging_dir(here: &Path) -> PathBuf {
    here.join("data").join("launcher").join("staging")
}

/// A folder name that is one plain path segment.
pub fn valid_id(id: &str) -> bool {
    !id.is_empty()
        && id.len() <= 64
        && !id.starts_with('.')
        && id
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || b == b'_' || b == b'-' || b == b'.')
}

fn rows_of(ini: &PluginIni) -> Vec<SettingRow> {
    ini.settings
        .iter()
        .map(|s| {
            let mut range = String::new();
            if s.has_min && s.kind == "int" {
                range += &format!("min {} ", s.min_i);
            } else if s.has_min {
                range += &format!("min {} ", s.min_f);
            }
            if s.has_max && s.kind == "int" {
                range += &format!("max {}", s.max_i);
            } else if s.has_max {
                range += &format!("max {}", s.max_f);
            }
            if !s.choices.is_empty() {
                range += &s.choices.join(" | ");
            }
            SettingRow {
                label: s.label.clone(),
                kind: s.kind.clone(),
                default: s.default.clone(),
                help: s.help.clone(),
                range: range.trim().to_string(),
            }
        })
        .collect()
}

/// Folders with a `plugin.ini`, sorted by folder name (a folder without one is skipped, as the host does).
pub fn scan(here: &Path) -> Vec<PluginRow> {
    let Ok(rd) = std::fs::read_dir(plugins_dir(here)) else {
        return Vec::new();
    };
    let mut out = Vec::new();
    for e in rd.flatten() {
        let name = e.file_name().to_string_lossy().into_owned();
        if name.starts_with('.') || !e.path().is_dir() {
            continue;
        }
        let Ok(bytes) = std::fs::read(e.path().join("plugin.ini")) else {
            continue;
        };
        let ini = parse_plugin_ini(&name, &bytes);
        let enabled = std::fs::symlink_metadata(e.path().join(PLUGIN_DISABLED_MARKER)).is_err();
        let settings = rows_of(&ini);
        out.push(PluginRow {
            id: name,
            ini,
            enabled,
            settings,
        });
    }
    out.sort_by_key(|a| a.id.to_ascii_lowercase());
    out
}

/// Create or delete the empty `disabled` file. The host reads it at the next game start.
pub fn set_enabled(here: &Path, id: &str, enabled: bool) -> Result<(), String> {
    if !valid_id(id) {
        return Err(format!("not a plugin folder name: {id:?}"));
    }
    let marker = plugins_dir(here).join(id).join(PLUGIN_DISABLED_MARKER);
    if enabled {
        match std::fs::remove_file(&marker) {
            Ok(()) => Ok(()),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(()),
            Err(e) => Err(e.to_string()),
        }
    } else {
        std::fs::write(&marker, b"").map_err(|e| e.to_string())
    }
}

/// What was added.
#[derive(Debug, Clone)]
pub struct Added {
    pub id: String,
    pub name: String,
    pub kind: Kind,
}

/// Move a finished folder into `data\plugins\<id>`, disabled. `staged` must be on the same volume.
/// With `replace`, an existing folder is set aside first and put back if the move fails.
pub fn land(here: &Path, id: &str, staged: &Path, replace: bool) -> Result<(), String> {
    if !valid_id(id) {
        return Err(format!("not a plugin folder name: {id:?}"));
    }
    std::fs::write(staged.join(PLUGIN_DISABLED_MARKER), b"").map_err(|e| e.to_string())?;
    let target = plugins_dir(here).join(id);
    std::fs::create_dir_all(plugins_dir(here)).map_err(|e| e.to_string())?;
    let old = staging_dir(here).join(format!("{id}.old"));
    let had = target.exists();
    if had {
        if !replace {
            return Err(format!("{id} is already in data\\plugins"));
        }
        let _ = std::fs::remove_dir_all(&old);
        std::fs::rename(&target, &old)
            .map_err(|e| format!("cannot set the old folder aside: {e}"))?;
    }
    if let Err(e) = std::fs::rename(staged, &target) {
        if had {
            let _ = std::fs::rename(&old, &target);
        }
        return Err(format!("cannot move the plugin into place: {e}"));
    }
    if had {
        let _ = std::fs::remove_dir_all(&old);
    }
    Ok(())
}

fn copy_tree(
    src: &Path,
    dst: &Path,
    depth: usize,
    files: &mut usize,
    total: &mut u64,
) -> Result<(), String> {
    if depth > MAX_DEPTH {
        return Err("folder is nested too deeply".into());
    }
    std::fs::create_dir_all(dst).map_err(|e| e.to_string())?;
    for e in std::fs::read_dir(src).map_err(|e| e.to_string())? {
        let e = e.map_err(|e| e.to_string())?;
        let ft = e.file_type().map_err(|e| e.to_string())?;
        let name = e.file_name();
        if ft.is_dir() {
            copy_tree(&e.path(), &dst.join(&name), depth + 1, files, total)?;
        } else if ft.is_file() {
            let len = e.metadata().map_err(|e| e.to_string())?.len();
            *files += 1;
            *total += len;
            if *files > MAX_FILES || *total > MAX_TOTAL {
                return Err("folder is too large to add".into());
            }
            std::fs::copy(e.path(), dst.join(&name)).map_err(|e| e.to_string())?;
        } else {
            return Err(format!(
                "{} is a link or special file",
                name.to_string_lossy()
            ));
        }
    }
    Ok(())
}

/// Add a plugin from a folder holding a `plugin.ini`.
pub fn add_folder(here: &Path, src: &Path) -> Result<Added, String> {
    let bytes = std::fs::read(src.join("plugin.ini"))
        .map_err(|_| "no plugin.ini in that folder".to_string())?;
    if bytes.len() > manifest::MAX_MANIFEST_BYTES {
        return Err("plugin.ini is too large".into());
    }
    let m = manifest::parse(&bytes, Some(HOST_API)).map_err(|e| e.to_string())?;
    check_id(&m.id)?;
    let staged = staging_dir(here).join(&m.id).join("files");
    let _ = std::fs::remove_dir_all(&staged);
    let (mut files, mut total) = (0, 0);
    let r = copy_tree(src, &staged, 0, &mut files, &mut total)
        .and_then(|()| land(here, &m.id, &staged, false));
    if r.is_err() {
        let _ = std::fs::remove_dir_all(&staged);
    }
    r?;
    Ok(Added {
        id: m.id,
        name: m.name,
        kind: m.kind,
    })
}

fn check_id(id: &str) -> Result<(), String> {
    if scol_catalogue::policy::is_reserved(id) {
        return Err(format!("{id:?} is a reserved plugin id"));
    }
    if !valid_id(id) {
        return Err(format!("{id:?} is not usable as a folder name"));
    }
    Ok(())
}

/// Add a plugin from a zip: `scol_plugins::verify` first, then extract the verified files.
pub fn add_zip(here: &Path, zip: &Path) -> Result<Added, String> {
    let report = verify_archive(zip).map_err(|e| e.to_string())?;
    let id = report.manifest.id.clone();
    check_id(&id)?;
    if plugins_dir(here).join(&id).exists() {
        return Err(format!("{id} is already in data\\plugins"));
    }
    let staged = staging_dir(here).join(&id).join("files");
    let _ = std::fs::remove_dir_all(&staged);
    let r = extract_zip(zip, &report, &staged).and_then(|()| land(here, &id, &staged, false));
    if r.is_err() {
        let _ = std::fs::remove_dir_all(&staged);
    }
    r?;
    Ok(Added {
        id,
        name: report.manifest.name,
        kind: report.manifest.kind,
    })
}

/// Folder or zip, by what the path is.
pub fn add_path(here: &Path, p: &Path) -> Result<Added, String> {
    if p.is_dir() {
        add_folder(here, p)
    } else if p.is_file() {
        add_zip(here, p)
    } else {
        Err(format!("{} is not a folder or a file", p.display()))
    }
}
