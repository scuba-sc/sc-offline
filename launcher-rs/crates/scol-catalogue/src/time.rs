// SPDX-License-Identifier: GPL-3.0-only
//! The one timestamp form of the index: `YYYY-MM-DDTHH:MM:SSZ`, UTC, exactly 20 bytes.

/// Seconds since 1970-01-01T00:00:00Z, or `None` when `s` is not in the exact form or not a real
/// date. Years 1970 to 9999.
pub fn parse_utc(s: &str) -> Option<i64> {
    let b = s.as_bytes();
    if b.len() != 20
        || b[4] != b'-'
        || b[7] != b'-'
        || b[10] != b'T'
        || b[13] != b':'
        || b[16] != b':'
        || b[19] != b'Z'
    {
        return None;
    }
    let num = |a: usize, z: usize| -> Option<i64> {
        let mut v = 0i64;
        for &c in &b[a..z] {
            if !c.is_ascii_digit() {
                return None;
            }
            v = v * 10 + i64::from(c - b'0');
        }
        Some(v)
    };
    let (y, mo, d) = (num(0, 4)?, num(5, 7)?, num(8, 10)?);
    let (h, mi, se) = (num(11, 13)?, num(14, 16)?, num(17, 19)?);
    if y < 1970 || !(1..=12).contains(&mo) || h > 23 || mi > 59 || se > 59 {
        return None;
    }
    let leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    let dim = [
        31,
        if leap { 29 } else { 28 },
        31,
        30,
        31,
        30,
        31,
        31,
        30,
        31,
        30,
        31,
    ];
    if d < 1 || d > dim[(mo - 1) as usize] {
        return None;
    }
    // Days from civil date (the standard era-based algorithm).
    let yy = if mo <= 2 { y - 1 } else { y };
    let era = yy / 400;
    let yoe = yy - era * 400;
    let doy = (153 * (if mo > 2 { mo - 3 } else { mo + 9 }) + 2) / 5 + d - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    let days = era * 146_097 + doe - 719_468;
    Some(days * 86_400 + h * 3600 + mi * 60 + se)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn known_values() {
        assert_eq!(parse_utc("1970-01-01T00:00:00Z"), Some(0));
        assert_eq!(parse_utc("2000-03-01T00:00:00Z"), Some(951_868_800));
        assert_eq!(parse_utc("2024-02-29T23:59:59Z"), Some(1_709_251_199));
        assert_eq!(
            parse_utc("2026-11-17T00:00:01Z"),
            Some(parse_utc("2026-11-17T00:00:00Z").unwrap() + 1)
        );
    }

    #[test]
    fn refuses_other_forms() {
        for s in [
            "2026-11-17T12:30:45",
            "2026-11-17 12:30:45Z",
            "2026-13-01T00:00:00Z",
            "2026-02-29T00:00:00Z",
            "2026-11-17T24:00:00Z",
            "2026-11-17T12:30:45+00:00",
            "1969-12-31T23:59:59Z",
            "2026-1-17T12:30:45Z",
            "2026-11-17T12:30:4aZ",
            "",
        ] {
            assert_eq!(parse_utc(s), None, "{s}");
        }
    }
}
