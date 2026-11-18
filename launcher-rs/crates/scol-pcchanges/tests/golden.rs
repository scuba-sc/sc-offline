// SPDX-License-Identifier: GPL-3.0-only
//! Golden checks against the pc-changes record the C++ launcher wrote (`tests/golden/pc-changes`,
//! made by `tools/launcher-contract`). Synthetic only; nothing was run in game.

mod common;
use common::*;
use scol_contract::record;
use scol_pcchanges::fake::FakeSystem;
use scol_pcchanges::{apply_pc_changes, record_add, PcWanted};
use std::path::{Path, PathBuf};

fn golden() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/golden/pc-changes")
}

fn read(name: &str) -> String {
    let bytes = std::fs::read(golden().join(name)).unwrap();
    String::from_utf8(bytes).unwrap()
}

#[test]
fn replaying_every_golden_case_gives_the_golden_record() {
    let cases = read("cases.txt");
    let names: Vec<&str> = cases.lines().filter(|l| !l.is_empty()).collect();
    assert_eq!(names.len(), 6);
    for name in names {
        let mut rec = read(&format!("{name}.leftover"));
        let steps = read(&format!("{name}.steps"));
        let mut sys = FakeSystem::new();
        let mut out = Vec::new();
        for line in steps.lines() {
            let f: Vec<&str> = line.split('\t').collect();
            match f[0] {
                // ApplyPcChanges writes the time only when the leftover record has none.
                "time" if rec.contains("time=") => {}
                "time" => {
                    let n: Vec<u32> = f[1..].iter().map(|v| v.parse().unwrap()).collect();
                    let when = record::pc_time(n[0], n[1], n[2], n[3], n[4]);
                    record_add(&mut sys, &mut out, &mut rec, "time", &when);
                }
                "add" => record_add(&mut sys, &mut out, &mut rec, f[1], f[2]),
                other => panic!("unknown step {other} in {name}"),
            }
        }
        assert_eq!(rec, read(&format!("{name}.record")), "case {name}");
        // The record is written as it stands, byte for byte, to the real path.
        if !steps.is_empty() {
            let last = sys.text(RECORD);
            assert!(
                last.is_none() || last.as_deref() == Some(rec.as_str()),
                "{name}"
            );
        }
        assert!(text(out).is_empty());
    }
}

#[test]
fn a_fresh_run_with_everything_on_writes_the_golden_fresh_all_record() {
    let mut sys = machine();
    let mut out = Vec::new();
    let rc = apply_pc_changes(&mut sys, &mut out, &all_wanted(), GAME, false);
    assert_eq!(rc, 0, "{}", text(out));
    assert_eq!(sys.text(RECORD).unwrap(), read("fresh-all.record"));
}

#[test]
fn a_fresh_run_with_only_the_hosts_line_writes_the_golden_hosts_only_record() {
    let mut sys = machine();
    sys.time.year = 2026;
    sys.time.month = 1;
    sys.time.day = 2;
    sys.time.hour = 3;
    sys.time.minute = 4;
    let wanted = PcWanted {
        eac_hosts: true,
        ..PcWanted::default()
    };
    let mut out = Vec::new();
    assert_eq!(
        apply_pc_changes(&mut sys, &mut out, &wanted, GAME, false),
        0
    );
    assert_eq!(sys.text(RECORD).unwrap(), read("fresh-hosts-only.record"));
}

#[test]
fn a_leftover_record_keeps_its_time_line() {
    let mut sys = machine();
    sys.put(RECORD, &read("leftover-keeps-time.leftover"));
    // The firewall rule of the leftover record is still in place.
    sys.rules.insert(R_GAME.to_string());
    let wanted = PcWanted {
        firewall: true,
        eac_hosts: true,
        ..PcWanted::default()
    };
    let mut out = Vec::new();
    assert_eq!(
        apply_pc_changes(&mut sys, &mut out, &wanted, GAME, false),
        0
    );
    let rec = sys.text(RECORD).unwrap();
    assert!(
        rec.starts_with("time=2026-11-16 20:00\nfirewall=added\n"),
        "{rec}"
    );
    assert_eq!(rec.matches("time=").count(), 1);
    assert!(text(out)
        .contains("[i] firewall rule \"sc-offline: block StarCitizen.exe\" already in place\n"));
}
