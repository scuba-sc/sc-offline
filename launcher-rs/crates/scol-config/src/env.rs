// SPDX-License-Identifier: GPL-3.0-only
//! The environment block `play` sets for the game process.
//!
//! `play_env` is the `SC_OFFLINE_*` block the C++ launcher sets (`PlayEnv` in
//! `launcher/contract_text.h`). `full_env` adds the capability switches, which the C++ launcher
//! does not set.
//!
//! An empty value means the variable is removed from the child's environment (`SetVar` in the
//! C++ launcher), so a value the player set in their shell does not leak through.

use crate::capabilities::Capabilities;
use crate::config::Config;
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

/// The 14 variables of `play`, in the order the C++ launcher sets them.
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
    ]
}

/// The capability switches: `1` for each one that is on, removed for each one that is off.
pub fn capability_env(caps: &Capabilities) -> Vec<EnvVar> {
    caps.env()
        .into_iter()
        .map(|(name, value)| EnvVar { name, value })
        .collect()
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
