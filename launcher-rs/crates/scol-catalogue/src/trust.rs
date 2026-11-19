// SPDX-License-Identifier: GPL-3.0-only
//! The trust root and the signature file.
//!
//! * Signed message: the ASCII line `sc-plugins-index-v1` and a newline, then the exact bytes of
//!   `index.json`. The prefix keeps a signature from being valid for any other kind of file.
//! * Signature file `index.json.sig`: one line `sc-plugins-index-sig-v1 <key id> <signature>` and a
//!   final newline. The key id is the first 8 bytes of SHA-256 of the 32-byte public key, 16
//!   lower-case hex digits. The signature is 64 bytes, 128 lower-case hex digits.
//! * The trust root is a list of public keys, so a key can be rotated by shipping a launcher that
//!   trusts both for a while. A signature made by a key that is not listed is refused.

use ed25519_compact::{PublicKey, Signature};
use scol_plugins::sha256;

use crate::error::Error;

/// Prefix of the signed message.
pub const SIGNED_PREFIX: &[u8] = b"sc-plugins-index-v1\n";
/// First word of the signature file.
pub const SIG_MAGIC: &str = "sc-plugins-index-sig-v1";

/// The release signing keys the launcher trusts, as 64 lower-case hex digits each.
///
/// Empty on purpose: the owner generates the signing key offline (`tools/sign_index.py keygen` in
/// sc-plugins) and adds its public half here in the release change. With no key, every index is
/// refused with [`Error::NoTrustKeys`]. The TEST key under `tests/` of sc-plugins must never be
/// added here.
pub const PRODUCTION_KEYS: &[&str] = &scol_contract::product::CATALOGUE_TRUST_KEYS;

#[derive(Debug, Clone)]
pub struct TrustKey {
    pub id: String,
    key: PublicKey,
}

#[derive(Debug, Clone, Default)]
pub struct TrustRoot {
    keys: Vec<TrustKey>,
}

pub fn from_hex(s: &str, len: usize) -> Option<Vec<u8>> {
    if s.len() != len * 2 {
        return None;
    }
    let mut out = Vec::with_capacity(len);
    let b = s.as_bytes();
    for pair in b.chunks(2) {
        let hi = nibble(pair[0])?;
        let lo = nibble(pair[1])?;
        out.push(hi << 4 | lo);
    }
    Some(out)
}

fn nibble(c: u8) -> Option<u8> {
    match c {
        b'0'..=b'9' => Some(c - b'0'),
        b'a'..=b'f' => Some(c - b'a' + 10),
        _ => None,
    }
}

/// The key id of a public key: 16 hex digits.
pub fn key_id(public: &[u8]) -> String {
    let digest = sha256::hex_digest(public);
    digest[..16].to_string()
}

impl TrustRoot {
    /// The keys compiled into the launcher.
    pub fn embedded() -> TrustRoot {
        TrustRoot::from_hex_keys(PRODUCTION_KEYS).expect("PRODUCTION_KEYS holds valid keys")
    }

    /// Build a root from hex public keys. `None` if one is not a valid key.
    pub fn from_hex_keys(keys: &[&str]) -> Option<TrustRoot> {
        let mut out = Vec::new();
        for k in keys {
            let raw = from_hex(k, 32)?;
            let key = PublicKey::from_slice(&raw).ok()?;
            out.push(TrustKey {
                id: key_id(&raw),
                key,
            });
        }
        Some(TrustRoot { keys: out })
    }

    pub fn is_empty(&self) -> bool {
        self.keys.is_empty()
    }

    pub fn key_ids(&self) -> Vec<&str> {
        self.keys.iter().map(|k| k.id.as_str()).collect()
    }

    /// Check `sig_text` over `index_bytes`.
    pub fn verify(&self, index_bytes: &[u8], sig_text: &str) -> Result<(), Error> {
        if self.keys.is_empty() {
            return Err(Error::NoTrustKeys);
        }
        let (id, sig) = parse_sig(sig_text)?;
        let Some(key) = self.keys.iter().find(|k| k.id == id) else {
            return Err(Error::UnknownKey(id));
        };
        let mut msg = Vec::with_capacity(SIGNED_PREFIX.len() + index_bytes.len());
        msg.extend_from_slice(SIGNED_PREFIX);
        msg.extend_from_slice(index_bytes);
        key.key.verify(&msg, &sig).map_err(|_| Error::BadSignature)
    }
}

/// Split the signature file into key id and signature.
pub fn parse_sig(text: &str) -> Result<(String, Signature), Error> {
    let bad = |m: &str| Error::SigFormat(m.to_string());
    if text.len() > 256 {
        return Err(bad("too long"));
    }
    let line = text
        .strip_suffix('\n')
        .ok_or_else(|| bad("missing final newline"))?;
    let parts: Vec<&str> = line.split(' ').collect();
    if parts.len() != 3 || parts[0] != SIG_MAGIC {
        return Err(bad(
            "expected `sc-plugins-index-sig-v1 <key id> <signature>`",
        ));
    }
    if parts[1].len() != 16 || from_hex(parts[1], 8).is_none() {
        return Err(bad("key id must be 16 lower-case hex digits"));
    }
    let raw =
        from_hex(parts[2], 64).ok_or_else(|| bad("signature must be 128 lower-case hex digits"))?;
    let sig = Signature::from_slice(&raw).map_err(|_| bad("signature is not 64 bytes"))?;
    Ok((parts[1].to_string(), sig))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hex_rules() {
        assert_eq!(from_hex("00ff", 2), Some(vec![0, 255]));
        assert_eq!(from_hex("00FF", 2), None);
        assert_eq!(from_hex("00f", 2), None);
        assert_eq!(from_hex("zz", 1), None);
    }

    /// The TEST public key (shared fixture) must never become a production key.
    #[test]
    fn test_key_is_not_a_production_key() {
        let test_pub = include_str!("../../../tests/catalogue/golden/TEST-ONLY-public.hex").trim();
        let raw = from_hex(test_pub, 32).expect("test key is 32 bytes of hex");
        let test_id = key_id(&raw);
        for k in PRODUCTION_KEYS {
            assert_ne!(*k, test_pub, "TEST key in PRODUCTION_KEYS");
            let r = from_hex(k, 32).expect("production key hex");
            assert_ne!(key_id(&r), test_id, "TEST key in PRODUCTION_KEYS");
        }
    }

    #[test]
    fn empty_root_refuses() {
        let root = TrustRoot::embedded();
        assert!(root.is_empty());
        assert_eq!(
            root.verify(b"x", "whatever").unwrap_err().code(),
            "no-trust-keys"
        );
    }

    #[test]
    fn sig_format() {
        let good = format!("{SIG_MAGIC} {} {}\n", "0".repeat(16), "0".repeat(128));
        assert!(parse_sig(&good).is_ok());
        for bad in [
            good.trim_end().to_string(),
            good.replace(SIG_MAGIC, "other"),
            good.replace("  ", " ").replacen(' ', "  ", 1),
            format!("{SIG_MAGIC} {} {}\n", "0".repeat(15), "0".repeat(128)),
            format!("{SIG_MAGIC} {} {}\n", "0".repeat(16), "0".repeat(127)),
            format!("{SIG_MAGIC} {} {}\n", "A".repeat(16), "0".repeat(128)),
            format!("{good}{good}"),
        ] {
            assert_eq!(parse_sig(&bad).unwrap_err().code(), "sig-format", "{bad:?}");
        }
    }
}
