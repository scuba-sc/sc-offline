// SPDX-License-Identifier: GPL-3.0-only
//! `plugins list`, `plugins enable`, `plugins disable`.
//!
//! The truth about on or off is the empty file `disabled` inside the plugin folder (the host looks
//! for it). `installed.json` also records the state the launcher last set; enable and disable
//! keep both in step, marker first.

use scol_catalogue::install::{InstalledFile, DISABLED_MARKER};
use scol_plugins::manifest::{self, HostApi};
use serde_json::{json, Value};

use crate::cli::{Ctx, Io};
use crate::layout::valid_id;
use crate::store;
use crate::txn::write_atomic;

/// One folder of `data/plugins/`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Row {
    pub folder: String,
    pub id: String,
    pub name: String,
    pub version: String,
    pub kind: String,
    pub api: String,
    pub author: String,
    /// `enabled` or `disabled`.
    pub state: String,
    /// `catalogue` or `local`.
    pub source: String,
    /// Why the folder is not a usable plugin, when it is not.
    pub problem: Option<String>,
}

/// Read every plugin folder. Never fails on one bad folder: that row carries a `problem`.
pub fn rows(ctx: &Ctx) -> Result<Vec<Row>, String> {
    let l = &ctx.layout;
    let installed = store::read_installed(l)?;
    let host = HostApi {
        major: ctx.host.api_major,
        minor: ctx.host.api_minor,
    };
    let mut out = Vec::new();
    for folder in store::plugin_folders(l) {
        let dir = l.plugins().join(&folder);
        if !dir.is_dir() {
            continue;
        }
        let state = if dir.join(DISABLED_MARKER).exists() {
            "disabled"
        } else {
            "enabled"
        };
        let source = installed.find(&folder).map_or("local", |r| {
            if r.source == "catalogue" {
                "catalogue"
            } else {
                "local"
            }
        });
        let mut row = Row {
            folder: folder.clone(),
            id: folder.clone(),
            name: String::new(),
            version: String::new(),
            kind: String::new(),
            api: String::new(),
            author: String::new(),
            state: state.into(),
            source: source.into(),
            problem: None,
        };
        match std::fs::read(dir.join("plugin.ini")) {
            Err(e) => row.problem = Some(format!("no readable plugin.ini ({e})")),
            Ok(buf) => match manifest::parse(&buf, Some(host)) {
                Err(e) => row.problem = Some(format!("plugin.ini line {}: {}", e.line, e.message)),
                Ok(m) => {
                    row.id = m.id.clone();
                    row.name = m.name.clone();
                    row.version = m.version.clone();
                    row.kind = m.kind.as_str().to_string();
                    row.api = m.api.clone();
                    row.author = m.author.clone().unwrap_or_default();
                    if m.id != folder {
                        row.problem =
                            Some(format!("folder name {folder} differs from id {}", m.id));
                    }
                }
            },
        }
        out.push(row);
    }
    Ok(out)
}

pub fn list(ctx: &Ctx, json_out: bool, io: &mut dyn Io) -> Result<(), String> {
    let rows = rows(ctx)?;
    if json_out {
        let v: Vec<Value> = rows
            .iter()
            .map(|r| {
                json!({
                    "folder": r.folder, "id": r.id, "name": r.name, "version": r.version,
                    "kind": r.kind, "api": r.api, "author": r.author, "state": r.state,
                    "source": r.source, "problem": r.problem,
                })
            })
            .collect();
        io.out(&serde_json::to_string_pretty(&v).expect("plain data"));
        return Ok(());
    }
    if rows.is_empty() {
        io.out("no plugins installed");
        return Ok(());
    }
    for r in &rows {
        match &r.problem {
            Some(p) if r.name.is_empty() => {
                io.out(&format!("{}  [{}]  PROBLEM: {p}", r.folder, r.state));
            }
            p => {
                io.out(&format!(
                    "{}  {}  {}  [{}]  {} plugin, api {}, source {}{}",
                    r.id,
                    r.name,
                    r.version,
                    r.state,
                    r.kind,
                    r.api,
                    r.source,
                    if r.author.is_empty() {
                        String::new()
                    } else {
                        format!(", by {}", r.author)
                    }
                ));
                if let Some(p) = p {
                    io.out(&format!("    PROBLEM: {p}"));
                }
            }
        }
        if r.kind == "native" && r.state == "enabled" {
            io.out("    native code: runs inside the game with your Windows rights");
        }
    }
    Ok(())
}

/// Turn a plugin on or off. Refused while the game runs, for an id that is not a plain plugin
/// id, and for a folder that is a link.
pub fn set_enabled(ctx: &Ctx, id: &str, enable: bool, io: &mut dyn Io) -> Result<(), String> {
    let l = &ctx.layout;
    if !valid_id(id) {
        return Err(format!(
            "{id:?} is not a plugin id (1 to 31 of a-z, 0-9, _)"
        ));
    }
    crate::install::begin(ctx, io)?;
    let dir = l.plugin_dir(id);
    if store::is_link(&dir) {
        return Err(format!("{} is a link; not touching it", dir.display()));
    }
    if !dir.is_dir() {
        return Err(format!("no plugin {id} is installed"));
    }
    let marker = dir.join(DISABLED_MARKER);
    let was_disabled = marker.exists();
    if enable {
        if was_disabled {
            std::fs::remove_file(&marker)
                .map_err(|e| format!("cannot remove {}: {e}", marker.display()))?;
        }
    } else if !was_disabled {
        std::fs::File::create(&marker)
            .map_err(|e| format!("cannot create {}: {e}", marker.display()))?;
    }
    // Keep installed.json in step when the launcher has a record.
    let installed = store::read_installed(l)?;
    if let Some(rec) = installed.find(id) {
        let want = if enable { "enabled" } else { "disabled" };
        if rec.state != want {
            let mut rec = rec.clone();
            rec.state = want.into();
            let after: InstalledFile = installed.with_record(rec);
            write_atomic(&l.installed_json(), &after.render())?;
        }
    }
    io.out(&format!(
        "{id} is now {}",
        if enable { "enabled" } else { "disabled" }
    ));
    if enable {
        if let Some(r) = rows(ctx)?.into_iter().find(|r| r.folder == id) {
            if r.kind == "native" {
                io.out("warning: this plugin is native code; it runs inside the game with your Windows rights");
            }
            if let Some(p) = r.problem {
                io.out(&format!("warning: {p}"));
            }
        }
    }
    Ok(())
}
