// SPDX-License-Identifier: GPL-3.0-only
//! Runs the Rust verifier and the plugin.ini parser on the shared corpus in
//! `launcher-rs/tests/zip-corpus`. The expected verdicts live in `expected.tsv` and were written
//! by hand. `tests/zip-corpus/run_python.py` runs the SDK Python tool on the same files and must
//! give the same verdicts. Not run in game.

use std::collections::BTreeSet;
use std::path::PathBuf;

use scol_plugins::manifest::{self, HostApi};
use scol_plugins::verify;

/// The plugin API of the SDK the corpus was checked against.
const SDK_API: HostApi = HostApi { major: 1, minor: 1 };

struct Row {
    kind: String,
    name: String,
    verdict: String,
    code: String,
}

fn corpus_dir() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/zip-corpus")
}

fn rows() -> Vec<Row> {
    let text = std::fs::read_to_string(corpus_dir().join("expected.tsv")).expect("expected.tsv");
    let mut out = Vec::new();
    for line in text.lines() {
        if line.starts_with('#') || line.is_empty() {
            continue;
        }
        let f: Vec<&str> = line.split('\t').collect();
        assert_eq!(f.len(), 6, "bad row: {line}");
        out.push(Row {
            kind: f[0].to_string(),
            name: f[1].to_string(),
            verdict: f[2].to_string(),
            code: f[3].to_string(),
        });
    }
    out
}

#[test]
fn every_zip_in_the_corpus_matches_expected() {
    let mut failures = Vec::new();
    let mut count = 0;
    for r in rows().iter().filter(|r| r.kind == "zip") {
        count += 1;
        let path = corpus_dir().join("cases").join(format!("{}.zip", r.name));
        let got = verify::verify_archive(&path);
        match (r.verdict.as_str(), got) {
            ("ok", Ok(_)) => {}
            ("ok", Err(e)) => failures.push(format!(
                "{}: expected ok, got {}: {}",
                r.name,
                e.code.as_str(),
                e.message
            )),
            ("refuse", Ok(_)) => {
                failures.push(format!("{}: expected refuse {}, got ok", r.name, r.code))
            }
            ("refuse", Err(e)) => {
                if e.code.as_str() != r.code {
                    failures.push(format!(
                        "{}: expected code {}, got {}: {}",
                        r.name,
                        r.code,
                        e.code.as_str(),
                        e.message
                    ));
                }
            }
            (v, _) => failures.push(format!("{}: bad verdict {v}", r.name)),
        }
    }
    assert!(count >= 100, "corpus looks truncated: {count} zips");
    assert!(
        failures.is_empty(),
        "{} of {count} zips differ:\n{}",
        failures.len(),
        failures.join("\n")
    );
}

#[test]
fn every_manifest_in_the_corpus_matches_expected() {
    let mut failures = Vec::new();
    let mut count = 0;
    for r in rows().iter().filter(|r| r.kind == "ini") {
        count += 1;
        let path = corpus_dir()
            .join("manifests")
            .join(format!("{}.ini", r.name));
        let bytes = std::fs::read(&path).expect("manifest file");
        // Syntax and field rules only. Folder identity and the host API bound are checked by
        // the zip verifier and by `host_rules_on_corpus_manifests` below.
        let got = manifest::parse(&bytes, None).map(|_| ());
        match (r.verdict.as_str(), got) {
            ("ok", Ok(())) | ("refuse", Err(_)) => {}
            ("ok", Err(e)) => failures.push(format!("{}: expected ok, got {e}", r.name)),
            ("refuse", Ok(())) => failures.push(format!("{}: expected refuse, got ok", r.name)),
            (v, _) => failures.push(format!("{}: bad verdict {v}", r.name)),
        }
    }
    assert!(count >= 100, "corpus looks truncated: {count} manifests");
    assert!(
        failures.is_empty(),
        "{} of {count} manifests differ:\n{}",
        failures.len(),
        failures.join("\n")
    );
}

#[test]
fn corpus_files_and_expected_rows_match() {
    let rows = rows();
    for (sub, ext, kind) in [("cases", "zip", "zip"), ("manifests", "ini", "ini")] {
        let listed: BTreeSet<String> = rows
            .iter()
            .filter(|r| r.kind == kind)
            .map(|r| format!("{}.{ext}", r.name))
            .collect();
        let on_disk: BTreeSet<String> = std::fs::read_dir(corpus_dir().join(sub))
            .expect("corpus folder")
            .map(|e| e.unwrap().file_name().to_string_lossy().into_owned())
            .collect();
        assert_eq!(listed, on_disk, "{sub}: files and expected.tsv rows differ");
    }
}

/// The SDK checker also applies the host API bound. `ok-api-max` is the one corpus manifest that
/// passes the syntax rules and fails that bound.
#[test]
fn host_rules_on_corpus_manifests() {
    for r in rows()
        .iter()
        .filter(|r| r.kind == "ini" && r.verdict == "ok")
    {
        let path = corpus_dir()
            .join("manifests")
            .join(format!("{}.ini", r.name));
        let bytes = std::fs::read(&path).expect("manifest file");
        let with_host = manifest::parse(&bytes, Some(SDK_API));
        if r.name == "ok-api-max" {
            assert!(
                with_host.is_err(),
                "api 65535.65535 must fail the host bound"
            );
        } else {
            assert!(with_host.is_ok(), "{}: {:?}", r.name, with_host.err());
        }
    }
}
