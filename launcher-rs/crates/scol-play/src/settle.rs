// SPDX-License-Identifier: GPL-3.0-only
//! `SettlePreviousUpdate` and `RollBackUpdate`: at every start of `play`, finish (clean up) or roll back
//! whatever an update that did not end left behind, from its journal `data\update\applied.txt`.
//! The update itself (download, verify, swap) is not part of this crate.

use scol_install::fsops::{delete_file, move_file};
use std::io::Write;

fn journal(here: &str) -> String {
    format!("{here}\\data\\update\\applied.txt")
}

fn is_file(p: &str) -> bool {
    std::fs::metadata(p).map(|m| m.is_file()).unwrap_or(false)
}

/// Undoes an unfinished swap from its journal: `R <rel>` means the old file was moved to
/// `<rel>.update-old`, `N <rel>` means a new file was created where none was. Newest first.
pub fn roll_back_update(here: &str) {
    let Ok(j) = std::fs::read_to_string(journal(here)) else {
        return;
    };
    for line in j.split('\n').rev() {
        if line.len() < 3 {
            continue;
        }
        let path = format!("{here}\\{}", &line[2..]);
        let _ = delete_file(&format!("{path}.update-new"));
        if line.starts_with('N') {
            let _ = delete_file(&path);
        } else if line.starts_with('R') && is_file(&format!("{path}.update-old")) {
            // A running new sc-offline.exe can't be deleted, but it can be renamed out of the way.
            if is_file(&path) && delete_file(&path).is_err() {
                let _ = move_file(&path, &format!("{path}.update-failed"), true);
            }
            let _ = move_file(&format!("{path}.update-old"), &path, true);
        }
    }
}

/// Run at every start of `play`: finish or roll back what a previous update left.
pub fn settle_previous_update(here: &str, out: &mut dyn Write) {
    let Ok(j) = std::fs::read_to_string(journal(here)) else {
        return;
    };
    if !j.ends_with("done\n") {
        let _ = writeln!(
            out,
            "[!] a previous update didn't finish; putting the old files back"
        );
        roll_back_update(here);
    }
    let mut leftover = false;
    for line in j.split('\n') {
        if line.len() > 2 && line.starts_with('R') {
            let old = format!("{here}\\{}.update-old", &line[2..]);
            if is_file(&old) && delete_file(&old).is_err() {
                leftover = true; // the old exe can still be locked for a moment
            }
        }
    }
    if !leftover {
        let _ = std::fs::remove_dir_all(format!("{here}\\data\\update"));
    }
}
