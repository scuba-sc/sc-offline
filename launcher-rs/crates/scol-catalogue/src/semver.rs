// SPDX-License-Identifier: GPL-3.0-only
//! Versions and SDK ranges. A small subset of SemVer, enough for `>=2.4.1, <2.5`.
//!
//! * Version: `N.N` or `N.N.N`, optionally `-pre.release` (a missing patch is 0). No `+build`.
//! * Precedence follows SemVer 2.0.0: a pre-release is older than its release, so
//!   `2.4.1-dev.6` does NOT satisfy `>=2.4.1`. A host on a development SDK sees only entries whose
//!   range admits that development build.
//! * Range: comparators joined by commas, each `>=`, `>`, `<=`, `<` or `=` and a version. All must hold.

use std::cmp::Ordering;
use std::fmt;

#[derive(Debug, Clone, PartialEq, Eq)]
enum Ident {
    Num(u64),
    Text(String),
}

impl Ord for Ident {
    fn cmp(&self, other: &Self) -> Ordering {
        match (self, other) {
            (Ident::Num(a), Ident::Num(b)) => a.cmp(b),
            (Ident::Num(_), Ident::Text(_)) => Ordering::Less,
            (Ident::Text(_), Ident::Num(_)) => Ordering::Greater,
            (Ident::Text(a), Ident::Text(b)) => a.cmp(b),
        }
    }
}

impl PartialOrd for Ident {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Version {
    pub major: u64,
    pub minor: u64,
    pub patch: u64,
    pre: Vec<Ident>,
    text: String,
}

fn number(s: &str) -> Option<u64> {
    if s.is_empty() || s.len() > 9 || !s.bytes().all(|c| c.is_ascii_digit()) {
        return None;
    }
    if s.len() > 1 && s.starts_with('0') {
        return None;
    }
    s.parse().ok()
}

impl Version {
    pub fn parse(s: &str) -> Option<Version> {
        let (core, pre) = match s.split_once('-') {
            Some((c, p)) => (c, Some(p)),
            None => (s, None),
        };
        let parts: Vec<&str> = core.split('.').collect();
        if parts.len() < 2 || parts.len() > 3 {
            return None;
        }
        let major = number(parts[0])?;
        let minor = number(parts[1])?;
        let patch = if parts.len() == 3 {
            number(parts[2])?
        } else {
            0
        };
        let mut ids = Vec::new();
        if let Some(p) = pre {
            for part in p.split('.') {
                if part.is_empty() || !part.bytes().all(|c| c.is_ascii_alphanumeric() || c == b'-')
                {
                    return None;
                }
                if part.bytes().all(|c| c.is_ascii_digit()) {
                    ids.push(Ident::Num(number(part)?));
                } else {
                    ids.push(Ident::Text(part.to_string()));
                }
            }
        }
        Some(Version {
            major,
            minor,
            patch,
            pre: ids,
            text: s.to_string(),
        })
    }
}

impl Ord for Version {
    fn cmp(&self, other: &Self) -> Ordering {
        (self.major, self.minor, self.patch)
            .cmp(&(other.major, other.minor, other.patch))
            .then_with(|| match (self.pre.is_empty(), other.pre.is_empty()) {
                (true, true) => Ordering::Equal,
                (true, false) => Ordering::Greater,
                (false, true) => Ordering::Less,
                (false, false) => self.pre.cmp(&other.pre),
            })
    }
}

impl PartialOrd for Version {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl fmt::Display for Version {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.text)
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Op {
    Ge,
    Gt,
    Le,
    Lt,
    Eq,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SdkRange {
    comparators: Vec<(Op, Version)>,
}

impl SdkRange {
    /// `None` when the text is not a valid range.
    pub fn parse(s: &str) -> Option<SdkRange> {
        if s.is_empty() || s.len() > 128 {
            return None;
        }
        let mut comparators = Vec::new();
        for part in s.split(',') {
            let part = part.trim_matches(' ');
            let (op, rest) = if let Some(r) = part.strip_prefix(">=") {
                (Op::Ge, r)
            } else if let Some(r) = part.strip_prefix("<=") {
                (Op::Le, r)
            } else if let Some(r) = part.strip_prefix('>') {
                (Op::Gt, r)
            } else if let Some(r) = part.strip_prefix('<') {
                (Op::Lt, r)
            } else {
                (Op::Eq, part.strip_prefix('=')?)
            };
            let v = Version::parse(rest.trim_start_matches(' '))?;
            comparators.push((op, v));
            if comparators.len() > 4 {
                return None;
            }
        }
        Some(SdkRange { comparators })
    }

    pub fn matches(&self, v: &Version) -> bool {
        self.comparators.iter().all(|(op, bound)| {
            let c = v.cmp(bound);
            match op {
                Op::Ge => c != Ordering::Less,
                Op::Gt => c == Ordering::Greater,
                Op::Le => c != Ordering::Greater,
                Op::Lt => c == Ordering::Less,
                Op::Eq => c == Ordering::Equal,
            }
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn v(s: &str) -> Version {
        Version::parse(s).unwrap_or_else(|| panic!("{s}"))
    }

    #[test]
    fn ordering() {
        assert!(v("2.4.1-dev.6") < v("2.4.1"));
        assert!(v("2.4.1-dev.6") > v("2.4.1-dev.5"));
        assert!(v("2.4.1-dev.10") > v("2.4.1-dev.9"));
        assert!(v("2.4.1-alpha") < v("2.4.1-alpha.1"));
        assert!(v("2.4") == v("2.4.0") || v("2.4").cmp(&v("2.4.0")) == Ordering::Equal);
        assert!(v("2.10.0") > v("2.9.9"));
    }

    #[test]
    fn rejects_bad_versions() {
        for s in [
            "",
            "2",
            "2.",
            "2.4.1.1",
            "02.4",
            "2.4.x",
            "2.4.1-",
            "2.4.1-a..b",
            "2.4.1+b",
            "v2.4",
            " 2.4",
        ] {
            assert!(Version::parse(s).is_none(), "{s}");
        }
    }

    #[test]
    fn ranges() {
        let r = SdkRange::parse(">=2.4.1, <2.5").unwrap();
        assert!(r.matches(&v("2.4.1")));
        assert!(r.matches(&v("2.4.9")));
        assert!(!r.matches(&v("2.4.0")));
        assert!(!r.matches(&v("2.5.0")));
        assert!(!r.matches(&v("2.4.1-dev.6")));
        let dev = SdkRange::parse(">=2.4.1-dev.1, <2.5").unwrap();
        assert!(dev.matches(&v("2.4.1-dev.6")));
        assert!(SdkRange::parse("=2.4.1").unwrap().matches(&v("2.4.1")));
        assert!(SdkRange::parse(">2.4.1").unwrap().matches(&v("2.4.2")));
        assert!(SdkRange::parse("<=2.4.1").unwrap().matches(&v("2.4.1")));
    }

    #[test]
    fn rejects_bad_ranges() {
        for s in [
            "",
            "2.4.1",
            ">=",
            ">=2.4.1,",
            ",<2.5",
            "~2.4",
            ">=2.4.1 <2.5",
            "^2.4",
            ">>2.4",
        ] {
            assert!(SdkRange::parse(s).is_none(), "{s}");
        }
    }
}
