// SPDX-License-Identifier: GPL-3.0-only
//! `sc-offline.ini` reader. A port of `ReadConfig` in `launcher/launcher.cpp`, including its
//! messages: the golden files compare both the parsed fields and the message lines.

use crate::capabilities::Capabilities;
use scol_contract::network::private_range;
use std::path::Path;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Language {
    English,
    Russian,
    Auto,
}

impl Language {
    pub fn word(self) -> &'static str {
        match self {
            Language::Auto => "auto",
            Language::Russian => "ru",
            Language::English => "en",
        }
    }
}

/// What to do with the game's logs from a session once the game closes (`clean_logs`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LogChoice {
    Ask,
    Always,
    Never,
}

impl LogChoice {
    pub fn word(self) -> &'static str {
        match self {
            LogChoice::Always => "always",
            LogChoice::Never => "never",
            LogChoice::Ask => "ask",
        }
    }
}

/// The settings of `sc-offline.ini`. Defaults are the C++ launcher's (`struct Config`), plus
/// the capability switches, which are on by default.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Config {
    pub game: String,
    pub channel: String,
    pub boot_map: String,
    pub start_ship: String,
    pub start: String,
    pub registry_probe: String,
    pub language: Language,
    /// `block_network`
    pub firewall: bool,
    pub eac_hosts: bool,
    pub eac_rename: bool,
    pub clean_logs: LogChoice,
    pub check_updates: bool,
    /// `update_channel = prerelease`
    pub prereleases: bool,
    pub crash_reports: bool,
    /// Legacy `discord_presence = off`.
    pub discord_off: bool,
    pub plugins: bool,
    pub relocation: bool,
    pub multiplayer: bool,
    /// Canonical `a.b.c.d/n` list, comma separated, or empty.
    pub multiplayer_allow: String,
    pub asop: bool,
    pub asop_fleet_list: String,
    /// New in the Rust launcher. The C++ launcher ignores these keys.
    pub capabilities: Capabilities,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            game: String::new(),
            channel: "LIVE".into(),
            boot_map: "PU_All".into(),
            start_ship: "DRAK_Cutlass_Black".into(),
            start: String::new(),
            registry_probe: String::new(),
            language: Language::Auto,
            firewall: true,
            eac_hosts: true,
            eac_rename: true,
            clean_logs: LogChoice::Ask,
            check_updates: true,
            prereleases: false,
            crash_reports: true,
            discord_off: false,
            plugins: true,
            relocation: true,
            multiplayer: true,
            multiplayer_allow: String::new(),
            asop: true,
            asop_fleet_list: "ships".into(),
            capabilities: Capabilities::default(),
        }
    }
}

/// The result of reading the file: the settings and the `[!] ...` lines the C++ launcher prints
/// (without the line ending).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Parsed {
    pub config: Config,
    pub messages: Vec<String>,
}

/// `Trim` of the C++ launcher: space, tab, CR, LF and double quote, from both ends.
pub fn trim(s: &str) -> &str {
    s.trim_matches(|c| matches!(c, ' ' | '\t' | '\r' | '\n' | '"'))
}

/// 1, on, yes, true or 0, off, no, false, without regard to case.
pub fn parse_on_off(v: &str) -> Option<bool> {
    const ON: [&str; 4] = ["1", "on", "yes", "true"];
    const OFF: [&str; 4] = ["0", "off", "no", "false"];
    if ON.iter().any(|w| v.eq_ignore_ascii_case(w)) {
        Some(true)
    } else if OFF.iter().any(|w| v.eq_ignore_ascii_case(w)) {
        Some(false)
    } else {
        None
    }
}

/// ask, always or never; the older on (= ask) and off (= never) still work.
pub fn parse_clean_logs(v: &str) -> Option<LogChoice> {
    if v.eq_ignore_ascii_case("ask") {
        return Some(LogChoice::Ask);
    }
    if v.eq_ignore_ascii_case("always") {
        return Some(LogChoice::Always);
    }
    if v.eq_ignore_ascii_case("never") {
        return Some(LogChoice::Never);
    }
    parse_on_off(v).map(|on| if on { LogChoice::Ask } else { LogChoice::Never })
}

/// `multiplayer_allow`: up to 8 private IPv4 ranges `a.b.c.d/n` (n 8 to 32, no host bits set),
/// separated by commas. Returns the list rewritten from the numbers, or None when any item is
/// wrong. An empty value is a valid empty list.
pub fn parse_allow_list(v: &str) -> Option<String> {
    let mut canonical = String::new();
    let mut count = 0usize;
    let mut pos = 0usize;
    while pos < v.len() {
        let comma = v[pos..].find(',').map(|i| pos + i).unwrap_or(v.len());
        let item = trim(&v[pos..comma]);
        pos = comma + 1;
        let b = item.as_bytes();
        let mut cursor = 0usize;
        let number = |max_digits: usize, cursor: &mut usize| -> Option<u32> {
            let start = *cursor;
            let mut value: u32 = 0;
            while *cursor < b.len() && b[*cursor].is_ascii_digit() {
                if *cursor - start == max_digits {
                    return None;
                }
                value = value * 10 + u32::from(b[*cursor] - b'0');
                *cursor += 1;
            }
            if *cursor == start {
                None
            } else {
                Some(value)
            }
        };
        let separator = |expected: u8, cursor: &mut usize| -> bool {
            if *cursor == b.len() || b[*cursor] != expected {
                return false;
            }
            *cursor += 1;
            true
        };
        let a = number(3, &mut cursor)?;
        if !separator(b'.', &mut cursor) {
            return None;
        }
        let bb = number(3, &mut cursor)?;
        if !separator(b'.', &mut cursor) {
            return None;
        }
        let c = number(3, &mut cursor)?;
        if !separator(b'.', &mut cursor) {
            return None;
        }
        let d = number(3, &mut cursor)?;
        if !separator(b'/', &mut cursor) {
            return None;
        }
        let n = number(2, &mut cursor)?;
        if cursor != b.len() {
            return None;
        }
        if a > 255 || bb > 255 || c > 255 || d > 255 || !(8..=32).contains(&n) || count >= 8 {
            return None;
        }
        let ip = (a << 24) | (bb << 16) | (c << 8) | d;
        let mask: u32 = if n == 32 {
            0xFFFF_FFFF
        } else {
            !(0xFFFF_FFFFu32 >> n)
        };
        if ip & !mask != 0 || !private_range(ip, ip | !mask) {
            return None;
        }
        count += 1;
        if comma < v.len() && pos == v.len() {
            return None;
        }
        if !canonical.is_empty() {
            canonical.push(',');
        }
        canonical.push_str(&format!("{a}.{bb}.{c}.{d}/{n}"));
    }
    Some(canonical)
}

impl Config {
    /// Parses the bytes of an `sc-offline.ini`: an optional UTF-8 BOM, then `key = value` lines.
    /// A line whose trimmed text starts with `#` is a comment. Whole-line comments only, since
    /// paths may hold `#`.
    pub fn parse(bytes: &[u8]) -> Parsed {
        let bytes = bytes.strip_prefix(&[0xEF, 0xBB, 0xBF][..]).unwrap_or(bytes);
        let text = String::from_utf8_lossy(bytes);
        let mut c = Config::default();
        let mut messages: Vec<String> = Vec::new();
        for (index, line) in text.split('\n').enumerate() {
            let line_no = index + 1;
            let trimmed = trim(line);
            if trimmed.is_empty() || trimmed.starts_with('#') {
                continue;
            }
            let Some(eq) = line.find('=') else {
                messages.push(format!(
                    "[!] sc-offline.ini line {line_no}: expected key = value"
                ));
                continue;
            };
            let k = trim(&line[..eq]);
            let v = trim(&line[eq + 1..]);
            let is = |name: &str| k.eq_ignore_ascii_case(name);
            let on_off =
                |slot: &mut bool, name: &str, messages: &mut Vec<String>| match parse_on_off(v) {
                    Some(b) => *slot = b,
                    None => messages.push(format!(
                        "[!] sc-offline.ini line {line_no}: {name} must be on or off"
                    )),
                };
            if is("game") {
                c.game = v.to_string();
            } else if is("language") {
                c.language = Language::English;
                if v == "ru" {
                    c.language = Language::Russian;
                } else if v == "auto" {
                    c.language = Language::Auto;
                } else if v != "en" {
                    messages.push(format!("[!] sc-offline.ini line {line_no}: language must be en, ru or auto; using English"));
                }
            } else if is("channel") {
                if !v.is_empty() {
                    c.channel = v.to_string();
                }
            } else if is("boot_map") {
                c.boot_map = v.to_string();
            } else if is("start_ship") {
                c.start_ship = v.to_string();
            } else if is("start") {
                c.start = v.to_string();
            } else if is("block_network") {
                on_off(&mut c.firewall, k, &mut messages);
            } else if is("eac_hosts") {
                on_off(&mut c.eac_hosts, k, &mut messages);
            } else if is("eac_rename") {
                on_off(&mut c.eac_rename, k, &mut messages);
            } else if is("crash_reports") {
                on_off(&mut c.crash_reports, "crash_reports", &mut messages);
            } else if is("discord_presence") {
                if let Some(on) = parse_on_off(v) {
                    c.discord_off = !on;
                }
            } else if is("plugins") {
                on_off(&mut c.plugins, "plugins", &mut messages);
            } else if is("relocation") {
                on_off(&mut c.relocation, "relocation", &mut messages);
            } else if is("multiplayer") {
                on_off(&mut c.multiplayer, "multiplayer", &mut messages);
            } else if is("multiplayer_allow") {
                match parse_allow_list(v) {
                    Some(list) => c.multiplayer_allow = list,
                    None => {
                        c.multiplayer_allow.clear();
                        messages.push(format!(
                            "[!] sc-offline.ini line {line_no}: multiplayer_allow must be up to 8 private IPv4 ranges like 100.64.0.0/10; ignored"
                        ));
                    }
                }
            } else if is("asop") {
                on_off(&mut c.asop, "asop", &mut messages);
            } else if is("asop_fleet_list") {
                if v.eq_ignore_ascii_case("game") || v.eq_ignore_ascii_case("ships") {
                    c.asop_fleet_list = v.to_string();
                } else {
                    messages.push(format!(
                        "[!] sc-offline.ini line {line_no}: asop_fleet_list must be game or ships"
                    ));
                }
            } else if is("mining") || is("mining_debug") {
                messages.push(format!(
                    "[!] sc-offline.ini line {line_no}: mining moved to plugin settings; use data/plugins/mining/import_settings.py deliberately while launcher/game are closed"
                ));
            } else if is("check_updates") {
                on_off(&mut c.check_updates, "check_updates", &mut messages);
            } else if is("update_channel") {
                if v.eq_ignore_ascii_case("stable") {
                    c.prereleases = false;
                } else if v.eq_ignore_ascii_case("prerelease") {
                    c.prereleases = true;
                } else {
                    messages.push(format!("[!] sc-offline.ini line {line_no}: update_channel must be stable or prerelease"));
                }
            } else if is("clean_logs") {
                match parse_clean_logs(v) {
                    Some(choice) => c.clean_logs = choice,
                    None => messages.push(format!("[!] sc-offline.ini line {line_no}: clean_logs must be ask, always or never")),
                }
            } else if is("registry_probe") {
                c.registry_probe = v.to_string();
            } else if let Some(switch) = Capabilities::switch_for_key(k) {
                match parse_on_off(v) {
                    Some(b) => c.capabilities.set(switch, b),
                    None => messages.push(format!(
                        "[!] sc-offline.ini line {line_no}: {} must be on or off",
                        switch.ini_key
                    )),
                }
            } else {
                messages.push(format!(
                    "[!] sc-offline.ini line {line_no}: unknown key '{k}'"
                ));
            }
        }
        Parsed {
            config: c,
            messages,
        }
    }
}

/// Reads and parses a file. None when it cannot be read (the C++ launcher then says "no
/// sc-offline.ini next to this exe; using defaults").
pub fn read_config(path: &Path) -> Option<Parsed> {
    std::fs::read(path).ok().map(|b| Config::parse(&b))
}
