// SPDX-License-Identifier: GPL-3.0-only
//! Frozen contract between the launcher and the game-side mod (dinput8.dll).
//!
//! Only constants that need no golden data live here. Every item cites the C++ line it was
//! copied from. Paths are relative to the sc-offline repository root. Line numbers are for the
//! `codex/2.4.1-offline-integration` base commit 23da1900, not for a working copy with later
//! edits to `launcher/launcher.cpp`. Read them with `git show 23da1900:launcher/launcher.cpp`.
//! Re-pin them when the C++ source moves. A cite described as "comment at" points at the
//! comment that states the fact, not at the code line that builds it.
//!
//! Nothing here was run in game. These are source-level copies, checked by unit tests only.

/// Environment variables the launcher sets for the game process on `play`.
///
/// Source: `launcher/launcher.cpp` lines 5302 to 5315 (the `SetVar` block under "What the mod
/// reads"). Values are set only for the `play` command.
pub mod env {
    /// `launcher/launcher.cpp:5302`
    pub const BOOT_MAP: &str = "SC_OFFLINE_BOOT_MAP";
    /// `launcher/launcher.cpp:5303`
    pub const MOD_LOG: &str = "SC_OFFLINE_MOD_LOG";
    /// `launcher/launcher.cpp:5304`
    pub const SPAWN_FILE: &str = "SC_OFFLINE_SPAWN_FILE";
    /// `launcher/launcher.cpp:5305`
    pub const SHIPS_FILE: &str = "SC_OFFLINE_SHIPS_FILE";
    /// `launcher/launcher.cpp:5306`
    pub const START: &str = "SC_OFFLINE_START";
    /// `launcher/launcher.cpp:5307`
    pub const START_SHIP: &str = "SC_OFFLINE_START_SHIP";
    /// `launcher/launcher.cpp:5308`. Value `on` or `off`.
    pub const PLUGINS: &str = "SC_OFFLINE_PLUGINS";
    /// `launcher/launcher.cpp:5309`. Value `on` or `off`.
    pub const RELOCATION: &str = "SC_OFFLINE_RELOCATION";
    /// `launcher/launcher.cpp:5310`. Value `on` or `off`.
    pub const MULTIPLAYER: &str = "SC_OFFLINE_MULTIPLAYER";
    /// `launcher/launcher.cpp:5311`
    pub const MULTIPLAYER_ALLOW: &str = "SC_OFFLINE_MULTIPLAYER_ALLOW";
    /// `launcher/launcher.cpp:5312`. Value `on` or `off`.
    pub const ASOP: &str = "SC_OFFLINE_ASOP";
    /// `launcher/launcher.cpp:5313`
    pub const ASOP_FLEET_LIST: &str = "SC_OFFLINE_ASOP_FLEET_LIST";
    /// `launcher/launcher.cpp:5314`. Not an `SC_OFFLINE_` name: the game's user folder.
    pub const SC_USER: &str = "SC_USER";
    /// `launcher/launcher.cpp:5315`
    pub const REGISTRY_PROBE: &str = "SC_OFFLINE_REGISTRY_PROBE";

    /// Set by the launcher window on the hidden child it runs, so the child pipes its output
    /// and reads `[y/N]` answers from stdin. `launcher/launcher.cpp:146`.
    pub const GUI: &str = "SC_OFFLINE_GUI";

    /// Every variable the launcher sets on `play`, in the order the C++ launcher sets them.
    pub const PLAY_BLOCK: [&str; 14] = [
        BOOT_MAP,
        MOD_LOG,
        SPAWN_FILE,
        SHIPS_FILE,
        START,
        START_SHIP,
        PLUGINS,
        RELOCATION,
        MULTIPLAYER,
        MULTIPLAYER_ALLOW,
        ASOP,
        ASOP_FLEET_LIST,
        SC_USER,
        REGISTRY_PROBE,
    ];
}

/// Capability switches read by the sco-core host. These are NOT set by the C++ launcher
/// today. Names are from the design document (section 4.3). The sco-core submodule is not
/// checked out in this worktree, so the reading source line was not re-checked here.
///
/// Owner decision 2026-11-17: each switch is ON by default in every release package, each is
/// a visible setting the player can turn OFF, and each one's text says it has not been run in
/// game. A switch is on only when the value is exactly `"1"`.
///
/// The one exception is the logging-only `SCO_ENTITIES_DIAG`. It is not a capability, so the
/// decision does not cover it. It is OFF by default (the safer reading) and stays pending an
/// owner answer. `Switch::default_on` carries this, and `capabilities.json` says the same.
pub mod capability_env {
    pub const LOADOUT_ASYNC: &str = "SCO_LOADOUT_ASYNC";
    pub const INVENTORY_READ: &str = "SCO_INVENTORY_READ";
    pub const ACTOR_STATS_WRITE: &str = "SCO_ACTOR_STATS_WRITE";
    pub const CVARS_NATIVE: &str = "SCO_CVARS_NATIVE";
    pub const AUDIO_NATIVE: &str = "SCO_AUDIO_NATIVE";
    /// Diagnostic logging only. Not a capability.
    pub const ENTITIES_DIAG: &str = "SCO_ENTITIES_DIAG";

    /// The exact value that turns a switch on.
    pub const ON_VALUE: &str = "1";

    /// One launcher-visible capability switch.
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub struct Switch {
        /// Environment variable the sco-core host reads.
        pub env: &'static str,
        /// Key in `sc-offline.ini` (new in the Rust launcher; the C++ launcher does not know it).
        pub ini_key: &'static str,
        /// What the switch turns on. Not a capability for the diagnostic switch.
        pub capability: &'static str,
        /// Evidence level from the SDK status registry. No switch is `in_game_verified`.
        pub evidence: &'static str,
        /// True for the one switch that is logging only.
        pub diagnostic_only: bool,
        /// Release default. True for every capability switch. False for the logging-only one.
        pub default_on: bool,
    }

    /// Every switch the launcher shows. Owner decision 2026-11-17: every capability switch is ON
    /// by default. The logging-only switch is OFF by default (see the module text).
    pub const SWITCHES: [Switch; 6] = [
        Switch {
            env: LOADOUT_ASYNC,
            ini_key: "loadout_async",
            capability: "game.loadout.async",
            evidence: "synthetic_only",
            diagnostic_only: false,
            default_on: true,
        },
        Switch {
            env: INVENTORY_READ,
            ini_key: "inventory_read",
            capability: "game.inventory.read",
            evidence: "synthetic_only",
            diagnostic_only: false,
            default_on: true,
        },
        Switch {
            env: ACTOR_STATS_WRITE,
            ini_key: "actor_stats_write",
            capability: "game.actor_stats.write",
            evidence: "off_pending_probe",
            diagnostic_only: false,
            default_on: true,
        },
        Switch {
            env: CVARS_NATIVE,
            ini_key: "cvars_native",
            capability: "game.cvars.read adapter",
            evidence: "synthetic_only",
            diagnostic_only: false,
            default_on: true,
        },
        Switch {
            env: AUDIO_NATIVE,
            ini_key: "audio_native",
            capability: "audio native adapter attempt",
            evidence: "synthetic_only",
            diagnostic_only: false,
            default_on: true,
        },
        Switch {
            env: ENTITIES_DIAG,
            ini_key: "entities_diag",
            capability: "diagnostic logging, not a capability",
            evidence: "not a capability",
            diagnostic_only: true,
            default_on: false,
        },
    ];

    /// The sentence every switch carries in the UI and in the docs.
    pub const NOT_RUN_IN_GAME: &str = "Not run in game.";
}

/// The generated capability table (`tools/capabilities.py`). Parsed by the launcher at run time
/// and checked by `tests/capabilities.rs`. Nothing in it was run in game.
pub const CAPABILITIES_JSON: &str = include_str!("capabilities.json");

/// Launcher process exit codes. Source: `launcher/launcher.cpp:156` to `158`.
pub mod exit_code {
    /// `kExitOk`
    pub const OK: i32 = 0;
    /// `kExitError`
    pub const ERROR: i32 = 1;
    /// `kExitEac`: Easy Anti-Cheat is active.
    pub const EAC_ACTIVE: i32 = 2;
    /// `kExitRunning`: the game is running.
    pub const GAME_RUNNING: i32 = 3;
    /// `kExitRestart`: window only. An update was applied and the window restarts itself.
    pub const RESTART: i32 = 10;
}

/// Named events of the stop channel.
///
/// Source: `src/stop_channel.h`. The doc comment at lines 6 and 7 gives the patterns and
/// `EventName` at lines 17 and 18 builds them with the format `Local\sc-offline-%ls-%lu`
/// where the kind is `stop` (line 32) or `stopped` (line 29) and the number is the game pid.
pub mod stop_channel {
    /// Prefix shared by both events (`src/stop_channel.h:18`).
    pub const NAMESPACE_PREFIX: &str = "Local\\sc-offline-";
    /// Kind text for the launcher-to-mod event (`src/stop_channel.h:32`).
    pub const STOP_KIND: &str = "stop";
    /// Kind text for the mod-to-launcher event (`src/stop_channel.h:29`).
    pub const STOPPED_KIND: &str = "stopped";

    /// `Local\sc-offline-stop-<pid>`. Launcher to mod: run the exit work now.
    pub fn stop_event_name(pid: u32) -> String {
        format!("{NAMESPACE_PREFIX}{STOP_KIND}-{pid}")
    }

    /// `Local\sc-offline-stopped-<pid>`. Mod to launcher: the exit work has run.
    pub fn stopped_event_name(pid: u32) -> String {
        format!("{NAMESPACE_PREFIX}{STOPPED_KIND}-{pid}")
    }
}

/// Files and suffixes in the game folder and next to the launcher.
pub mod files {
    /// Marker file in Bin64 recording what the launcher changed. `launcher/launcher.cpp:1810`.
    pub const MARKER: &str = "sc-offline.installed";
    /// Suffix of the backup the launcher makes of a game file it replaces
    /// (`dinput8.dll` and `default_1.xml`). `launcher/launcher.cpp:1809` and `1813`.
    pub const BACKUP_SUFFIX: &str = ".sc-offline-backup";
    /// The mod DLL name placed in Bin64. `launcher/launcher.cpp:1808`.
    pub const MOD_DLL: &str = "dinput8.dll";
    /// Settings file next to the launcher. `launcher/launcher.cpp:292` (parser messages) and
    /// `launcher/launcher.cpp:5210` (path build).
    pub const INI: &str = "sc-offline.ini";
    /// Record of PC changes under `%ProgramData%\sc-offline\`. `launcher/launcher.cpp:18`.
    pub const PC_CHANGES: &str = "pc-changes.txt";
    /// The game's player data file the launcher replaces and restores, in the channel's
    /// `user\client\0` folder. `GamePaths` in `launcher/launcher.cpp`.
    pub const DEFAULT_XML: &str = "default_1.xml";
    /// Folder of `default_1.xml`, relative to the channel folder (the parent of Bin64).
    pub const USER_DIR: &str = "user\\client\\0";
    /// Per-plugin marker that keeps a plugin disabled, in `data\plugins\<id>\`.
    /// `launcher/launcher.cpp:4292`.
    pub const PLUGIN_DISABLED_MARKER: &str = "disabled";
}

/// Keys of `sc-offline.ini`. Source: the key parser at `launcher/launcher.cpp:294` to `354`.
/// The C++ parser compares keys with `_wcsicmp`, so keys are case-insensitive.
pub mod ini {
    /// `launcher/launcher.cpp:294`
    pub const GAME: &str = "game";
    /// `launcher/launcher.cpp:295`
    pub const LANGUAGE: &str = "language";
    /// `launcher/launcher.cpp:302`
    pub const CHANNEL: &str = "channel";
    /// `launcher/launcher.cpp:303`
    pub const BOOT_MAP: &str = "boot_map";
    /// `launcher/launcher.cpp:304`
    pub const START_SHIP: &str = "start_ship";
    /// `launcher/launcher.cpp:305`
    pub const START: &str = "start";
    /// `launcher/launcher.cpp:306`
    pub const BLOCK_NETWORK: &str = "block_network";
    /// `launcher/launcher.cpp:306`
    pub const EAC_HOSTS: &str = "eac_hosts";
    /// `launcher/launcher.cpp:306`
    pub const EAC_RENAME: &str = "eac_rename";
    /// `launcher/launcher.cpp:310`
    pub const CRASH_REPORTS: &str = "crash_reports";
    /// `launcher/launcher.cpp:317`
    pub const PLUGINS: &str = "plugins";
    /// `launcher/launcher.cpp:320`
    pub const RELOCATION: &str = "relocation";
    /// `launcher/launcher.cpp:323`
    pub const MULTIPLAYER: &str = "multiplayer";
    /// `launcher/launcher.cpp:326`
    pub const MULTIPLAYER_ALLOW: &str = "multiplayer_allow";
    /// `launcher/launcher.cpp:333`
    pub const ASOP: &str = "asop";
    /// `launcher/launcher.cpp:336`
    pub const ASOP_FLEET_LIST: &str = "asop_fleet_list";
    /// `launcher/launcher.cpp:343`
    pub const CHECK_UPDATES: &str = "check_updates";
    /// `launcher/launcher.cpp:346`
    pub const UPDATE_CHANNEL: &str = "update_channel";
    /// `launcher/launcher.cpp:351`
    pub const CLEAN_LOGS: &str = "clean_logs";
    /// `launcher/launcher.cpp:354`
    pub const REGISTRY_PROBE: &str = "registry_probe";

    /// `launcher/launcher.cpp:313`. Legacy: `off` turns the Discord plugin off, then the key is
    /// dropped from the file (`launcher/launcher.cpp:5210`).
    pub const DISCORD_PRESENCE: &str = "discord_presence";
    /// `launcher/launcher.cpp:340`. Legacy: the parser only prints a "moved to plugin settings" notice.
    pub const MINING: &str = "mining";
    /// `launcher/launcher.cpp:340`. Legacy, same notice as `mining`.
    pub const MINING_DEBUG: &str = "mining_debug";

    /// Legacy keys the C++ parser still recognises but no longer applies as settings.
    pub const LEGACY: [&str; 3] = [DISCORD_PRESENCE, MINING, MINING_DEBUG];

    /// Active keys the C++ parser applies as settings, in source order.
    pub const ALL: [&str; 20] = [
        GAME,
        LANGUAGE,
        CHANNEL,
        BOOT_MAP,
        START_SHIP,
        START,
        BLOCK_NETWORK,
        EAC_HOSTS,
        EAC_RENAME,
        CRASH_REPORTS,
        PLUGINS,
        RELOCATION,
        MULTIPLAYER,
        MULTIPLAYER_ALLOW,
        ASOP,
        ASOP_FLEET_LIST,
        CHECK_UPDATES,
        UPDATE_CHANNEL,
        CLEAN_LOGS,
        REGISTRY_PROBE,
    ];

    /// True when `key` is an active ini key. Case-insensitive, like the C++ parser.
    pub fn is_known_key(key: &str) -> bool {
        ALL.iter().any(|k| k.eq_ignore_ascii_case(key))
    }

    /// True when `key` is a legacy key the C++ parser recognises (no "unknown key" report).
    pub fn is_legacy_key(key: &str) -> bool {
        LEGACY.iter().any(|k| k.eq_ignore_ascii_case(key))
    }

    /// True for any key the C++ parser does not report as unknown.
    pub fn is_recognised_key(key: &str) -> bool {
        is_known_key(key) || is_legacy_key(key)
    }
}

/// Names and texts the C++ launcher keeps for the PC changes it records and undoes.
///
/// Source: `launcher/launcher.cpp:2071` to `2080` and `2199`. The dump tool writes them to
/// `tests/golden/constants.txt`, and a test compares these constants with that file.
pub mod pc_changes {
    /// The game exe name the launcher looks for (`kGameExe` in `launcher/launcher.cpp`).
    pub const GAME_EXE: &str = "StarCitizen.exe";
    /// Host the EAC hosts line points at 127.0.0.1.
    pub const EAC_HOST: &str = "modules-cdn.eac-prod.on.epicgames.com";
    /// Tag at the end of the hosts line. Only lines carrying it are removed again.
    pub const HOSTS_TAG: &str = "# added by sc-offline, removed when the game closes";
    /// Record keys and Windows Firewall rule names, in the order of `kFwRules`.
    pub const FIREWALL_RULES: [(&str, &str); 4] = [
        ("firewall", "sc-offline: block StarCitizen.exe"),
        ("fw_launcher", "sc-offline: block RSI Launcher.exe"),
        ("fw_crash", "sc-offline: block CrashHandler.exe"),
        (
            "fw_mp",
            "sc-offline: allow StarCitizen.exe multiplayer (LAN)",
        ),
    ];
    /// Record key and value for the hosts line.
    pub const HOSTS_KEY: &str = "hosts";
    pub const HOSTS_ADDED: &str = "added";
    /// Record key and value for the EAC exe rename.
    pub const EAC_KEY: &str = "eac";
    pub const EAC_RENAMED: &str = "renamed";
    /// Record key of the time stamp.
    pub const TIME_KEY: &str = "time";
}

/// `key=value` text records: `Bin64\sc-offline.installed` and `%ProgramData%\sc-offline\pc-changes.txt`.
///
/// Ported from `launcher/contract_text.h` (the C++ launcher uses the same code). Text is `&str`:
/// both records are ASCII.
pub mod record {
    /// Value of the first `key=` line, without trailing CR or spaces. Empty when absent.
    /// A key matches only at the start of a line and is case sensitive.
    pub fn field(text: &str, key: &str) -> String {
        let prefix = format!("{key}=");
        for line in text.split('\n') {
            if let Some(rest) = line.strip_prefix(prefix.as_str()) {
                return rest.trim_end_matches(['\r', ' ']).to_string();
            }
        }
        String::new()
    }

    /// Appends `key=value\n` unless the record already holds that exact value for the key.
    /// True when the record changed. Note the C++ launcher does not add a newline first: a
    /// leftover record that lacks a final newline gets the new key glued on.
    pub fn add(record: &mut String, key: &str, value: &str) -> bool {
        if field(record, key) == value {
            return false;
        }
        record.push_str(&format!("{key}={value}\n"));
        true
    }

    /// `2026-11-17 09:05:03`, the `time=` value of the install marker.
    pub fn marker_time(
        year: u32,
        month: u32,
        day: u32,
        hour: u32,
        minute: u32,
        second: u32,
    ) -> String {
        format!("{year:04}-{month:02}-{day:02} {hour:02}:{minute:02}:{second:02}")
    }

    /// `2026-11-17 09:05`, the `time=` value of pc-changes.txt.
    pub fn pc_time(year: u32, month: u32, day: u32, hour: u32, minute: u32) -> String {
        format!("{year:04}-{month:02}-{day:02} {hour:02}:{minute:02}")
    }

    /// The `Bin64\sc-offline.installed` text. `mode` is `play` or `install`; `xml` is `backup`,
    /// `none` or `skip`.
    pub fn marker_text(version: &str, sha256: &str, mode: &str, when: &str, xml: &str) -> String {
        format!("version={version}\nsha256={sha256}\nmode={mode}\ntime={when}\nxml={xml}\n")
    }
}

/// Files an update must never replace. Source: `PlayerOwned` at `launcher/launcher.cpp:799`.
pub mod player_owned {
    /// Exact relative paths (backslash separators), compared without regard to ASCII case.
    pub const KEEP: [&str; 11] = [
        "sc-offline.ini",
        "data\\wallet.txt",
        "data\\spawn.txt",
        "data\\bookmarks.txt",
        "data\\locations_found.txt",
        "data\\buildables.txt",
        "data\\game-path.txt",
        "data\\game-build.txt",
        "data\\mod.log",
        "data\\launcher.log",
        "data\\registry-dump.txt",
    ];
    /// Everything under this folder is kept too.
    pub const KEEP_FOLDER: &str = "data\\update\\";

    /// True when an update must leave this launcher-relative path alone.
    pub fn is_player_owned(rel: &str) -> bool {
        if KEEP.iter().any(|k| k.eq_ignore_ascii_case(rel)) {
            return true;
        }
        rel.len() >= KEEP_FOLDER.len()
            && rel.is_char_boundary(KEEP_FOLDER.len())
            && rel[..KEEP_FOLDER.len()].eq_ignore_ascii_case(KEEP_FOLDER)
    }
}

/// How the launcher keeps a plugin's `[settings]` values in `data\storage\<id>.db` (table
/// `sco_kv`). Source: `SavePluginSettings` and `LoadKept` in `launcher/launcher.cpp`.
pub mod settings_store {
    /// Key prefix: `sco.settings.<name>`.
    pub const KEY_PREFIX: &str = "sco.settings.";

    /// The `sco_kv` key of a setting.
    pub fn key(name: &str) -> String {
        format!("{KEY_PREFIX}{name}")
    }

    /// The `sco_kv` value: `<type>:<canonical text>`. The database column holds it as a blob.
    pub fn value(setting_type: &str, canonical: &str) -> String {
        format!("{setting_type}:{canonical}")
    }

    /// Splits a stored value at the first colon into type and text.
    pub fn split_value(stored: &str) -> Option<(&str, &str)> {
        stored.split_once(':')
    }
}

/// Address ranges the launcher accepts for `multiplayer_allow`. Source:
/// `src/private_network_ranges.h`.
pub mod network {
    /// True when the whole range `first..=last` lies inside one private range: 10/8,
    /// 100.64/10, 127/8, 169.254/16, 172.16/12 or 192.168/16.
    pub fn private_range(first: u32, last: u32) -> bool {
        first <= last
            && (first >= 0x0A00_0000 && last <= 0x0AFF_FFFF
                || first >= 0x6440_0000 && last <= 0x647F_FFFF
                || first >= 0x7F00_0000 && last <= 0x7FFF_FFFF
                || first >= 0xA9FE_0000 && last <= 0xA9FE_FFFF
                || first >= 0xAC10_0000 && last <= 0xAC1F_FFFF
                || first >= 0xC0A8_0000 && last <= 0xC0A8_FFFF)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashSet;

    #[test]
    fn stop_event_names_match_header_patterns() {
        assert_eq!(
            stop_channel::stop_event_name(1234),
            "Local\\sc-offline-stop-1234"
        );
        assert_eq!(
            stop_channel::stopped_event_name(1234),
            "Local\\sc-offline-stopped-1234"
        );
        assert_eq!(
            stop_channel::stop_event_name(u32::MAX),
            "Local\\sc-offline-stop-4294967295"
        );
    }

    #[test]
    fn stop_event_names_fit_the_cpp_buffer() {
        // The C++ buffer is wchar_t[64] (src/stop_channel.h:17), including the terminator.
        assert!(stop_channel::stopped_event_name(u32::MAX).chars().count() < 64);
    }

    #[test]
    fn exit_codes_are_frozen() {
        assert_eq!(
            [
                exit_code::OK,
                exit_code::ERROR,
                exit_code::EAC_ACTIVE,
                exit_code::GAME_RUNNING,
                exit_code::RESTART
            ],
            [0, 1, 2, 3, 10]
        );
    }

    #[test]
    fn file_names_are_frozen() {
        assert_eq!(files::MARKER, "sc-offline.installed");
        assert_eq!(files::BACKUP_SUFFIX, ".sc-offline-backup");
        assert_eq!(
            format!("{}{}", files::MOD_DLL, files::BACKUP_SUFFIX),
            "dinput8.dll.sc-offline-backup"
        );
        assert_eq!(files::INI, "sc-offline.ini");
        assert_eq!(files::PC_CHANGES, "pc-changes.txt");
    }

    #[test]
    fn play_block_is_unique_and_prefixed() {
        let set: HashSet<_> = env::PLAY_BLOCK.iter().collect();
        assert_eq!(set.len(), env::PLAY_BLOCK.len());
        for name in env::PLAY_BLOCK {
            assert!(
                name.starts_with("SC_OFFLINE_") || name == env::SC_USER,
                "{name}"
            );
        }
        assert_eq!(env::PLAY_BLOCK.first(), Some(&"SC_OFFLINE_BOOT_MAP"));
    }

    #[test]
    fn capability_switches_use_exact_one() {
        assert_eq!(capability_env::ON_VALUE, "1");
        for name in [
            capability_env::LOADOUT_ASYNC,
            capability_env::INVENTORY_READ,
            capability_env::ACTOR_STATS_WRITE,
            capability_env::CVARS_NATIVE,
            capability_env::AUDIO_NATIVE,
            capability_env::ENTITIES_DIAG,
        ] {
            assert!(name.starts_with("SCO_"), "{name}");
        }
    }

    #[test]
    fn capability_table_matches_the_names_and_is_not_in_the_cpp_key_list() {
        let names: Vec<_> = capability_env::SWITCHES.iter().map(|s| s.env).collect();
        assert_eq!(
            names,
            [
                capability_env::LOADOUT_ASYNC,
                capability_env::INVENTORY_READ,
                capability_env::ACTOR_STATS_WRITE,
                capability_env::CVARS_NATIVE,
                capability_env::AUDIO_NATIVE,
                capability_env::ENTITIES_DIAG
            ]
        );
        let keys: HashSet<_> = capability_env::SWITCHES.iter().map(|s| s.ini_key).collect();
        assert_eq!(keys.len(), 6);
        for key in keys {
            assert!(
                !ini::is_recognised_key(key),
                "{key} must not clash with a C++ key"
            );
        }
        assert!(capability_env::SWITCHES
            .iter()
            .all(|s| s.evidence != "in_game_verified"));
        assert_eq!(
            capability_env::SWITCHES
                .iter()
                .filter(|s| s.diagnostic_only)
                .count(),
            1
        );
    }

    #[test]
    fn ini_keys_are_unique_lowercase_and_case_insensitive() {
        let set: HashSet<_> = ini::ALL.iter().collect();
        assert_eq!(set.len(), ini::ALL.len());
        for key in ini::ALL {
            assert_eq!(key, key.to_ascii_lowercase());
            assert!(ini::is_known_key(&key.to_ascii_uppercase()));
        }
        // Legacy keys: recognised by the C++ parser, but not active settings.
        for key in ini::LEGACY {
            assert!(!ini::is_known_key(key), "{key}");
            assert!(ini::is_legacy_key(&key.to_ascii_uppercase()), "{key}");
            assert!(ini::is_recognised_key(key), "{key}");
            assert!(!ini::ALL.contains(&key), "{key}");
        }
        assert!(ini::is_legacy_key("discord_presence"));
        assert!(ini::is_legacy_key("mining_debug"));
        assert!(!ini::is_recognised_key("no_such_key"));
    }
}
