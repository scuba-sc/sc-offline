// SPDX-License-Identifier: GPL-3.0-only
//! Release tag versions, as the C++ launcher compares them: 1 to 4 dot-separated numbers, an
//! optional `-` suffix of `[0-9A-Za-z.]`, a leading `v` allowed. A pre-release sorts below the same
//! numbers without one.

/// A parsed tag version.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TagVersion {
    pub num: [u32; 4],
    pub pre: String,
}

/// `v0.7.0-rc1` -> numbers and `rc1`. None for anything else.
pub fn parse(s: &str) -> Option<TagVersion> {
    let mut v = s;
    if let Some(r) = v.strip_prefix(['v', 'V']) {
        v = r;
    }
    let (nums, pre) = match v.split_once('-') {
        Some((n, p)) => {
            if p.is_empty() || !p.bytes().all(|c| c.is_ascii_alphanumeric() || c == b'.') {
                return None;
            }
            (n, p.to_string())
        }
        None => (v, String::new()),
    };
    let mut num = [0u32; 4];
    for (n, part) in nums.split('.').enumerate() {
        if n == 4 || part.is_empty() || !part.bytes().all(|c| c.is_ascii_digit()) || part.len() > 6
        {
            return None;
        }
        let x: u32 = part.parse().ok()?;
        if x > 100_000 {
            return None;
        }
        num[n] = x;
    }
    Some(TagVersion { num, pre })
}

/// a > b. False when either does not parse.
pub fn greater(a: &str, b: &str) -> bool {
    let (Some(x), Some(y)) = (parse(a), parse(b)) else {
        return false;
    };
    if x.num != y.num {
        return x.num > y.num;
    }
    if x.pre.is_empty() != y.pre.is_empty() {
        return x.pre.is_empty();
    }
    x.pre > y.pre
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ordering() {
        assert!(greater("v0.7.1", "0.7.0"));
        assert!(greater("0.7.0", "0.7.0-rc1"));
        assert!(!greater("0.7.0-rc1", "0.7.0"));
        assert!(greater("0.7.0-rc2", "0.7.0-rc1"));
        assert!(greater("0.8.0-dev241.2", "0.8.0-dev241.1"));
        assert!(!greater("0.6.9", "0.7.0"));
        assert!(!greater("0.7.0", "0.7.0"));
        assert!(!greater("junk", "0.7.0"));
    }

    /// The release-candidate chain, the same cases as `tools/launcher_update_version_test.cpp` in the C++ launcher.
    #[test]
    fn release_candidates_sort_below_their_release() {
        let chain = ["0.7.8", "0.8.0-rc.1", "0.8.0-rc.2", "0.8.0", "0.9.0-rc.1"];
        for (i, lower) in chain.iter().enumerate() {
            for higher in &chain[i + 1..] {
                assert!(greater(higher, lower), "{higher} > {lower}");
                assert!(!greater(lower, higher), "{lower} < {higher}");
            }
            assert!(!greater(lower, lower));
        }
        assert!(greater("v0.8.0", "0.8.0-rc.1"));
        assert!(greater("v0.8.0-rc.1", "0.7.7"));
        assert!(!greater("v0.8.0-rc.1", "0.8.0-rc.1"));
        assert!(!greater("0.8.0-rc_1", "0.7.7"));
        assert!(!greater("0.8.0-", "0.7.7"));
        let v = parse("v0.8.0-rc.1").unwrap();
        assert_eq!((v.num, v.pre.as_str()), ([0, 8, 0, 0], "rc.1"));
    }

    #[test]
    fn rejects() {
        for bad in [
            "",
            "v",
            "1.",
            ".1",
            "1..2",
            "1.2.3.4.5",
            "1-",
            "1-a_b",
            "1.x",
            "999999999",
        ] {
            assert!(parse(bad).is_none(), "{bad}");
        }
    }
}
