// SPDX-License-Identifier: GPL-3.0-only
//! The one error type of the crate. Each variant has a stable `code()` for logs and tests.

use std::fmt;

use scol_plugins::verify::VerifyError;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Error {
    /// The index or a downloaded file is over its byte cap.
    TooLarge(String),
    /// The signature file is not in the documented format.
    SigFormat(String),
    /// The signature names a key that is not in the trust root.
    UnknownKey(String),
    /// The signature does not match the bytes.
    BadSignature,
    /// The trust root has no keys, so nothing can be verified.
    NoTrustKeys,
    /// The index is not valid JSON of the documented shape, or a field breaks a rule.
    IndexFormat(String),
    /// The index is past its expiry.
    Expired { expires: String },
    /// The index says it was issued later than now (beyond the clock skew allowance).
    NotYetValid { issued: String },
    /// The index sequence is below the last accepted one.
    Rollback { got: u64, last: u64 },
    /// The index has the last accepted sequence but different bytes.
    SequenceReplay { sequence: u64 },
    /// The id is reserved for the host.
    ReservedId(String),
    /// The review level is not offered by a launcher.
    NotListed { id: String, review: String },
    /// The plugin api major differs or its minor is newer than the host's.
    IncompatibleApi {
        id: String,
        plugin: String,
        host: String,
    },
    /// The SDK range does not match the SDK the host is pinned to.
    IncompatibleSdk {
        id: String,
        range: String,
        host: String,
    },
    /// A folder with this id exists and is not a catalogue install.
    LocalClash(String),
    /// The installed version is newer and the downgrade was not confirmed.
    Downgrade {
        id: String,
        installed: String,
        offered: String,
    },
    /// The download URL breaks the rules (scheme, host, form).
    BadUrl(String),
    /// The fetcher failed.
    Fetch(String),
    /// The zip size or SHA-256 differs from the index.
    ZipDigest(String),
    /// The zip verifier refused the archive.
    ZipRefused(VerifyError),
    /// The zip passes the verifier but does not match the index entry.
    ZipMismatch(String),
    /// installed.json is not valid.
    InstalledFormat(String),
    /// A file could not be read.
    Io(String),
}

impl Error {
    pub fn code(&self) -> &'static str {
        match self {
            Error::TooLarge(_) => "too-large",
            Error::SigFormat(_) => "sig-format",
            Error::UnknownKey(_) => "unknown-key",
            Error::BadSignature => "bad-signature",
            Error::NoTrustKeys => "no-trust-keys",
            Error::IndexFormat(_) => "index-format",
            Error::Expired { .. } => "expired",
            Error::NotYetValid { .. } => "not-yet-valid",
            Error::Rollback { .. } => "rollback",
            Error::SequenceReplay { .. } => "sequence-replay",
            Error::ReservedId(_) => "reserved-id",
            Error::NotListed { .. } => "not-listed",
            Error::IncompatibleApi { .. } => "incompatible-api",
            Error::IncompatibleSdk { .. } => "incompatible-sdk",
            Error::LocalClash(_) => "local-clash",
            Error::Downgrade { .. } => "downgrade",
            Error::BadUrl(_) => "bad-url",
            Error::Fetch(_) => "fetch",
            Error::ZipDigest(_) => "zip-digest",
            Error::ZipRefused(_) => "zip-refused",
            Error::ZipMismatch(_) => "zip-mismatch",
            Error::InstalledFormat(_) => "installed-format",
            Error::Io(_) => "io",
        }
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::TooLarge(m) => write!(f, "too large: {m}"),
            Error::SigFormat(m) => write!(f, "bad signature file: {m}"),
            Error::UnknownKey(k) => {
                write!(f, "signed by key {k}, which this launcher does not trust")
            }
            Error::BadSignature => write!(f, "the index signature does not match"),
            Error::NoTrustKeys => {
                write!(
                    f,
                    "this launcher has no catalogue signing key, so no index can be trusted"
                )
            }
            Error::IndexFormat(m) => write!(f, "bad index: {m}"),
            Error::Expired { expires } => write!(f, "the index expired at {expires}"),
            Error::NotYetValid { issued } => {
                write!(
                    f,
                    "the index says it was issued at {issued}, which is in the future"
                )
            }
            Error::Rollback { got, last } => {
                write!(
                    f,
                    "index sequence {got} is older than the last accepted {last}"
                )
            }
            Error::SequenceReplay { sequence } => write!(
                f,
                "index sequence {sequence} was already accepted with different contents"
            ),
            Error::ReservedId(id) => write!(f, "id {id} is reserved"),
            Error::NotListed { id, review } => {
                write!(f, "{id} has review level {review}, which is not offered")
            }
            Error::IncompatibleApi { id, plugin, host } => {
                write!(f, "{id} needs plugin api {plugin}; this host speaks {host}")
            }
            Error::IncompatibleSdk { id, range, host } => {
                write!(f, "{id} needs SDK {range}; this host is on {host}")
            }
            Error::LocalClash(id) => write!(
                f,
                "a local plugin folder named {id} exists; it is not replaced without a confirm"
            ),
            Error::Downgrade {
                id,
                installed,
                offered,
            } => write!(
                f,
                "{id} {installed} is installed; {offered} is older (a downgrade needs a confirm)"
            ),
            Error::BadUrl(m) => write!(f, "bad download url: {m}"),
            Error::Fetch(m) => write!(f, "download failed: {m}"),
            Error::ZipDigest(m) => write!(f, "download does not match the index: {m}"),
            Error::ZipRefused(e) => write!(f, "{e}"),
            Error::ZipMismatch(m) => write!(f, "zip does not match the index entry: {m}"),
            Error::InstalledFormat(m) => write!(f, "bad installed.json: {m}"),
            Error::Io(m) => write!(f, "{m}"),
        }
    }
}

impl std::error::Error for Error {}
