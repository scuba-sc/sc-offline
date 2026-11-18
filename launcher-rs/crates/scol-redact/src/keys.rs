// SPDX-License-Identifier: GPL-3.0-only
//! Values of identity keys (`RedactIdentityKeys` of `launcher/redact.h`).

use crate::{c_isspace, common_word, equals_nocase, in_set, MAX_VALUE_SCAN, MIN_DIGITS_LEARNED};

fn word_char(c: u8) -> bool {
    c.is_ascii_alphanumeric() || c == b'_'
}

fn blank(c: u8) -> bool {
    c == b' ' || c == b'\t'
}

fn is_quote(c: u8) -> bool {
    c == b'\'' || c == b'"'
}

fn ends(w: &[u8], suf: &str) -> bool {
    w.ends_with(suf.as_bytes())
}

fn id_key(k: &[u8]) -> bool {
    ends(k, "geid")
        || ends(k, "accountid")
        || ends(k, "playerid")
        || ends(k, "userid")
        || ends(k, "rsiid")
        || k == b"uid"
}

fn nick_key(k: &[u8]) -> bool {
    ends(k, "nickname")
        || k == b"nick"
        || ends(k, "displayname")
        || ends(k, "playername")
        || ends(k, "charactername")
        || ends(k, "charname")
        || ends(k, "screenname")
        || ends(k, "profilename")
        || ends(k, "accountname")
}

/// Plain words that end an unquoted multi-word name.
fn stop_word(w: &[u8]) -> bool {
    const WORDS: &[&str] = &[
        "and",
        "or",
        "to",
        "for",
        "at",
        "in",
        "on",
        "by",
        "with",
        "from",
        "is",
        "was",
        "has",
        "had",
        "went",
        "left",
        "joined",
        "entered",
        "killed",
        "logged",
        "connected",
        "disconnected",
        "spawned",
        "died",
    ];
    let l = w.to_ascii_lowercase();
    WORDS.iter().any(|x| x.as_bytes() == &l[..])
}

/// Plain words after `username` that are not a name.
fn not_value(w: &[u8]) -> bool {
    const WORDS: &[&str] = &[
        "is", "was", "not", "empty", "null", "none", "missing", "unknown", "invalid", "required",
        "must", "cannot", "can", "has", "does", "did", "and", "or", "for",
    ];
    let l = w.to_ascii_lowercase();
    WORDS.iter().any(|x| x.as_bytes() == &l[..])
}

fn find_sub(hay: &[u8], needle: &[u8], from: usize) -> Option<usize> {
    if from >= hay.len() || hay.len() - from < needle.len() {
        return None;
    }
    hay[from..]
        .windows(needle.len())
        .position(|w| w == needle)
        .map(|p| p + from)
}

/// What a line shows before it is read word by word: (an id key with a value, a context word, a name context).
fn line_info(s: &[u8], a: usize) -> (bool, bool, bool) {
    let b = s[a..]
        .iter()
        .position(|&c| c == b'\n')
        .map_or(s.len(), |p| p + a);
    let low: Vec<u8> = s[a..b]
        .iter()
        .filter(|&&c| c != b'_')
        .map(|c| c.to_ascii_lowercase())
        .collect();
    let mut id_value = false;
    for key in [&b"geid"[..], b"accountid", b"playerid", b"userid"] {
        let n = key.len();
        let mut at = find_sub(&low, key, 0);
        while let Some(pos) = at {
            if id_value {
                break;
            }
            let mut q = pos + n;
            while q < low.len() && (low[q] == b'"' || low[q] == b'\'' || low[q] == b'\\') {
                q += 1;
            }
            while q < low.len() && (low[q] == b' ' || low[q] == b'\t') {
                q += 1;
            }
            id_value = q < low.len()
                && (matches!(low[q], b'=' | b':' | b'[' | b'#')
                    || low[q].is_ascii_digit()
                    || low[q..].starts_with(b"<redacted>"));
            at = find_sub(&low, key, pos + 1);
        }
    }
    // The context words count only as whole words (letters and digits; `_` and punctuation separate).
    let (
        mut has_login,
        mut has_char,
        mut has_player,
        mut has_account,
        mut has_nick,
        mut has_handle,
    ) = (false, false, false, false, false, false);
    let mut k = a;
    while k < b {
        if !s[k].is_ascii_alphanumeric() {
            k += 1;
            continue;
        }
        let mut e = k;
        while e < b && s[e].is_ascii_alphanumeric() {
            e += 1;
        }
        let n = e - k;
        let is = |w: &str| w.len() == n && equals_nocase(s, k, w.as_bytes());
        if (5..=8).contains(&n) {
            has_login = has_login || is("login");
            has_char = has_char || is("character");
            has_player = has_player || is("player");
            has_account = has_account || is("account");
            has_nick = has_nick || is("nickname");
            has_handle = has_handle || is("handle");
        }
        k = e;
    }
    let context =
        id_value || has_login || has_char || has_player || has_account || has_nick || has_handle;
    let name_ctx =
        id_value || has_account || has_nick || has_handle || find_sub(&low, b"geid", 0).is_some();
    (id_value, context, name_ctx)
}

/// Values of identity keys become `<redacted>`; the key stays so the line still reads. A key is matched on its
/// last part, case-insensitive, with underscores ignored, so player_id, rsiHandle and targetAccountId all
/// count. See `launcher/redact.h` for the full list of shapes. When `learned` is given, each value that was
/// hidden is added to it (`learn` filters them), except the value of the weak keys user, owner, player and
/// login, which are hidden where they stand but never taught to other lines.
pub fn redact_identity_keys(s: &[u8], mut learned: Option<&mut Vec<Vec<u8>>>) -> Vec<u8> {
    let len = s.len();
    let mut out: Vec<u8> = Vec::with_capacity(len);
    let mut i = 0;
    let (mut id_line, mut ctx_line, mut name_ctx, mut at_line_start) = (false, false, false, true);
    while i < len {
        if at_line_start {
            (id_line, ctx_line, name_ctx) = line_info(s, i);
            at_line_start = false;
        }
        if !word_char(s[i]) {
            if s[i] == b'\n' {
                at_line_start = true;
            }
            // 'Name' [200099887766] or "Name" [id]: a quoted name followed by a bracketed 9 to 12 digit id.
            if is_quote(s[i]) {
                let mut c = i + 1;
                while c < len && c < i + 66 && s[c] != s[i] && s[c] != b'\n' {
                    c += 1;
                }
                if c < len && s[c] == s[i] && c > i + 1 {
                    let mut b = c + 1;
                    while b < len && blank(s[b]) {
                        b += 1;
                    }
                    let mut dend = b + 1;
                    while dend < len && s[dend].is_ascii_digit() {
                        dend += 1;
                    }
                    if b < len
                        && s[b] == b'['
                        && dend < len
                        && s[dend] == b']'
                        && dend - (b + 1) >= 9
                        && dend - (b + 1) <= 12
                    {
                        if let Some(l) = learned.as_deref_mut() {
                            l.push(s[i + 1..c].to_vec());
                            l.push(s[b + 1..dend].to_vec());
                        }
                        out.push(s[i]);
                        out.extend_from_slice(b"<redacted>");
                        out.extend_from_slice(&s[c..b + 1]);
                        out.extend_from_slice(b"<redacted>");
                        i = dend;
                        continue;
                    }
                }
            }
            out.push(s[i]);
            i += 1;
            continue;
        }
        let mut j = i;
        while j < len && word_char(s[j]) {
            j += 1;
        }
        let word = &s[i..j];
        let lw = word.to_ascii_lowercase();
        let key: Vec<u8> = lw.iter().copied().filter(|&c| c != b'_').collect();
        out.extend_from_slice(word);
        i = j;
        {
            // geid200099887766: an id key with its digits glued on
            let nd = word.iter().rev().take_while(|c| c.is_ascii_digit()).count();
            if nd >= 4 && nd < word.len() {
                let base: Vec<u8> = lw[..lw.len() - nd]
                    .iter()
                    .copied()
                    .filter(|&c| c != b'_')
                    .collect();
                if id_key(&base) || ((9..=12).contains(&nd) && ends(&base, "id")) {
                    if let Some(l) = learned.as_deref_mut() {
                        l.push(word[word.len() - nd..].to_vec());
                    }
                    out.truncate(out.len() - nd);
                    out.extend_from_slice(b"<redacted>");
                    continue;
                }
            }
        }
        let mut p = j;
        let (mut bracket, mut quoted_only, mut multiword, mut ok, mut xml) =
            (false, false, false, false, false);
        let mut weak = false; // user=, owner=, player=, login=: hidden here, but not learned for other lines
        let is_nick = nick_key(&key);
        let is_id = id_key(&key);
        let is_handle = ends(&key, "handle");
        let is_account = ends(&key, "account");
        if ends(&key, "username") || key == b"setlocalplayerid" {
            let local = key == b"setlocalplayerid";
            while p < len && blank(s[p]) {
                p += 1;
            }
            let mut sep = false;
            if local && p < len && s[p] == b'(' {
                // SetLocalPlayerId(id) or (id=...)
                p += 1;
                while p < len && blank(s[p]) {
                    p += 1;
                }
                let mut q = p;
                while q < len && word_char(s[q]) {
                    q += 1;
                }
                if q > p && q < len && s[q] == b'=' {
                    p = q + 1;
                }
                while p < len && blank(s[p]) {
                    p += 1;
                }
            } else if p < len && (s[p] == b'=' || s[p] == b':') {
                p += 1;
                while p < len && blank(s[p]) {
                    p += 1;
                }
                sep = true;
            }
            ok = p > j;
            multiword = sep && !local;
            if ok && !local && !sep {
                let mut q = p;
                while q < len && word_char(s[q]) {
                    q += 1;
                }
                if not_value(&s[p..q]) {
                    ok = false;
                }
            }
        } else if is_nick || is_id || is_handle || is_account {
            let mut q = p;
            if q < len && s[q] == b'\\' {
                q += 1; // a JSON-escaped key
            }
            if q < len && is_quote(s[q]) {
                q += 1; // a quoted key, as in JSON
            }
            while q < len && blank(s[q]) {
                q += 1;
            }
            if q < len && (s[q] == b'=' || s[q] == b':') && !is_account {
                p = q + 1;
                while p < len && blank(s[p]) {
                    p += 1;
                }
                ok = true;
                multiword = is_nick;
                if is_handle
                    && p + 1 < len
                    && s[p] == b'0'
                    && (s[p + 1] == b'x' || s[p + 1] == b'X')
                {
                    ok = false;
                }
            } else {
                q = j;
                while q < len && blank(s[q]) {
                    q += 1;
                }
                if q < len && s[q] == b'[' && (!is_nick || ends(&key, "nickname")) {
                    p = q + 1;
                    bracket = true;
                    ok = true;
                } else if is_id
                    && q > j
                    && q < len
                    && (s[q].is_ascii_digit()
                        || (s[q] == b'#' && q + 1 < len && s[q + 1].is_ascii_digit()))
                {
                    p = if s[q] == b'#' { q + 1 } else { q }; // geid 123456, geid #123456
                    ok = true;
                } else if is_id
                    && q == j
                    && q + 1 < len
                    && is_quote(s[q])
                    && s[q + 1].is_ascii_digit()
                {
                    p = q; // accountid"5544332211"
                    ok = true;
                } else if j < len
                    && s[j] == b'>'
                    && out.len() > word.len()
                    && out[out.len() - word.len() - 1] == b'<'
                {
                    p = j + 1; // <Nickname>x</Nickname>, <geid>1</geid>
                    ok = true;
                    xml = true;
                }
            }
        } else if key == b"name" && ctx_line {
            // "geid 1 - accountId 2 - name X", name=X, "name":"X"
            let mut q = p;
            if q < len && is_quote(s[q]) {
                q += 1;
            }
            let after_key = q;
            while q < len && blank(s[q]) {
                q += 1;
            }
            let mut sep = false;
            if q < len && (s[q] == b'=' || s[q] == b':') {
                sep = true;
                q += 1;
                while q < len && blank(s[q]) {
                    q += 1;
                }
            }
            if !sep && after_key == p && p < len && s[p] == b'[' && p + 1 < len && name_ctx {
                p += 1; // Name[x] on a line with a handle, account or id
                bracket = true;
                ok = true;
            } else if q < len
                && (word_char(s[q]) || is_quote(s[q]))
                && (sep || (id_line && q > after_key))
            {
                p = q;
                ok = true;
                multiword = true;
                weak = !id_line; // a bare name=X is hidden where it stands; only the login status line teaches it
            }
        } else if matches!(
            &lw[..],
            b"user" | b"owner" | b"player" | b"login" | b"character"
        ) {
            let mut q = p;
            while q < len && blank(s[q]) {
                q += 1;
            }
            if lw == b"player" && p < len && s[p] == b'[' {
                // Player[Name] Requesting Inventory
                p += 1;
                bracket = true;
                ok = true;
            } else if q + 1 < len
                && (s[q] == b'=' || s[q] == b':')
                && !(s[q] == b':' && s[q + 1] == b':')
            {
                p = q + 1;
                while p < len && blank(s[p]) {
                    p += 1;
                }
                ok = p < len && !c_isspace(s[p]);
                weak = lw != b"character";
                if ok {
                    // not when the value is itself the next key (`Character: geid 5`, `user: name=x`)
                    let mut t = p;
                    let mut vw: Vec<u8> = Vec::new();
                    let mut vraw: Vec<u8> = Vec::new();
                    while t < len && word_char(s[t]) {
                        vraw.push(s[t].to_ascii_lowercase());
                        if s[t] != b'_' {
                            vw.push(s[t].to_ascii_lowercase());
                        }
                        t += 1;
                    }
                    if id_key(&vw)
                        || nick_key(&vw)
                        || ends(&vw, "handle")
                        || ends(&vw, "account")
                        || vw == b"name"
                        || stop_word(&vw)
                        || (t < len && (s[t] == b'=' || s[t] == b':'))
                    {
                        ok = false;
                    }
                    // `Character: createdAt 1581606700563 - ...`: a word that is followed by a number is a key.
                    {
                        let mut u = t;
                        while u < len && blank(s[u]) {
                            u += 1;
                        }
                        if u > t && u < len && s[u].is_ascii_digit() {
                            ok = false;
                        }
                    }
                    // Ordinary values are not names: a flag, a common word, a short number or a hex handle.
                    if !vraw.is_empty()
                        && (common_word(&vraw)
                            || vraw.starts_with(b"0x")
                            || (vraw.len() < MIN_DIGITS_LEARNED
                                && vraw.iter().all(|c| c.is_ascii_digit())))
                    {
                        ok = false;
                    }
                    // owner, login and character are weak keys: they count only on a line about a handle,
                    // account or id.
                    if (lw == b"owner" || lw == b"login" || lw == b"character") && !name_ctx {
                        ok = false;
                    }
                }
            } else if lw == b"user" {
                let k = out.len() - word.len();
                let mut b = k;
                while b > 0 && blank(out[b - 1]) {
                    b -= 1;
                }
                if b < k
                    && b >= 3
                    && equals_nocase(&out, b - 3, b"for")
                    && (b == 3 || !word_char(out[b - 4]))
                {
                    while p < len && blank(s[p]) {
                        p += 1;
                    }
                    ok = p > j && p < len && is_quote(s[p]);
                    quoted_only = true;
                }
            }
        }
        if !ok && key.len() >= 2 && ends(&key, "id") {
            // ownerId:5544332211, citizen_id=2000998877, id 2000998877, geid(2000998877)
            let mut q = j;
            if q < len && s[q] == b'\\' {
                q += 1;
            }
            let mut seps = 0;
            while q < len && seps < 3 && s[q] != 0 && b" \t=:#(<-\"'[".contains(&s[q]) {
                q += 1;
                seps += 1;
            }
            let mut d = q;
            while d < len && s[d].is_ascii_digit() {
                d += 1;
            }
            if d - q >= 9 && d - q <= 12 && (d >= len || !s[d].is_ascii_alphanumeric()) {
                p = q;
                ok = true;
            }
        }
        if !ok && word.len() > 7 && equals_nocase(word, 0, b"player_") {
            // Player_<id> batch names
            let mut q = j;
            while q < len && blank(s[q]) {
                q += 1;
            }
            // A word followed by `=` or `:` (`Player_Count=5`, `PLAYER_COUNT: 5`) is a key and keeps its name; any
            // other Player_<x> is a batch name and loses what follows, even if it is only an event name.
            let key_like = q < len
                && (s[q] == b'=' || s[q] == b':')
                && !(q + 1 < len && s[q] == b':' && s[q + 1] == b':');
            if !key_like {
                out.truncate(out.len() - word.len() + 7);
                out.extend_from_slice(b"<redacted>");
                continue;
            }
        }
        if !ok || p >= len {
            continue;
        }
        if !bracket && s[p] == b'[' && p + 1 < len && is_quote(s[p + 1]) {
            p += 1; // "nickname":["x"]
        }
        let mut vs = p;
        let mut e = p;
        if bracket {
            let lim = len.min(p + MAX_VALUE_SCAN);
            let close = (p..lim).find(|&c| s[c] == b']');
            if let Some(c) = close {
                e = c;
            } else {
                // no close in the window: hide the rest of the token
                while vs < len && blank(s[vs]) {
                    vs += 1;
                }
                e = vs;
                while e < len && !c_isspace(s[e]) {
                    e += 1;
                }
            }
        } else if is_quote(s[p]) || (s[p] == b'\\' && p + 1 < len && is_quote(s[p + 1])) {
            let esc = s[p] == b'\\';
            let qp = if esc { p + 1 } else { p };
            let q = s[qp];
            let lim = len.min(qp + 1 + MAX_VALUE_SCAN);
            let mut close = None;
            let mut stop = lim;
            for c in qp + 1..lim {
                if s[c] == b'\n' {
                    stop = c;
                    break;
                }
                if s[c] == q && (esc || s[c - 1] != b'\\') {
                    close = Some(c);
                    break;
                }
            }
            if let Some(c) = close {
                vs = qp + 1;
                e = c;
                if esc && e > vs && s[e - 1] == b'\\' {
                    e -= 1;
                }
            } else if quoted_only {
                continue;
            } else {
                e = stop; // unterminated: hide up to the end of the window or line
            }
        } else {
            if quoted_only {
                continue;
            }
            if xml {
                // the value runs to the closing tag
                while e < len && s[e] != b'<' && s[e] != b'\n' {
                    e += 1;
                }
                if e > vs {
                    if let Some(l) = learned.as_deref_mut() {
                        l.push(s[vs..e].to_vec());
                    }
                    out.extend_from_slice(&s[j..vs]);
                    out.extend_from_slice(b"<redacted>");
                    i = e;
                }
                continue;
            }
            // O'Brien: an apostrophe between two letters is part of the name
            let apostrophe = |k: usize| {
                s[k] == b'\''
                    && k > p
                    && k + 1 < len
                    && s[k - 1].is_ascii_alphabetic()
                    && s[k + 1].is_ascii_alphabetic()
            };
            while e < len && !c_isspace(s[e]) && (!in_set(b",;)]}\"'", s[e]) || apostrophe(e)) {
                e += 1;
            }
            let mut more = 0;
            while multiword && more < 5 && e < len && blank(s[e]) {
                let mut q = e;
                while q < len && blank(s[q]) {
                    q += 1;
                }
                let mut t = q;
                // a word runs to the next space, comma, quote or bracket; a letter-apostrophe-letter stays inside it
                while t < len && !c_isspace(s[t]) && (!in_set(b",;)]}\"'", s[t]) || apostrophe(t)) {
                    t += 1;
                }
                let next = &s[q..t];
                // find_first_of with a C string stops at its NUL, so only the listed characters count
                let has_special = next.iter().any(|c| b"=[<]()\"',;:{}".contains(c));
                if t == q || next == b"-" || has_special {
                    // keep a word that only has apostrophes between letters (O'Brien)
                    let mut only_apos = t > q && next != b"-" && !stop_word(next);
                    for &c in next {
                        if in_set(b"=[<]()\",;:{}", c) {
                            only_apos = false;
                        }
                    }
                    if !only_apos {
                        break;
                    }
                }
                if stop_word(next) {
                    break;
                }
                e = t;
                more += 1;
            }
        }
        if e <= vs {
            continue;
        }
        if !weak {
            if let Some(l) = learned.as_deref_mut() {
                l.push(s[vs..e].to_vec());
            }
        }
        out.extend_from_slice(&s[j..vs]);
        out.extend_from_slice(b"<redacted>");
        i = e;
    }
    out
}
