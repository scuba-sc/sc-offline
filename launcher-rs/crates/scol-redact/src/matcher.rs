// SPDX-License-Identifier: GPL-3.0-only
//! The matcher that hides every learned value (`LearnedMatcher` of `launcher/redact.h`).

use crate::{common_word, marker_len, SUBSTRING_LEARNED_LENGTH};

/// Every learned value becomes `<redacted>`. A value of `SUBSTRING_LEARNED_LENGTH` or more characters (or one
/// with a character other than a letter or digit) is found anywhere, in any ASCII case, inside other words
/// too, with an Aho-Corasick automaton: one pass, whatever the number of values. A shorter value, or an
/// ordinary word such as a handle called `Character`, is replaced only as a whole token (a run of letters,
/// digits and non-ASCII bytes), in relaxed mode. Strict mode finds every value anywhere. Touching or
/// overlapping hits become one marker.
#[derive(Clone)]
pub struct LearnedMatcher {
    cls: [usize; 256],
    width: usize,
    /// The automaton: transitions, and the longest value ending at a node.
    next: Vec<i32>,
    longest: Vec<i32>,
    /// Short values, sorted.
    tokens: Vec<Vec<u8>>,
}

impl LearnedMatcher {
    pub fn new(values: &[Vec<u8>], strict: bool) -> Self {
        let mut m = LearnedMatcher {
            cls: [0; 256],
            width: 1,
            next: Vec::new(),
            longest: Vec::new(),
            tokens: Vec::new(),
        };
        let mut long_values: Vec<Vec<u8>> = Vec::new();
        for v in values {
            if v.is_empty() {
                continue;
            }
            let plain = v.iter().all(|&c| c.is_ascii_alphanumeric() || c >= 0x80);
            let low = v.to_ascii_lowercase();
            // relaxed mode: a short value or an ordinary word is replaced only as a whole token
            let whole_token =
                plain && !strict && (v.len() < SUBSTRING_LEARNED_LENGTH || common_word(&low));
            if whole_token {
                m.tokens.push(low);
            } else {
                long_values.push(low);
            }
        }
        m.tokens.sort();
        if long_values.is_empty() {
            return m;
        }
        // alphabet: the bytes that occur in values, plus one class for everything else
        let mut ncls = 1;
        for v in &long_values {
            for &c in v {
                if m.cls[c as usize] == 0 {
                    m.cls[c as usize] = ncls;
                    ncls += 1;
                }
            }
        }
        let w = ncls;
        m.width = w;
        m.next = vec![-1; w];
        m.longest = vec![0];
        let mut fail: Vec<i32> = vec![0];
        for v in &long_values {
            let mut n = 0usize;
            for &c in v {
                let k = m.cls[c as usize];
                if m.next[n * w + k] < 0 {
                    m.next[n * w + k] = m.longest.len() as i32;
                    m.longest.push(0);
                    fail.push(0);
                    let new_len = m.longest.len() * w;
                    m.next.resize(new_len, -1);
                }
                n = m.next[n * w + k] as usize;
            }
            m.longest[n] = v.len() as i32;
        }
        let mut queue: Vec<usize> = Vec::new();
        for k in 0..w {
            let t = m.next[k];
            if t < 0 {
                m.next[k] = 0;
            } else {
                fail[t as usize] = 0;
                queue.push(t as usize);
            }
        }
        let mut h = 0;
        while h < queue.len() {
            let n = queue[h];
            h += 1;
            m.longest[n] = m.longest[n].max(m.longest[fail[n] as usize]);
            for k in 0..w {
                let t = m.next[n * w + k];
                let via_fail = m.next[fail[n] as usize * w + k];
                if t < 0 {
                    m.next[n * w + k] = via_fail;
                } else {
                    fail[t as usize] = via_fail;
                    queue.push(t as usize);
                }
            }
        }
        m
    }

    pub fn apply(&self, text: &[u8]) -> Vec<u8> {
        let mut cur: Vec<u8> = Vec::new();
        let mut src: &[u8] = text;
        if !self.longest.is_empty() {
            let mut diff = vec![0i32; text.len() + 1];
            let mut state = 0usize;
            let mut any = false;
            let mut i = 0;
            while i < text.len() {
                let m = marker_len(text, i);
                if m != 0 {
                    state = 0;
                    i += m;
                    continue;
                }
                state = self.next
                    [state * self.width + self.cls[text[i].to_ascii_lowercase() as usize]]
                    as usize;
                let len = self.longest[state];
                if len > 0 {
                    diff[i + 1 - len as usize] += 1;
                    diff[i + 1] -= 1;
                    any = true;
                }
                i += 1;
            }
            if any {
                cur.reserve(text.len());
                let mut depth = 0;
                let mut hidden = false;
                for (i, &c) in text.iter().enumerate() {
                    depth += diff[i];
                    if depth > 0 {
                        if !hidden {
                            cur.extend_from_slice(b"<redacted>");
                        }
                        hidden = true;
                    } else {
                        hidden = false;
                        cur.push(c);
                    }
                }
                src = &cur;
            }
        }
        if self.tokens.is_empty() {
            return src.to_vec();
        }
        let input = src;
        let token_char = |c: u8| c.is_ascii_alphanumeric() || c >= 0x80;
        let mut out = Vec::with_capacity(input.len());
        let mut i = 0;
        while i < input.len() {
            let m = marker_len(input, i);
            if m != 0 {
                out.extend_from_slice(&input[i..i + m]);
                i += m;
                continue;
            }
            if !token_char(input[i]) {
                out.push(input[i]);
                i += 1;
                continue;
            }
            let mut e = i;
            while e < input.len() && token_char(input[e]) {
                e += 1;
            }
            let word = input[i..e].to_ascii_lowercase();
            if self.tokens.binary_search(&word).is_ok() {
                out.extend_from_slice(b"<redacted>");
            } else {
                out.extend_from_slice(&input[i..e]);
            }
            i = e;
        }
        out
    }
}

/// Every learned value becomes `<redacted>` (`RedactLearned`).
pub fn redact_learned(text: &[u8], values: &[Vec<u8>]) -> Vec<u8> {
    LearnedMatcher::new(values, false).apply(text)
}
