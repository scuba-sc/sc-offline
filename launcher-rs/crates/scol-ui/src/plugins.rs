// SPDX-License-Identifier: GPL-3.0-only
//! The Plugins page's data and actions. The list is the folders under `data\plugins` with the
//! `plugin.ini` of each read as text (the window never loads plugin code). Changes go through
//! `scol-plugin-manager`, which is transactional and records what it installed in
//! `data\launcher\installed.json`: enable, disable, add from a folder or zip, remove. Everything that
//! lands is disabled until the player turns it on.

use std::collections::BTreeSet;
use std::path::{Path, PathBuf};

use scol_catalogue::policy::Host;
use scol_catalogue::semver::Version;
use scol_config::plugin_ini::{parse_plugin_ini, PluginIni};
use scol_contract::files::PLUGIN_DISABLED_MARKER;
use scol_plugin_manager::cli::{Capture, Ctx};
use scol_plugin_manager::install::{self, Opts};
use scol_plugin_manager::layout::Layout;
use scol_plugin_manager::txn::Fault;
use scol_plugin_manager::{list, store};

use crate::backend::Backend;

/// The SDK this launcher is pinned to (sco-core 2.4.1).
pub const HOST_SDK: &str = "2.4.1";
/// The plugin API this launcher is built against.
pub const HOST_API: (u16, u16) = (1, 1);

#[derive(Debug, Clone)]
pub struct SettingRow {
    /// The key of the setting in the plugin's store.
    pub name: String,
    pub label: String,
    pub kind: String,
    pub default: String,
    /// What the game reads at its next start: the kept value, else the default.
    pub value: String,
    /// A value is kept in the plugin's database (Reset would drop it).
    pub kept: bool,
    pub help: String,
    pub range: String,
}

#[derive(Debug, Clone)]
pub struct PluginRow {
    pub id: String,
    pub ini: PluginIni,
    pub enabled: bool,
    pub settings: Vec<SettingRow>,
    /// `catalogue` or `local`.
    pub source: String,
    /// Why the folder is not a usable plugin, when it is not.
    pub problem: Option<String>,
}

pub fn plugins_dir(here: &Path) -> PathBuf {
    here.join("data").join("plugins")
}

/// The plugin manager's context for this install: the real clock, the host this launcher is built
/// against, the backend's trust root and its game probe.
pub fn pm_ctx(here: &Path, backend: &dyn Backend) -> Ctx {
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0, |d| d.as_secs() as i64);
    Ctx {
        layout: Layout::new(here),
        now,
        game_running: backend.game_probe(),
        trust: backend.trust(),
        host: Host {
            api_major: HOST_API.0,
            api_minor: HOST_API.1,
            sdk: Version::parse(HOST_SDK).expect("HOST_SDK is a version"),
        },
        fault: Fault::None,
    }
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
                name: s.name.clone(),
                label: s.label.clone(),
                kind: s.kind.clone(),
                default: s.default.clone(),
                value: s.value.clone(),
                kept: s.kept,
                help: s.help.clone(),
                range: range.trim().to_string(),
            }
        })
        .collect()
}

/// Folders with a `plugin.ini`, sorted by folder name (a folder without one is skipped, as the host does).
pub fn scan(ctx: &Ctx) -> Vec<PluginRow> {
    let here = &ctx.layout.root;
    let Ok(rd) = std::fs::read_dir(plugins_dir(here)) else {
        return Vec::new();
    };
    let pm_rows = list::rows(ctx).unwrap_or_default();
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
        let pm = pm_rows.iter().find(|r| r.folder == name);
        out.push(PluginRow {
            id: name,
            ini,
            enabled,
            settings,
            source: pm.map_or("local", |r| r.source.as_str()).to_string(),
            problem: pm.and_then(|r| r.problem.clone()),
        });
    }
    out.sort_by_key(|a| a.id.to_ascii_lowercase());
    out
}

/// Turn a plugin on or off. The manager refuses while the game runs.
pub fn set_enabled(ctx: &Ctx, id: &str, enable: bool) -> Result<(), String> {
    list::set_enabled(ctx, id, enable, &mut Capture::default())
}

/// Remove a plugin folder. Its settings store stays.
pub fn remove(ctx: &Ctx, id: &str) -> Result<(), String> {
    install::remove(ctx, id, false, &mut Capture::default())
}

/// What was added.
#[derive(Debug, Clone)]
pub struct Added {
    pub id: String,
    pub name: String,
    /// `native`, `lua` or `data`.
    pub kind: String,
}

fn folders(ctx: &Ctx) -> BTreeSet<String> {
    store::plugin_folders(&ctx.layout)
}

/// Add a plugin from a folder holding a `plugin.ini`, or from a plugin zip. The manager verifies the
/// package, stages it and lands it disabled.
pub fn add_path(ctx: &Ctx, p: &Path) -> Result<Added, String> {
    if !p.is_dir() && !p.is_file() {
        return Err(format!("{} is not a folder or a file", p.display()));
    }
    let before = folders(ctx);
    install::install_local(ctx, p, Opts::default(), &mut Capture::default())?;
    let after = folders(ctx);
    let id = after
        .difference(&before)
        .next()
        .cloned()
        .ok_or_else(|| "the plugin manager added nothing".to_string())?;
    Ok(describe(ctx, &id))
}

/// The name and kind of an installed folder.
pub fn describe(ctx: &Ctx, id: &str) -> Added {
    let ini = std::fs::read(plugins_dir(&ctx.layout.root).join(id).join("plugin.ini"))
        .map(|b| parse_plugin_ini(id, &b))
        .ok();
    Added {
        id: id.to_string(),
        name: ini
            .as_ref()
            .map(|i| i.name.clone())
            .filter(|n| !n.is_empty())
            .unwrap_or_else(|| id.to_string()),
        kind: ini.map(|i| i.kind).unwrap_or_default(),
    }
}
