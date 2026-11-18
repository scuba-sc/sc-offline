// SPDX-License-Identifier: GPL-3.0-only
//! The log redactor of the sc-offline launcher, in Rust.
//!
//! This is the port of `launcher/redact.h`. The crash-report bundle (strict mode) and the read-only
//! MCP server (relaxed mode) share it. Text goes through five steps, in this order:
//!
//! 1. `%5C`, `%2F` and `%40` are decoded when that exposes a path or an address ([`paths_and_emails`]),
//!    then paths under the user's profile and any `Users` path ([`redact_paths`]), e-mail addresses
//!    ([`redact_emails`]) and Windows account SIDs.
//! 2. The digits after geid, accountid, account_id and playerid ([`redact_legacy_ids`]).
//! 3. The Windows account name ([`redact_account_names`]).
//! 4. Identity values learned from the whole file being read, login lines first ([`Identities`],
//!    see [`learn`]).
//! 5. Values of identity keys: handle, nickname, character name, GEID, account and player ids
//!    ([`redact_identity_keys`]).
//!
//! The code works on bytes, as the C++ does, and gives the same bytes back for the same input. The
//! shared vectors in `launcher-rs/tests/mcp-vectors` are written by the C++ code and this crate
//! must reproduce them. Redaction is a best effort for known shapes. Free text can still hold a name
//! that no key or line announced, so a person should read what they share.
//!
//! Nothing here reads a file, opens a socket or runs in game. It was tested with synthetic text only.

#![forbid(unsafe_code)]

mod keys;
mod learn;
mod matcher;
mod paths;

pub use keys::redact_identity_keys;
pub use learn::{
    add_learned, is_login_line, learn, learn_legacy_handles, learn_line, Identities, LoginScanner,
    Prepare, Source, MAX_LEARNED, MAX_LEARNED_STRICT, MAX_LEGACY, MAX_LOGIN_PER_LINE,
    MAX_OTHER_PER_LINE, MAX_PRIORITY, MAX_PRIORITY_PER_SOURCE, MAX_SCAN_LOGIN_BYTES,
    MAX_SCAN_LOGIN_LINES,
};
pub use matcher::{redact_learned, LearnedMatcher};
pub use paths::{
    decode_path_escapes, paths_and_emails, redact_account_names, redact_emails, redact_legacy_ids,
    redact_paths, redact_sids, MIN_ACCOUNT_NAME_LENGTH, SUBSTRING_NAME_LENGTH,
};

/// Longest line `learn` and `LoginScanner` read.
pub const MAX_LEARN_LINE: usize = 16 << 10;
/// Shortest value that is learned.
pub const MIN_LEARNED_LENGTH: usize = 3;
/// Longest value that is learned.
pub const MAX_LEARNED_LENGTH: usize = 64;
/// A shorter number would hide ports, versions and times.
pub const MIN_DIGITS_LEARNED: usize = 7;
/// A shorter learned value is replaced only as a whole token.
pub const SUBSTRING_LEARNED_LENGTH: usize = 6;
/// How far past a key the closing quote or bracket of a value is searched for.
pub const MAX_VALUE_SCAN: usize = 256;

/// ASCII-only lower case of one byte (`LowerAscii`).
pub fn lower_ascii(c: u8) -> u8 {
    c.to_ascii_lowercase()
}

/// ASCII-only lower case of a byte string.
pub fn lower_ascii_bytes(s: &[u8]) -> Vec<u8> {
    s.to_ascii_lowercase()
}

pub(crate) fn is_slash(c: u8) -> bool {
    c == b'\\' || c == b'/'
}

/// `EqualsNoCase`: `word` matches `s` at `at`, ASCII-case-insensitively.
pub(crate) fn equals_nocase(s: &[u8], at: usize, word: &[u8]) -> bool {
    at + word.len() <= s.len() && s[at..at + word.len()].eq_ignore_ascii_case(word)
}

/// The C `std::isspace` of the "C" locale: space, tab, newline, vertical tab, form feed, return.
pub(crate) fn c_isspace(c: u8) -> bool {
    c == b' ' || (b'\t'..=b'\r').contains(&c)
}

/// `std::strchr(set, c) != nullptr`. The C function also finds the terminating NUL, so a NUL byte is
/// "in" every set. The C++ code relies on that in a few places, and so does this port.
pub(crate) fn in_set(set: &[u8], c: u8) -> bool {
    c == 0 || set.contains(&c)
}

/// A letter, digit or non-ASCII byte.
pub(crate) fn is_alnum_byte(c: u8) -> bool {
    c.is_ascii_alphanumeric() || c >= 0x80
}

/// Names that are machine words, not people (`SystemToken`). `low` is lower case.
pub fn system_token(low: &[u8]) -> bool {
    const WORDS: &[&[u8]] = &[
        b"system",
        b"local",
        b"none",
        b"null",
        b"default",
        b"localsystem",
        b"localservice",
        b"networkservice",
        b"anonymous",
        b"nobody",
        b"unknown",
    ];
    WORDS.contains(&low)
}

/// Plain words the logs use (`CommonWord`). `low` is lower case.
pub fn common_word(low: &[u8]) -> bool {
    const COMMON: &[&str] = &[
        "true",
        "false",
        "null",
        "none",
        "empty",
        "default",
        "unknown",
        "invalid",
        "missing",
        "name",
        "user",
        "users",
        "handle",
        "nickname",
        "account",
        "guest",
        "player",
        "players",
        "yes",
        "not",
        "set",
        "redacted",
        "profile",
        "userprofile",
        "the",
        "and",
        "for",
        "with",
        "from",
        "was",
        "were",
        "has",
        "have",
        "are",
        "you",
        "your",
        "this",
        "that",
        "star",
        "citizen",
        "server",
        "client",
        "game",
        "loading",
        "loaded",
        "started",
        "starting",
        "error",
        "warning",
        "info",
        "debug",
        "spawn",
        "killed",
        "entered",
        "left",
        "login",
        "logout",
        "success",
        "failed",
        "failure",
        "status",
        "character",
        "session",
        "admin",
        "local",
        "remote",
        "online",
        "offline",
        "test",
        "tester",
        "main",
        "list",
        "item",
        "items",
        "data",
        "value",
        "type",
        "mode",
    ];
    COMMON.iter().any(|w| w.as_bytes() == low)
}

/// The length of a redaction marker (`<redacted>`, `<user>`, `%USERPROFILE%`) that starts at `s[i]`, or 0.
/// The matcher never matches inside one.
pub fn marker_len(s: &[u8], i: usize) -> usize {
    match s[i] {
        b'<' => {
            if equals_nocase(s, i, b"<redacted>") {
                10
            } else if equals_nocase(s, i, b"<user>") {
                6
            } else {
                0
            }
        }
        b'%' if equals_nocase(s, i, b"%userprofile%") => 13,
        _ => 0,
    }
}

/// Redacts a text with no learned values (`Redact`). Idempotent.
pub fn redact<R: AsRef<[u8]>, N: AsRef<[u8]>>(
    text: &[u8],
    profile_roots: &[R],
    account_names: &[N],
) -> Vec<u8> {
    let id = Identities {
        profile_roots: profile_roots.iter().map(|r| r.as_ref().to_vec()).collect(),
        account_names: account_names.iter().map(|r| r.as_ref().to_vec()).collect(),
        ..Identities::default()
    };
    redact_with(text, &id)
}

/// Everything a string goes through (`RedactWith`). Idempotent.
pub fn redact_with(text: &[u8], id: &Identities) -> Vec<u8> {
    Redactor::new(id).apply(text)
}

/// Redacts many strings with the same identities: the learned values are compiled once (`Redactor`).
#[derive(Clone)]
pub struct Redactor {
    id: Identities,
    matcher: LearnedMatcher,
}

impl Redactor {
    pub fn new(id: &Identities) -> Self {
        let mut values: Vec<Vec<u8>> = id.priority.clone();
        values.extend(id.learned.iter().cloned());
        values.extend(id.legacy.iter().cloned());
        Redactor {
            id: id.clone(),
            matcher: LearnedMatcher::new(&values, id.strict),
        }
    }

    pub fn apply(&self, text: &[u8]) -> Vec<u8> {
        // The key pass runs before the matcher as well as after it: a learned value that equals a key word
        // (a handle called Account) must not turn the key into a marker before the key pass has hidden the
        // value it announces.
        let step = paths_and_emails(text, &self.id.profile_roots);
        let step = redact_legacy_ids(&step);
        let step = redact_account_names(&step, &self.id.account_names);
        let step = redact_identity_keys(&step, None);
        let step = self.matcher.apply(&step);
        redact_identity_keys(&step, None)
    }
}
