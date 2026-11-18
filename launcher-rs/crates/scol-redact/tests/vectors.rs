// SPDX-License-Identifier: GPL-3.0-only
//! Replays the redaction vectors that the C++ code wrote (`tools/launcher-mcp/dump.cpp` into
//! `launcher-rs/tests/mcp-vectors/vectors.json`). Every output and every learned list must be identical to what
//! `launcher/redact.h` gave. Synthetic text only; nothing here runs in game.

use scol_redact::{learn, redact_identity_keys, redact_with, Identities};
use serde_json::Value;

fn vectors() -> Value {
    let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../tests/mcp-vectors/vectors.json");
    let text = std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("cannot read {}: {e}", path.display()));
    serde_json::from_str(&text).expect("vectors.json is JSON")
}

/// Resolves a text value: a string, `{"hex"}`, `{"repeat"}`, `{"lines"}` or an array of those.
fn text(v: &Value) -> Vec<u8> {
    match v {
        Value::String(s) => s.as_bytes().to_vec(),
        Value::Array(a) => a.iter().flat_map(text).collect(),
        Value::Object(o) => {
            if let Some(h) = o.get("hex") {
                let h = h.as_str().unwrap();
                (0..h.len())
                    .step_by(2)
                    .map(|i| u8::from_str_radix(&h[i..i + 2], 16).unwrap())
                    .collect()
            } else if let Some(r) = o.get("repeat") {
                let unit = text(&r["unit"]);
                let n = r["count"].as_u64().unwrap() as usize;
                unit.repeat(n)
            } else if let Some(l) = o.get("lines") {
                let t = text(&l["template"]);
                let (a, b) = (l["from"].as_i64().unwrap(), l["to"].as_i64().unwrap());
                let mut out = Vec::new();
                for n in a..=b {
                    out.extend_from_slice(&replace(&t, b"{n}", n.to_string().as_bytes()));
                    out.push(b'\n');
                }
                out
            } else {
                panic!("not a text value: {v}")
            }
        }
        _ => panic!("not a text value: {v}"),
    }
}

fn replace(s: &[u8], what: &[u8], with: &[u8]) -> Vec<u8> {
    let mut out = Vec::new();
    let mut i = 0;
    while i < s.len() {
        if s[i..].starts_with(what) {
            out.extend_from_slice(with);
            i += what.len();
        } else {
            out.push(s[i]);
            i += 1;
        }
    }
    out
}

fn list(v: Option<&Value>) -> Vec<Vec<u8>> {
    v.and_then(|v| v.as_array())
        .map(|a| a.iter().map(text).collect())
        .unwrap_or_default()
}

fn show(b: &[u8]) -> String {
    let s = String::from_utf8_lossy(b);
    if s.len() > 300 {
        format!(
            "{}... ({} bytes)",
            &s[..s.char_indices().nth(300).map_or(s.len(), |x| x.0)],
            b.len()
        )
    } else {
        s.into_owned()
    }
}

fn first_difference(a: &[u8], b: &[u8]) -> usize {
    a.iter()
        .zip(b)
        .position(|(x, y)| x != y)
        .unwrap_or(a.len().min(b.len()))
}

#[test]
fn redaction_vectors_match_the_cpp_code() {
    let v = vectors();
    let cases = v["redact"].as_array().unwrap();
    assert!(
        cases.len() > 300,
        "the vector file has {} redaction cases",
        cases.len()
    );
    let mut failures = Vec::new();
    let mut checked = 0usize;
    for c in cases {
        let name = c["name"].as_str().unwrap();
        let mut id = if let Some(lists) = c.get("lists") {
            Identities::with_lists(
                list(lists.get("priority")),
                list(lists.get("learned")),
                list(lists.get("legacy")),
            )
        } else {
            Identities::default()
        };
        id.strict = c.get("strict").and_then(|s| s.as_bool()).unwrap_or(false);
        id.profile_roots = list(c.get("roots"));
        id.account_names = list(c.get("names"));
        let learned_from = c.get("learn").map(|l| l.as_array().unwrap().clone());
        if let Some(texts) = &learned_from {
            for t in texts {
                learn(&mut id, &text(t));
            }
        }
        let expected = &c["expected"];
        let key_pass = c.get("key_pass").and_then(|k| k.as_bool()).unwrap_or(false);
        let inputs = c["inputs"].as_array().unwrap();
        let outputs = expected["outputs"].as_array().unwrap();
        assert_eq!(inputs.len(), outputs.len(), "{name}");
        for (k, (input, want)) in inputs.iter().zip(outputs).enumerate() {
            let input = text(input);
            let want = text(want);
            let got = if key_pass {
                redact_identity_keys(&input, None)
            } else {
                redact_with(&input, &id)
            };
            checked += 1;
            if got != want {
                let at = first_difference(&got, &want);
                failures.push(format!(
                    "{name} input {k}: first difference at byte {at}\n  input: {}\n  rust:  {}\n  cpp:   {}",
                    show(&input),
                    show(&got),
                    show(&want)
                ));
            }
        }
        if learned_from.is_some() {
            for (field, got) in [
                ("priority", &id.priority),
                ("learned", &id.learned),
                ("legacy", &id.legacy),
            ] {
                let want = list(expected.get(field));
                if *got != want {
                    failures.push(format!(
                        "{name}: learned list {field} differs\n  rust: {:?}\n  cpp:  {:?}",
                        got.iter().map(|x| show(x)).collect::<Vec<_>>(),
                        want.iter().map(|x| show(x)).collect::<Vec<_>>()
                    ));
                }
            }
            for (field, got) in [
                ("priority_dropped", id.priority_dropped),
                ("legacy_dropped", id.legacy_dropped),
            ] {
                let want = expected[field].as_u64().unwrap() as usize;
                if got != want {
                    failures.push(format!(
                        "{name}: {field} is {got} in Rust and {want} in C++"
                    ));
                }
            }
        }
    }
    assert!(checked > 600, "only {checked} redactions were checked");
    if !failures.is_empty() {
        let n = failures.len();
        panic!(
            "{n} of {checked} redactions differ from the C++ code:\n{}",
            failures.into_iter().take(8).collect::<Vec<_>>().join("\n")
        );
    }
}
