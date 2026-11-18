// SPDX-License-Identifier: GPL-3.0-only
//! Checks the generated `capabilities.json` without a JSON dependency: the file is written by
//! `tools/capabilities.py` with one `"key": value` pair per line, so a line scan is enough.
//! Source and test evidence only. Nothing here was run in game.

use scol_contract::{capability_env, env, CAPABILITIES_JSON};
use std::collections::HashSet;

/// Every `"key": "value"` string on its own line, in file order.
fn string_values(key: &str) -> Vec<String> {
    let prefix = format!("\"{key}\": \"");
    CAPABILITIES_JSON
        .lines()
        .filter_map(|l| {
            let t = l.trim_start();
            let rest = t.strip_prefix(&prefix)?;
            Some(rest.trim_end_matches(',').trim_end_matches('"').to_string())
        })
        .collect()
}

fn known_names() -> HashSet<&'static str> {
    let mut set: HashSet<&'static str> = env::PLAY_BLOCK.iter().copied().collect();
    set.insert(env::GUI);
    set.extend([
        capability_env::LOADOUT_ASYNC,
        capability_env::INVENTORY_READ,
        capability_env::ACTOR_STATS_WRITE,
        capability_env::CVARS_NATIVE,
        capability_env::AUDIO_NATIVE,
        capability_env::ENTITIES_DIAG,
    ]);
    set.retain(|n| n.starts_with("SC_OFFLINE_") || n.starts_with("SCO_"));
    set
}

#[test]
fn every_switch_name_is_a_known_name() {
    let known = known_names();
    let mut seen = 0;
    // "switch" on capability rows, "name" on the switch and diagnostic lists.
    for key in ["switch", "name"] {
        for v in string_values(key) {
            if v.starts_with("SCO_") || v.starts_with("SC_OFFLINE_") {
                assert!(known.contains(v.as_str()), "unknown switch name {v}");
                seen += 1;
            }
        }
    }
    assert!(seen > 0, "no switch names found in capabilities.json");
}

#[test]
fn capability_row_switches_are_listed_switches() {
    let listed: HashSet<String> = CAPABILITIES_JSON
        .split("\"diagnostics\"")
        .next()
        .unwrap()
        .lines()
        .filter_map(|l| {
            l.trim_start()
                .strip_prefix("\"name\": \"")
                .map(|r| r.trim_end_matches(',').trim_end_matches('"').to_string())
        })
        .collect();
    assert!(!listed.is_empty());
    for s in string_values("switch") {
        assert!(
            listed.contains(&s),
            "capability row names unlisted switch {s}"
        );
    }
}

#[test]
fn switches_follow_the_owner_decision() {
    // Every listed switch is on in release packages and the player can turn it off.
    let on = string_values("release_default");
    assert!(on.iter().filter(|v| *v == "on").count() >= 3);
    assert!(CAPABILITIES_JSON.contains("\"player_can_turn_off\": true"));
    assert!(!CAPABILITIES_JSON.contains("\"player_can_turn_off\": false"));
    assert!(!CAPABILITIES_JSON.to_lowercase().contains("experimental"));
    for required in [
        capability_env::LOADOUT_ASYNC,
        capability_env::INVENTORY_READ,
        capability_env::ACTOR_STATS_WRITE,
    ] {
        assert!(
            CAPABILITIES_JSON.contains(&format!("\"name\": \"{required}\"")),
            "{required} missing"
        );
    }
    assert!(CAPABILITIES_JSON.contains("Not run in game"));
}

#[test]
fn every_capability_env_switch_is_in_the_table() {
    // Reverse drift check: a switch added to the Rust constants must appear in the generated
    // table. ENTITIES_DIAG is a diagnostic, listed under "diagnostics" rather than "switches".
    for name in [
        capability_env::LOADOUT_ASYNC,
        capability_env::INVENTORY_READ,
        capability_env::ACTOR_STATS_WRITE,
        capability_env::CVARS_NATIVE,
        capability_env::AUDIO_NATIVE,
        capability_env::ENTITIES_DIAG,
    ] {
        assert!(
            CAPABILITIES_JSON.contains(&format!("\"name\": \"{name}\"")),
            "{name} is in scol-contract but not in capabilities.json"
        );
    }
}
