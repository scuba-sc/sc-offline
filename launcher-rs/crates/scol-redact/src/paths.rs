// SPDX-License-Identifier: GPL-3.0-only
//! Paths, e-mail addresses, SIDs, account names and the legacy id shapes. Port of the first half of
//! `launcher/redact.h`.

use crate::{
    equals_nocase, in_set, is_alnum_byte, is_slash, lower_ascii, marker_len, system_token,
};

fn slash_run_end(s: &[u8], mut i: usize) -> usize {
    while i < s.len() && is_slash(s[i]) {
        i += 1;
    }
    i
}

/// The end of `root` matched at `s[at]`, or None. A run of slashes in either matches a run in the other.
/// Letters compare ASCII-case-insensitively only, so a non-ASCII account name matches byte for byte.
fn match_root(s: &[u8], at: usize, root: &[u8]) -> Option<usize> {
    let mut i = at;
    let mut k = 0;
    while k < root.len() {
        if is_slash(root[k]) {
            let e = slash_run_end(s, i);
            if e == i {
                return None;
            }
            i = e;
            k = slash_run_end(root, k);
        } else {
            if i >= s.len() || lower_ascii(s[i]) != lower_ascii(root[k]) {
                return None;
            }
            i += 1;
            k += 1;
        }
    }
    Some(i)
}

/// Paths under the user's profile must not carry the account name out of the PC (`RedactPaths`).
/// First every case-insensitive (ASCII only) occurrence of a profile root becomes `%USERPROFILE%`; then the
/// name in any `Users` path component that follows a separator is replaced by `<user>`.
pub fn redact_paths<R: AsRef<[u8]>>(text: &[u8], profile_roots: &[R]) -> Vec<u8> {
    let mut s: Vec<u8> = text.to_vec();
    for root in profile_roots {
        let mut root = root.as_ref().to_vec();
        while root.len() > 3 && is_slash(*root.last().unwrap()) {
            root.pop();
        }
        if root.len() < 3 {
            continue;
        }
        let mut out = Vec::with_capacity(s.len());
        let mut i = 0;
        while i < s.len() {
            let end = match_root(&s, i, &root);
            let mut boundary = false;
            if let Some(end) = end {
                // The root ends the account name unless the name goes on: a letter, digit or non-ASCII byte, or
                // '-' / '_' followed by one. '.' always ends it.
                let cont = |k: usize| k < s.len() && is_alnum_byte(s[k]);
                boundary = end >= s.len()
                    || !(cont(end) || ((s[end] == b'-' || s[end] == b'_') && cont(end + 1)));
            }
            if boundary {
                out.extend_from_slice(b"%USERPROFILE%");
                i = end.unwrap();
            } else {
                out.push(s[i]);
                i += 1;
            }
        }
        s = out;
    }
    let mut out = Vec::with_capacity(s.len());
    let mut i = 0;
    while i < s.len() {
        if !is_slash(s[i]) {
            out.push(s[i]);
            i += 1;
            continue;
        }
        let run = slash_run_end(&s, i);
        let wl = if equals_nocase(&s, run, b"users") {
            5
        } else if equals_nocase(&s, run, b"home") {
            4
        } else {
            0
        };
        let users = wl != 0 && run + wl < s.len() && is_slash(s[run + wl]);
        if !users {
            out.extend_from_slice(&s[i..run]);
            i = run;
            continue;
        }
        let name_start = slash_run_end(&s, run + wl);
        out.extend_from_slice(&s[i..name_start]);
        let stops = |c: u8| {
            is_slash(c) || matches!(c, b':' | b'*' | b'?' | b'"' | b'<' | b'>' | b'|') || c < 0x20
        };
        let mut e = name_start;
        while e < s.len() && !stops(s[e]) {
            e += 1;
        }
        let mut name_end = e;
        if !(e < s.len() && is_slash(s[e])) {
            name_end = name_start;
            while name_end < e && !in_set(b"',;)]}", s[name_end]) {
                name_end += 1;
            }
        }
        i = name_end;
        if name_end > name_start {
            out.extend_from_slice(b"<user>");
        }
    }
    out
}

/// Account names shorter than this are not searched for in free text.
pub const MIN_ACCOUNT_NAME_LENGTH: usize = 3;

/// Names this long or longer are also found inside other words.
pub const SUBSTRING_NAME_LENGTH: usize = 6;

/// Every case-insensitive (ASCII only) occurrence of a Windows account name becomes `<user>`
/// (`RedactAccountNames`). A name of 3 to 5 characters is replaced as a whole word; a longer one is
/// matched inside other words too. Shorter names, the words this function writes itself and the machine
/// words of `system_token` are skipped.
pub fn redact_account_names<N: AsRef<[u8]>>(text: &[u8], names: &[N]) -> Vec<u8> {
    // Longest first, so `zorbax.CORP` goes whole before `zorbax` is looked for. The sort is stable.
    let mut names: Vec<&[u8]> = names.iter().map(|n| n.as_ref()).collect();
    names.sort_by_key(|n| std::cmp::Reverse(n.len()));
    let mut s = text.to_vec();
    for name in names {
        if name.len() < MIN_ACCOUNT_NAME_LENGTH {
            continue;
        }
        let low = name.to_ascii_lowercase();
        if low == b"user"
            || low == b"users"
            || low == b"redacted"
            || low == b"userprofile"
            || system_token(&low)
        {
            continue;
        }
        let anywhere = name.len() >= SUBSTRING_NAME_LENGTH;
        let mut out = Vec::with_capacity(s.len());
        let mut i = 0;
        while i < s.len() {
            let m = marker_len(&s, i);
            if m != 0 {
                // never inside a marker
                out.extend_from_slice(&s[i..i + m]);
                i += m;
                continue;
            }
            let starts = i + name.len() <= s.len()
                && equals_nocase(&s, i, name)
                && (anywhere
                    || ((i == 0 || !is_alnum_byte(s[i - 1]))
                        && (i + name.len() >= s.len() || !is_alnum_byte(s[i + name.len()]))));
            if starts {
                out.extend_from_slice(b"<user>");
                i += name.len();
            } else {
                out.push(s[i]);
                i += 1;
            }
        }
        s = out;
    }
    s
}

/// E-mail addresses anywhere in the text become `<redacted>` (`RedactEmails`). Every scan stays inside the
/// one `@` it belongs to, so a hostile line cannot make this quadratic.
pub fn redact_emails(s: &[u8]) -> Vec<u8> {
    let hi = |c: u8| c >= 0x80;
    let local = |c: u8| hi(c) || c.is_ascii_alphanumeric() || in_set(b"._%+-'!#$&*^`{|}~", c);
    let domain = |c: u8| hi(c) || c.is_ascii_alphanumeric() || c == b'.' || c == b'-' || c == b'_';
    let alpha = |c: u8| hi(c) || c.is_ascii_alphabetic();
    let mut out: Vec<u8> = Vec::with_capacity(s.len());
    let mut i = 0;
    while i < s.len() {
        // `@`, or its percent-encoded form %40
        let at_len = if s[i] == b'@' {
            1
        } else if s[i] == b'%' && i + 2 < s.len() && s[i + 1] == b'4' && s[i + 2] == b'0' {
            3
        } else {
            0
        };
        if at_len == 0 {
            out.push(s[i]);
            i += 1;
            continue;
        }
        let mut b = out.len();
        if b > 0 && out[b - 1] == b'"' {
            // a quoted local part: "pat q"@example.com
            let mut q = b - 1;
            let lo = b.saturating_sub(66);
            while q > lo && out[q - 1] != b'"' {
                q -= 1;
            }
            if q > lo {
                b = q - 1;
            }
        }
        let mut n = 0;
        while b > 0 && n < 128 && local(out[b - 1]) {
            b -= 1;
            n += 1;
        }
        let ds = i + at_len;
        let mut e = ds;
        while e < s.len() && domain(s[e]) {
            e += 1;
        }
        while e > ds && (s[e - 1] == b'.' || s[e - 1] == b'-') {
            e -= 1;
        }
        let mut dot = None;
        let mut k = e;
        while k > ds {
            if s[k - 1] == b'.' {
                dot = Some(k - 1);
                break;
            }
            k -= 1;
        }
        let tld = match dot {
            Some(d) => d > ds && e - d > 2 && alpha(s[d + 1]) && alpha(s[e - 1]),
            None => false,
        };
        if b < out.len() && tld {
            out.truncate(b);
            out.extend_from_slice(b"<redacted>");
            i = e;
        } else {
            out.push(s[i]);
            i += 1;
        }
    }
    out
}

/// `%5C`, `%2F` and `%40` (any case of the hex digits) become a backslash, / and @; the other spellings of @
/// that logs and JSON use (`@`, `\x40`, `&#64;`, `&#x40;`, `&commat;`) become @ (`DecodePathEscapes`).
pub fn decode_path_escapes(s: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(s.len());
    let mut i = 0;
    while i < s.len() {
        if s[i] == b'%' && i + 2 < s.len() {
            let a = s[i + 1];
            let b = lower_ascii(s[i + 2]);
            let c = if a == b'5' && b == b'c' {
                b'\\'
            } else if a == b'2' && b == b'f' {
                b'/'
            } else if a == b'4' && b == b'0' {
                b'@'
            } else {
                0
            };
            if c != 0 {
                out.push(c);
                i += 3;
                continue;
            }
        } else if s[i] == b'\\' || s[i] == b'&' {
            let mut n = 0;
            for at in [&b"\\u0040"[..], b"\\x40", b"&#64;", b"&#x40;", b"&commat;"] {
                if at[0] == s[i] && equals_nocase(s, i, at) {
                    n = at.len();
                    break;
                }
            }
            if n != 0 {
                out.push(b'@');
                i += n;
                continue;
            }
        }
        out.push(s[i]);
        i += 1;
    }
    out
}

/// Windows account SIDs (`S-1-5-21-<n>-<n>-<n>-<rid>`): the whole run of digits and dashes after `S-1-5-21-`
/// becomes `<redacted>` (`RedactSids`).
pub fn redact_sids(s: &[u8]) -> Vec<u8> {
    const PREFIX: &[u8] = b"S-1-5-21-";
    let pl = PREFIX.len();
    let mut out = Vec::new();
    let mut i = 0;
    let mut copied = 0;
    while i < s.len() {
        if lower_ascii(s[i]) != b's'
            || !equals_nocase(s, i, PREFIX)
            || (i > 0 && s[i - 1].is_ascii_alphanumeric())
        {
            i += 1;
            continue;
        }
        let mut e = i + pl;
        while e < s.len() && (s[e].is_ascii_digit() || s[e] == b'-') {
            e += 1;
        }
        while e > i + pl && s[e - 1] == b'-' {
            e -= 1;
        }
        if e == i + pl {
            i += pl;
            continue;
        }
        out.extend_from_slice(&s[copied..i]);
        out.extend_from_slice(b"<redacted>");
        copied = e;
        i = e;
    }
    if copied == 0 {
        return s.to_vec();
    }
    out.extend_from_slice(&s[copied..]);
    out
}

/// Step 1 of the pipeline: paths, then e-mail addresses, on the text with `%5C`, `%2F` and `%40` decoded.
/// When nothing was found in the decoded text the original bytes are kept (`PathsAndEmails`).
pub fn paths_and_emails<R: AsRef<[u8]>>(text: &[u8], profile_roots: &[R]) -> Vec<u8> {
    let decoded = decode_path_escapes(text);
    let redacted = redact_sids(&redact_emails(&redact_paths(&decoded, profile_roots)));
    if redacted == decoded {
        text.to_vec()
    } else {
        redacted
    }
}

/// `"geid 200012345"`, `"accountId=9876543"`, `geid""1234`, geid123: one of geid, accountid, account_id and
/// playerid in any case, up to three of space, = : " [ after it, then three or more digits. The digits become
/// `<redacted>` (`RedactLegacyIds`). A key glued to a letter or digit before it does not count, except after an `r`.
pub fn redact_legacy_ids(s: &[u8]) -> Vec<u8> {
    const KEYS: [&[u8]; 4] = [b"geid", b"accountid", b"account_id", b"playerid"];
    let mut out = Vec::new();
    let mut copied = 0;
    let mut last_end = usize::MAX;
    let mut i = 0;
    while i < s.len() {
        let c = lower_ascii(s[i]);
        if c != b'g' && c != b'a' && c != b'p' {
            i += 1;
            continue;
        }
        let mut kl = 0;
        for key in KEYS {
            let n = key.len();
            if i + n > s.len() {
                continue;
            }
            if s[i..i + n].eq_ignore_ascii_case(key) {
                kl = n;
                break;
            }
        }
        if kl == 0 {
            i += 1;
            continue;
        }
        if i > 0
            && i != last_end
            && s[i - 1].is_ascii_alphanumeric()
            && lower_ascii(s[i - 1]) != b'r'
        {
            i += kl;
            continue;
        }
        let mut a = i + kl;
        let mut seps = 0;
        while a < s.len() && seps < 3 && matches!(s[a], b' ' | b'=' | b':' | b'"' | b'[') {
            a += 1;
            seps += 1;
        }
        let mut e = a;
        while e < s.len() && s[e].is_ascii_digit() {
            e += 1;
        }
        if e - a < 3 {
            i += kl;
            continue;
        }
        out.extend_from_slice(&s[copied..a]);
        out.extend_from_slice(b"<redacted>");
        copied = e;
        last_end = e;
        i = e;
    }
    if copied == 0 {
        return s.to_vec();
    }
    out.extend_from_slice(&s[copied..]);
    out
}
