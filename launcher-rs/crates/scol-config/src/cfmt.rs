// SPDX-License-Identifier: GPL-3.0-only
//! C library number text the launcher relies on: `%.17g` (how the host stores a float setting),
//! `strtoll` and `strtod` (how the launcher reads `min` and `max`). Only what the launcher uses.

/// `printf("%.17g", v)` for a finite `v`, as the MSVC runtime prints it (exponent of at least
/// two digits). Non-finite values give `inf`, `-inf` and `nan`.
pub fn format_g17(v: f64) -> String {
    if v.is_nan() {
        return if v.is_sign_negative() {
            "-nan".into()
        } else {
            "nan".into()
        };
    }
    if v.is_infinite() {
        return if v < 0.0 { "-inf".into() } else { "inf".into() };
    }
    if v == 0.0 {
        return if v.is_sign_negative() {
            "-0".into()
        } else {
            "0".into()
        };
    }
    const P: i32 = 17;
    // 17 significant digits in scientific form; the exponent after rounding picks the style.
    let sci = format!("{:.*e}", (P - 1) as usize, v);
    let (mantissa, exp) = sci.split_once('e').expect("scientific text");
    let x: i32 = exp.parse().expect("exponent");
    if !(-4..P).contains(&x) {
        let mut m = mantissa.to_string();
        if m.contains('.') {
            m = m.trim_end_matches('0').trim_end_matches('.').to_string();
        }
        let sign = if x < 0 { '-' } else { '+' };
        format!("{m}e{sign}{:02}", x.abs())
    } else {
        let decimals = (P - 1 - x) as usize;
        let mut s = format!("{v:.decimals$}");
        if s.contains('.') {
            s = s.trim_end_matches('0').trim_end_matches('.').to_string();
        }
        s
    }
}

fn skip_space(b: &[u8]) -> usize {
    b.iter()
        .take_while(|c| matches!(c, b' ' | b'\t' | b'\n' | b'\x0b' | b'\x0c' | b'\r'))
        .count()
}

/// `strtoll(s, nullptr, 10)`: blanks, a sign, then the digits it can read; 0 when there are none;
/// clamped to the `long long` range on overflow.
pub fn strtoll(s: &str) -> i64 {
    let b = s.as_bytes();
    let mut i = skip_space(b);
    let mut neg = false;
    if i < b.len() && (b[i] == b'+' || b[i] == b'-') {
        neg = b[i] == b'-';
        i += 1;
    }
    let mut v: i128 = 0;
    while i < b.len() && b[i].is_ascii_digit() {
        v = (v * 10 + i128::from(b[i] - b'0')).min(i128::from(i64::MAX) + 1);
        i += 1;
    }
    let v = if neg { -v } else { v };
    v.clamp(i128::from(i64::MIN), i128::from(i64::MAX)) as i64
}

/// `strtod(s, nullptr)` for decimal text: blanks, a sign, digits with an optional fraction and
/// exponent; 0 when nothing parses. Hexadecimal, `inf` and `nan` forms are not read (they give 0).
pub fn strtod(s: &str) -> f64 {
    let b = s.as_bytes();
    let mut i = skip_space(b);
    let start = i;
    if i < b.len() && (b[i] == b'+' || b[i] == b'-') {
        i += 1;
    }
    let digits_from = i;
    while i < b.len() && b[i].is_ascii_digit() {
        i += 1;
    }
    let mut seen = i > digits_from;
    if i < b.len() && b[i] == b'.' {
        let frac_from = i + 1;
        let mut j = frac_from;
        while j < b.len() && b[j].is_ascii_digit() {
            j += 1;
        }
        if seen || j > frac_from {
            seen = true;
            i = j;
        }
    }
    if !seen {
        return 0.0;
    }
    if i < b.len() && (b[i] == b'e' || b[i] == b'E') {
        let mut j = i + 1;
        if j < b.len() && (b[j] == b'+' || b[j] == b'-') {
            j += 1;
        }
        let exp_from = j;
        while j < b.len() && b[j].is_ascii_digit() {
            j += 1;
        }
        if j > exp_from {
            i = j;
        }
    }
    s[start..i].parse::<f64>().unwrap_or(0.0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn g17_matches_known_c_output() {
        assert_eq!(format_g17(0.1), "0.10000000000000001");
        assert_eq!(format_g17(0.7), "0.69999999999999996");
        assert_eq!(format_g17(1e-7), "9.9999999999999995e-08");
        assert_eq!(format_g17(1.2345678901234568e17), "1.2345678901234568e+17");
        assert_eq!(format_g17(1000.0), "1000");
        assert_eq!(format_g17(-2500.0), "-2500");
        assert_eq!(format_g17(0.0), "0");
        assert_eq!(format_g17(-0.0), "-0");
        assert_eq!(format_g17(100.0), "100");
        assert_eq!(format_g17(1e16), "10000000000000000");
        assert_eq!(format_g17(1e17), "1e+17");
        assert_eq!(format_g17(1e21), "1e+21");
        assert_eq!(format_g17(0.0001), "0.0001");
        assert_eq!(format_g17(0.00001), "1.0000000000000001e-05");
        assert_eq!(format_g17(0.30000000000000004), "0.30000000000000004");
    }

    #[test]
    fn strto_prefix_rules() {
        assert_eq!(strtoll("12abc"), 12);
        assert_eq!(strtoll("abc"), 0);
        assert_eq!(strtoll("-3"), -3);
        assert_eq!(strtoll("+9"), 9);
        assert_eq!(strtoll("99999999999999999999"), i64::MAX);
        assert_eq!(strtoll("-99999999999999999999"), i64::MIN);
        assert_eq!(strtod("xyz"), 0.0);
        assert_eq!(strtod("1e2"), 100.0);
        assert_eq!(strtod("1e"), 1.0);
        assert_eq!(strtod("2.5x"), 2.5);
        assert_eq!(strtod(".5"), 0.5);
        assert_eq!(strtod("-"), 0.0);
    }
}
