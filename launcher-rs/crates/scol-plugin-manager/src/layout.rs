// SPDX-License-Identifier: GPL-3.0-only
//! Where things live under the launcher folder. Everything this crate writes is under
//! `data/plugins/<id>/` or `data/launcher/`; the one exception is `remove --purge`, which deletes
//! the plugin's own settings store `data/storage/<id>.db` (and its sidecars) on request.

use std::path::{Path, PathBuf};

/// Files the plugin settings store may leave next to `<id>.db`.
pub const STORAGE_SUFFIXES: [&str; 4] = [".db", ".db-wal", ".db-shm", ".db-journal"];

#[derive(Debug, Clone)]
pub struct Layout {
    pub root: PathBuf,
}

impl Layout {
    pub fn new(root: &Path) -> Layout {
        Layout {
            root: root.to_path_buf(),
        }
    }
    pub fn data(&self) -> PathBuf {
        self.root.join("data")
    }
    pub fn plugins(&self) -> PathBuf {
        self.data().join("plugins")
    }
    pub fn plugin_dir(&self, id: &str) -> PathBuf {
        self.plugins().join(id)
    }
    pub fn launcher(&self) -> PathBuf {
        self.data().join("launcher")
    }
    pub fn staging(&self, id: &str) -> PathBuf {
        self.launcher().join("staging").join(id)
    }
    pub fn installed_json(&self) -> PathBuf {
        self.launcher().join("installed.json")
    }
    pub fn catalogue_state(&self) -> PathBuf {
        self.launcher().join("catalogue-state.json")
    }
    pub fn journal(&self) -> PathBuf {
        self.launcher().join("plugin-journal.json")
    }
    pub fn storage_files(&self, id: &str) -> Vec<PathBuf> {
        STORAGE_SUFFIXES
            .iter()
            .map(|s| self.data().join("storage").join(format!("{id}{s}")))
            .collect()
    }
}

/// The plugin id grammar of the host: 1 to 31 of `a-z`, `0-9`, `_`.
pub fn valid_id(s: &str) -> bool {
    !s.is_empty()
        && s.len() <= 31
        && s.bytes()
            .all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == b'_')
}

/// Seconds since 1970 as `YYYY-MM-DDTHH:MM:SSZ`.
pub fn utc_text(unix: i64) -> String {
    let days = unix.div_euclid(86_400);
    let secs = unix.rem_euclid(86_400);
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ids() {
        assert!(valid_id("creative"));
        assert!(valid_id("a_1"));
        for bad in ["", "Creative", "a-b", "a.b", "../x", &"a".repeat(32), "a b"] {
            assert!(!valid_id(bad), "{bad}");
        }
    }

    #[test]
    fn time() {
        assert_eq!(utc_text(0), "1970-01-01T00:00:00Z");
        assert_eq!(utc_text(1_795_000_000), "2026-11-18T11:06:40Z");
    }
}
