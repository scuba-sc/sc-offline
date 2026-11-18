// SPDX-License-Identifier: GPL-3.0-only
//! The capability switches as launcher settings.
//!
//! Owner decision 2026-11-17: every capability switch is ON by default in every release package, each one
//! is a visible setting the player can turn OFF, and its text says it has not been run in game.
//! `SCO_ENTITIES_DIAG` is diagnostic hooks that patch game functions to write log lines; it is not a
//! feature and is OFF by default. A switch is on for the game only when the environment value is exactly
//! `1`. An OFF switch is left out of the game's environment (the launcher removes an inherited value), so
//! turning a switch off in the launcher also beats a value set in the player's own shell.

use scol_contract::capability_env::{Switch, ON_VALUE, SWITCHES};

/// On or off for each entry of [`SWITCHES`], in that order.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Capabilities {
    on: [bool; SWITCHES.len()],
}

impl Default for Capabilities {
    /// Five capability switches on, the diagnostic switch off. See the module text.
    fn default() -> Self {
        let mut on = [false; SWITCHES.len()];
        for (slot, s) in on.iter_mut().zip(SWITCHES.iter()) {
            *slot = s.default_on;
        }
        Self { on }
    }
}

impl Capabilities {
    fn index(switch: &Switch) -> usize {
        SWITCHES
            .iter()
            .position(|s| s.env == switch.env)
            .expect("switch is in the table")
    }

    pub fn is_on(&self, switch: &Switch) -> bool {
        self.on[Self::index(switch)]
    }

    pub fn set(&mut self, switch: &Switch, on: bool) {
        self.on[Self::index(switch)] = on;
    }

    /// The switch with this `sc-offline.ini` key, ignoring ASCII case.
    pub fn switch_for_key(key: &str) -> Option<&'static Switch> {
        SWITCHES
            .iter()
            .find(|s| s.ini_key.eq_ignore_ascii_case(key))
    }

    /// `(environment name, value)` for every switch: the value is `1` when on and empty (the
    /// variable is removed) when off.
    pub fn env(&self) -> Vec<(&'static str, String)> {
        SWITCHES
            .iter()
            .map(|s| {
                (
                    s.env,
                    if self.is_on(s) {
                        ON_VALUE.to_string()
                    } else {
                        String::new()
                    },
                )
            })
            .collect()
    }

    /// The line the launcher shows next to a switch. It always says the switch was not run in
    /// game, whatever the evidence level.
    pub fn status_text(switch: &Switch) -> String {
        format!(
            "{} Evidence: {}. {}",
            switch.capability,
            switch.evidence,
            scol_contract::capability_env::NOT_RUN_IN_GAME
        )
    }
}
