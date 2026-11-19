// SPDX-License-Identifier: GPL-3.0-only
//! A plugin's `[settings]` values, kept in its own database `data/storage/<id>.db` (table `sco_kv`, key
//! `sco.settings.<name>`, value `<type>:<canonical text>` as a blob), the way the C++ Plugins page does
//! (`LoadKept` and `SavePluginSettings` in `launcher/launcher.cpp`). The host reads these values at its next
//! start, so a save only makes sense while the game is closed: the host keeps its values in memory while it
//! runs and would neither see the change nor announce `settings.changed`. This module does not check for
//! a running game; the `plugins settings` command does, and a window must too.
//!
//! The rules for a value (type, range, choices) are `scol_config::plugin_ini::canon_setting`, the ones the
//! host uses. SQLite is the `bundled` copy of `rusqlite`. Nothing here loads plugin code. Not run in game.

use std::collections::HashMap;
use std::path::{Path, PathBuf};

use rusqlite::{params, Connection, OpenFlags, TransactionBehavior};
use scol_config::cfmt::format_g17;
use scol_config::plugin_ini::{
    apply_kept, parse_plugin_ini, pending_writes, PluginIni, PluginSetting, SettingError,
};

use crate::layout::{valid_id, Layout};

/// What can go wrong reading or saving settings.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SettingsError {
    /// Not a plugin id the host accepts.
    BadId(String),
    /// No `data/plugins/<id>/plugin.ini`.
    NoPlugin(String),
    /// The plugin declares no setting of that name.
    UnknownSetting { id: String, name: String },
    /// A value does not fit its declaration. `why` is the text the C++ window shows.
    Invalid {
        name: String,
        label: String,
        why: String,
    },
    /// `data/storage/<id>.db` could not be opened.
    StorageOpen(String),
    /// The write failed and was rolled back.
    StorageWrite(String),
}

impl std::fmt::Display for SettingsError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            SettingsError::BadId(id) => write!(f, "{id:?} is not a plugin id"),
            SettingsError::NoPlugin(id) => {
                write!(f, "no plugin {id} (no data/plugins/{id}/plugin.ini)")
            }
            SettingsError::UnknownSetting { id, name } => {
                write!(f, "plugin {id} has no setting {name}")
            }
            SettingsError::Invalid { label, why, .. } => write!(f, "{label}: {why}"),
            SettingsError::StorageOpen(id) => write!(f, "couldn't open data\\storage\\{id}.db"),
            SettingsError::StorageWrite(e) => write!(f, "couldn't write the values: {e}"),
        }
    }
}

impl std::error::Error for SettingsError {}

/// What a save did.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Saved {
    /// `sco_kv` keys written.
    pub written: Vec<String>,
    /// `sco_kv` keys deleted.
    pub deleted: Vec<String>,
}

impl Saved {
    pub fn changed(&self) -> usize {
        self.written.len() + self.deleted.len()
    }
}

/// The text the window shows for a refused value (the `launcher.setting.*` strings).
pub fn explain(s: &PluginSetting, e: SettingError) -> String {
    let number = |lower: bool| {
        if s.kind == "float" {
            format_g17(if lower { s.min_f } else { s.max_f })
        } else if lower {
            s.min_i.to_string()
        } else {
            s.max_i.to_string()
        }
    };
    match e {
        SettingError::BoolExpected => "true or false".into(),
        SettingError::IntegerExpected => "a whole number".into(),
        SettingError::AtLeast => format!("at least {}", number(true)),
        SettingError::AtMost => format!("at most {}", number(false)),
        SettingError::NumberExpected => "a number such as 1.5".into(),
        SettingError::FiniteExpected => "not a finite number".into(),
        SettingError::StringLimit => "at most 255 characters".into(),
        SettingError::AsciiExpected => "printable ASCII only".into(),
        SettingError::ChoiceExpected => "pick one of the choices".into(),
    }
}

/// `data/storage/<id>.db`.
pub fn db_path(root: &Path, id: &str) -> PathBuf {
    Layout::new(root)
        .data()
        .join("storage")
        .join(format!("{id}.db"))
}

fn open(path: &Path, flags: OpenFlags) -> Option<Connection> {
    let c = Connection::open_with_flags(path, flags | OpenFlags::SQLITE_OPEN_NO_MUTEX).ok()?;
    c.busy_timeout(std::time::Duration::from_millis(2000))
        .ok()?;
    Some(c)
}

/// The stored value of a key as text, whatever its column type.
fn stored(c: &Connection, key: &str) -> Option<String> {
    use rusqlite::types::ValueRef;
    let mut st = c.prepare("SELECT value FROM sco_kv WHERE key = ?1").ok()?;
    let mut rows = st.query(params![key]).ok()?;
    let row = rows.next().ok()??;
    let bytes: Vec<u8> = match row.get_ref(0).ok()? {
        ValueRef::Blob(b) | ValueRef::Text(b) => b.to_vec(),
        _ => return None,
    };
    Some(String::from_utf8_lossy(&bytes).into_owned())
}

/// Reads `data/plugins/<id>/plugin.ini` with each setting's kept value applied (`ReadPlugins` and `LoadKept`):
/// the kept value when the plugin's database has one that still fits the declaration, else the default.
/// Opens the database read-only first, read-write only when that fails (a write-ahead log may need it).
pub fn read_plugin(root: &Path, id: &str) -> Result<PluginIni, SettingsError> {
    if !valid_id(id) {
        return Err(SettingsError::BadId(id.to_string()));
    }
    let l = Layout::new(root);
    let ini = std::fs::read(l.plugin_dir(id).join("plugin.ini"))
        .map_err(|_| SettingsError::NoPlugin(id.to_string()))?;
    let mut p = parse_plugin_ini(id, &ini);
    let db = db_path(root, id);
    if !p.settings.is_empty() && db.is_file() {
        let conn = open(&db, OpenFlags::SQLITE_OPEN_READ_ONLY)
            .or_else(|| open(&db, OpenFlags::SQLITE_OPEN_READ_WRITE));
        if let Some(c) = conn {
            apply_kept(&mut p, |key| stored(&c, key));
        }
    }
    Ok(p)
}

/// Saves the edited values (`SavePluginSettings`). `edits` maps a setting name to the text typed; a setting
/// without an entry keeps its current value. With `reset`, every kept value is deleted instead. All edits
/// are checked first: one that does not fit its declaration refuses the whole save and writes nothing. The
/// database and its table are created when missing; the writes are one transaction.
pub fn save(
    root: &Path,
    id: &str,
    edits: &HashMap<String, String>,
    reset: bool,
) -> Result<Saved, SettingsError> {
    let p = read_plugin(root, id)?;
    for name in edits.keys() {
        if !p.settings.iter().any(|s| &s.name == name) {
            return Err(SettingsError::UnknownSetting {
                id: id.to_string(),
                name: name.clone(),
            });
        }
    }
    let puts = pending_writes(&p, edits, reset).map_err(|(name, e)| {
        let s = p.settings.iter().find(|s| s.name == name);
        SettingsError::Invalid {
            label: s.map_or(name.clone(), |s| s.label.clone()),
            why: s.map_or_else(String::new, |s| explain(s, e)),
            name,
        }
    })?;
    if puts.is_empty() {
        return Ok(Saved::default());
    }
    let dir = db_path(root, id)
        .parent()
        .map(Path::to_path_buf)
        .unwrap_or_default();
    std::fs::create_dir_all(&dir).map_err(|_| SettingsError::StorageOpen(id.to_string()))?;
    let mut conn = open(
        &db_path(root, id),
        OpenFlags::SQLITE_OPEN_READ_WRITE | OpenFlags::SQLITE_OPEN_CREATE,
    )
    .ok_or_else(|| SettingsError::StorageOpen(id.to_string()))?;
    let w = |e: rusqlite::Error| SettingsError::StorageWrite(e.to_string());
    let tx = conn
        .transaction_with_behavior(TransactionBehavior::Immediate)
        .map_err(w)?;
    tx.execute_batch(
        "CREATE TABLE IF NOT EXISTS sco_kv(key TEXT PRIMARY KEY NOT NULL, value BLOB NOT NULL) WITHOUT ROWID",
    )
    .map_err(w)?;
    let mut saved = Saved::default();
    for (key, value) in &puts {
        match value {
            Some(v) => {
                tx.execute(
                    "INSERT OR REPLACE INTO sco_kv(key, value) VALUES(?1, ?2)",
                    params![key, v.as_bytes()],
                )
                .map_err(w)?;
                saved.written.push(key.clone());
            }
            None => {
                tx.execute("DELETE FROM sco_kv WHERE key = ?1", params![key])
                    .map_err(w)?;
                saved.deleted.push(key.clone());
            }
        }
    }
    tx.commit().map_err(w)?;
    Ok(saved)
}

/// `name=value` words of the command line into an edit map.
pub fn parse_assignments(words: &[String]) -> Result<HashMap<String, String>, String> {
    let mut m = HashMap::new();
    for w in words {
        let Some((k, v)) = w.split_once('=') else {
            return Err(format!("{w:?} is not name=value"));
        };
        if k.is_empty() {
            return Err(format!("{w:?} has no setting name"));
        }
        m.insert(k.to_string(), v.to_string());
    }
    Ok(m)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn world(name: &str, ini: &str) -> PathBuf {
        let root =
            std::env::temp_dir().join(format!("scol-settings-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&root);
        let dir = root.join("data").join("plugins").join("demo");
        std::fs::create_dir_all(&dir).unwrap();
        std::fs::write(dir.join("plugin.ini"), ini).unwrap();
        root
    }

    const INI: &str = "name = Demo\nversion = 1.0\n[settings]\nspeed = int default 5 min 1 max 9 label \"Speed\"\nratio = float default 0.5 min 0 max 1\nfast = bool default false\nmode = enum(a,b,c) default b\nnote = string default hi\n";

    #[test]
    fn defaults_without_a_database() {
        let root = world("defaults", INI);
        let p = read_plugin(&root, "demo").unwrap();
        let v: Vec<(&str, &str, bool)> = p
            .settings
            .iter()
            .map(|s| (s.name.as_str(), s.value.as_str(), s.kept))
            .collect();
        assert_eq!(
            v,
            [
                ("speed", "5", false),
                ("ratio", "0.5", false),
                ("fast", "false", false),
                ("mode", "b", false),
                ("note", "hi", false)
            ]
        );
        assert!(!db_path(&root, "demo").exists(), "reading creates nothing");
        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn save_then_read_back_in_the_store_format() {
        let root = world("roundtrip", INI);
        let edits =
            parse_assignments(&["speed=7".into(), "mode=c".into(), "fast=true".into()]).unwrap();
        let saved = save(&root, "demo", &edits, false).unwrap();
        assert_eq!(saved.written.len(), 3);
        // the stored rows are `<type>:<text>` blobs under `sco.settings.<name>`
        let c = Connection::open(db_path(&root, "demo")).unwrap();
        let v: Vec<u8> = c
            .query_row(
                "SELECT value FROM sco_kv WHERE key = 'sco.settings.speed'",
                [],
                |r| r.get(0),
            )
            .unwrap();
        assert_eq!(v, b"int:7");
        let t: String = c
            .query_row(
                "SELECT typeof(value) FROM sco_kv WHERE key = 'sco.settings.mode'",
                [],
                |r| r.get(0),
            )
            .unwrap();
        assert_eq!(t, "blob");
        drop(c);
        let p = read_plugin(&root, "demo").unwrap();
        let get = |n: &str| p.settings.iter().find(|s| s.name == n).unwrap().clone();
        assert_eq!((get("speed").value, get("speed").kept), ("7".into(), true));
        assert_eq!(get("mode").value, "c");
        assert_eq!(get("fast").value, "true");
        assert!(!get("ratio").kept);
        // saving the same values again changes nothing
        let again = save(&root, "demo", &edits, false).unwrap();
        assert_eq!(again.changed(), 0);
        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn a_refused_value_writes_nothing() {
        let root = world("refused", INI);
        let edits = parse_assignments(&["speed=8".into(), "ratio=2".into()]).unwrap();
        let e = save(&root, "demo", &edits, false).unwrap_err();
        match &e {
            SettingsError::Invalid { name, why, .. } => {
                assert_eq!(name, "ratio");
                assert_eq!(why, "at most 1");
            }
            other => panic!("{other:?}"),
        }
        assert!(!db_path(&root, "demo").exists());
        for (text, why) in [
            ("speed=abc", "a whole number"),
            ("speed=0", "at least 1"),
            ("speed=10", "at most 9"),
            ("fast=yes", "true or false"),
            ("mode=z", "pick one of the choices"),
            ("ratio=x", "a number such as 1.5"),
        ] {
            let e = save(
                &root,
                "demo",
                &parse_assignments(&[text.into()]).unwrap(),
                false,
            )
            .unwrap_err();
            assert!(e.to_string().ends_with(why), "{text}: {e}");
        }
        let e = save(
            &root,
            "demo",
            &parse_assignments(&["x=1".into()]).unwrap(),
            false,
        )
        .unwrap_err();
        assert!(matches!(e, SettingsError::UnknownSetting { .. }));
        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn reset_deletes_only_kept_values() {
        let root = world("reset", INI);
        let edits = parse_assignments(&["speed=2".into()]).unwrap();
        save(&root, "demo", &edits, false).unwrap();
        let r = save(&root, "demo", &HashMap::new(), true).unwrap();
        assert_eq!(r.deleted, vec!["sco.settings.speed".to_string()]);
        let p = read_plugin(&root, "demo").unwrap();
        assert!(p.settings.iter().all(|s| !s.kept));
        assert_eq!(p.settings[0].value, "5");
        assert_eq!(
            save(&root, "demo", &HashMap::new(), true)
                .unwrap()
                .changed(),
            0
        );
        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn a_stored_value_of_another_type_or_out_of_range_is_ignored() {
        let root = world("stale", INI);
        let db = db_path(&root, "demo");
        std::fs::create_dir_all(db.parent().unwrap()).unwrap();
        let c = Connection::open(&db).unwrap();
        c.execute_batch(
            "CREATE TABLE sco_kv(key TEXT PRIMARY KEY NOT NULL, value BLOB NOT NULL) WITHOUT ROWID",
        )
        .unwrap();
        for (k, v) in [
            ("sco.settings.speed", &b"string:7"[..]),
            ("sco.settings.ratio", b"float:3"),
            ("sco.settings.mode", b"enum:c"),
        ] {
            c.execute("INSERT INTO sco_kv VALUES (?1, ?2)", params![k, v])
                .unwrap();
        }
        drop(c);
        let p = read_plugin(&root, "demo").unwrap();
        let get = |n: &str| p.settings.iter().find(|s| s.name == n).unwrap().clone();
        assert!(!get("speed").kept, "wrong type");
        assert!(!get("ratio").kept, "out of range");
        assert!(get("mode").kept);
        assert_eq!(get("mode").value, "c");
        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn ids_and_missing_plugins() {
        let root = world("ids", INI);
        assert!(matches!(
            read_plugin(&root, "../x"),
            Err(SettingsError::BadId(_))
        ));
        assert!(matches!(
            read_plugin(&root, "nope"),
            Err(SettingsError::NoPlugin(_))
        ));
        assert!(parse_assignments(&["novalue".into()]).is_err());
        assert!(parse_assignments(&["=1".into()]).is_err());
        assert_eq!(
            parse_assignments(&["note=a=b".into()]).unwrap()["note"],
            "a=b"
        );
        let _ = std::fs::remove_dir_all(&root);
    }
}
