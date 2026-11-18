// SPDX-License-Identifier: GPL-3.0-only
//! Identity values learned from the text (`Identities`, `Learn`, `LoginScanner` of `launcher/redact.h`).

use std::collections::HashSet;

use crate::{
    common_word, redact_identity_keys, system_token, MAX_LEARNED_LENGTH, MAX_LEARN_LINE,
    MIN_DIGITS_LEARNED, MIN_LEARNED_LENGTH,
};

/// Values from real login lines: the user's own, never evicted (5 sources x 64).
pub const MAX_PRIORITY: usize = 384;
/// Values from every other line (relaxed mode); a full list takes no more.
pub const MAX_LEARNED: usize = 128;
/// The same in strict mode (crash reports).
pub const MAX_LEARNED_STRICT: usize = 1024;
/// The crash redactor's own handle shapes (strict mode): a separate list, so noise cannot starve them.
pub const MAX_LEGACY: usize = 16384;
/// Entries (a value and its words each count) one login line can add.
pub const MAX_LOGIN_PER_LINE: usize = 32;
/// Entries one other line can add (strict mode: `MAX_LOGIN_PER_LINE`).
pub const MAX_OTHER_PER_LINE: usize = 8;
/// Priority values one log file, or one text given to `learn`, can add.
pub const MAX_PRIORITY_PER_SOURCE: usize = 64;
/// Login lines parsed per scanned file.
pub const MAX_SCAN_LOGIN_LINES: usize = 4096;
/// Bytes of login lines parsed per scanned file.
pub const MAX_SCAN_LOGIN_BYTES: usize = 4 << 20;

/// The values learned from a file, the profile roots and the Windows account names: what `redact_with`
/// knows about.
#[derive(Clone, Default, Debug)]
pub struct Identities {
    /// USERPROFILE, for `redact_paths`.
    pub profile_roots: Vec<Vec<u8>>,
    /// The Windows account name, for `redact_account_names`.
    pub account_names: Vec<Vec<u8>>,
    /// Values from real login lines ("User Login Success", "AccountLoginCharacterStatus"): the user's own
    /// handle, nickname, character name and ids. At most `MAX_PRIORITY`; once full, a new one is dropped,
    /// never an old one.
    pub priority: Vec<Vec<u8>>,
    /// Values from every other line. At most `MAX_LEARNED` (`MAX_LEARNED_STRICT` in strict mode); once full,
    /// a new one is dropped.
    pub learned: Vec<Vec<u8>>,
    /// Strict mode only: the crash redactor's own handle shapes, a list of their own (at most `MAX_LEGACY`).
    pub legacy: Vec<Vec<u8>>,
    /// How many values were refused because a list was full: login-line values and legacy handle shapes.
    pub priority_dropped: usize,
    pub legacy_dropped: usize,
    /// Strict mode (crash reports): every learned value is hidden anywhere, also inside other words. It
    /// over-redacts on purpose.
    pub strict: bool,
    /// The lower-case form of every value in priority, learned and legacy.
    pub seen: HashSet<Vec<u8>>,
}

impl Identities {
    /// An identity set with the given lists set directly (the `seen` set is rebuilt from them).
    pub fn with_lists(priority: Vec<Vec<u8>>, learned: Vec<Vec<u8>>, legacy: Vec<Vec<u8>>) -> Self {
        let mut id = Identities {
            priority,
            learned,
            legacy,
            ..Identities::default()
        };
        id.seen = id
            .priority
            .iter()
            .chain(id.learned.iter())
            .chain(id.legacy.iter())
            .map(|v| v.to_ascii_lowercase())
            .collect();
        id
    }
}

/// Where a learned value came from.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Source {
    Other,
    Login,
    /// The crash redactor's own handle shapes (strict mode only).
    Legacy,
}

/// Adds a value to a learned list when it is worth replacing everywhere: 3 to 64 characters, not a marker, a
/// hex number or one of the machine words of `system_token`, and not already known (`AddLearned`).
/// `allow_common` false (the words of a multi-word name) refuses common words always.
pub fn add_learned(
    id: &mut Identities,
    v: &[u8],
    src: Source,
    allow_common: bool,
    priority_room: usize,
) {
    let mut v = v.to_vec();
    while matches!(v.last(), Some(b' ' | b'\t' | b']')) {
        v.pop();
    }
    let a = v
        .iter()
        .take_while(|&&c| c == b' ' || c == b'\t' || c == b'[')
        .count();
    v.drain(..a);
    if v.len() < MIN_LEARNED_LENGTH || v.len() > MAX_LEARNED_LENGTH {
        return;
    }
    if v.iter().any(|c| b"<>%\r\n".contains(c)) {
        return;
    }
    let low = v.to_ascii_lowercase();
    // `handle=0x7ff...` style system handles are not names: skipped when they come from an ordinary line in
    // relaxed mode.
    if low.starts_with(b"0x") && src == Source::Other && !id.strict {
        return;
    }
    let strict_own = id.strict && (src == Source::Legacy || src == Source::Login) && allow_common;
    let own_value = allow_common && (src == Source::Login || (src == Source::Legacy && id.strict));
    // A machine word (SYSTEM, Nobody) is never taught, except in strict mode from a login line or a legacy
    // handle shape.
    if !strict_own && system_token(&low) {
        return;
    }
    // Pieces of a marker (red, ted, use) from an ordinary line are not names.
    if src == Source::Other && !strict_own {
        for m in [&b"<redacted>"[..], b"<user>", b"%userprofile%"] {
            if m.windows(low.len()).any(|w| w == &low[..]) {
                return;
            }
        }
    }
    if common_word(&low) && !own_value {
        return;
    }
    let short_number = v.len() < MIN_DIGITS_LEARNED && v.iter().all(|c| c.is_ascii_digit());
    if short_number && !(src == Source::Legacy && id.strict) {
        return;
    }
    if id.seen.contains(&low) {
        return;
    }
    match src {
        Source::Login => {
            if id.priority.len() >= MAX_PRIORITY || priority_room == 0 {
                id.priority_dropped += 1;
                return;
            }
            id.seen.insert(low);
            id.priority.push(v);
        }
        Source::Legacy => {
            if id.legacy.len() >= MAX_LEGACY {
                id.legacy_dropped += 1;
                return;
            }
            id.seen.insert(low);
            id.legacy.push(v);
        }
        Source::Other => {
            if id.learned.len()
                >= if id.strict {
                    MAX_LEARNED_STRICT
                } else {
                    MAX_LEARNED
                }
            {
                return;
            }
            id.seen.insert(low);
            id.learned.push(v);
        }
    }
}

/// The old crash redactor's handle shapes: the plain text between `Handle[` and `]`, `Nickname[` and `]`,
/// `nickname="` and `"`, `handle="` and `"`, when it is 3 to 64 letters, digits, `_` or `-`. Strict mode only:
/// `learn` calls it (`LearnLegacyHandles`).
pub fn learn_legacy_handles(id: &mut Identities, text: &[u8]) {
    for key in [&b"Handle["[..], b"Nickname[", b"nickname=\"", b"handle=\""] {
        let kl = key.len();
        let close = if key[kl - 1] == b'[' { b']' } else { b'"' };
        let mut from = 0;
        while let Some(p) = find(text, key, from) {
            from = p + 1;
            let a = p + kl;
            let mut b = a;
            while b < text.len() && b - a <= 64 && text[b] != close {
                b += 1;
            }
            if b >= text.len() || text[b] != close || b == a || b - a > 64 {
                continue;
            }
            let plain = text[a..b]
                .iter()
                .all(|&c| c.is_ascii_alphanumeric() || c == b'_' || c == b'-');
            if plain {
                add_learned(id, &text[a..b], Source::Legacy, true, MAX_PRIORITY);
            }
        }
    }
}

pub(crate) fn find(hay: &[u8], needle: &[u8], from: usize) -> Option<usize> {
    if needle.is_empty() {
        return if from <= hay.len() { Some(from) } else { None };
    }
    if from >= hay.len() || hay.len() - from < needle.len() {
        return None;
    }
    hay[from..]
        .windows(needle.len())
        .position(|w| w == needle)
        .map(|p| p + from)
}

fn contains_nocase(s: &[u8], word: &[u8]) -> bool {
    if word.is_empty() {
        return true;
    }
    s.windows(word.len()).any(|w| w.eq_ignore_ascii_case(word))
}

/// A line of a real login: what the game writes when the user signs in and when it names the character.
pub fn is_login_line(line: &[u8]) -> bool {
    contains_nocase(line, b"user login success")
        || contains_nocase(line, b"accountlogincharacterstatus")
}

/// Learns from one line (at most `MAX_LEARN_LINE` of it): the values the identity keys announce (`LearnLine`).
/// A login line's values go to the priority list, at most `*priority_room` new ones (when given; the room
/// shrinks by what was added); any other line adds at most `MAX_OTHER_PER_LINE` entries.
pub fn learn_line(
    id: &mut Identities,
    raw_line: &[u8],
    login: bool,
    priority_room: Option<&mut usize>,
) {
    let line = &raw_line[..raw_line.len().min(MAX_LEARN_LINE)];
    let mut found: Vec<Vec<u8>> = Vec::new();
    redact_identity_keys(line, Some(&mut found));
    let cap = if login || id.strict {
        MAX_LOGIN_PER_LINE
    } else {
        MAX_OTHER_PER_LINE
    };
    let src = if login { Source::Login } else { Source::Other };
    let before = id.priority.len();
    let room = priority_room.as_deref().copied().unwrap_or(MAX_PRIORITY);
    let mut used = 0usize;
    let mut add = |id: &mut Identities, v: &[u8], allow_common: bool| -> bool {
        if used >= cap {
            return false;
        }
        used += 1;
        let added = id.priority.len() - before;
        add_learned(id, v, src, allow_common, room.saturating_sub(added));
        true
    };
    for v in &found {
        if !add(id, v, true) {
            break;
        }
        if !v.iter().any(|&c| c == b' ' || c == b'\t') {
            continue;
        }
        let mut a = 0;
        while a < v.len() {
            while a < v.len() && !v[a].is_ascii_alphanumeric() && v[a] < 0x80 {
                a += 1;
            }
            let mut b = a;
            while b < v.len() && (v[b].is_ascii_alphanumeric() || v[b] >= 0x80) {
                b += 1;
            }
            if b > a && (a > 0 || b < v.len()) && !add(id, &v[a..b], false) {
                break;
            }
            a = if b == a { a + 1 } else { b };
        }
    }
    if let Some(r) = priority_room {
        let added = id.priority.len() - before;
        *r -= (*r).min(added);
    }
}

/// Reads every identity value the text announces into the learned lists (`Learn`). Only a line that shows a
/// login shape ranks first, and the text as a whole may add at most `MAX_PRIORITY_PER_SOURCE` such values.
pub fn learn(id: &mut Identities, text: &[u8]) {
    if id.strict {
        learn_legacy_handles(id, text);
    }
    let mut room = MAX_PRIORITY_PER_SOURCE;
    let mut pos = 0;
    while pos < text.len() {
        let nl = text[pos..]
            .iter()
            .position(|&c| c == b'\n')
            .map_or(text.len(), |p| p + pos);
        let line = &text[pos..pos + (nl - pos).min(MAX_LEARN_LINE)];
        pos = nl + 1;
        learn_line(id, line, is_login_line(line), Some(&mut room));
    }
}

/// Cleans a log line before it is parsed (for example makes it valid UTF-8).
pub type Prepare = fn(&[u8]) -> Vec<u8>;

/// Feeds a file in chunks of any size and learns from the login lines only (`LoginScanner`). A line is cut at
/// its newline across chunks. Only the first `MAX_LEARN_LINE` bytes of a line are kept. The work is bounded
/// whatever the file holds: at most `MAX_SCAN_LOGIN_LINES` login lines and `MAX_SCAN_LOGIN_BYTES` bytes of
/// them are parsed, and the file adds at most `MAX_PRIORITY_PER_SOURCE` priority values.
pub struct LoginScanner<'a> {
    id: &'a mut Identities,
    prepare: Option<Prepare>,
    line: Vec<u8>,
    skipping: bool,
    capped: bool,
    cut_login: bool,
    lines: usize,
    bytes: usize,
    room: usize,
}

impl<'a> LoginScanner<'a> {
    /// `prepare` cleans a line first (for example makes it valid UTF-8).
    pub fn new(id: &'a mut Identities, prepare: Option<Prepare>) -> Self {
        LoginScanner {
            id,
            prepare,
            line: Vec::new(),
            skipping: false,
            capped: false,
            cut_login: false,
            lines: 0,
            bytes: 0,
            room: MAX_PRIORITY_PER_SOURCE,
        }
    }

    pub fn feed(&mut self, data: &[u8]) {
        let n = data.len();
        let mut at = 0;
        while at < n {
            let nl = data[at..].iter().position(|&c| c == b'\n').map(|p| p + at);
            let end = nl.unwrap_or(n);
            if !self.skipping {
                let room = MAX_LEARN_LINE - MAX_LEARN_LINE.min(self.line.len());
                let take = (end - at).min(room);
                self.line.extend_from_slice(&data[at..at + take]);
                if take < end - at {
                    self.skipping = true; // keep the first MAX_LEARN_LINE bytes, drop the rest
                }
            }
            at = end;
            if nl.is_some() {
                at += 1;
                self.end_line();
            }
        }
    }

    /// The last line of a file may have no newline.
    pub fn finish(&mut self) {
        self.end_line();
    }

    /// A login line was left out: too many, or too much text.
    pub fn capped(&self) -> bool {
        self.capped
    }

    /// A login line was longer than `MAX_LEARN_LINE`: only its start was read.
    pub fn cut_login(&self) -> bool {
        self.cut_login
    }

    fn end_line(&mut self) {
        if !self.line.is_empty() {
            while self.line.last() == Some(&b'\r') {
                self.line.pop();
            }
            if is_login_line(&self.line) {
                if self.skipping {
                    self.cut_login = true;
                }
                if self.lines >= MAX_SCAN_LOGIN_LINES || self.bytes >= MAX_SCAN_LOGIN_BYTES {
                    self.capped = true;
                } else {
                    self.lines += 1;
                    self.bytes += self.line.len();
                    let prepared = match self.prepare {
                        Some(p) => p(&self.line),
                        None => self.line.clone(),
                    };
                    learn_line(self.id, &prepared, true, Some(&mut self.room));
                }
            }
        }
        self.line.clear();
        self.skipping = false;
    }
}
