// SPDX-License-Identifier: GPL-3.0-only
//! `plugin.ini` and its `[settings]` lines, read with the rules the host and the C++ launcher
//! use (`ParsePluginIni`, `ParseSettingLine`, `CanonSetting` in `launcher/launcher.cpp`).
//!
//! This reads text only. It never loads plugin code.

use crate::cfmt::{format_g17, strtod, strtoll};

/// One `[settings]` line.
#[derive(Debug, Clone, PartialEq, Default)]
pub struct PluginSetting {
    pub name: String,
    /// bool, int, float, string or enum
    pub kind: String,
    pub label: String,
    pub help: String,
    pub default: String,
    pub has_min: bool,
    pub has_max: bool,
    pub min_i: i64,
    pub max_i: i64,
    pub min_f: f64,
    pub max_f: f64,
    pub choices: Vec<String>,
    /// What the game reads at its next start: the kept value, else the default.
    pub value: String,
    /// A value is kept in the plugin's database.
    pub kept: bool,
}

/// One plugin folder's `plugin.ini`.
#[derive(Debug, Clone, PartialEq, Default)]
pub struct PluginIni {
    /// The folder name: the host requires id == folder.
    pub id: String,
    pub name: String,
    pub version: String,
    pub author: String,
    /// `description`: not a host key, shown when a plugin adds one.
    pub about: String,
    pub kind: String,
    /// A `[settings]` line this reader does not understand (the host may refuse the plugin).
    pub settings_bad: bool,
    pub settings: Vec<PluginSetting>,
}

/// Why a typed value is refused. The launcher turns these into text.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SettingError {
    BoolExpected,
    IntegerExpected,
    AtLeast,
    AtMost,
    NumberExpected,
    FiniteExpected,
    StringLimit,
    AsciiExpected,
    ChoiceExpected,
}

/// The most settings the launcher reads from one plugin.
pub const MAX_SETTINGS: usize = 32;

fn trim_a(s: &str) -> &str {
    s.trim_matches(|c| matches!(c, ' ' | '\t' | '\r'))
}

/// `;` or `#` starts a comment at the start of a line or after a space or tab, outside double quotes.
fn strip_comment(s: &str) -> &str {
    let b = s.as_bytes();
    let mut quoted = false;
    for (i, &c) in b.iter().enumerate() {
        if c == b'"' {
            quoted = !quoted;
        } else if !quoted
            && (c == b';' || c == b'#')
            && (i == 0 || b[i - 1] == b' ' || b[i - 1] == b'\t')
        {
            return &s[..i];
        }
    }
    s
}

/// The next bare word or "quoted text" of a `[settings]` line. A bare word may hold a `(...)`
/// group, as in `enum(a,b,c)`. Returns the token and whether it was quoted.
fn next_token(s: &str, i: &mut usize) -> Option<(String, bool)> {
    let b = s.as_bytes();
    while *i < b.len() && (b[*i] == b' ' || b[*i] == b'\t') {
        *i += 1;
    }
    if *i >= b.len() {
        return None;
    }
    if b[*i] == b'"' {
        let e = s[*i + 1..].find('"').map(|p| *i + 1 + p)?;
        let tok = s[*i + 1..e].to_string();
        *i = e + 1;
        return Some((tok, true));
    }
    let start = *i;
    let mut paren = false;
    while *i < b.len() && (paren || (b[*i] != b' ' && b[*i] != b'\t')) {
        if b[*i] == b'(' {
            paren = true;
        } else if b[*i] == b')' {
            paren = false;
        }
        *i += 1;
    }
    Some((s[start..*i].to_string(), false))
}

/// `[+-]digits[.digits][(e|E)[+-]digits]`: the float grammar the host accepts.
fn plain_decimal(s: &str) -> bool {
    let b = s.as_bytes();
    let mut i = 0usize;
    let digits = |i: &mut usize| {
        let from = *i;
        while *i < b.len() && b[*i].is_ascii_digit() {
            *i += 1;
        }
        *i > from
    };
    if i < b.len() && (b[i] == b'+' || b[i] == b'-') {
        i += 1;
    }
    if !digits(&mut i) {
        return false;
    }
    if i < b.len() && b[i] == b'.' {
        i += 1;
        if !digits(&mut i) {
            return false;
        }
    }
    if i < b.len() && (b[i] == b'e' || b[i] == b'E') {
        i += 1;
        if i < b.len() && (b[i] == b'+' || b[i] == b'-') {
            i += 1;
        }
        if !digits(&mut i) {
            return false;
        }
    }
    i == b.len()
}

/// The value of a setting that has no usable `default`: 0 (or the nearest bound), false, "",
/// the first choice.
fn zero_text(s: &PluginSetting) -> String {
    match s.kind.as_str() {
        "bool" => "false".into(),
        "int" => {
            let mut v = 0i64;
            if s.has_min && v < s.min_i {
                v = s.min_i;
            }
            if s.has_max && v > s.max_i {
                v = s.max_i;
            }
            v.to_string()
        }
        "float" => {
            let mut v = 0f64;
            if s.has_min && v < s.min_f {
                v = s.min_f;
            }
            if s.has_max && v > s.max_f {
                v = s.max_f;
            }
            format_g17(v)
        }
        "enum" if !s.choices.is_empty() => s.choices[0].clone(),
        _ => String::new(),
    }
}

/// Checks `text` against the declaration (type, range, choices) with the host's rules and gives
/// the text the host would store.
pub fn canon_setting(s: &PluginSetting, text: &str) -> Result<String, SettingError> {
    match s.kind.as_str() {
        "bool" => {
            if text == "true" || text == "false" {
                Ok(text.to_string())
            } else {
                Err(SettingError::BoolExpected)
            }
        }
        "int" => {
            let b = text.as_bytes();
            let mut i = 0usize;
            let mut neg = false;
            if i < b.len() && (b[i] == b'+' || b[i] == b'-') {
                neg = b[i] == b'-';
                i += 1;
            }
            if i == b.len() || b.len() - i > 18 {
                return Err(SettingError::IntegerExpected);
            }
            let mut v: i64 = 0;
            for &c in &b[i..] {
                if !c.is_ascii_digit() {
                    return Err(SettingError::IntegerExpected);
                }
                v = v * 10 + i64::from(c - b'0');
            }
            if neg {
                v = -v;
            }
            if s.has_min && v < s.min_i {
                return Err(SettingError::AtLeast);
            }
            if s.has_max && v > s.max_i {
                return Err(SettingError::AtMost);
            }
            Ok(v.to_string())
        }
        "float" => {
            if text.len() > 64 || !plain_decimal(text) {
                return Err(SettingError::NumberExpected);
            }
            let v = strtod(text);
            if !v.is_finite() {
                return Err(SettingError::FiniteExpected);
            }
            if s.has_min && v < s.min_f {
                return Err(SettingError::AtLeast);
            }
            if s.has_max && v > s.max_f {
                return Err(SettingError::AtMost);
            }
            Ok(format_g17(v))
        }
        "string" => {
            if text.len() > 255 {
                return Err(SettingError::StringLimit);
            }
            if text.bytes().any(|c| !(0x20..=0x7e).contains(&c)) {
                return Err(SettingError::AsciiExpected);
            }
            Ok(text.to_string())
        }
        _ => {
            if s.choices.iter().any(|c| c == text) {
                Ok(text.to_string())
            } else {
                Err(SettingError::ChoiceExpected)
            }
        }
    }
}

/// `name = type [default V] [min N] [max N] [label "text"] [help "text"]`. None when the line
/// has something this reader does not understand (the host would refuse the whole plugin for it).
pub fn parse_setting_line(name: &str, rhs: &str) -> Option<PluginSetting> {
    let mut s = PluginSetting {
        name: name.to_string(),
        ..Default::default()
    };
    let mut i = 0usize;
    let (tok, quoted) = next_token(rhs, &mut i)?;
    if quoted {
        return None;
    }
    let par = tok.find('(');
    s.kind = tok[..par.unwrap_or(tok.len())].to_string();
    if let Some(par) = par {
        if !tok.ends_with(')') {
            return None;
        }
        let list = &tok[par + 1..tok.len() - 1];
        for item in list.split(',') {
            let c = trim_a(item);
            if !c.is_empty() {
                s.choices.push(c.to_string());
            }
        }
    }
    if !matches!(
        s.kind.as_str(),
        "bool" | "int" | "float" | "string" | "enum"
    ) {
        return None;
    }
    if if s.kind == "enum" {
        s.choices.is_empty()
    } else {
        par.is_some()
    } {
        return None;
    }
    while let Some((key, key_quoted)) = next_token(rhs, &mut i) {
        if key_quoted {
            return None;
        }
        let (val, _) = next_token(rhs, &mut i)?;
        match key.as_str() {
            "default" => s.default = val,
            "label" => s.label = val,
            "help" => s.help = val,
            "min" | "max" if s.kind == "int" || s.kind == "float" => {
                let lo = key == "min";
                if s.kind == "int" {
                    let v = strtoll(&val);
                    if lo {
                        s.min_i = v;
                    } else {
                        s.max_i = v;
                    }
                } else {
                    let v = strtod(&val);
                    if lo {
                        s.min_f = v;
                    } else {
                        s.max_f = v;
                    }
                }
                if lo {
                    s.has_min = true;
                } else {
                    s.has_max = true;
                }
            }
            _ => return None,
        }
    }
    s.value = if s.default.is_empty() {
        zero_text(&s)
    } else {
        canon_setting(&s, &s.default).unwrap_or_else(|_| zero_text(&s))
    };
    if s.label.is_empty() {
        s.label = s.name.clone();
    }
    Some(s)
}

/// Reads the text of a `plugin.ini`. `folder` is the plugin's folder name (its id).
pub fn parse_plugin_ini(folder: &str, bytes: &[u8]) -> PluginIni {
    let bytes = bytes.strip_prefix(&[0xEF, 0xBB, 0xBF][..]).unwrap_or(bytes);
    let text = String::from_utf8_lossy(bytes);
    let mut p = PluginIni {
        id: folder.to_string(),
        name: folder.to_string(),
        ..Default::default()
    };
    let mut in_settings = false;
    let mut other_section = false;
    for raw in text.split('\n') {
        let line = trim_a(strip_comment(raw));
        if line.is_empty() {
            continue;
        }
        if line.starts_with('[') {
            in_settings = line == "[settings]";
            other_section = !in_settings;
            continue;
        }
        let Some(eq) = line.find('=') else {
            continue;
        };
        let key = trim_a(&line[..eq]);
        let value = trim_a(&line[eq + 1..]);
        if in_settings {
            if p.settings.len() >= MAX_SETTINGS {
                p.settings_bad = true;
            } else {
                match parse_setting_line(key, value) {
                    Some(s) => p.settings.push(s),
                    None => p.settings_bad = true,
                }
            }
        } else if !other_section {
            match key {
                "name" if !value.is_empty() => p.name = value.to_string(),
                "version" => p.version = value.to_string(),
                "author" => p.author = value.to_string(),
                "description" => p.about = value.to_string(),
                "kind" => p.kind = value.to_string(),
                _ => {}
            }
        }
    }
    p
}

/// The golden text of a parsed plugin (the format `tools/launcher-contract` writes).
pub fn render(p: &PluginIni) -> String {
    let b = |v: bool| if v { "true" } else { "false" };
    let mut t = format!(
        "id={}\nname={}\nversion={}\nauthor={}\nabout={}\nkind={}\nsettings_bad={}\nsettings={}\n",
        p.id,
        p.name,
        p.version,
        p.author,
        p.about,
        p.kind,
        b(p.settings_bad),
        p.settings.len()
    );
    for (i, s) in p.settings.iter().enumerate() {
        let k = format!("s{i}.");
        let (min, max) = if s.kind == "float" {
            (format_g17(s.min_f), format_g17(s.max_f))
        } else {
            (s.min_i.to_string(), s.max_i.to_string())
        };
        t.push_str(&format!(
            "{k}name={}\n{k}type={}\n{k}label={}\n{k}help={}\n{k}default={}\n{k}has_min={}\n{k}has_max={}\n{k}min={min}\n{k}max={max}\n{k}choices={}\n{k}value={}\n{k}kept={}\n",
            s.name,
            s.kind,
            s.label,
            s.help,
            s.default,
            b(s.has_min),
            b(s.has_max),
            s.choices.join("|"),
            s.value,
            b(s.kept)
        ));
    }
    t
}

/// One row of a settings save: the `sco_kv` key and `Some(value)` to write or `None` to delete.
pub type PendingWrite = (String, Option<String>);

/// What saving the edited values of a plugin writes into its settings store
/// (`SavePluginSettings`): `(sco_kv key, Some(value))` to write, `(key, None)` to delete.
///
/// `edits` maps a setting name to the text in the editor; a setting without an entry keeps its
/// current value. With `reset`, every setting that has a kept value is deleted. An edit that does
/// not fit its declaration fails the whole save and writes nothing.
pub fn pending_writes(
    p: &PluginIni,
    edits: &std::collections::HashMap<String, String>,
    reset: bool,
) -> Result<Vec<PendingWrite>, (String, SettingError)> {
    let mut put = Vec::new();
    for s in &p.settings {
        let key = scol_contract::settings_store::key(&s.name);
        if reset {
            if s.kept {
                put.push((key, None));
            }
            continue;
        }
        let text = edits.get(&s.name).unwrap_or(&s.value);
        let canon = canon_setting(s, text).map_err(|e| (s.name.clone(), e))?;
        if canon != s.value {
            put.push((
                key,
                Some(scol_contract::settings_store::value(&s.kind, &canon)),
            ));
        }
    }
    Ok(put)
}

/// Reads each setting's kept value, if the store has one that still fits its declaration
/// (`LoadKept`). `get` returns the stored value for a `sco_kv` key.
pub fn apply_kept(p: &mut PluginIni, get: impl Fn(&str) -> Option<String>) {
    for s in &mut p.settings {
        let Some(stored) = get(&scol_contract::settings_store::key(&s.name)) else {
            continue;
        };
        if let Some((kind, text)) = scol_contract::settings_store::split_value(&stored) {
            if kind == s.kind {
                if let Ok(canon) = canon_setting(s, text) {
                    s.value = canon;
                    s.kept = true;
                }
            }
        }
    }
}
