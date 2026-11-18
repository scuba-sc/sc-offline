// SPDX-License-Identifier: GPL-3.0-only
//! The redactor never panics, whatever bytes it gets. The release profile aborts on panic, so a bad index would end
//! the whole MCP server. Deterministic pseudo-random text built from the shapes the redactor knows plus raw bytes;
//! synthetic only.

use scol_redact::{learn, redact_identity_keys, redact_with, Identities, LoginScanner};

struct Rng(u64);
impl Rng {
    fn next(&mut self) -> u64 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        self.0
    }
    fn below(&mut self, n: usize) -> usize {
        (self.next() % n as u64) as usize
    }
}

const FRAGMENTS: &[&[u8]] = &[
    b"nickname",
    b"handle",
    b"geid",
    b"GEID",
    b"accountId",
    b"account_id",
    b"playerId",
    b"username",
    b"user",
    b"owner",
    b"player",
    b"login",
    b"Character",
    b"name",
    b"Player_",
    b"SetLocalPlayerId",
    b"Handle[",
    b"Nickname[",
    b"nickname=\"",
    b"handle=\"",
    b"User Login Success",
    b"AccountLoginCharacterStatus",
    b"C:\\Users\\",
    b"/home/",
    b"%5C",
    b"%40",
    b"%2F",
    b"\\u0040",
    b"&#64;",
    b"@",
    b"S-1-5-21-",
    b"<redacted>",
    b"<user>",
    b"%USERPROFILE%",
    b"<",
    b">",
    b"[",
    b"]",
    b"(",
    b")",
    b"{",
    b"}",
    b"\"",
    b"'",
    b"\\",
    b"=",
    b":",
    b" ",
    b"  ",
    b"\t",
    b"\n",
    b"\r",
    b"-",
    b"_",
    b"#",
    b",",
    b";",
    b".",
    b"0",
    b"123",
    b"200099887766",
    b"Zorbax9",
    b"Bob Smith",
    b"\xc3\xa9",
    b"\xff",
    b"\xc3",
    b"\xe2\x82",
    b"\x00",
    b"a@b.co",
    b"x.y",
];

fn soup(rng: &mut Rng, pieces: usize) -> Vec<u8> {
    let mut out = Vec::new();
    for _ in 0..pieces {
        if rng.below(10) == 0 {
            out.push(rng.next() as u8);
        } else {
            out.extend_from_slice(FRAGMENTS[rng.below(FRAGMENTS.len())]);
        }
    }
    out
}

#[test]
fn random_text_never_panics() {
    let mut rng = Rng(0x5eed_1234_abcd_ef01);
    for round in 0..4000 {
        let n = 1 + rng.below(40);
        let text = soup(&mut rng, n);
        let mut id = Identities {
            strict: round % 2 == 1,
            ..Identities::default()
        };
        id.profile_roots = vec![
            b"C:\\Users\\Alex".to_vec(),
            b"/home/ab/".to_vec(),
            b"x".to_vec(),
            Vec::new(),
        ];
        id.account_names = vec![
            b"Alex".to_vec(),
            b"zorbax9".to_vec(),
            b"ab".to_vec(),
            Vec::new(),
        ];
        learn(&mut id, &text);
        let once = redact_with(&text, &id);
        let _ = redact_with(&once, &id);
        let _ = redact_identity_keys(&text, None);
        let mut found = Vec::new();
        let _ = redact_identity_keys(&text, Some(&mut found));
    }
}

#[test]
fn login_scanner_handles_any_chunking() {
    let mut rng = Rng(0x1234_5678_9abc_def1);
    for _ in 0..300 {
        let mut text = Vec::new();
        for _ in 0..1 + rng.below(8) {
            text.extend_from_slice(b"User Login Success - ");
            let n = 1 + rng.below(12);
            text.extend_from_slice(&soup(&mut rng, n));
            text.push(b'\n');
        }
        let mut id = Identities::default();
        {
            let mut scanner = LoginScanner::new(&mut id, None);
            let mut at = 0;
            while at < text.len() {
                let n = (1 + rng.below(9)).min(text.len() - at);
                scanner.feed(&text[at..at + n]);
                at += n;
            }
            scanner.finish();
        }
        let _ = redact_with(&text, &id);
    }
}

#[test]
fn long_and_nested_shapes_stay_fast_enough() {
    // Shapes that could make a naive scan quadratic: many keys with no value, many `@`, many separators.
    let started = std::time::Instant::now();
    let id = Identities::default();
    for unit in [
        &b"nickname="[..],
        b"a@",
        b"C:\\Users\\",
        b"geid ",
        b"\"x\" [",
        b"Player_",
        b"username ",
        b"name ",
        b"(",
        b"'",
    ] {
        let text = unit.repeat(30_000);
        let _ = redact_with(&text, &id);
    }
    assert!(
        started.elapsed().as_secs() < 60,
        "took {:?}",
        started.elapsed()
    );
}
