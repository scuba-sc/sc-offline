// SPDX-License-Identifier: GPL-3.0-only
//! The environment block `play` sets for the game process.
//!
//! `play_env` is the `SC_OFFLINE_*` block the C++ launcher sets (`PlayEnv` in
//! `launcher/contract_text.h`). `capability_env` and `apply_capabilities` are the capability
//! switches (`ApplyExperimentalEnv` in `launcher/launcher.cpp`); `full_env` is both blocks.
//!
//! An empty value means the variable is removed from the child's environment (`SetVar` in the
//! C++ launcher), so a value the player set in their shell does not leak through.

use crate::capabilities::Capabilities;
use crate::config::Config;
use scol_contract::capability_env::{ON_VALUE, SWITCHES};
use scol_contract::env as names;

/// One variable for the game process. An empty `value` means "remove it".
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EnvVar {
    pub name: &'static str,
    pub value: String,
}

impl EnvVar {
    fn new(name: &'static str, value: impl Into<String>) -> Self {
        Self {
            name,
            value: value.into(),
        }
    }

    pub fn is_unset(&self) -> bool {
        self.value.is_empty()
    }
}

/// Folders the block points at.
#[derive(Debug, Clone, Copy)]
pub struct PlayPaths<'a> {
    /// `<launcher folder>\data`
    pub data: &'a str,
    /// `<channel>\user\client\0`, the game's user folder.
    pub user_dir: &'a str,
}

fn on_off(b: bool) -> &'static str {
    if b {
        "on"
    } else {
        "off"
    }
}

/// The 15 variables of `play`, in the order the C++ launcher sets them.
pub fn play_env(c: &Config, paths: PlayPaths<'_>) -> Vec<EnvVar> {
    let data = paths.data;
    vec![
        EnvVar::new(names::BOOT_MAP, c.boot_map.as_str()),
        EnvVar::new(names::MOD_LOG, format!("{data}\\mod.log")),
        EnvVar::new(names::SPAWN_FILE, format!("{data}\\spawn.txt")),
        EnvVar::new(names::SHIPS_FILE, format!("{data}\\ships.txt")),
        EnvVar::new(names::START, c.start.as_str()),
        EnvVar::new(names::START_SHIP, c.start_ship.as_str()),
        EnvVar::new(names::PLUGINS, on_off(c.plugins)),
        EnvVar::new(names::RELOCATION, on_off(c.relocation)),
        EnvVar::new(names::MULTIPLAYER, on_off(c.multiplayer)),
        EnvVar::new(names::MULTIPLAYER_ALLOW, c.multiplayer_allow.as_str()),
        EnvVar::new(names::ASOP, on_off(c.asop)),
        EnvVar::new(names::ASOP_FLEET_LIST, c.asop_fleet_list.as_str()),
        EnvVar::new(names::SC_USER, paths.user_dir),
        EnvVar::new(names::REGISTRY_PROBE, c.registry_probe.as_str()),
        EnvVar::new(names::MENU, c.menu.word()),
    ]
}

/// The capability switches: `1` for each one that is on, removed for each one that is off.
pub fn capability_env(caps: &Capabilities) -> Vec<EnvVar> {
    caps.env()
        .into_iter()
        .map(|(name, value)| EnvVar { name, value })
        .collect()
}

/// What `ApplyExperimentalEnv` of the C++ launcher produces for a game process.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CapabilityApply {
    /// `1` for each switch whose key is on, an empty value (remove it) for each one that is off.
    pub env: Vec<EnvVar>,
    /// The switches that are on, as the launcher prints them:
    /// `exp_loadout_async (SCO_LOADOUT_ASYNC=1), ...`. Empty when none is on.
    pub on_summary: String,
    /// The variables that were present in the inherited environment although their key is off,
    /// comma separated. A variable that is present with an empty value counts as present.
    pub removed: String,
}

/// The capability block together with the two texts the C++ launcher prints. `inherited` says
/// whether a variable is present in the environment the launcher itself runs in.
pub fn apply_capabilities(
    caps: &Capabilities,
    inherited: &dyn Fn(&str) -> bool,
) -> CapabilityApply {
    let mut on_summary = String::new();
    let mut removed = String::new();
    for s in &SWITCHES {
        if caps.is_on(s) {
            if !on_summary.is_empty() {
                on_summary.push_str(", ");
            }
            on_summary.push_str(&format!("{} ({}={})", s.ini_key, s.env, ON_VALUE));
        } else if inherited(s.env) {
            if !removed.is_empty() {
                removed.push_str(", ");
            }
            removed.push_str(s.env);
        }
    }
    CapabilityApply {
        env: capability_env(caps),
        on_summary,
        removed,
    }
}

/// `play_env` followed by `capability_env`.
pub fn full_env(c: &Config, paths: PlayPaths<'_>) -> Vec<EnvVar> {
    let mut v = play_env(c, paths);
    v.extend(capability_env(&c.capabilities));
    v
}

/// The golden text of a block: one `NAME=value` line each, `<unset>` for an empty value.
pub fn render(block: &[EnvVar]) -> String {
    block
        .iter()
        .map(|e| {
            format!(
                "{}={}\n",
                e.name,
                if e.is_unset() {
                    "<unset>"
                } else {
                    e.value.as_str()
                }
            )
        })
        .collect()
}

/// The golden text of a capability result: the block as `render` writes it, then the `on=` and
/// `removed=` lines.
pub fn render_capabilities(apply: &CapabilityApply) -> String {
    format!(
        "{}on={}\nremoved={}\n",
        render(&apply.env),
        apply.on_summary,
        apply.removed
    )
}
