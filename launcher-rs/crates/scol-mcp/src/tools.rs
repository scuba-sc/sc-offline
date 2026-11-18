// SPDX-License-Identifier: GPL-3.0-only
//! The four tool definitions and the server instructions, word for word from `launcher/mcp_server.h`.
//! Written by reading that header. `tests/vectors.rs` checks `tools/list` and `initialize` against what
//! the C++ server prints.

/// One tool: name, title, description and the two JSON schemas.
pub struct ToolDef {
    pub name: &'static str,
    pub title: &'static str,
    pub description: &'static str,
    pub input_schema: &'static str,
    pub output_schema: &'static str,
}

/// The `instructions` text of the `initialize` reply.
pub const INSTRUCTIONS: &str = r#"This server gives a read-only view of one sc-offline install: its status, plugins, logs and settings. It never starts or stops the game, installs, updates, or changes settings or plugins. Paths under the user's profile are shown as %USERPROFILE%. The game's Game.log is served only when the server was started with --game-log. To write plugins, use the sco-core SDK MCP server (sco-core-mcp); to test plugins out of game, use the scuba-engine MCP server."#;

/// The four read-only tools, in the order `tools/list` returns them.
pub const TOOLS: [ToolDef; 4] = [
    ToolDef {
        name: r#"get_status"#,
        title: r#"Install status"#,
        description: r#"Returns the state of this sc-offline install: the launcher version, whether the Star Citizen folder was found and its build, whether the mod is installed, the Easy Anti-Cheat state, the launcher's network-block, EAC-rename and plugins settings, plugin counts, PC changes still to be undone, whether Wine is in use and whether the game is running. These are the read-only checks behind `sc-offline.exe status`; the tool takes no arguments and never changes anything or uses the network. Paths are redacted."#,
        input_schema: r#"{"type":"object","properties":{},"additionalProperties":false}"#,
        output_schema: r#"{"type":"object","properties":{
"launcher_version":{"type":"string","description":"Version of this sc-offline launcher."},
"game_folder_found":{"type":"boolean","description":"Whether the Star Citizen Bin64 folder was found."},
"game_folder":{"type":"string","description":"The game's Bin64 folder with the user profile redacted; empty when not found."},
"game_build":{"type":"string","description":"The game build the launcher read; empty when unknown."},
"mod_installed":{"type":"boolean","description":"Whether sc-offline's dinput8.dll or its marker is in the game folder."},
"eac":{"type":"string","enum":["active","disabled","not_installed"],"description":"Easy Anti-Cheat state: active (EasyAntiCheat_EOS.exe present), disabled (renamed to .bak) or not_installed."},
"network_blocked":{"type":"boolean","description":"The block_network setting: the launcher blocks the game in Windows Firewall while you play."},
"eac_rename":{"type":"boolean","description":"The eac_rename setting."},
"plugins_setting_on":{"type":"boolean","description":"The plugins setting in sc-offline.ini."},
"plugins_enabled":{"type":"integer","minimum":0,"description":"Plugin folders without a disabled file."},
"plugins_total":{"type":"integer","minimum":0,"description":"Plugin folders that have a plugin.ini."},
"pc_changes_left":{"type":"string","description":"The record of PC changes not yet undone; empty when none."},
"wine":{"type":"boolean","description":"Whether the launcher runs under Wine or Proton."},
"game_running":{"type":"boolean","description":"Whether this install's StarCitizen.exe is running."}},
"required":["launcher_version","game_folder_found","game_folder","game_build","mod_installed","eac","network_blocked","eac_rename","plugins_setting_on","plugins_enabled","plugins_total","pc_changes_left","wine","game_running"],
"additionalProperties":false}"#,
    },
    ToolDef {
        name: r#"list_plugins"#,
        title: r#"Installed plugins"#,
        description: r#"Lists the plugin folders under data\plugins that have a plugin.ini, sorted by folder name and capped at 256. Each entry has the folder, whether it is enabled (no disabled file), the plugin.ini fields id, name, version, author, api, kind, entry and description (empty when absent, each shortened to 256 bytes) and warnings such as an id that does not match the folder or a missing id, name or version. The tool takes no arguments and is read-only: it does not load or run any plugin."#,
        input_schema: r#"{"type":"object","properties":{},"additionalProperties":false}"#,
        output_schema: r#"{"type":"object","properties":{
"count":{"type":"integer","minimum":0,"description":"Number of plugins returned."},
"truncated":{"type":"boolean","description":"True when more than 256 plugin folders exist and the list was cut."},
"plugins":{"type":"array","description":"Plugin folders sorted by folder name.","items":{"type":"object","properties":{
"folder":{"type":"string","description":"Folder name under data\\plugins."},
"enabled":{"type":"boolean","description":"False when the folder has a disabled file."},
"id":{"type":"string","description":"plugin.ini id; empty when absent."},
"name":{"type":"string","description":"plugin.ini name; empty when absent."},
"version":{"type":"string","description":"plugin.ini version; empty when absent."},
"author":{"type":"string","description":"plugin.ini author; empty when absent."},
"api":{"type":"string","description":"plugin.ini api; empty when absent."},
"kind":{"type":"string","description":"plugin.ini kind; empty when absent."},
"entry":{"type":"string","description":"plugin.ini entry; empty when absent."},
"description":{"type":"string","description":"plugin.ini description; empty when absent."},
"warnings":{"type":"array","description":"Problems found in this plugin's plugin.ini.","items":{"type":"string"}}},
"required":["folder","enabled","id","name","version","author","api","kind","entry","description","warnings"],"additionalProperties":false}}},
"required":["count","truncated","plugins"],"additionalProperties":false}"#,
    },
    ToolDef {
        name: r#"read_log"#,
        title: r#"Read a log"#,
        description: r#"Returns the last lines of one log: the mod's data\mod.log, the launcher's data\launcher.log or its previous run launcher.log.1, or the game's Game.log (log game reads the Game.log of the game folder that get_status reports; it is served only when the server was started as `sc-offline.exe mcp --game-log`, because Game.log can contain account names, otherwise the call returns an error that says how to enable it). It scans at most the last 2 MiB of the file, can keep only the lines containing a case-insensitive ASCII text, and returns at most 500 lines and 64 KiB of text, dropping the oldest first. Before filtering and returning it first learns every handle, nickname, character name and identifier that the scanned part of the file and earlier calls in this session announce, and that the login lines (User Login Success, AccountLoginCharacterStatus) anywhere in the first 64 MiB of every log announce (login lines first; the result says when a log was longer than 64 MiB), then redacts paths under the user's profile (%USERPROFILE%) and Users and home paths (<user>; %5C, %2F and %40 are decoded first), the Windows account name (<user>; a name of 3 to 5 characters as a whole word, also next to - and _, a longer name also inside other words, a name shorter than 3 characters only in paths), the values of identity keys (nickname, handle, character and account names, GEID, account, player and user ids, also with a prefix such as rsiHandle or targetAccountId; as <redacted>), the digits after geid, accountid, account_id and playerid, what follows Player_ in batch names, and e-mail addresses, and replaces invalid UTF-8; values after the weak keys user=, owner=, player= and login= are hidden where they stand but not learned; free text can still hold a name that no key or login line announced, and obfuscated addresses or paths are not caught, so read a log before you share it. The tool is read-only: it never writes, rotates or deletes a log."#,
        input_schema: r#"{"type":"object","properties":{
"log":{"type":"string","enum":["mod","launcher","launcher_previous","game"],"description":"Which log: mod (data\\mod.log), launcher (data\\launcher.log), launcher_previous (data\\launcher.log.1) or game (the game's Game.log next to the channel folder; only when the server runs with --game-log)."},
"lines":{"type":"integer","minimum":1,"maximum":500,"default":100,"description":"How many of the last matching lines to return."},
"contains":{"type":"string","minLength":1,"maxLength":200,"description":"Keep only lines containing this text, compared case-insensitively for ASCII letters, before the last lines are taken."}},
"required":["log"],"additionalProperties":false}"#,
        output_schema: r#"{"type":"object","properties":{
"log":{"type":"string","enum":["mod","launcher","launcher_previous","game"],"description":"The log that was read."},
"path":{"type":"string","description":"The file's path with the user profile redacted; empty when unknown."},
"exists":{"type":"boolean","description":"Whether the file exists."},
"lines":{"type":"array","description":"The returned lines, oldest first, without line endings.","items":{"type":"string"}},
"matched_lines":{"type":"integer","minimum":0,"description":"Lines that matched in the scanned part of the file, before the last N were taken."},
"truncated":{"type":"boolean","description":"True when older content was not scanned or more lines matched than were returned."},
"identity_scan_complete":{"type":"boolean","description":"False when a log was longer than 64 MiB (or could not be read), so login lines past that point were not learned from."},
"notice":{"type":"string","description":"Empty, or what was not scanned and what that means for redaction."}},
"required":["log","path","exists","lines","matched_lines","truncated","identity_scan_complete","notice"],"additionalProperties":false}"#,
    },
    ToolDef {
        name: r#"get_settings"#,
        title: r#"sc-offline.ini settings"#,
        description: r#"Returns the settings in the sc-offline.ini next to the launcher as section, key and value entries in file order, skipping comments and blank lines. Values are redacted for the user profile and shortened to 1 KiB, and at most 1000 entries are returned. The tool takes no arguments and is read-only: it never writes or migrates the file."#,
        input_schema: r#"{"type":"object","properties":{},"additionalProperties":false}"#,
        output_schema: r#"{"type":"object","properties":{
"path":{"type":"string","description":"The ini file's path with the user profile redacted."},
"exists":{"type":"boolean","description":"Whether the file exists."},
"truncated":{"type":"boolean","description":"True when the file has more settings than were returned."},
"settings":{"type":"array","description":"Settings in file order.","items":{"type":"object","properties":{
"section":{"type":"string","description":"The [section] the key is under; empty before any section."},
"key":{"type":"string","description":"The setting's key."},
"value":{"type":"string","description":"The setting's value as written, redacted."}},
"required":["section","key","value"],"additionalProperties":false}}},
"required":["path","exists","truncated","settings"],"additionalProperties":false}"#,
    },
];
