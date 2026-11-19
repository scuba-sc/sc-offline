// SPDX-License-Identifier: GPL-3.0-only
//! The Capabilities page in plain words. The model behind the page: which text goes with each
//! switch, how the evidence is put to a player, and the read-only rows of `capabilities.json` that
//! have no switch (grouped as "Works by itself" and "Not available yet").
//!
//! Raw identifiers (`exp_loadout_async`, `game.loadout.async`, `synthetic_only`) never appear in
//! the main text. They are in the tooltip and in the collapsed Details row (`details`). The unit test
//! `main_text_has_no_raw_identifiers` walks every string the page shows without Details.
//!
//! Evidence put to the player comes from the owner's in-game sessions on build 4.10.200.58746:
//! gear equip through `game.loadout.async` attached items; prefab placement, ship spawn, boarding and
//! Flight Ready worked; the seat probe logged Enter and Exit; a saved-spot move moved the player
//! (arrival was not confirmed). Everything else is "not run in game yet". These lines are text only:
//! no default changes because of them.

use std::path::Path;

use scol_contract::capability_env::{Switch, SWITCHES};

use crate::i18n::{tr, trf, Lang};

/// How well a capability is known to work, for the one-line evidence note.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Evidence {
    /// Seen working in game once (owner sessions, one or two each).
    SeenOnce,
    /// Moved the player in game, arrival not confirmed.
    PartlySeen,
    /// Found in the game files by the signature check; not run in game.
    FileOnly,
    /// Not run in game yet.
    NotRun,
    /// A bug-report tool, not a game feature.
    Diagnostic,
}

impl Evidence {
    pub fn key(self) -> &'static str {
        match self {
            Evidence::SeenOnce => "launcher.ui.caps.ev.seen_once",
            Evidence::PartlySeen => "launcher.ui.caps.ev.moves",
            Evidence::FileOnly => "launcher.ui.caps.ev.file",
            Evidence::NotRun => "launcher.ui.caps.ev.not_run",
            Evidence::Diagnostic => "launcher.ui.caps.ev.diag",
        }
    }

    pub fn is_good(self) -> bool {
        matches!(self, Evidence::SeenOnce)
    }
}

/// The short id of a switch: its ini key without `exp_`.
pub fn short(sw: &Switch) -> &str {
    sw.ini_key.strip_prefix("exp_").unwrap_or(sw.ini_key)
}

pub fn switch_name(lang: Lang, sw: &Switch) -> String {
    tr(lang, &format!("launcher.ui.cap.{}.name", short(sw)))
}

pub fn switch_about(lang: Lang, sw: &Switch) -> String {
    tr(lang, &format!("launcher.ui.cap.{}.about", short(sw)))
}

pub fn switch_evidence(sw: &Switch) -> Evidence {
    if sw.diagnostic_only {
        Evidence::Diagnostic
    } else if sw.ini_key == "exp_loadout_async" {
        Evidence::SeenOnce
    } else {
        Evidence::NotRun
    }
}

/// The technical lines of a switch, for the collapsed Details row and the tooltip.
pub fn switch_details(lang: Lang, sw: &Switch) -> Vec<String> {
    vec![
        trf(lang, "launcher.ui.caps.detail.key", &[("key", sw.ini_key)]),
        trf(
            lang,
            "launcher.ui.caps.detail.cap",
            &[("capability", sw.capability)],
        ),
        trf(
            lang,
            "launcher.ui.caps.detail.level",
            &[("level", sw.evidence)],
        ),
    ]
}

/// A row of `capabilities.json`.
#[derive(Debug, Clone)]
pub struct RegistryRow {
    pub capability: String,
    pub level: String,
    /// `ON` or `OFF`
    pub default: String,
    pub reason: String,
    /// The row has a launcher switch (`SCO_*`).
    pub has_switch: bool,
}

/// All rows of `capabilities.json` (the one next to the launcher wins over the built-in table).
pub fn registry_rows(here: &Path) -> Vec<RegistryRow> {
    let text = std::fs::read_to_string(here.join("capabilities.json"))
        .unwrap_or_else(|_| scol_contract::CAPABILITIES_JSON.to_string());
    let Ok(v) = serde_json::from_str::<serde_json::Value>(&text) else {
        return Vec::new();
    };
    v.get("capabilities")
        .and_then(|c| c.as_array())
        .map(|rows| {
            rows.iter()
                .map(|r| {
                    let g = |k: &str| r.get(k).and_then(|x| x.as_str()).unwrap_or("").to_string();
                    RegistryRow {
                        capability: g("capability"),
                        level: g("evidence_level"),
                        default: g("registry_default"),
                        reason: g("reason"),
                        has_switch: r.get("switch").is_some_and(|s| !s.is_null()),
                    }
                })
                .collect()
        })
        .unwrap_or_default()
}

/// A read-only row as the player sees it.
#[derive(Debug, Clone)]
pub struct PlainRow {
    /// The technical name, for the tooltip and Details only.
    pub capability: String,
    pub level: String,
    pub name: String,
    pub evidence: Evidence,
    pub reason_key: &'static str,
}

/// Capability words made readable: `game.world.free_camera` becomes `World: free camera`.
pub fn plain_name(capability: &str) -> String {
    let c = capability.strip_prefix("game.").unwrap_or(capability);
    let text = c
        .split('.')
        .map(|p| p.replace('_', " "))
        .collect::<Vec<_>>()
        .join(": ");
    let mut chars = text.chars();
    match chars.next() {
        Some(f) => f.to_uppercase().collect::<String>() + chars.as_str(),
        None => text,
    }
}

fn row_evidence(capability: &str, level: &str) -> Evidence {
    match capability {
        "game.entities.prefabs"
        | "spawn.ship"
        | "game.vehicles.flight_ready"
        | "game.vehicles.seat" => Evidence::SeenOnce,
        "teleport" => Evidence::PartlySeen,
        _ if level == "sigcheck_resolved" => Evidence::FileOnly,
        _ => Evidence::NotRun,
    }
}

fn reason_key(r: &RegistryRow) -> &'static str {
    if r.default == "ON" {
        "launcher.ui.caps.why.auto"
    } else if r.reason.contains("refuses") {
        "launcher.ui.caps.why.blocked"
    } else if r.level == "off_pending_probe" {
        "launcher.ui.caps.why.waiting"
    } else {
        "launcher.ui.caps.why.tests_only"
    }
}

/// The rows without a switch: (works by itself, not available yet). Rows that belong to a switch are
/// covered by that switch and left out.
pub fn plain_rows(here: &Path) -> (Vec<PlainRow>, Vec<PlainRow>) {
    let (mut auto, mut later) = (Vec::new(), Vec::new());
    for r in registry_rows(here).iter().filter(|r| !r.has_switch) {
        let row = PlainRow {
            capability: r.capability.clone(),
            level: r.level.clone(),
            name: plain_name(&r.capability),
            evidence: row_evidence(&r.capability, &r.level),
            reason_key: reason_key(r),
        };
        if r.default == "ON" {
            auto.push(row);
        } else {
            later.push(row);
        }
    }
    (auto, later)
}

/// The technical lines of a read-only row.
pub fn row_details(lang: Lang, r: &PlainRow) -> String {
    format!(
        "{}  |  {}",
        trf(
            lang,
            "launcher.ui.caps.detail.cap",
            &[("capability", &r.capability)]
        ),
        trf(
            lang,
            "launcher.ui.caps.detail.level",
            &[("level", &r.level)]
        )
    )
}

/// Every string the page shows without opening Details or a tooltip, for one language. The wording
/// test checks these.
pub fn main_text(lang: Lang, here: &Path) -> Vec<String> {
    let mut out = vec![
        tr(lang, "launcher.ui.caps.intro"),
        tr(lang, "launcher.ui.caps.group.switches"),
        tr(lang, "launcher.ui.caps.group.auto"),
        tr(lang, "launcher.ui.caps.group.later"),
        tr(lang, "launcher.ui.caps.details"),
        tr(lang, "launcher.plugins.state_on"),
        tr(lang, "launcher.plugins.state_off"),
    ];
    for sw in &SWITCHES {
        out.push(switch_name(lang, sw));
        out.push(switch_about(lang, sw));
        out.push(tr(lang, switch_evidence(sw).key()));
    }
    let (auto, later) = plain_rows(here);
    for r in auto.iter().chain(later.iter()) {
        out.push(r.name.clone());
        out.push(tr(lang, r.evidence.key()));
        out.push(tr(lang, r.reason_key));
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    const RAW: [&str; 13] = [
        "exp_",
        "_async",
        "_native",
        "synthetic_only",
        "off_pending_probe",
        "sigcheck",
        "SCO_",
        "capabilities.json",
        "capability_env",
        "diag ",
        "cvars",
        "_",
        "->",
    ];

    /// The switch with this ini key. Panics for a key that is not in the table.
    fn find(key: &str) -> &'static Switch {
        SWITCHES
            .iter()
            .find(|s| s.ini_key == key)
            .expect("a switch of the table")
    }

    #[test]
    fn main_text_has_no_raw_identifiers() {
        let here = Path::new("D:/no/such/install");
        for lang in [Lang::En, Lang::Ru] {
            let text = main_text(lang, here);
            assert!(text.len() > 100, "the table has 71 rows");
            for line in &text {
                assert!(!line.is_empty());
                for raw in RAW {
                    assert!(
                        !line.contains(raw),
                        "{lang:?}: raw text {raw:?} in the main text: {line:?}"
                    );
                }
                // No dotted identifier such as game.world.camera (a letter, a dot, a letter).
                let chars: Vec<char> = line.chars().collect();
                for w in chars.windows(3) {
                    assert!(
                        !(w[0].is_alphanumeric() && w[1] == '.' && w[2].is_lowercase()),
                        "{lang:?}: dotted identifier in the main text: {line:?}"
                    );
                }
                // Keys of the text table must never leak either.
                assert!(
                    !line.starts_with("launcher."),
                    "{lang:?}: missing text {line:?}"
                );
            }
        }
    }

    #[test]
    fn every_switch_has_a_name_and_a_sentence_in_both_languages() {
        for lang in [Lang::En, Lang::Ru] {
            for sw in &SWITCHES {
                let n = switch_name(lang, sw);
                let a = switch_about(lang, sw);
                assert!(
                    !n.starts_with("launcher."),
                    "{lang:?} name of {}",
                    sw.ini_key
                );
                assert!(
                    !a.starts_with("launcher."),
                    "{lang:?} sentence of {}",
                    sw.ini_key
                );
                assert!(a.ends_with('.') || a.ends_with('。'), "one sentence: {a}");
            }
        }
    }

    #[test]
    fn gear_switch_says_seen_once_and_the_rest_not_run() {
        let ev = |k: &str| switch_evidence(find(k));
        assert_eq!(ev("exp_loadout_async"), Evidence::SeenOnce);
        assert_eq!(ev("exp_inventory_read"), Evidence::NotRun);
        assert_eq!(ev("exp_actor_stats_write"), Evidence::NotRun);
        assert_eq!(ev("exp_cvars_native"), Evidence::NotRun);
        assert_eq!(ev("exp_audio_native"), Evidence::NotRun);
        assert_eq!(ev("exp_entities_diag"), Evidence::Diagnostic);
        assert_eq!(
            tr(Lang::En, Evidence::SeenOnce.key()),
            "Seen working in game once"
        );
        assert_eq!(tr(Lang::En, Evidence::NotRun.key()), "Not run in game yet");
    }

    #[test]
    fn plain_names_read_like_words() {
        assert_eq!(plain_name("game.world.free_camera"), "World: free camera");
        assert_eq!(plain_name("teleport"), "Teleport");
        assert_eq!(plain_name("game.private_session"), "Private session");
    }

    #[test]
    fn rows_without_a_switch_are_grouped() {
        let (auto, later) = plain_rows(Path::new("D:/no/such/install"));
        assert!(!auto.is_empty() && !later.is_empty());
        assert!(later
            .iter()
            .all(|r| r.reason_key != "launcher.ui.caps.why.auto"));
        assert!(auto
            .iter()
            .all(|r| r.reason_key == "launcher.ui.caps.why.auto"));
        // The cvar-write refusal is put as "blocked on purpose".
        assert!(later
            .iter()
            .any(|r| r.reason_key == "launcher.ui.caps.why.blocked"));
    }
}
