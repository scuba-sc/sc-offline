// SPDX-License-Identifier: GPL-3.0-only
//! The signed index: strict parse and the acceptance rules.
//!
//! `index.json` (schema 1) is written by `tools/build_index.py` in sc-plugins in a canonical form
//! (sorted keys, no spaces, ASCII only, one final newline, entries sorted by id). This module does
//! not rely on the form: it checks the signature over the exact bytes it was given, and then parses
//! strictly. An unknown field, a duplicate key, a wrong type, a bad value or an unsorted or
//! duplicated id is an error. Nothing is parsed before the signature is checked.
//!
//! ```text
//! { "schema": 1, "sequence": 7, "issued": "...Z", "expires": "...Z",
//!   "plugins": [ { "id", "name", "version", "kind", "api", "sdk", "license", "maintainers",
//!                  "review", "evidence",
//!                  "source": { "repository", "commit", "path" },
//!                  "zip": { "url", "sha256", "size" } } ] }
//! ```

use scol_plugins::manifest::Kind;
use scol_plugins::sha256;
use scol_plugins::unicode;
use serde::Deserialize;

use crate::error::Error;
use crate::semver::{SdkRange, Version};
use crate::time::parse_utc;
use crate::trust::TrustRoot;

/// Schema number this crate reads. A different number is refused.
pub const SCHEMA: u32 = 1;
/// Cap on `index.json`.
pub const MAX_INDEX_BYTES: usize = 1024 * 1024;
/// Cap on entries.
pub const MAX_ENTRIES: usize = 1024;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Review {
    FirstParty,
    Reviewed,
    CommunityUnreviewed,
}

impl Review {
    pub fn as_str(self) -> &'static str {
        match self {
            Review::FirstParty => "first-party",
            Review::Reviewed => "reviewed",
            Review::CommunityUnreviewed => "community-unreviewed",
        }
    }

    /// A launcher offers only first-party and reviewed plugins.
    pub fn is_listed(self) -> bool {
        !matches!(self, Review::CommunityUnreviewed)
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Entry {
    pub id: String,
    pub name: String,
    pub version: String,
    pub kind: Kind,
    /// The plugin api as `major.minor`.
    pub api: String,
    pub api_major: u16,
    pub api_minor: u16,
    pub sdk: String,
    pub license: String,
    pub maintainers: Vec<String>,
    pub review: Review,
    pub evidence: String,
    pub repository: String,
    pub commit: String,
    pub path: String,
    pub zip_url: String,
    pub zip_sha256: String,
    pub zip_size: u64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Index {
    pub sequence: u64,
    pub issued: String,
    pub expires: String,
    pub issued_unix: i64,
    pub expires_unix: i64,
    pub plugins: Vec<Entry>,
}

impl Index {
    pub fn find(&self, id: &str) -> Option<&Entry> {
        self.plugins.iter().find(|e| e.id == id)
    }
}

/// An index that passed every check, with the digest the launcher records in `installed.json`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AcceptedIndex {
    pub index: Index,
    /// SHA-256 of the exact index bytes, lower-case hex.
    pub digest: String,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct RawIndex {
    schema: u32,
    sequence: u64,
    issued: String,
    expires: String,
    plugins: Vec<RawEntry>,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct RawEntry {
    id: String,
    name: String,
    version: String,
    kind: String,
    api: String,
    sdk: String,
    license: String,
    maintainers: Vec<String>,
    review: String,
    evidence: String,
    source: RawSource,
    zip: RawZip,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct RawSource {
    repository: String,
    commit: String,
    path: String,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct RawZip {
    url: String,
    sha256: String,
    size: u64,
}

fn bad(m: impl Into<String>) -> Error {
    Error::IndexFormat(m.into())
}

fn is_hex(s: &str, n: usize) -> bool {
    s.len() == n
        && s.bytes()
            .all(|c| c.is_ascii_digit() || (b'a'..=b'f').contains(&c))
}

/// 1..=max bytes, no control, format (bidi override, zero width) or surrogate-class characters.
/// The text is shown to the player on the consent screen, so it must not be able to spoof.
fn text_ok(s: &str, max: usize) -> bool {
    !s.is_empty() && s.len() <= max && !s.chars().any(unicode::is_control_format_surrogate)
}

fn id_ok(s: &str) -> bool {
    !s.is_empty()
        && s.len() <= 31
        && s.bytes()
            .all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == b'_')
}

fn api_parts(s: &str) -> Option<(u16, u16)> {
    let (a, b) = s.split_once('.')?;
    let n = |t: &str| -> Option<u16> {
        if t.is_empty()
            || t.len() > 5
            || !t.bytes().all(|c| c.is_ascii_digit())
            || (t.len() > 1 && t.starts_with('0'))
        {
            return None;
        }
        t.parse().ok()
    };
    Some((n(a)?, n(b)?))
}

fn repo_ok(s: &str) -> bool {
    let Some(rest) = s.strip_prefix("https://github.com/") else {
        return false;
    };
    let mut parts = rest.split('/');
    let (Some(o), Some(r), None) = (parts.next(), parts.next(), parts.next()) else {
        return false;
    };
    let seg = |t: &str| {
        !t.is_empty()
            && t != "."
            && t != ".."
            && t.bytes()
                .all(|c| c.is_ascii_alphanumeric() || matches!(c, b'_' | b'.' | b'-'))
    };
    seg(o) && seg(r)
}

fn path_ok(s: &str) -> bool {
    !s.is_empty()
        && s.len() <= 256
        && !s.starts_with('/')
        && s.split('/').all(|p| !p.is_empty() && p != "." && p != "..")
        && s.bytes()
            .all(|c| c.is_ascii_alphanumeric() || matches!(c, b'_' | b'.' | b'/' | b'-'))
}

fn convert(raw: RawEntry) -> Result<Entry, Error> {
    let id = raw.id;
    let field = |what: &str| bad(format!("{id}: bad {what}"));
    if !id_ok(&id) {
        return Err(bad(format!(
            "bad id {:?}",
            id.chars().take(40).collect::<String>()
        )));
    }
    if !text_ok(&raw.name, 63) {
        return Err(field("name"));
    }
    // Same grammar as the Python tool (VERSION_RE): the downgrade guard needs a parseable version.
    if !text_ok(&raw.version, 31) || Version::parse(&raw.version).is_none() {
        return Err(field("version"));
    }
    let kind = match raw.kind.as_str() {
        "native" => Kind::Native,
        "lua" => Kind::Lua,
        "data" => Kind::Data,
        _ => return Err(field("kind")),
    };
    let (api_major, api_minor) = api_parts(&raw.api).ok_or_else(|| field("api"))?;
    if SdkRange::parse(&raw.sdk).is_none() {
        return Err(field("sdk range"));
    }
    if !text_ok(&raw.license, 64) {
        return Err(field("license"));
    }
    if raw.maintainers.is_empty()
        || raw.maintainers.len() > 16
        || !raw.maintainers.iter().all(|m| text_ok(m, 64))
    {
        return Err(field("maintainers"));
    }
    let review = match raw.review.as_str() {
        "first-party" => Review::FirstParty,
        "reviewed" => Review::Reviewed,
        "community-unreviewed" => Review::CommunityUnreviewed,
        _ => return Err(field("review")),
    };
    if !text_ok(&raw.evidence, 1000) {
        return Err(field("evidence"));
    }
    if !repo_ok(&raw.source.repository) {
        return Err(field("source repository"));
    }
    if !is_hex(&raw.source.commit, 40) {
        return Err(field("source commit"));
    }
    if !path_ok(&raw.source.path) {
        return Err(field("source path"));
    }
    if !raw.zip.url.starts_with("https://")
        || !text_ok(&raw.zip.url, 512)
        || raw.zip.url.contains(' ')
    {
        return Err(field("zip url"));
    }
    if !is_hex(&raw.zip.sha256, 64) {
        return Err(field("zip sha256"));
    }
    if raw.zip.size == 0 || raw.zip.size > scol_plugins::verify::MAX_ARCHIVE_BYTES {
        return Err(field("zip size"));
    }
    Ok(Entry {
        id,
        name: raw.name,
        version: raw.version,
        kind,
        api: raw.api,
        api_major,
        api_minor,
        sdk: raw.sdk,
        license: raw.license,
        maintainers: raw.maintainers,
        review,
        evidence: raw.evidence,
        repository: raw.source.repository,
        commit: raw.source.commit,
        path: raw.source.path,
        zip_url: raw.zip.url,
        zip_sha256: raw.zip.sha256,
        zip_size: raw.zip.size,
    })
}

/// Parse an index that is already known to be signed. Strict; does not check expiry or sequence.
pub fn parse_index(bytes: &[u8]) -> Result<Index, Error> {
    let text = std::str::from_utf8(bytes).map_err(|_| bad("not UTF-8"))?;
    let raw: RawIndex = serde_json::from_str(text).map_err(|e| bad(e.to_string()))?;
    if raw.schema != SCHEMA {
        return Err(bad(format!(
            "schema {} is not supported (this launcher reads {SCHEMA})",
            raw.schema
        )));
    }
    let issued_unix =
        parse_utc(&raw.issued).ok_or_else(|| bad("issued is not YYYY-MM-DDTHH:MM:SSZ"))?;
    let expires_unix =
        parse_utc(&raw.expires).ok_or_else(|| bad("expires is not YYYY-MM-DDTHH:MM:SSZ"))?;
    if expires_unix <= issued_unix {
        return Err(bad("expires is not after issued"));
    }
    if raw.plugins.len() > MAX_ENTRIES {
        return Err(bad("too many entries"));
    }
    let mut plugins = Vec::with_capacity(raw.plugins.len());
    for r in raw.plugins {
        plugins.push(convert(r)?);
    }
    for pair in plugins.windows(2) {
        if pair[0].id >= pair[1].id {
            return Err(bad(format!(
                "entries must be sorted by id and unique ({} before {})",
                pair[0].id, pair[1].id
            )));
        }
    }
    Ok(Index {
        sequence: raw.sequence,
        issued: raw.issued,
        expires: raw.expires,
        issued_unix,
        expires_unix,
        plugins,
    })
}

/// An index issued this far ahead of the local clock is still accepted (one day).
pub const MAX_CLOCK_SKEW_SECS: i64 = 24 * 60 * 60;

/// The newest index the launcher accepted before, kept by the caller (for example in a small
/// state file next to `installed.json`) and passed back on the next check.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LastAccepted {
    pub sequence: u64,
    /// SHA-256 of that index's exact bytes, lower-case hex ([`AcceptedIndex::digest`]).
    pub digest: String,
}

/// Everything the launcher checks before it trusts an index.
///
/// * `bytes`, `sig_text`: the downloaded `index.json` and `index.json.sig`.
/// * `trust`: the keys to trust.
/// * `now_unix`: the current time, passed in so tests are deterministic.
/// * `last`: the sequence and digest of the last accepted index (`None` for none), kept by the
///   caller. A lower sequence is a rollback and is refused. The same sequence is accepted only
///   with the same digest (a re-fetch of the same bytes); the same sequence with different bytes is
///   a replay or a reused number and is refused.
///
/// Order: size, signature, parse, expiry, issued, sequence.
pub fn open_index(
    bytes: &[u8],
    sig_text: &str,
    trust: &TrustRoot,
    now_unix: i64,
    last: Option<&LastAccepted>,
) -> Result<AcceptedIndex, Error> {
    if bytes.len() > MAX_INDEX_BYTES {
        return Err(Error::TooLarge(format!(
            "index over {MAX_INDEX_BYTES} bytes"
        )));
    }
    trust.verify(bytes, sig_text)?;
    let index = parse_index(bytes)?;
    if now_unix >= index.expires_unix {
        return Err(Error::Expired {
            expires: index.expires,
        });
    }
    if index.issued_unix > now_unix + MAX_CLOCK_SKEW_SECS {
        return Err(Error::NotYetValid {
            issued: index.issued,
        });
    }
    let digest = sha256::hex_digest(bytes);
    if let Some(last) = last {
        if index.sequence < last.sequence {
            return Err(Error::Rollback {
                got: index.sequence,
                last: last.sequence,
            });
        }
        if index.sequence == last.sequence && digest != last.digest {
            return Err(Error::SequenceReplay {
                sequence: index.sequence,
            });
        }
    }
    Ok(AcceptedIndex { digest, index })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn helpers() {
        assert!(api_parts("1.1").is_some());
        assert!(api_parts("1").is_none());
        assert!(api_parts("01.1").is_none());
        assert!(api_parts("1.1.1").is_none());
        assert!(repo_ok("https://github.com/scuba-sc/sc-creative"));
        assert!(!repo_ok("https://github.com/scuba-sc/sc-creative/x"));
        assert!(!repo_ok("http://github.com/a/b"));
        assert!(!repo_ok("https://github.com/../b"));
        assert!(!repo_ok("https://github.com/a/.."));
        assert!(!repo_ok("https://github.com/./b"));
        assert!(text_ok("Pilot Tools", 63));
        assert!(!text_ok("Pilot\u{202e}Tools", 63));
        assert!(!text_ok("Pi\u{200b}lot", 63));
        assert!(!text_ok("Pi\nlot", 63));
        assert!(text_ok("Pilote \u{e9}t\u{e9}", 63));
        assert!(path_ok("plugins/creative"));
        assert!(!path_ok("../x"));
        assert!(!path_ok("/x"));
        assert!(is_hex(&"a".repeat(40), 40));
        assert!(!is_hex(&"A".repeat(40), 40));
    }
}
