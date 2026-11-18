// SPDX-License-Identifier: GPL-3.0-only
//! The journaled swap. One change is: put a staged folder at `data/plugins/<id>/` (replacing what
//! is there, or removing it when there is no staged folder) and replace `installed.json`, as one
//! unit that is rolled back as a whole if any step fails or the process dies half way.
//!
//! Method (a directory-level journal, simpler and stronger than a per-file one because a folder
//! rename on one volume is atomic):
//!
//! 1. write `data/launcher/plugin-journal.json` (id, whether a folder was there, whether a new one
//!    comes, the old `installed.json` text);
//! 2. rename the old folder to `staging/<id>/old`;
//! 3. rename `staging/<id>/new` to `plugins/<id>`;
//! 4. replace `installed.json` (write a temp file, rename over);
//! 5. delete the journal, then the staging folder.
//!
//! [`recover`] runs before every mutating command: a journal on disk means an earlier run stopped
//! half way, and it is rolled back (old folder back in place, new folder gone, old `installed.json`
//! restored). The tests stop the run at each step to prove the file tree comes back unchanged.

use std::path::Path;

use serde_json::{json, Value};

use crate::layout::Layout;

/// Test hooks: stop the process "dead" at a step (no rollback code runs), like a crash or a power cut.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum Fault {
    #[default]
    None,
    /// After the journal is written, before anything moved.
    AfterJournal,
    /// After the old folder moved aside, before the new one is in place.
    AfterBackup,
    /// After the new folder is in place, before `installed.json` is replaced.
    AfterSwap,
    /// After `installed.json` is replaced, before the journal is deleted.
    AfterRecord,
}

#[derive(Debug)]
pub enum TxnError {
    /// A step failed and the change was rolled back (or the rollback error is in the text).
    Failed(String),
    /// A test fault stopped the run on purpose; nothing was rolled back.
    Crashed,
}

pub struct Change<'a> {
    pub id: &'a str,
    /// A staged folder to move to `plugins/<id>`; must be `staging/<id>/new`. `None` removes.
    pub has_new: bool,
    /// The complete new text of `installed.json`.
    pub installed_after: &'a str,
}

pub fn write_atomic(path: &Path, text: &str) -> Result<(), String> {
    let mut tmp = path.as_os_str().to_owned();
    tmp.push(".tmp");
    let tmp = std::path::PathBuf::from(tmp);
    std::fs::write(&tmp, text).map_err(|e| format!("cannot write {}: {e}", tmp.display()))?;
    std::fs::rename(&tmp, path).map_err(|e| {
        let _ = std::fs::remove_file(&tmp);
        format!("cannot replace {}: {e}", path.display())
    })
}

fn exists(p: &Path) -> bool {
    std::fs::symlink_metadata(p).is_ok()
}

pub fn apply(l: &Layout, ch: &Change, fault: Fault) -> Result<(), TxnError> {
    let fail = |m: String| TxnError::Failed(m);
    std::fs::create_dir_all(l.launcher())
        .map_err(|e| fail(format!("cannot create data/launcher: {e}")))?;
    std::fs::create_dir_all(l.plugins())
        .map_err(|e| fail(format!("cannot create data/plugins: {e}")))?;
    if exists(&l.journal()) {
        return Err(fail(
            "an unfinished plugin change is on disk; it must be rolled back first".into(),
        ));
    }
    let target = l.plugin_dir(ch.id);
    let had_old = exists(&target);
    let before = match std::fs::read_to_string(l.installed_json()) {
        Ok(t) => Value::String(t),
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => Value::Null,
        Err(e) => return Err(fail(format!("cannot read installed.json: {e}"))),
    };
    let journal = json!({
        "schema": 1,
        "id": ch.id,
        "had_old": had_old,
        "has_new": ch.has_new,
        "installed_before": before,
    });
    let mut text = serde_json::to_string_pretty(&journal).expect("plain data");
    text.push('\n');
    write_atomic(&l.journal(), &text).map_err(fail)?;

    let stage = l.staging(ch.id);
    let old = stage.join("old");
    let new = stage.join("new");
    if let Err(e) = std::fs::create_dir_all(&stage) {
        let _ = std::fs::remove_file(l.journal());
        return Err(fail(format!("cannot create the staging folder: {e}")));
    }
    let step = || -> Result<(), Result<(), String>> {
        // Err(Ok(())) means "crash now"; Err(Err(m)) means "failed, roll back".
        let f = |e: std::io::Error, what: &str| Err(Err(format!("{what}: {e}")));
        if fault == Fault::AfterJournal {
            return Err(Ok(()));
        }
        if had_old {
            if let Err(e) = std::fs::rename(&target, &old) {
                return f(e, "cannot move the current plugin folder aside");
            }
        }
        if fault == Fault::AfterBackup {
            return Err(Ok(()));
        }
        if ch.has_new {
            if let Err(e) = std::fs::rename(&new, &target) {
                return f(e, "cannot move the new plugin folder into place");
            }
        }
        if fault == Fault::AfterSwap {
            return Err(Ok(()));
        }
        write_atomic(&l.installed_json(), ch.installed_after).map_err(Err)?;
        if fault == Fault::AfterRecord {
            return Err(Ok(()));
        }
        Ok(())
    };
    match step() {
        Ok(()) => {
            let _ = std::fs::remove_file(l.journal());
            let _ = std::fs::remove_dir_all(&stage);
            Ok(())
        }
        Err(Ok(())) => Err(TxnError::Crashed),
        Err(Err(m)) => match rollback(l, &journal) {
            Ok(()) => Err(fail(format!("{m}; rolled back"))),
            Err(r) => Err(fail(format!("{m}; rollback also failed: {r}"))),
        },
    }
}

fn rollback(l: &Layout, journal: &Value) -> Result<(), String> {
    let id = journal["id"].as_str().ok_or("journal has no id")?;
    let had_old = journal["had_old"].as_bool().unwrap_or(false);
    let has_new = journal["has_new"].as_bool().unwrap_or(false);
    let target = l.plugin_dir(id);
    let stage = l.staging(id);
    let old = stage.join("old");
    let new = stage.join("new");
    if had_old {
        if exists(&old) {
            if exists(&target) {
                std::fs::remove_dir_all(&target)
                    .map_err(|e| format!("cannot clear {}: {e}", target.display()))?;
            }
            std::fs::rename(&old, &target)
                .map_err(|e| format!("cannot restore {}: {e}", target.display()))?;
        }
    } else if has_new && !exists(&new) && exists(&target) {
        std::fs::remove_dir_all(&target)
            .map_err(|e| format!("cannot remove {}: {e}", target.display()))?;
    }
    match &journal["installed_before"] {
        Value::String(t) => write_atomic(&l.installed_json(), t)?,
        _ => {
            let _ = std::fs::remove_file(l.installed_json());
        }
    }
    let _ = std::fs::remove_file(l.journal());
    let _ = std::fs::remove_dir_all(&stage);
    // Only goes when empty.
    let _ = std::fs::remove_dir(l.launcher().join("staging"));
    Ok(())
}

/// Roll back an interrupted change. Returns a line to show when there was one.
pub fn recover(l: &Layout) -> Result<Option<String>, String> {
    // A half-staged download or folder from an earlier run is never reused.
    let _ = std::fs::remove_dir_all(l.launcher().join("staging").join(".incoming"));
    let text = match std::fs::read_to_string(l.journal()) {
        Ok(t) => t,
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Ok(None),
        Err(e) => return Err(format!("cannot read the plugin journal: {e}")),
    };
    let journal: Value = serde_json::from_str(&text)
        .map_err(|e| format!("the plugin journal is not valid, not touching anything: {e}"))?;
    let id = journal["id"].as_str().unwrap_or("?").to_string();
    rollback(l, &journal)?;
    Ok(Some(format!(
        "an earlier change to plugin {id} did not finish; it was rolled back"
    )))
}
