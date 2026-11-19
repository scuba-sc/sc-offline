// SPDX-License-Identifier: GPL-3.0-only
//! Checks the generated `capabilities.json` (written by `tools/capabilities.py`) against the
//! constants in this crate. The file is read with `serde_json`.
//! Source and test evidence only. Nothing here was run in game.

use scol_contract::capability_env::{self, SWITCHES};
use scol_contract::{env, CAPABILITIES_JSON};
use serde_json::Value;
use std::collections::HashSet;

fn table() -> Value {
    serde_json::from_str(CAPABILITIES_JSON).expect("capabilities.json is valid JSON")
}

fn rows<'a>(t: &'a Value, key: &str) -> &'a Vec<Value> {
    t[key]
        .as_array()
        .unwrap_or_else(|| panic!("capabilities.json has no array {key}"))
}

fn text<'a>(row: &'a Value, key: &str) -> &'a str {
    row[key]
        .as_str()
        .unwrap_or_else(|| panic!("{row} has no string {key}"))
}

fn known_names() -> HashSet<&'static str> {
    let mut set: HashSet<&'static str> = env::PLAY_BLOCK.iter().copied().collect();
    set.insert(env::GUI);
    set.extend(SWITCHES.iter().map(|s| s.env));
    set.retain(|n| n.starts_with("SC_OFFLINE_") || n.starts_with("SCO_"));
    set
}

/// Every string in the document, depth first.
fn all_strings(v: &Value, out: &mut Vec<String>) {
    match v {
        Value::String(s) => out.push(s.clone()),
        Value::Array(a) => a.iter().for_each(|x| all_strings(x, out)),
        Value::Object(o) => o.values().for_each(|x| all_strings(x, out)),
        _ => {}
    }
}

#[test]
fn every_switch_name_is_a_known_name() {
    let t = table();
    let known = known_names();
    let mut seen = 0;
    for key in ["switches", "diagnostics"] {
        for row in rows(&t, key) {
            let name = text(row, "name");
            assert!(known.contains(name), "unknown switch name {name}");
            seen += 1;
        }
    }
    assert_eq!(seen, SWITCHES.len());
}

#[test]
fn capability_row_switches_are_listed_switches() {
    let t = table();
    let listed: HashSet<&str> = rows(&t, "switches")
        .iter()
        .map(|r| text(r, "name"))
        .collect();
    assert!(!listed.is_empty());
    let mut with_switch = 0;
    for row in rows(&t, "capabilities") {
        if let Some(s) = row["switch"].as_str() {
            assert!(
                listed.contains(s),
                "capability row names unlisted switch {s}"
            );
            with_switch += 1;
        }
    }
    assert!(with_switch > 0);
}

#[test]
fn json_agrees_with_the_switch_table() {
    let t = table();
    for s in &SWITCHES {
        let list = if s.diagnostic_only {
            "diagnostics"
        } else {
            "switches"
        };
        let row = rows(&t, list)
            .iter()
            .find(|r| r["name"] == s.env)
            .unwrap_or_else(|| panic!("{} is in scol-contract but not under {list}", s.env));
        assert_eq!(text(row, "ini_key"), s.ini_key, "{}", s.env);
        assert_eq!(
            text(row, "release_default"),
            if s.default_on { "on" } else { "off" },
            "{}",
            s.env
        );
        // Every note states the evidence: "Not run in game", or "Seen once in game" for the one switch the owner ran.
        let note = text(row, "note");
        assert!(
            note.contains("Not run in game") || note.contains("Seen once in game"),
            "{}",
            s.env
        );
    }
    assert_eq!(
        rows(&t, "switches").len() + rows(&t, "diagnostics").len(),
        SWITCHES.len()
    );
}

#[test]
fn switches_follow_the_owner_decision() {
    let t = table();
    // Every capability switch is on in release packages and the player can turn it off.
    let switches = rows(&t, "switches");
    assert_eq!(switches.len(), 5);
    for row in switches {
        assert_eq!(text(row, "release_default"), "on");
        assert_eq!(text(row, "on_value"), capability_env::ON_VALUE);
        assert_eq!(row["player_can_turn_off"], true);
        assert!(text(row, "ini_key").starts_with("exp_"));
    }
    // The diagnostic switch is off, is not a feature and says what it does.
    let diags = rows(&t, "diagnostics");
    assert_eq!(diags.len(), 1);
    assert_eq!(text(&diags[0], "name"), capability_env::ENTITIES_DIAG);
    assert_eq!(text(&diags[0], "ini_key"), "exp_entities_diag");
    assert_eq!(text(&diags[0], "release_default"), "off");
    let note = text(&diags[0], "note");
    assert!(note.contains("patch game functions"), "{note}");
    assert!(note.contains("log"), "{note}");
    assert!(note.contains("Not a capability"), "{note}");
    for required in [
        capability_env::LOADOUT_ASYNC,
        capability_env::INVENTORY_READ,
        capability_env::ACTOR_STATS_WRITE,
    ] {
        assert!(
            switches.iter().any(|r| r["name"] == required),
            "{required} missing"
        );
    }
    let mut strings = Vec::new();
    all_strings(&t, &mut strings);
    assert!(strings.iter().any(|s| s.contains("Not run in game")));
    // No label that suggests the switches are a lesser tier.
    assert!(
        strings
            .iter()
            .all(|s| !s.to_lowercase().contains("experimental")),
        "the word experimental is not used for these switches"
    );
}

#[test]
fn every_capability_env_switch_is_in_the_table() {
    // Reverse drift check: a switch added to the Rust constants must appear in the generated
    // table. The diagnostic switch is listed under "diagnostics" rather than "switches".
    let t = table();
    let listed: HashSet<&str> = rows(&t, "switches")
        .iter()
        .chain(rows(&t, "diagnostics"))
        .map(|r| text(r, "name"))
        .collect();
    for s in &SWITCHES {
        assert!(
            listed.contains(s.env),
            "{} is in scol-contract but not in capabilities.json",
            s.env
        );
    }
}
