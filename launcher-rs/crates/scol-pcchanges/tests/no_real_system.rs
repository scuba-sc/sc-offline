// SPDX-License-Identifier: GPL-3.0-only
//! Guard: no test in the workspace may build the real system implementation. It runs netsh as
//! administrator, edits the real hosts file and renames the real EAC exe. Synthetic only.

use std::path::{Path, PathBuf};

fn rs_files(dir: &Path, out: &mut Vec<PathBuf>) {
    let Ok(rd) = std::fs::read_dir(dir) else {
        return;
    };
    for e in rd.flatten() {
        let p = e.path();
        if p.is_dir() {
            rs_files(&p, out);
        } else if p.extension().is_some_and(|x| x == "rs") {
            out.push(p);
        }
    }
}

#[test]
fn no_test_names_the_real_system() {
    let crates = Path::new(env!("CARGO_MANIFEST_DIR")).join("..");
    let this = Path::new(file!()).file_name().unwrap().to_owned();
    // Needles are built in pieces so this file does not match itself.
    let needles = [
        format!("{}{}", "Real", "System"),
        format!("{}{}{}", "scol_pcchanges", "::", "real"),
        format!("{}{}{}", "real", "-", "system"),
    ];
    let mut bad = Vec::new();
    let mut seen = 0;
    for c in std::fs::read_dir(&crates).unwrap().flatten() {
        let mut files = Vec::new();
        rs_files(&c.path().join("tests"), &mut files);
        for f in files {
            if f.file_name() == Some(&this) {
                continue;
            }
            seen += 1;
            let text = std::fs::read_to_string(&f).unwrap_or_default();
            if needles.iter().any(|n| text.contains(n.as_str())) {
                bad.push(f);
            }
        }
    }
    assert!(seen > 0, "no test files found; the scan is broken");
    assert!(
        bad.is_empty(),
        "tests must not use the real system: {bad:?}"
    );
}
