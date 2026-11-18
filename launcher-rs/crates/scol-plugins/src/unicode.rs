// SPDX-License-Identifier: GPL-3.0-only
//! The few Unicode operations the zip verifier needs, matching the Python tool.
//!
//! * NFC and NFKC come from the `unicode-normalization` crate.
//! * Full case folding, the control/format/surrogate category test and the cp437 table come from
//!   `unicode_data.rs`, which is generated from Python (see `tools/gen_unicode_data.py`).
//! * Upper-casing for the reserved device names uses the standard library.

use crate::unicode_data::{CASEFOLD, CONTROL_FORMAT_RANGES, CP437_HIGH};
use unicode_normalization::UnicodeNormalization;

pub use crate::unicode_data::PYTHON_UNICODE_VERSION;

/// `str.casefold()`.
pub fn casefold(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for c in s.chars() {
        match CASEFOLD.binary_search_by_key(&(c as u32), |&(cp, _)| cp) {
            Ok(i) => out.push_str(CASEFOLD[i].1),
            Err(_) => out.push(c),
        }
    }
    out
}

/// True when the general category of `c` is Cc, Cf or Cs.
pub fn is_control_format_surrogate(c: char) -> bool {
    let cp = c as u32;
    CONTROL_FORMAT_RANGES
        .binary_search_by(|&(lo, hi)| {
            if cp < lo {
                std::cmp::Ordering::Greater
            } else if cp > hi {
                std::cmp::Ordering::Less
            } else {
                std::cmp::Ordering::Equal
            }
        })
        .is_ok()
}

pub fn nfc(s: &str) -> String {
    s.nfc().collect()
}

pub fn nfkc(s: &str) -> String {
    s.nfkc().collect()
}

/// `str.upper()` for the device-name test.
pub fn upper(s: &str) -> String {
    s.to_uppercase()
}

/// Decode bytes the way Python's `cp437` codec does (ZIP names without the UTF-8 flag).
pub fn cp437_decode(bytes: &[u8]) -> String {
    bytes
        .iter()
        .map(|&b| {
            if b < 0x80 {
                b as char
            } else {
                CP437_HIGH[(b - 0x80) as usize]
            }
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn casefold_examples() {
        assert_eq!(casefold("Stra\u{df}e"), "strasse");
        assert_eq!(casefold("\u{1e9e}"), "ss");
        assert_eq!(casefold("\u{3a3}"), "\u{3c3}");
        assert_eq!(casefold("\u{3c2}"), "\u{3c3}");
        assert_eq!(casefold("ABC"), "abc");
        assert_eq!(casefold("\u{212a}"), "k");
        assert_eq!(casefold("plain"), "plain");
    }

    #[test]
    fn categories() {
        for c in [
            '\u{0}', '\u{1f}', '\u{7f}', '\u{9f}', '\u{ad}', '\u{200b}', '\u{202e}', '\u{feff}',
        ] {
            assert!(is_control_format_surrogate(c), "{:x}", c as u32);
        }
        for c in ['a', ' ', '/', '\u{a0}', '\u{e9}', '\u{4e2d}', '\u{1f600}'] {
            assert!(!is_control_format_surrogate(c), "{:x}", c as u32);
        }
    }

    #[test]
    fn normalisation() {
        assert_eq!(nfc("e\u{301}"), "\u{e9}");
        assert_eq!(nfkc("\u{fb01}"), "fi");
        assert_eq!(nfc("\u{fb01}"), "\u{fb01}");
    }

    #[test]
    fn cp437() {
        assert_eq!(cp437_decode(b"abc"), "abc");
        assert_eq!(cp437_decode(&[0x82]), "\u{e9}");
        assert_eq!(cp437_decode(&[0xe1]), "\u{df}");
        assert_eq!(cp437_decode(&[0xff]), "\u{a0}");
    }

    #[test]
    fn upper_for_device_names() {
        assert_eq!(upper("con"), "CON");
        // Dotless i upper-cases to ASCII I, so "CONIN$" can be spelled with it.
        assert_eq!(upper("con\u{131}n$"), "CONIN$");
    }
}
