// SPDX-License-Identifier: GPL-3.0-only
//! `plugin.ini` parser.
//!
//! This is a port of the host rules as the sco-core SDK checker (`sco-plugin-check`, the
//! `parse_manifest_bytes` and `[settings]` code) states them, one for one:
//!
//! * `key = value` lines before the first `[section]` give the plugin identity. Unknown keys are
//!   skipped, even when repeated. Known keys may appear once.
//! * `;` or `#` starts a comment at the start of a line or after a space or tab. Inside
//!   `[settings]` a comment marker inside double quotes is text.
//! * `[settings]` may appear once. Other sections are skipped.
//! * At most 16 KiB, a UTF-8 byte order mark is skipped, lines end at `\n`, a trailing `\r` is
//!   trimmed. Text is handled as bytes, so long lines and NUL bytes behave as in the host.
//!
//! The parser never reads the file system. Not run in game.

use std::fmt;

pub const MAX_MANIFEST_BYTES: usize = 16 * 1024;
pub const MAX_REQUIRES: usize = 16;
pub const MAX_SETTINGS: usize = 32;
pub const MAX_CHOICES: usize = 16;

/// What kind of plugin the manifest declares.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Native,
    Lua,
    Data,
}

impl Kind {
    pub fn as_str(self) -> &'static str {
        match self {
            Kind::Native => "native",
            Kind::Lua => "lua",
            Kind::Data => "data",
        }
    }
}

/// Type of one `[settings]` entry.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SettingType {
    Bool,
    Int,
    Float,
    String,
    Enum,
}

impl SettingType {
    pub fn as_str(self) -> &'static str {
        match self {
            SettingType::Bool => "bool",
            SettingType::Int => "int",
            SettingType::Float => "float",
            SettingType::String => "string",
            SettingType::Enum => "enum",
        }
    }
}

/// One declared setting. Field texts are kept as written (validated, lossy UTF-8).
#[derive(Debug, Clone, PartialEq)]
pub struct Setting {
    pub name: String,
    pub ty: SettingType,
    pub default: Option<String>,
    pub min: Option<String>,
    pub max: Option<String>,
    pub label: Option<String>,
    pub help: Option<String>,
    pub choices: Vec<String>,
}

/// A parsed, valid manifest.
#[derive(Debug, Clone, PartialEq)]
pub struct Manifest {
    pub id: String,
    pub name: String,
    pub version: String,
    pub author: Option<String>,
    /// The `api` text as written, for example `1.0`.
    pub api: String,
    pub api_major: u16,
    pub api_minor: u16,
    pub kind: Kind,
    /// Raw bytes of `entry`. Valid entry names may hold bytes that are not UTF-8.
    pub entry: Option<Vec<u8>>,
    pub requires: Vec<String>,
    pub settings: Vec<Setting>,
}

impl Manifest {
    /// `entry` as text, when it is valid UTF-8.
    pub fn entry_str(&self) -> Option<&str> {
        self.entry
            .as_deref()
            .and_then(|b| std::str::from_utf8(b).ok())
    }
}

/// The plugin API the host speaks. Used only for the discovery rules (folder installs).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HostApi {
    pub major: u16,
    pub minor: u16,
}

/// Why a manifest was refused. `line` is 1-based, 0 when the problem is not on one line.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ManifestError {
    pub line: usize,
    pub message: String,
}

impl fmt::Display for ManifestError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.line > 0 {
            write!(f, "plugin.ini line {}: {}", self.line, self.message)
        } else {
            write!(f, "plugin.ini: {}", self.message)
        }
    }
}

impl std::error::Error for ManifestError {}

/// Escape bytes so a diagnostic never carries control characters (same scheme as the SDK tool).
pub fn safe_text(bytes: &[u8]) -> String {
    const HEX: &[u8; 16] = b"0123456789ABCDEF";
    let mut out = String::new();
    for &c in bytes {
        if (0x20..=0x7e).contains(&c) && c != b'\\' && c != b'"' {
            out.push(c as char);
        } else if c == b'\\' || c == b'"' {
            out.push('\\');
            out.push(c as char);
        } else {
            out.push_str("\\x");
            out.push(HEX[(c >> 4) as usize] as char);
            out.push(HEX[(c & 15) as usize] as char);
        }
    }
    out
}

fn err_line(line: usize, what: &str) -> ManifestError {
    ManifestError {
        line,
        message: what.to_string(),
    }
}

fn err_setting(line: usize, name: &[u8], what: &str) -> ManifestError {
    ManifestError {
        line,
        message: format!(
            "setting '{}': {}",
            safe_text(name),
            safe_text(what.as_bytes())
        ),
    }
}

// ---- text helpers ---------------------------------------------------------------------------

fn is_space(c: u8) -> bool {
    c == b' ' || c == b'\t'
}

fn trim(mut s: &[u8]) -> &[u8] {
    while let Some((&c, rest)) = s.split_first() {
        if is_space(c) {
            s = rest;
        } else {
            break;
        }
    }
    while let Some((&c, rest)) = s.split_last() {
        if is_space(c) || c == b'\r' {
            s = rest;
        } else {
            break;
        }
    }
    s
}

/// A comment starts with `;` or `#` at the start of the line or after a space or tab.
fn strip_comment(s: &[u8]) -> &[u8] {
    for i in 0..s.len() {
        if (s[i] == b';' || s[i] == b'#') && (i == 0 || is_space(s[i - 1])) {
            return &s[..i];
        }
    }
    s
}

/// Same, but a marker inside double quotes is text.
fn strip_settings_comment(s: &[u8]) -> &[u8] {
    let mut quoted = false;
    for i in 0..s.len() {
        let c = s[i];
        if c == b'"' {
            quoted = !quoted;
        } else if !quoted && (c == b';' || c == b'#') && (i == 0 || is_space(s[i - 1])) {
            return &s[..i];
        }
    }
    s
}

fn id_char(c: u8) -> bool {
    c.is_ascii_lowercase() || c.is_ascii_digit() || c == b'_'
}

fn valid_id(s: &[u8]) -> bool {
    !s.is_empty() && s.len() <= 31 && s.iter().all(|&c| id_char(c))
}

fn reserved_id(s: &[u8]) -> bool {
    matches!(s, b"sco" | b"host" | b"menu" | b"game")
}

/// Lowercase dotted: `teleport`, `spawn.ship`. No empty part.
fn valid_capability(s: &[u8]) -> bool {
    if s.is_empty() || s.len() > 63 || s[0] == b'.' || s[s.len() - 1] == b'.' {
        return false;
    }
    let mut prev = 0u8;
    for &c in s {
        if c == b'.' {
            if prev == b'.' {
                return false;
            }
        } else if !id_char(c) {
            return false;
        }
        prev = c;
    }
    true
}

/// 1..max bytes, printable ASCII and UTF-8 bytes. No control characters.
fn valid_text(s: &[u8], max: usize) -> bool {
    !s.is_empty() && s.len() <= max && s.iter().all(|&c| c >= 0x20 && c != 0x7f)
}

/// A bare file name. No separators, drive, wildcard or reserved character, not `.` or `..`.
fn valid_entry(s: &[u8]) -> bool {
    if s.is_empty() || s.len() > 63 || s == b"." || s == b".." {
        return false;
    }
    s.iter().all(|&c| c >= 0x20 && !b"/\\:*?\"<>|".contains(&c))
}

fn parse_number(d: &[u8]) -> Option<u16> {
    if d.is_empty() || d.len() > 5 {
        return None;
    }
    let mut v: u32 = 0;
    for &c in d {
        if !c.is_ascii_digit() {
            return None;
        }
        v = v * 10 + u32::from(c - b'0');
    }
    u16::try_from(v).ok()
}

fn parse_api(s: &[u8]) -> Option<(u16, u16)> {
    let dot = s.iter().position(|&c| c == b'.')?;
    Some((parse_number(&s[..dot])?, parse_number(&s[dot + 1..])?))
}

fn lossy(s: &[u8]) -> String {
    String::from_utf8_lossy(s).into_owned()
}

/// Split on the first `,`. Returns (head, rest) where rest is None after the last item.
fn split_comma(s: &[u8]) -> (&[u8], Option<&[u8]>) {
    match s.iter().position(|&c| c == b',') {
        Some(i) => (&s[..i], Some(&s[i + 1..])),
        None => (s, None),
    }
}

// ---- [settings] -----------------------------------------------------------------------------

struct Decl {
    ty: SettingType,
    has_min: bool,
    has_max: bool,
    min_i: i64,
    max_i: i64,
    min_f: f64,
    max_f: f64,
    choices: Vec<Vec<u8>>,
}

impl Decl {
    fn new(ty: SettingType) -> Decl {
        Decl {
            ty,
            has_min: false,
            has_max: false,
            min_i: 0,
            max_i: 0,
            min_f: 0.0,
            max_f: 0.0,
            choices: Vec::new(),
        }
    }
}

struct Tok<'a> {
    t: &'a [u8],
    quoted: bool,
}

struct Lexer<'a> {
    s: &'a [u8],
    p: usize,
    bad: bool,
}

impl<'a> Lexer<'a> {
    fn next(&mut self) -> Option<Tok<'a>> {
        let s = self.s;
        while self.p < s.len() && is_space(s[self.p]) {
            self.p += 1;
        }
        if self.p >= s.len() {
            return None;
        }
        if s[self.p] == b'"' {
            let start = self.p + 1;
            let Some(off) = s[start..].iter().position(|&c| c == b'"') else {
                self.bad = true;
                return None;
            };
            let t = &s[start..start + off];
            self.p = start + off + 1;
            if self.p < s.len() && !is_space(s[self.p]) {
                self.bad = true;
                return None;
            }
            return Some(Tok { t, quoted: true });
        }
        let b = self.p;
        while self.p < s.len() && !is_space(s[self.p]) {
            if s[self.p] == b'"' {
                self.bad = true;
                return None;
            }
            self.p += 1;
        }
        Some(Tok {
            t: &s[b..self.p],
            quoted: false,
        })
    }
}

fn valid_word(s: &[u8], max: usize) -> bool {
    !s.is_empty() && s.len() <= max && s.iter().all(|&c| id_char(c))
}

/// `[+-]digits`, at most 19 digits, in int64 range.
fn parse_i64(s: &[u8]) -> Option<i64> {
    let mut i = 0;
    let mut neg = false;
    if let Some(&c) = s.first() {
        if c == b'+' || c == b'-' {
            neg = c == b'-';
            i = 1;
        }
    }
    if i == s.len() || s.len() - i > 19 {
        return None;
    }
    let mut v: u64 = 0;
    for &c in &s[i..] {
        if !c.is_ascii_digit() {
            return None;
        }
        v = v * 10 + u64::from(c - b'0');
    }
    if neg {
        if v > 9_223_372_036_854_775_808 {
            return None;
        }
        Some(if v == 9_223_372_036_854_775_808 {
            i64::MIN
        } else {
            -(v as i64)
        })
    } else {
        i64::try_from(v).ok()
    }
}

/// `[+-]digits[.digits][(e|E)[+-]digits]`, finite. No nan, inf, hex or leading or trailing dot.
fn parse_f64(s: &[u8]) -> Option<f64> {
    if s.len() > 64 {
        return None;
    }
    let mut i = 0;
    if i < s.len() && (s[i] == b'+' || s[i] == b'-') {
        i += 1;
    }
    let d = i;
    while i < s.len() && s[i].is_ascii_digit() {
        i += 1;
    }
    if i == d {
        return None;
    }
    if i < s.len() && s[i] == b'.' {
        i += 1;
        let f = i;
        while i < s.len() && s[i].is_ascii_digit() {
            i += 1;
        }
        if i == f {
            return None;
        }
    }
    if i < s.len() && (s[i] == b'e' || s[i] == b'E') {
        i += 1;
        if i < s.len() && (s[i] == b'+' || s[i] == b'-') {
            i += 1;
        }
        let e = i;
        while i < s.len() && s[i].is_ascii_digit() {
            i += 1;
        }
        if i == e {
            return None;
        }
    }
    if i != s.len() {
        return None;
    }
    let v: f64 = std::str::from_utf8(s).ok()?.parse().ok()?;
    v.is_finite().then_some(v)
}

/// Ok when `text` is a value of `d` (type, range, choice). Err carries the reason.
fn check_setting(d: &Decl, text: &[u8]) -> Result<(), String> {
    match d.ty {
        SettingType::Bool => {
            if text == b"true" || text == b"false" {
                Ok(())
            } else {
                Err(format!("'{}' is not true or false", lossy(text)))
            }
        }
        SettingType::Int => {
            let Some(i) = parse_i64(text) else {
                return Err(format!("'{}' is not an integer", lossy(text)));
            };
            if d.has_min && i < d.min_i {
                return Err(format!("{} is below min {}", i, d.min_i));
            }
            if d.has_max && i > d.max_i {
                return Err(format!("{} is above max {}", i, d.max_i));
            }
            Ok(())
        }
        SettingType::Float => {
            let Some(f) = parse_f64(text) else {
                return Err(format!("'{}' is not a number", lossy(text)));
            };
            if d.has_min && f < d.min_f {
                return Err(format!("{} is below min {}", f, d.min_f));
            }
            if d.has_max && f > d.max_f {
                return Err(format!("{} is above max {}", f, d.max_f));
            }
            Ok(())
        }
        SettingType::String => {
            if text.len() > 255 || text.iter().any(|&c| c < 0x20 || c == 0x7f) {
                Err("a string is at most 255 printable characters".to_string())
            } else {
                Ok(())
            }
        }
        SettingType::Enum => {
            if d.choices.iter().any(|c| c.as_slice() == text) {
                return Ok(());
            }
            let list: Vec<String> = d.choices.iter().map(|c| lossy(c)).collect();
            Err(format!(
                "'{}' is not one of {}",
                lossy(text),
                list.join(", ")
            ))
        }
    }
}

const FIELDS: [&str; 5] = ["default", "min", "max", "label", "help"];
const F_DEFAULT: usize = 0;
const F_MIN: usize = 1;
const F_MAX: usize = 2;
const F_LABEL: usize = 3;
const F_HELP: usize = 4;

/// Parses one declaration (comment stripped, trimmed).
fn setting_line(
    line: &[u8],
    settings: &mut Vec<Setting>,
    lineno: usize,
) -> Result<(), ManifestError> {
    let Some(eq) = line.iter().position(|&c| c == b'=') else {
        return Err(err_line(lineno, "expected name = type ..."));
    };
    let name = trim(&line[..eq]);
    if !valid_word(name, 31) {
        return Err(err_line(lineno, "setting name must be 1-31 of [a-z0-9_]"));
    }
    if settings.iter().any(|s| s.name.as_bytes() == name) {
        return Err(err_setting(lineno, name, "declared twice"));
    }
    if settings.len() == MAX_SETTINGS {
        return Err(err_setting(lineno, name, "more than 32 settings"));
    }

    let rest = &line[eq + 1..];
    let mut lx = Lexer {
        s: rest,
        p: 0,
        bad: false,
    };
    while lx.p < rest.len() && is_space(rest[lx.p]) {
        lx.p += 1;
    }
    let type_text: &[u8];
    if rest.len() - lx.p >= 5 && &rest[lx.p..lx.p + 5] == b"enum(" {
        let Some(off) = rest[lx.p..].iter().position(|&c| c == b')') else {
            return Err(err_setting(lineno, name, "enum( needs a closing )"));
        };
        type_text = &rest[lx.p..lx.p + off + 1];
        lx.p += off + 1;
        if lx.p < rest.len() && !is_space(rest[lx.p]) {
            return Err(err_setting(lineno, name, "expected a space after the type"));
        }
    } else {
        match lx.next() {
            Some(t) if !t.quoted => type_text = t.t,
            _ => {
                return Err(err_setting(
                    lineno,
                    name,
                    "missing type (bool, int, float, string or enum(a,b,c))",
                ))
            }
        }
    }

    let mut d = Decl::new(SettingType::Bool);
    if type_text == b"bool" {
        d.ty = SettingType::Bool;
    } else if type_text == b"int" {
        d.ty = SettingType::Int;
    } else if type_text == b"float" {
        d.ty = SettingType::Float;
    } else if type_text == b"string" {
        d.ty = SettingType::String;
    } else if type_text.len() >= 5 && &type_text[..5] == b"enum(" {
        d.ty = SettingType::Enum;
        // Without "enum(" and ")". The type text ends at the closing parenthesis.
        let mut rest = Some(&type_text[5..type_text.len() - 1]);
        while let Some(r) = rest {
            let (head, tail) = split_comma(r);
            let c = trim(head);
            if !valid_word(c, 31) {
                return Err(err_setting(
                    lineno,
                    name,
                    "enum choices are a comma list of 1-31 of [a-z0-9_]",
                ));
            }
            if d.choices.iter().any(|x| x.as_slice() == c) {
                return Err(err_setting(lineno, name, "enum lists a choice twice"));
            }
            if d.choices.len() == MAX_CHOICES {
                return Err(err_setting(lineno, name, "enum lists more than 16 choices"));
            }
            d.choices.push(c.to_vec());
            rest = tail;
        }
    } else {
        return Err(err_setting(
            lineno,
            name,
            "type must be bool, int, float, string or enum(a,b,c)",
        ));
    }

    let mut have = [false; 5];
    let mut value: [&[u8]; 5] = [&[]; 5];
    while let Some(key) = lx.next() {
        let mut f: Option<usize> = None;
        if !key.quoted {
            for (i, field) in FIELDS.iter().enumerate() {
                if key.t == field.as_bytes() {
                    f = Some(i);
                }
            }
        }
        let Some(f) = f else {
            return Err(err_setting(
                lineno,
                name,
                &format!(
                    "unknown field '{}' (default, min, max, label, help)",
                    lossy(key.t)
                ),
            ));
        };
        if have[f] {
            return Err(err_setting(
                lineno,
                name,
                &format!("field '{}' given twice", FIELDS[f]),
            ));
        }
        let Some(v) = lx.next() else {
            if lx.bad {
                break;
            }
            return Err(err_setting(
                lineno,
                name,
                &format!("field '{}' needs a value", FIELDS[f]),
            ));
        };
        have[f] = true;
        value[f] = v.t;
    }
    if lx.bad {
        return Err(err_setting(
            lineno,
            name,
            "a quote is not closed, or not at the start of a value",
        ));
    }

    if have[F_MIN] || have[F_MAX] {
        if d.ty != SettingType::Int && d.ty != SettingType::Float {
            return Err(err_setting(
                lineno,
                name,
                "min and max apply to int and float only",
            ));
        }
        // The bounds are values of the type, checked before any bound exists.
        let probe = Decl::new(d.ty);
        for f in F_MIN..=F_MAX {
            if !have[f] {
                continue;
            }
            if let Err(why) = check_setting(&probe, value[f]) {
                let label = if f == F_MIN { "min " } else { "max " };
                return Err(err_setting(lineno, name, &format!("{label}{why}")));
            }
            if d.ty == SettingType::Int {
                let b = parse_i64(value[f]).unwrap_or(0);
                if f == F_MIN {
                    d.has_min = true;
                    d.min_i = b;
                } else {
                    d.has_max = true;
                    d.max_i = b;
                }
            } else {
                let b = parse_f64(value[f]).unwrap_or(0.0);
                if f == F_MIN {
                    d.has_min = true;
                    d.min_f = b;
                } else {
                    d.has_max = true;
                    d.max_f = b;
                }
            }
        }
        let inverted = if d.ty == SettingType::Int {
            d.min_i > d.max_i
        } else {
            d.min_f > d.max_f
        };
        if d.has_min && d.has_max && inverted {
            return Err(err_setting(
                lineno,
                name,
                &format!(
                    "min {} is above max {}",
                    lossy(value[F_MIN]),
                    lossy(value[F_MAX])
                ),
            ));
        }
    }
    if have[F_LABEL] && !valid_text(value[F_LABEL], 63) {
        return Err(err_setting(
            lineno,
            name,
            "label must be 1-63 printable characters",
        ));
    }
    if have[F_HELP] && !valid_text(value[F_HELP], 255) {
        return Err(err_setting(
            lineno,
            name,
            "help must be 1-255 printable characters",
        ));
    }
    if have[F_DEFAULT] {
        if let Err(why) = check_setting(&d, value[F_DEFAULT]) {
            return Err(err_setting(lineno, name, &format!("default {why}")));
        }
    }

    let opt = |f: usize| have[f].then(|| lossy(value[f]));
    settings.push(Setting {
        name: lossy(name),
        ty: d.ty,
        default: opt(F_DEFAULT),
        min: opt(F_MIN),
        max: opt(F_MAX),
        label: opt(F_LABEL),
        help: opt(F_HELP),
        choices: d.choices.iter().map(|c| lossy(c)).collect(),
    });
    Ok(())
}

// ---- manifest -------------------------------------------------------------------------------

const KEYS: [&str; 8] = [
    "id", "name", "version", "author", "api", "kind", "entry", "requires",
];
const K_ID: usize = 0;
const K_NAME: usize = 1;
const K_VERSION: usize = 2;
const K_AUTHOR: usize = 3;
const K_API: usize = 4;
const K_KIND: usize = 5;
const K_ENTRY: usize = 6;
const K_REQUIRES: usize = 7;

/// Parse `plugin.ini` bytes.
///
/// `host` switches on the discovery rules a folder install adds: a plugin must not require
/// itself, and its `api` major must equal the host's and its minor must not be newer. The zip
/// verifier passes `None`: it reports the declared API and does not compare it.
pub fn parse(buf: &[u8], host: Option<HostApi>) -> Result<Manifest, ManifestError> {
    if buf.len() > MAX_MANIFEST_BYTES {
        return Err(ManifestError {
            line: 0,
            message: format!("too big (over {MAX_MANIFEST_BYTES} bytes)"),
        });
    }
    let mut all = buf;
    if all.starts_with(&[0xEF, 0xBB, 0xBF]) {
        all = &all[3..];
    }

    let mut seen = [false; 8];
    let mut id = String::new();
    let mut name = String::new();
    let mut version = String::new();
    let mut author: Option<String> = None;
    let mut api = String::new();
    let (mut api_major, mut api_minor) = (0u16, 0u16);
    let mut kind = Kind::Native;
    let mut entry: Option<Vec<u8>> = None;
    let mut requires: Vec<String> = Vec::new();
    let mut settings: Vec<Setting> = Vec::new();

    // 0 top level, 1 [settings], 2 another section
    let mut section = 0;
    let mut seen_settings = false;
    let mut line_no = 0usize;

    while !all.is_empty() {
        let nl = all.iter().position(|&c| c == b'\n');
        let raw = &all[..nl.unwrap_or(all.len())];
        all = &all[raw.len() + usize::from(nl.is_some())..];
        line_no += 1;
        let mut ln = trim(strip_comment(raw));
        if section == 1 {
            ln = trim(strip_settings_comment(raw));
        }
        if ln.is_empty() {
            continue;
        }
        if ln[0] == b'[' {
            if ln[ln.len() - 1] != b']' {
                return Err(err_line(line_no, "a [section] line ends with ]"));
            }
            let sec = trim(&ln[1..ln.len() - 1]);
            if sec == b"settings" {
                if seen_settings {
                    return Err(err_line(line_no, "duplicate section '[settings]'"));
                }
                seen_settings = true;
                section = 1;
            } else {
                // A later minor's: skipped like an unknown key.
                section = 2;
            }
            continue;
        }
        if section == 1 {
            setting_line(ln, &mut settings, line_no)?;
            continue;
        }
        if section == 2 {
            continue;
        }
        let Some(eq) = ln.iter().position(|&c| c == b'=') else {
            return Err(err_line(line_no, "expected key = value"));
        };
        let key = trim(&ln[..eq]);
        let val = trim(&ln[eq + 1..]);
        let Some(k) = KEYS.iter().position(|name| key == name.as_bytes()) else {
            continue; // unknown key, even twice: a later minor's
        };
        if seen[k] {
            return Err(ManifestError {
                line: line_no,
                message: format!("duplicate key '{}'", KEYS[k]),
            });
        }
        seen[k] = true;
        match k {
            K_ID => {
                if !valid_id(val) {
                    return Err(err_line(line_no, "id must be 1-31 of [a-z0-9_]"));
                }
                if reserved_id(val) {
                    return Err(err_line(line_no, "id is reserved"));
                }
                id = lossy(val);
            }
            K_NAME => {
                if !valid_text(val, 63) {
                    return Err(err_line(line_no, "name must be 1-63 printable characters"));
                }
                name = lossy(val);
            }
            K_VERSION => {
                if !valid_text(val, 31) {
                    return Err(err_line(
                        line_no,
                        "version must be 1-31 printable characters",
                    ));
                }
                version = lossy(val);
            }
            K_AUTHOR => {
                if !valid_text(val, 63) {
                    return Err(err_line(
                        line_no,
                        "author must be 1-63 printable characters",
                    ));
                }
                author = Some(lossy(val));
            }
            K_API => {
                let Some((major, minor)) = parse_api(val) else {
                    return Err(err_line(line_no, "api must be <major>.<minor>"));
                };
                api_major = major;
                api_minor = minor;
                api = lossy(val);
            }
            K_KIND => {
                kind = match val {
                    b"native" => Kind::Native,
                    b"lua" => Kind::Lua,
                    b"data" => Kind::Data,
                    _ => return Err(err_line(line_no, "kind must be native, lua or data")),
                };
            }
            K_ENTRY => {
                if !valid_entry(val) {
                    return Err(err_line(
                        line_no,
                        "entry must be a file name in the plugin folder",
                    ));
                }
                entry = Some(val.to_vec());
            }
            K_REQUIRES => {
                let mut rest = Some(val);
                while let Some(r) = rest {
                    let (head, tail) = split_comma(r);
                    let cap = trim(head);
                    if !valid_capability(cap) {
                        return Err(err_line(
                            line_no,
                            "requires must be a comma list of capability names",
                        ));
                    }
                    if requires.iter().any(|x| x.as_bytes() == cap) {
                        return Err(err_line(line_no, "requires lists a capability twice"));
                    }
                    if requires.len() == MAX_REQUIRES {
                        return Err(err_line(
                            line_no,
                            "requires lists more than 16 capabilities",
                        ));
                    }
                    requires.push(lossy(cap));
                    rest = tail;
                }
            }
            _ => unreachable!(),
        }
    }

    for k in [K_ID, K_NAME, K_VERSION, K_API, K_KIND] {
        if !seen[k] {
            return Err(ManifestError {
                line: 0,
                message: format!("missing key '{}'", KEYS[k]),
            });
        }
    }
    if kind == Kind::Data && seen[K_ENTRY] {
        return Err(ManifestError {
            line: 0,
            message: "a data pack has no entry".into(),
        });
    }
    if kind != Kind::Data && !seen[K_ENTRY] {
        return Err(ManifestError {
            line: 0,
            message: "missing key 'entry'".into(),
        });
    }
    if kind == Kind::Data && !settings.is_empty() {
        return Err(ManifestError {
            line: 0,
            message: "a data pack has no settings".into(),
        });
    }
    if let Some(host) = host {
        // A plugin that requires itself or one of its own services is a requires cycle.
        for r in &requires {
            let rest = r.strip_prefix(id.as_str());
            if matches!(rest, Some(x) if x.is_empty() || x.starts_with('.')) {
                return Err(ManifestError {
                    line: 0,
                    message: format!("requires '{r}' names the plugin itself (a requires cycle)"),
                });
            }
        }
        if api_major != host.major || api_minor > host.minor {
            return Err(ManifestError {
                line: 0,
                message: format!("api {api}, but this SDK is {}.{}", host.major, host.minor),
            });
        }
    }
    Ok(Manifest {
        id,
        name,
        version,
        author,
        api,
        api_major,
        api_minor,
        kind,
        entry,
        requires,
        settings,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    const BASE: &str =
        "id = pilot\nname = Pilot\nversion = 1.0\napi = 1.0\nkind = native\nentry = pilot.dll\n";

    fn ok(text: &str) -> Manifest {
        parse(text.as_bytes(), None).unwrap_or_else(|e| panic!("{e}: {text:?}"))
    }

    fn refuse(text: &str) -> ManifestError {
        parse(text.as_bytes(), None).expect_err(text)
    }

    #[test]
    fn minimal_native() {
        let m = ok(BASE);
        assert_eq!(m.id, "pilot");
        assert_eq!(m.name, "Pilot");
        assert_eq!(m.api, "1.0");
        assert_eq!((m.api_major, m.api_minor), (1, 0));
        assert_eq!(m.kind, Kind::Native);
        assert_eq!(m.entry_str(), Some("pilot.dll"));
        assert!(m.requires.is_empty() && m.settings.is_empty() && m.author.is_none());
    }

    #[test]
    fn comments_bom_crlf_and_unknown_keys() {
        let text = format!(
            "\u{feff}; header comment\r\n# another\r\n{}future_key = 1\r\nfuture_key = 2\r\nauthor = A B ; trailing\r\n[future]\r\nid = nonsense\r\n",
            BASE.replace('\n', "\r\n")
        );
        let m = ok(&text);
        assert_eq!(m.id, "pilot");
        assert_eq!(m.author.as_deref(), Some("A B"));
    }

    #[test]
    fn comment_marker_needs_space_or_line_start() {
        let m = ok(&BASE.replace("version = 1.0", "version = 1#b"));
        assert_eq!(m.version, "1#b");
        let m = ok(&BASE.replace("version = 1.0", "version = 1.0 #c"));
        assert_eq!(m.version, "1.0");
    }

    #[test]
    fn identity_keys_after_a_section_are_not_identity() {
        // Everything after [settings] is a setting line, not an identity key.
        let e = refuse(&format!("{BASE}[settings]\nauthor = x\n"));
        assert!(e.message.contains("setting 'author'"), "{e}");
        // Another section is skipped, so a missing key is reported at the end.
        let e = refuse(
            "[other]\nid = pilot\nname = Pilot\nversion = 1\napi = 1.0\nkind = native\nentry = a\n",
        );
        assert_eq!(e.message, "missing key 'id'");
    }

    #[test]
    fn required_keys() {
        for key in ["id", "name", "version", "api", "kind", "entry"] {
            let text: String = BASE
                .lines()
                .filter(|l| !l.starts_with(key))
                .map(|l| format!("{l}\n"))
                .collect();
            let e = refuse(&text);
            assert_eq!(e.message, format!("missing key '{key}'"));
        }
    }

    #[test]
    fn duplicate_known_key() {
        let e = refuse(&format!("{BASE}name = Other\n"));
        assert_eq!(e.message, "duplicate key 'name'");
        assert_eq!(e.line, 7);
    }

    #[test]
    fn id_rules() {
        for bad in [
            "",
            "Pilot",
            "pi-lot",
            "pi lot",
            &"a".repeat(32),
            "sco",
            "host",
            "menu",
            "game",
        ] {
            refuse(&BASE.replace("id = pilot", &format!("id = {bad}")));
        }
        ok(&BASE.replace("id = pilot", &format!("id = {}", "a".repeat(31))));
        ok(&BASE.replace("id = pilot", "id = sco2"));
    }

    #[test]
    fn text_field_rules() {
        refuse(&BASE.replace("name = Pilot", "name ="));
        refuse(&BASE.replace("name = Pilot", &format!("name = {}", "n".repeat(64))));
        ok(&BASE.replace("name = Pilot", &format!("name = {}", "n".repeat(63))));
        refuse(&BASE.replace("version = 1.0", &format!("version = {}", "v".repeat(32))));
        ok(&BASE.replace("version = 1.0", &format!("version = {}", "v".repeat(31))));
        refuse(&format!("{BASE}author = {}\n", "a".repeat(64)));
        refuse(&BASE.replace("name = Pilot", "name = Pi\u{1}lot"));
        refuse(&BASE.replace("name = Pilot", "name = Pi\u{7f}lot"));
        ok(&BASE.replace("name = Pilot", "name = Pil\u{f6}t"));
    }

    #[test]
    fn api_rules() {
        for bad in [
            "1", "1.", ".1", "a.b", "1.0.0", "1.-1", "1.65536", "123456.0", "",
        ] {
            refuse(&BASE.replace("api = 1.0", &format!("api = {bad}")));
        }
        let m = ok(&BASE.replace("api = 1.0", "api = 65535.65535"));
        assert_eq!((m.api_major, m.api_minor), (65535, 65535));
        ok(&BASE.replace("api = 1.0", "api = 00001.00000"));
    }

    #[test]
    fn kind_and_entry_rules() {
        refuse(&BASE.replace("kind = native", "kind = Native"));
        refuse(&BASE.replace("kind = native", "kind = exe"));
        ok(&BASE
            .replace("kind = native", "kind = lua")
            .replace("pilot.dll", "main.lua"));
        // data pack: no entry, no settings.
        let data = "id = pilot\nname = P\nversion = 1\napi = 1.0\nkind = data\n";
        let m = ok(data);
        assert!(m.entry.is_none());
        assert_eq!(
            refuse(&format!("{data}entry = a.txt\n")).message,
            "a data pack has no entry"
        );
        assert_eq!(
            refuse(&format!("{data}[settings]\nx = bool\n")).message,
            "a data pack has no settings"
        );
        for bad in [
            "",
            ".",
            "..",
            "a/b",
            "a\\b",
            "C:x",
            "a*",
            "a?",
            "a\"",
            "a<",
            "a>",
            "a|",
            &"e".repeat(64),
        ] {
            refuse(&BASE.replace("entry = pilot.dll", &format!("entry = {bad}")));
        }
        ok(&BASE.replace("entry = pilot.dll", &format!("entry = {}", "e".repeat(63))));
        // DEL is allowed in an entry name, as in the host.
        ok(&BASE.replace("entry = pilot.dll", "entry = pi\u{7f}lot"));
    }

    #[test]
    fn requires_rules() {
        let m = ok(&format!(
            "{BASE}requires = sco.storage, teleport ,spawn.ship\n"
        ));
        assert_eq!(m.requires, ["sco.storage", "teleport", "spawn.ship"]);
        for bad in [
            "",
            "Teleport",
            ".a",
            "a.",
            "a..b",
            "a,,b",
            "a,",
            ",a",
            "a b",
            "a,a",
            &"r".repeat(64),
        ] {
            refuse(&format!("{BASE}requires = {bad}\n"));
        }
        let many: Vec<String> = (0..16).map(|i| format!("r{i}")).collect();
        ok(&format!("{BASE}requires = {}\n", many.join(",")));
        let many: Vec<String> = (0..17).map(|i| format!("r{i}")).collect();
        refuse(&format!("{BASE}requires = {}\n", many.join(",")));
    }

    #[test]
    fn line_shape_rules() {
        assert_eq!(
            refuse(&format!("{BASE}bare\n")).message,
            "expected key = value"
        );
        assert_eq!(
            refuse(&format!("{BASE}[settings\n")).message,
            "a [section] line ends with ]"
        );
        assert_eq!(
            refuse(&format!("{BASE}[\n")).message,
            "a [section] line ends with ]"
        );
        ok(&format!("{BASE}[ other ]\nanything\n"));
        ok(&format!("{BASE}[ settings ]\n"));
        assert_eq!(
            refuse(&format!("{BASE}[settings]\n[settings]\n")).message,
            "duplicate section '[settings]'"
        );
        // A NUL byte is text, not a terminator, so it fails the printable-text rule.
        let mut bytes = BASE.as_bytes().to_vec();
        bytes.extend_from_slice(b"author = a\0b\n");
        assert!(parse(&bytes, None).is_err());
    }

    #[test]
    fn size_limit() {
        let mut text = BASE.to_string();
        while text.len() < MAX_MANIFEST_BYTES {
            text.push_str("; pad\n");
        }
        text.truncate(MAX_MANIFEST_BYTES);
        assert_eq!(text.len(), MAX_MANIFEST_BYTES);
        ok(&text);
        text.push(';');
        assert_eq!(
            parse(text.as_bytes(), None).unwrap_err().message,
            "too big (over 16384 bytes)"
        );
    }

    #[test]
    fn host_rules_are_optional() {
        let host = Some(HostApi { major: 1, minor: 1 });
        parse(BASE.as_bytes(), host).unwrap();
        let newer = BASE.replace("api = 1.0", "api = 1.2");
        assert!(parse(newer.as_bytes(), None).is_ok());
        assert!(parse(newer.as_bytes(), host)
            .unwrap_err()
            .message
            .starts_with("api 1.2, but this SDK is 1.1"));
        let other_major = BASE.replace("api = 1.0", "api = 2.0");
        assert!(parse(other_major.as_bytes(), host).is_err());
        let cyc = format!("{BASE}requires = pilot\n");
        assert!(parse(cyc.as_bytes(), None).is_ok());
        assert!(parse(cyc.as_bytes(), host)
            .unwrap_err()
            .message
            .contains("requires cycle"));
        let svc = format!("{BASE}requires = pilot.service\n");
        assert!(parse(svc.as_bytes(), host).is_err());
        let near = format!("{BASE}requires = pilot2\n");
        assert!(parse(near.as_bytes(), host).is_ok());
    }

    // ---- [settings] ----

    fn settings(body: &str) -> Vec<Setting> {
        ok(&format!("{BASE}[settings]\n{body}")).settings
    }

    fn bad_setting(body: &str) -> ManifestError {
        refuse(&format!("{BASE}[settings]\n{body}"))
    }

    #[test]
    fn settings_types_and_fields() {
        let s = settings(
            "enabled = bool default true label \"Enabled\" help \"Turn it on\"\n\
             count = int min 1 max 10 default 5\n\
             ratio = float min -1.5 max 1e2 default 0.25\n\
             title = string default \"hello ; world\"\n\
             mode = enum(fast,slow) default slow\n",
        );
        assert_eq!(s.len(), 5);
        assert_eq!(s[0].ty, SettingType::Bool);
        assert_eq!(s[0].label.as_deref(), Some("Enabled"));
        assert_eq!(s[1].min.as_deref(), Some("1"));
        assert_eq!(s[2].ty, SettingType::Float);
        assert_eq!(s[3].default.as_deref(), Some("hello ; world"));
        assert_eq!(s[4].choices, ["fast", "slow"]);
        assert_eq!(s[4].default.as_deref(), Some("slow"));
    }

    #[test]
    fn settings_comments() {
        let s = settings("a = bool ; comment\n# whole line\nb = string default \"x # y\" # real\n");
        assert_eq!(s.len(), 2);
        assert_eq!(s[1].default.as_deref(), Some("x # y"));
    }

    #[test]
    fn settings_bad_declarations() {
        for body in [
            "noequals\n",
            "Bad = bool\n",
            "= bool\n",
            "a =\n",
            "a = quux\n",
            "a = \"bool\"\n",
            "a = bool\na = bool\n",
            "a = bool default maybe\n",
            "a = bool default\n",
            "a = bool default true default true\n",
            "a = bool wibble 1\n",
            "a = bool min 1\n",
            "a = int default 1.5\n",
            "a = int default 99999999999999999999\n",
            "a = int min 5 max 1\n",
            "a = int min 1 max 3 default 4\n",
            "a = int min 1 max 3 default 0\n",
            "a = int min x\n",
            "a = float default nan\n",
            "a = float default inf\n",
            "a = float default 0x10\n",
            "a = float default .5\n",
            "a = float default 5.\n",
            "a = float default 1e999\n",
            "a = float min 2 max 1\n",
            "a = string default \"unterminated\n",
            "a = string default x\"y\n",
            "a = string default \"x\"y\n",
            "a = bool label \"\"\n",
            "a = bool help \"\"\n",
            "a = enum(a,a)\n",
            "a = enum()\n",
            "a = enum(a,)\n",
            "a = enum(A)\n",
            "a = enum(a\n",
            "a = enum(a)x\n",
            "a = enum(a,b) default c\n",
        ] {
            bad_setting(body);
        }
    }

    #[test]
    fn settings_limits() {
        let many: String = (0..32).map(|i| format!("s{i} = bool\n")).collect();
        assert_eq!(settings(&many).len(), 32);
        let too_many: String = (0..33).map(|i| format!("s{i} = bool\n")).collect();
        assert!(bad_setting(&too_many)
            .message
            .contains("more than 32 settings"));
        assert_eq!(settings(&format!("{} = bool\n", "n".repeat(31))).len(), 1);
        bad_setting(&format!("{} = bool\n", "n".repeat(32)));
        let choices: Vec<String> = (0..16).map(|i| format!("c{i}")).collect();
        settings(&format!("a = enum({})\n", choices.join(",")));
        let choices: Vec<String> = (0..17).map(|i| format!("c{i}")).collect();
        bad_setting(&format!("a = enum({})\n", choices.join(",")));
        settings(&format!("a = string default \"{}\"\n", "x".repeat(255)));
        bad_setting(&format!("a = string default \"{}\"\n", "x".repeat(256)));
        settings(&format!("a = bool label \"{}\"\n", "x".repeat(63)));
        bad_setting(&format!("a = bool label \"{}\"\n", "x".repeat(64)));
        settings(&format!("a = bool help \"{}\"\n", "x".repeat(255)));
        bad_setting(&format!("a = bool help \"{}\"\n", "x".repeat(256)));
    }

    #[test]
    fn int_edges() {
        settings("a = int default 9223372036854775807\n");
        settings("a = int default -9223372036854775808\n");
        bad_setting("a = int default 9223372036854775808\n");
        bad_setting("a = int default -9223372036854775809\n");
        settings("a = int default +0000000000000000007\n");
        bad_setting("a = int default 00000000000000000007\n");
    }

    #[test]
    fn diagnostics_escape_control_bytes() {
        let e = parse(
            format!("{BASE}[settings]\nenabled = bool default \u{1b}[2J\n").as_bytes(),
            None,
        )
        .unwrap_err();
        let text = e.to_string();
        assert!(!text.contains('\u{1b}'), "{text:?}");
        assert!(text.contains("\\x1B[2J"), "{text}");
    }
}
